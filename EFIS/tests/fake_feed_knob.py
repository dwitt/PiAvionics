"""A fake EFIS server for trying the page without the aircraft or the real modules.

fake_feed_knob.py: fake_feed.py extended for mockups (2026-11): the knob (position, pressed), a flight plan
(dtk, xtrack, bearing), the CDI scale (cdi_auto, and the page's cdi_scale command), heading and track (yaw,
mag_track), the "imu_starting" scenario, and EFIS_DIR=<dir> to serve a scratch copy of the page. Use it with
tests/states.py for screenshots. Run it on the Mac only.

Serves the EFIS page files and a websocket at /ws that sends the same JSON shape as aio_server.py, with
the module / validity facts of a chosen scenario (the failures in EFIS/docs/MODULE_STATUS_PLAN.md). The
screen orientation facts come from the real display_orientation.py with a pretend screen.

    python3 -m venv venv && venv/bin/pip install aiohttp          (once, anywhere with Python)
    venv/bin/python tests/fake_feed_knob.py [--port 8097]
    open http://localhost:8097/?pi=true      (localhost is treated as the controller page;
                                              ?pi=landscape for the turned screen)

    curl 'localhost:8097/scenario?name=pitot'        choose a scenario (list: /scenario)
    curl 'localhost:8097/stall'  and  /resume        stop / restart the data (data link down)
    curl 'localhost:8097/control?airspeed=60'        fake airspeed (for the orientation lock)

tests/page_shots.py drives a headless Chrome through the scenarios and saves a screenshot of each.
"""
VERSION = "1.8.1"
import argparse
import asyncio
import copy
import json
import os
import random
import sys
import tempfile
import time

from aiohttp import web

HERE = os.path.dirname(os.path.abspath(__file__))
HERE = os.path.dirname(os.path.abspath(__file__))
EFIS = os.path.abspath(os.path.join(HERE, ".."))
SERVE = os.environ.get("EFIS_DIR", EFIS)   # page files to serve (a scratch copy for mockups)
sys.path.insert(0, EFIS)
import display_orientation as display_orientation        # noqa: E402
import aircraft_settings        # noqa: E402


def good_facts():
    """What the server sends when everything is healthy (module_status.py's facts)."""
    def module(sensors):
        return {"present": True, "missing": False, "state": 1, "version": "1.0.0", "uptime": 600,
                "age": 0.4, "restarted_age": None, "sensors": sensors}
    return {
        "modules": {
            "imu": module({"gyro_accel": True, "magnetometer": True, "calibration": True}),
            "air": module({"pitot": True, "aoa": True, "static": True, "oat": True}),
            "gps": module({"nmea": True, "fix": True}),
        },
        "valid": {"airspeed": True, "altitude": True, "vsi": True, "static_pressure": True,
                  "differential_pressure": True, "oat": True, "attitude": True, "heading": True,
                  "turn_rate": True, "acceleration": True, "gps_position": True},
        "data_lost": {"air_data": False, "raw_pressure": False, "oat": False, "attitude": False,
                      "acceleration": False, "gps_position": False, "gps_status": False},
        "heading_status": {"valid": True, "suspect": False, "gyro_seconds": None},
        "system_state": 1,
    }


def missing(facts, name):
    facts["modules"][name] = {"present": False, "missing": True, "state": None, "version": None,
                              "uptime": None, "age": None, "restarted_age": None, "sensors": None}


def pitot(f):
    f["modules"]["air"]["sensors"]["pitot"] = False
    f["valid"]["airspeed"] = f["valid"]["differential_pressure"] = False


def static(f):
    f["modules"]["air"]["sensors"]["static"] = False
    f["valid"]["altitude"] = f["valid"]["vsi"] = f["valid"]["static_pressure"] = False


def oat(f):
    f["modules"]["air"]["sensors"]["oat"] = False
    f["valid"]["oat"] = False


def aoa(f):
    f["modules"]["air"]["sensors"]["aoa"] = False


def att(f):
    f["modules"]["imu"]["sensors"]["gyro_accel"] = False
    for k in ("attitude", "heading", "turn_rate", "acceleration"):
        f["valid"][k] = False


def mag(f, seconds=40.0):
    f["modules"]["imu"]["sensors"]["magnetometer"] = False
    f["heading_status"] = {"valid": True, "suspect": True, "gyro_seconds": seconds}


def gps_lost(f):
    f["modules"]["gps"]["sensors"] = {"nmea": False, "fix": False}
    f["valid"]["gps_position"] = False


def gps_nofix(f):
    f["modules"]["gps"]["sensors"]["fix"] = False
    f["valid"]["gps_position"] = False
    f["data_lost"]["gps_position"] = True


def airmod(f):
    missing(f, "air")
    for k in ("airspeed", "altitude", "vsi", "static_pressure", "differential_pressure", "oat"):
        f["valid"][k] = False
    f["data_lost"]["air_data"] = f["data_lost"]["oat"] = f["data_lost"]["raw_pressure"] = True


def imumod(f):
    missing(f, "imu")
    for k in ("attitude", "heading", "turn_rate", "acceleration"):
        f["valid"][k] = False
    f["data_lost"]["attitude"] = f["data_lost"]["acceleration"] = True


def restart(f):
    f["modules"]["gps"]["restarted_age"] = 2.0
    f["modules"]["gps"]["uptime"] = 2


def imu_starting(f):
    """The IMU heartbeat says STARTING (uptime 2 s) and no attitude has arrived yet."""
    f["modules"]["imu"].update({"state": 0, "uptime": 2, "sensors": None})
    for k in ("attitude", "heading", "turn_rate", "acceleration"):
        f["valid"][k] = False
    f["data_lost"]["attitude"] = f["data_lost"]["acceleration"] = True
    f["heading_status"] = {"valid": False, "suspect": False, "gyro_seconds": None}


def several(f):
    pitot(f)
    oat(f)
    mag(f)
    gps_lost(f)


SCENARIOS = {
    "ok": lambda f: None,
    "pitot": pitot, "static": static, "oat": oat, "aoa": aoa, "att": att,
    "mag": mag, "mag_blank": lambda f: mag(f, 301.0),
    "imu_cal": lambda f: f["modules"]["imu"]["sensors"].update({"calibration": False}),
    "gps_lost": gps_lost, "gps_nofix": gps_nofix,
    "airmod": airmod, "imumod": imumod, "restart": restart, "several": several, "gps_2d": lambda f: None, "gps_poor": lambda f: None,
    "slow": lambda f: None, "taxi_9kt": lambda f: None, "pitot_marker": pitot, "static_marker": static, "race_gyro": lambda f: None, "race_pitot": lambda f: None,
    "oat_marker": oat, "gyro_marker": att, "efis_nodisplay": lambda f: None,
    "all_air_imu": lambda f: (airmod(f), imumod(f)),
    "imu_starting": imu_starting,
}


def efis_facts(facts, scenario):
    """The server's own heartbeat as the real server would work it out from the module facts."""
    ok = {name: facts["modules"][name]["present"] and facts["modules"][name]["state"] == 1
          for name in ("air", "imu", "gps")}
    mask = (1 if ok["air"] else 0) | (2 if ok["imu"] else 0) | (4 if ok["gps"] else 0)
    state = 1 if mask == 7 else 2
    display = scenario != "efis_nodisplay"
    if display:
        mask |= 8
    else:
        state = 3
    return {"version": "1.6.1", "state": state, "mask": mask, "uptime": 3725, "display_clients": 1 if display else 0}


# Extra values a scenario changes in the data itself (not in the facts). With the modules sending invalid-data
# markers (AIR_Module 1.0.35, IMU_AHRS 1.0.16) the real server sends None for a failed value.
SCENARIO_DATA = {
    # the moment after a marker nulled a value but before the validity caught up: null values, flags still valid
    "race_gyro": {"pitch": None, "roll": None, "yaw": None, "turn_rate": None},
    "imu_starting": {"pitch": None, "roll": None, "yaw": None, "turn_rate": None},
    "race_pitot": {"airspeed": None, "differential_pressure": None},
    "pitot_marker": {"airspeed": None, "differential_pressure": None},
    "static_marker": {"altitude": None, "vsi": None, "static_pressure": None},
    "oat_marker": {"oat": None},
    "gyro_marker": {"yaw": None, "pitch": None, "roll": None, "turn_rate": None,
                    "accx": None, "accy": None, "accz": None},
    # a receiver with no fix still reports its status: fix quality 0 and a few satellites
    "gps_nofix": {"gps_fix_quality": 0, "gps_fix_3d": 1, "gps_satellites": 3, "gps_hdop": 9.9, "gps_vdop": 9.9},
    "gps_2d": {"gps_fix_quality": 1, "gps_fix_3d": 2, "gps_satellites": 4, "gps_hdop": 2.4, "gps_vdop": 5.2},
    "gps_poor": {"gps_fix_quality": 1, "gps_fix_3d": 3, "gps_satellites": 5, "gps_hdop": 4.2, "gps_vdop": 6.1},
    "slow": {"gps_speed": 4, "true_track": 0, "mag_track": 0},
    "taxi_9kt": {"gps_speed": 9, "true_track": 92, "mag_track": 102},
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8097)
    args = parser.parse_args()

    state = {"scenario": "ok", "stalled": False, "airspeed": 112.0, "position": 0, "pressed": False, "pitch": 3.0, "roll": 8.0, "accy": 0.02, "altitude": 2400.0, "plan": True, "rate": 0.0, "noise": 0.0, "gs": 112.0, "aircraft_save": None, "limits": {"vso": 35, "vs1": 40, "vfe": 87, "vno": 108, "vne": 171}}
    saved = os.path.join(tempfile.mkdtemp(prefix="efis_orientation_"), "display_orientation.json")

    async def fake_runner(transform):
        print(f"(pretend screen rotated: wlr-randr --transform {transform})", flush=True)
        return True
    orientation = display_orientation.DisplayOrientation(fake_runner, saved)
    settings = aircraft_settings.AircraftSettings(os.path.join(os.path.dirname(saved), "aircraft_settings.json"),
                                                  log=lambda t: print(t, flush=True))

    def message():
        facts = good_facts()
        SCENARIOS[state["scenario"]](facts)
        data = {
            "altitude": state["altitude"], "airspeed": state["airspeed"] + random.gauss(0, state["noise"]), "vsi": 150, "pitch": state["pitch"], "roll": state["roll"],
            "yaw": state.get("yaw", 90), "accy": state["accy"], "accz": 1.0, "turn_rate": 1.5, "oat": 22, "static_pressure": 9420,
            "differential_pressure": 620, "gps_speed": state["gs"], "gps_altitude": 2410, "true_track": 92, "mag_track": state.get("mag_track", 102),
            "gps_fix_quality": 2, "gps_fix_3d": 3, "gps_declination": -10, "gps_satellites": 11, "gps_hdop": 0.9, "gps_vdop": 1.2,
            # navigation to a waypoint (computed by the real server from the flight plan and GPS position)
            "wpt_id": "CYFD", "dist": 12.3, "dtk": state.get("dtk", 92), "xtrack": state.get("xtrack", 0.4), "to_from": "TO", "bearing": state.get("bearing", 98),
            "aircraft": {"airspeed": dict(state["limits"]), "vspeeds": dict(settings.settings["vspeeds"]), "attitude": dict(settings.settings["attitude"])}, "tm_hour": time.gmtime().tm_hour, "tm_min": time.gmtime().tm_min,
            "aircraft_save": state["aircraft_save"], "controller_state": {"cdi_scale": state.get("cdi_scale", "auto")}, "cdi_auto_nm": state.get("cdi_auto", 5.0), "display": orientation.facts(), "position": state["position"], "pressed": state["pressed"],
        }
        data.update(facts)
        data["efis"] = efis_facts(facts, state["scenario"])
        data.update(SCENARIO_DATA.get(state["scenario"], {}))
        if not state["plan"]:
            data.update({"wpt_id": None, "dist": None, "dtk": None, "xtrack": None, "to_from": None, "bearing": None})
        return data

    async def ws_handler(request):
        ws = web.WebSocketResponse()
        await ws.prepare(request)

        async def sender():
            while not ws.closed:
                await orientation.tick()
                state["airspeed"] = min(250.0, max(0.0, state["airspeed"] + state["rate"] * 0.1))
                if not state["stalled"]:
                    try:
                        await ws.send_str(json.dumps(message()))
                    except ConnectionResetError:
                        return
                await asyncio.sleep(0.1)
        task = asyncio.create_task(sender())
        async for msg in ws:
            if msg.type == web.WSMsgType.TEXT and msg.data.startswith("json"):
                command = json.loads(msg.data[4:])
                if "cdi_scale" in command:
                    state["cdi_scale"] = command["cdi_scale"]
                    print("CDI scale ->", command["cdi_scale"], flush=True)
                if "pitch_offset" in command:
                    print("pitch offset ->", settings.set_pitch_offset(command["pitch_offset"]), flush=True)
                if isinstance(command.get("log"), str):
                    print("Page:", command["log"], flush=True)
                if "orientation_request" in command:
                    print("orientation:", await orientation.request(command["orientation_request"],
                                                                    (state["airspeed"], None)), flush=True)
                if command.get("orientation_keep"):
                    await orientation.keep()
                if command.get("orientation_revert"):
                    await orientation.revert()
                if "aircraft_settings" in command:
                    change = command["aircraft_settings"]
                    ok, reason = settings.change(change.get("group"), change.get("speeds"), (state["airspeed"], state["gs"]))
                    if ok:
                        state["limits"] = dict(settings.settings["airspeed"])
                    state["aircraft_save"] = {"id": command.get("request_id"), "ok": ok, "reason": reason}
                    print("airspeed limits:", ok, reason, flush=True)
        task.cancel()
        return ws

    async def scenario(request):
        name = request.query.get("name")
        if name in SCENARIOS:
            state["scenario"] = name
        return web.Response(text=f"scenario: {state['scenario']}\nchoices: {', '.join(SCENARIOS)}\n")

    async def stall(request):
        state["stalled"] = True
        return web.Response(text="stalled")

    async def resume(request):
        state["stalled"] = False
        return web.Response(text="resumed")

    async def control(request):
        if "airspeed" in request.query:
            state["airspeed"] = float(request.query["airspeed"])
        if "position" in request.query:
            state["position"] = int(request.query["position"])
        for k in ("vso", "vs1", "vfe", "vno", "vne"):
            if k in request.query:
                state["limits"][k] = float(request.query[k])
        if "plan" in request.query:
            state["plan"] = request.query["plan"] == "1"
        if "pressed" in request.query:
            state["pressed"] = request.query["pressed"] == "1"
        for k in ("pitch", "roll", "accy", "altitude", "rate", "noise", "gs", "bearing", "dtk", "xtrack", "cdi_auto", "yaw", "mag_track"):
            if k in request.query:
                state[k] = float(request.query[k])
        return web.Response(text="ok")

    async def index(request):
        return web.FileResponse(os.path.join(SERVE, "index.html"))

    async def pixi(request):
        # the Pi's copy script renames support/pixi-v8.6.6.mjs to support/pixi.mjs
        return web.FileResponse(os.path.join(SERVE, "support", "pixi-v8.6.6.mjs"))

    app = web.Application()
    app.add_routes([web.get("/", index), web.get("/ws", ws_handler), web.get("/scenario", scenario),
                    web.get("/stall", stall), web.get("/resume", resume), web.get("/control", control),
                    web.get("/support/pixi.mjs", pixi),
                    *([web.get("/support/Tahoma.ttf", lambda r: web.FileResponse(os.environ["FONT_REG"])),
                       web.get("/support/Tahoma Bold.ttf", lambda r: web.FileResponse(os.environ["FONT_BOLD"]))]
                      if os.environ.get("FONT_REG") else []),
                    web.static("/support", os.path.join(SERVE, "support"))])
    print(f"fake EFIS feed on http://localhost:{args.port}/  (scenarios: {', '.join(SCENARIOS)})", flush=True)
    web.run_app(app, host="127.0.0.1", port=args.port, print=None)


if __name__ == "__main__":
    main()
