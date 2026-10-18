"""A fake EFIS server for trying the page without the aircraft or the real modules.

Serves the EFIS page files and a websocket at /ws that sends the same JSON shape as aio_server.py, with
the module / validity facts of a chosen scenario (the failures in EFIS/docs/MODULE_STATUS_PLAN.md). The
screen orientation facts come from the real display_orientation.py with a pretend screen.

    python3 -m venv venv && venv/bin/pip install aiohttp          (once, anywhere with Python)
    venv/bin/python tests/fake_feed.py [--port 8097]
    open http://localhost:8097/?pi=1         (localhost is treated as the controller page)

    curl 'localhost:8097/scenario?name=pitot'        choose a scenario (list: /scenario)
    curl 'localhost:8097/stall'  and  /resume        stop / restart the data (data link down)
    curl 'localhost:8097/control?airspeed=60'        fake airspeed (for the orientation lock)

tests/page_shots.py drives a headless Chrome through the scenarios and saves a screenshot of each.
VERSION 1.1.0"""
import argparse
import asyncio
import copy
import json
import os
import sys
import tempfile
import time

from aiohttp import web

HERE = os.path.dirname(os.path.abspath(__file__))
EFIS = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, EFIS)
import display_orientation as display_orientation        # noqa: E402


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
    "airmod": airmod, "imumod": imumod, "restart": restart, "several": several,
    "all_air_imu": lambda f: (airmod(f), imumod(f)),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8097)
    args = parser.parse_args()

    state = {"scenario": "ok", "stalled": False, "airspeed": 112.0}
    saved = os.path.join(tempfile.mkdtemp(prefix="efis_orientation_"), "display_orientation.json")

    async def fake_runner(transform):
        print(f"(pretend screen rotated: wlr-randr --transform {transform})", flush=True)
        return True
    orientation = display_orientation.DisplayOrientation(fake_runner, saved)

    def message():
        facts = good_facts()
        SCENARIOS[state["scenario"]](facts)
        data = {
            "altitude": 2400, "airspeed": state["airspeed"], "vsi": 150, "pitch": 3.0, "roll": 8.0,
            "yaw": 90, "accy": 0.02, "accz": 1.0, "turn_rate": 1.5, "oat": 22, "static_pressure": 9420,
            "differential_pressure": 620, "gps_speed": 112, "gps_altitude": 2410, "true_track": 92,
            "gps_fix_quality": 2, "gps_satellites": 11, "gps_hdop": 0.9, "gps_vdop": 1.2,
            "controller_state": None, "display": orientation.facts(), "position": 0, "pressed": False,
        }
        data.update(facts)
        return data

    async def ws_handler(request):
        ws = web.WebSocketResponse()
        await ws.prepare(request)

        async def sender():
            while not ws.closed:
                await orientation.tick()
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
                if "orientation_request" in command:
                    print("orientation:", await orientation.request(command["orientation_request"],
                                                                    (state["airspeed"], None)), flush=True)
                if command.get("orientation_keep"):
                    await orientation.keep()
                if command.get("orientation_revert"):
                    await orientation.revert()
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
        return web.Response(text="ok")

    async def index(request):
        return web.FileResponse(os.path.join(EFIS, "index.html"))

    async def pixi(request):
        # the Pi's copy script renames support/pixi-v8.6.6.mjs to support/pixi.mjs
        return web.FileResponse(os.path.join(EFIS, "support", "pixi-v8.6.6.mjs"))

    app = web.Application()
    app.add_routes([web.get("/", index), web.get("/ws", ws_handler), web.get("/scenario", scenario),
                    web.get("/stall", stall), web.get("/resume", resume), web.get("/control", control),
                    web.get("/support/pixi.mjs", pixi),
                    web.static("/support", os.path.join(EFIS, "support"))])
    print(f"fake EFIS feed on http://localhost:{args.port}/  (scenarios: {', '.join(SCENARIOS)})", flush=True)
    web.run_app(app, host="127.0.0.1", port=args.port, print=None)


if __name__ == "__main__":
    main()
