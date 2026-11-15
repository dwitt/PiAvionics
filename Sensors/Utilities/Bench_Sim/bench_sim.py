#!/usr/bin/env python3
"""Bench simulator: stands in for the AIR and GPS modules on the CAN bus.

BENCH USE ONLY. Run it by hand on the Pi with the real AIR and GPS modules unplugged; Ctrl-C stops it.
It sends the same frames, at the same rates, as AIR_Module and GPS_Module, plus their heartbeats
(version 9.9.9, so the module panel shows they are simulated). Nothing in the EFIS changes.

It "flies": the heading comes from the real IMU (CAN 0x48), so turning the IMU steers the aircraft.
The air vector (TAS along the heading) plus the wind gives the ground speed and track, and the
position moves along that track. The altitude follows the QNH set on the EFIS or the backup display.

Usage (on the Pi):  ~/EFIS/venv/bin/python bench_sim.py [--lat 43.25 --lon -80.30] [--ias 100]
                    [--alt 3500] [--wind 270/20] [--heading 30] [--declination -10]
While it runs, type a command and Enter:  ias 90 | alt 4500 | wind 300/25 | hdg 45 | hdg imu | vs 500 | q
"""

VERSION = "1.0.0"

import argparse
import math
import struct
import sys
import threading
import time
from datetime import datetime, timezone

import can  # python-can, in the EFIS venv

SIM_VERSION = (9, 9, 9)          # the heartbeats' version: shows in the EFIS module panel
MODULE_AIR, MODULE_GPS = 2, 3
HEARTBEAT_BASE = 0x700
STATE_OK = 1
AIR_MASK = 0x0F                  # pitot, AOA, static, OAT all good
GPS_MASK = 0x03                  # NMEA and fix

ID_AIR, ID_AOA, ID_OAT, ID_RAW = 0x28, 0x29, 0x2A, 0x2B
ID_QNH_FIRST, ID_QNH_LAST = 0x2C, 0x2F
ID_AHRS = 0x48
ID_GPS1, ID_GPS2, ID_GPS3, ID_TIME = 0x63, 0x64, 0x65, 0x66

# ISA and the page's TAS formula (speedDisplay.mjs), so the EFIS's TAS matches the one used here
P0 = 101325.0                    # Pa
A0_KT = 661.4788                 # speed of sound at sea level, knots
GAMMA = 1.401
RAIR = 287.05
MPS_TO_KT = 1.943844
RECOVERY = 0.95
NM_PER_DEG_LAT = 60.0


def static_pressure_pa(pressure_alt_ft):
    return P0 * (1 - 6.8755856e-6 * pressure_alt_ft) ** 5.2558797


def isa_temp_c(pressure_alt_ft):
    return 15.0 - 1.98 * pressure_alt_ft / 1000.0


def impact_pressure_pa(cas_kt):
    """Pitot minus static for a calibrated airspeed (subsonic)."""
    return P0 * ((1 + 0.2 * (cas_kt / A0_KT) ** 2) ** 3.5 - 1)


def page_tas_kt(static_pa, diff_pa, oat_c):
    """The EFIS page's TAS (speedDisplay.mjs) from the values sent on CAN."""
    mach = math.sqrt(2 / (GAMMA - 1) * ((diff_pa / static_pa + 1) ** ((GAMMA - 1) / GAMMA) - 1))
    t_static = (oat_c + 273.15) / (1 + 0.2 * RECOVERY * mach ** 2)
    return mach * math.sqrt(GAMMA * RAIR) * MPS_TO_KT * math.sqrt(t_static)


class Sim:
    def __init__(self, args):
        self.lock = threading.Lock()
        self.lat, self.lon = args.lat, args.lon
        self.ias = args.ias
        self.pressure_alt = args.alt
        self.vs = 0.0
        self.wind_from, self.wind_kt = args.wind
        self.heading_override = args.heading      # None = follow the IMU
        self.declination = args.declination       # degrees, East positive (true = magnetic + declination)
        self.imu_heading = None
        self.imu_heard = 0.0
        self.qnh_inhg100 = 2992
        self.qnh_heard = False
        self.start = time.monotonic()
        self.foreign = {}                         # module -> time a real heartbeat was last heard

    # --- the aircraft -------------------------------------------------------
    def heading(self, now):
        if self.heading_override is not None:
            return self.heading_override
        if self.imu_heading is not None and now - self.imu_heard < 1.0:
            return self.imu_heading
        return None

    def air(self):
        static_pa = static_pressure_pa(self.pressure_alt)
        diff_pa = impact_pressure_pa(self.ias)
        oat = isa_temp_c(self.pressure_alt)
        return static_pa, diff_pa, oat, page_tas_kt(static_pa, diff_pa, oat)

    def ground_vector(self, now):
        """(ground speed kt, magnetic track deg), or None without a heading."""
        hdg = self.heading(now)
        if hdg is None:
            return None
        tas = self.air()[3]
        ax, ay = tas * math.sin(math.radians(hdg)), tas * math.cos(math.radians(hdg))
        to = self.wind_from + 180.0                   # the wind blows towards
        wx, wy = self.wind_kt * math.sin(math.radians(to)), self.wind_kt * math.cos(math.radians(to))
        gx, gy = ax + wx, ay + wy
        return math.hypot(gx, gy), math.degrees(math.atan2(gx, gy)) % 360

    def step(self, dt, now):
        with self.lock:
            self.pressure_alt += self.vs * dt / 60.0
            g = self.ground_vector(now)
            if g is None:
                return
            gs, mag_track = g
            true_track = math.radians(mag_track + self.declination)
            dist_nm = gs * dt / 3600.0
            self.lat += dist_nm * math.cos(true_track) / NM_PER_DEG_LAT
            self.lon += dist_nm * math.sin(true_track) / (NM_PER_DEG_LAT * math.cos(math.radians(self.lat)))

    def indicated_alt(self):
        return self.pressure_alt + (self.qnh_inhg100 - 2992) * 10.0     # about 1000 ft per inHg


def heartbeat(module, uptime_s):
    up = int(uptime_s) & 0xFFFF
    return bytes([module, *SIM_VERSION, STATE_OK, AIR_MASK if module == MODULE_AIR else GPS_MASK,
                  up & 0xFF, up >> 8])


def listener(bus, sim, stop):
    """Read the IMU heading, the QNH, and watch for the real modules' heartbeats."""
    while not stop.is_set():
        msg = bus.recv(0.2)
        if msg is None or msg.is_error_frame or len(msg.data) < 8:
            continue
        now = time.monotonic()
        aid = msg.arbitration_id
        with sim.lock:
            if aid == ID_AHRS:
                yaw = struct.unpack_from("<h", msg.data, 0)[0]
                if yaw != 0x7FFF:           # the invalid marker
                    sim.imu_heading = (yaw / 10.0) % 360
                    sim.imu_heard = now
            elif ID_QNH_FIRST <= aid <= ID_QNH_LAST:
                qnh_x4 = struct.unpack_from("<h", msg.data, 2)[0]
                if 2700 * 4 <= qnh_x4 <= 3200 * 4:
                    sim.qnh_inhg100 = round(qnh_x4 / 4)
                    sim.qnh_heard = True
            elif aid in (HEARTBEAT_BASE + MODULE_AIR, HEARTBEAT_BASE + MODULE_GPS):
                if tuple(msg.data[1:4]) != SIM_VERSION:
                    sim.foreign[aid - HEARTBEAT_BASE] = now


def commands(sim, stop):
    for line in sys.stdin:
        words = line.split()
        if not words:
            continue
        try:
            with sim.lock:
                if words[0] == "q":
                    stop.set()
                    return
                elif words[0] == "ias":
                    sim.ias = float(words[1])
                elif words[0] == "alt":
                    sim.pressure_alt = float(words[1])
                elif words[0] == "vs":
                    sim.vs = float(words[1])
                elif words[0] == "wind":
                    d, s = words[1].split("/")
                    sim.wind_from, sim.wind_kt = float(d), float(s)
                elif words[0] == "hdg":
                    sim.heading_override = None if words[1] == "imu" else float(words[1]) % 360
                else:
                    print("commands: ias N | alt FT | vs FPM | wind DIR/KT | hdg DEG | hdg imu | q")
                    continue
            print("ok", flush=True)
        except (IndexError, ValueError):
            print("commands: ias N | alt FT | vs FPM | wind DIR/KT | hdg DEG | hdg imu | q")


def send(bus, can_id, data):
    try:
        bus.send(can.Message(arbitration_id=can_id, data=data, is_extended_id=False), timeout=0.05)
    except can.CanError as e:
        print(f"send 0x{can_id:X} failed: {e}", flush=True)


def main():
    p = argparse.ArgumentParser(description="Bench simulator for the AIR and GPS modules (bench use only)")
    p.add_argument("--lat", type=float, default=43.25, help="start latitude (default near CYFD)")
    p.add_argument("--lon", type=float, default=-80.30, help="start longitude")
    p.add_argument("--ias", type=float, default=100.0, help="indicated airspeed, kt")
    p.add_argument("--alt", type=float, default=3500.0, help="pressure altitude, ft")
    p.add_argument("--wind", type=lambda s: tuple(float(v) for v in s.split("/")), default=(270.0, 20.0),
                   help="wind from DIR (magnetic) / KT, default 270/20")
    p.add_argument("--heading", type=float, default=None, help="fixed magnetic heading (default: the IMU's)")
    p.add_argument("--declination", type=float, default=-10.0, help="magnetic declination, East positive")
    p.add_argument("--channel", default="can0")
    args = p.parse_args()

    bus = can.interface.Bus(channel=args.channel, interface="socketcan")
    sim = Sim(args)
    stop = threading.Event()
    threading.Thread(target=listener, args=(bus, sim, stop), daemon=True).start()

    print(f"bench_sim {VERSION}: BENCH USE ONLY. Checking for the real AIR and GPS modules...", flush=True)
    time.sleep(2.5)
    if sim.foreign:
        names = ", ".join({MODULE_AIR: "AIR", MODULE_GPS: "GPS"}[m] for m in sim.foreign)
        print(f"The real {names} module is on the bus. Unplug it and run again.")
        stop.set()
        bus.shutdown()
        return 1
    print("No real AIR or GPS module heard: simulating both. Type a command (ias, alt, vs, wind, hdg, q) or Ctrl-C.",
          flush=True)
    threading.Thread(target=commands, args=(sim, stop), daemon=True).start()

    period = {ID_AIR: 0.1, ID_AOA: 0.2, ID_OAT: 0.2, ID_RAW: 0.2,
              ID_GPS1: 1.0, ID_GPS2: 1.0, ID_GPS3: 1.0, ID_TIME: 1.0,
              HEARTBEAT_BASE + MODULE_AIR: 1.0, HEARTBEAT_BASE + MODULE_GPS: 1.0}
    due = {k: time.monotonic() for k in period}
    last = time.monotonic()
    last_status = 0.0
    try:
        while not stop.is_set():
            now = time.monotonic()
            sim.step(now - last, now)
            last = now
            with sim.lock:
                if sim.foreign and any(now - t < 3 for t in sim.foreign.values()):
                    print("A real AIR or GPS module has appeared on the bus: stopping.", flush=True)
                    break
                static_pa, diff_pa, oat, tas = sim.air()
                g = sim.ground_vector(now)
                frames = {
                    ID_AIR: struct.pack("<hlh", round(sim.ias), round(sim.indicated_alt()), round(sim.vs)),
                    ID_AOA: struct.pack("<hhhh", 4, 0, 0, 0),
                    ID_OAT: struct.pack("<hBBBBBB", round(oat * 10), 0, 0, 0, 0, 0, 0),
                    ID_RAW: struct.pack("<hbhhB", round(static_pa / 10), round(oat), round(diff_pa),
                                        sim.qnh_inhg100, 0),
                    HEARTBEAT_BASE + MODULE_AIR: heartbeat(MODULE_AIR, now - sim.start),
                    HEARTBEAT_BASE + MODULE_GPS: heartbeat(MODULE_GPS, now - sim.start),
                    ID_GPS1: struct.pack("<ll", round(sim.lat * 1e6), round(sim.lon * 1e6)),
                    ID_GPS3: struct.pack("<BBBbhh", 2, 3, 11, round(sim.declination), 90, 120),
                }
                if g is not None:
                    gs, mag_track = g
                    frames[ID_GPS2] = struct.pack("<hhhh", round(gs), round(sim.pressure_alt),
                                                  round(mag_track + sim.declination) % 360, round(mag_track) % 360)
                utc = datetime.now(timezone.utc)
                frames[ID_TIME] = struct.pack("<bbbbbbbb", utc.year - 2000, utc.month, utc.day,
                                              utc.hour, utc.minute, utc.second, 0, 0)
                status = None
                if now - last_status >= 5:
                    hdg = sim.heading(now)
                    status = (f"IAS {sim.ias:.0f} TAS {tas:.0f} ALT {sim.indicated_alt():.0f} "
                              f"(QNH {sim.qnh_inhg100 / 100:.2f}{'' if sim.qnh_heard else ' default'}) "
                              f"HDG {'---' if hdg is None else f'{hdg:03.0f}'}"
                              f"{' (IMU)' if sim.heading_override is None else ' (fixed)'} "
                              f"WIND {sim.wind_from:03.0f}/{sim.wind_kt:.0f} "
                              + (f"GS {g[0]:.0f} TRK {g[1]:03.0f} " if g else "no heading: GPS speed not sent ")
                              + f"POS {sim.lat:.4f} {sim.lon:.4f}")
                    last_status = now
            if status:
                print(status, flush=True)
            for cid, data in frames.items():
                if now >= due[cid]:
                    send(bus, cid, data)
                    due[cid] = now + period[cid]
            time.sleep(0.02)
    except KeyboardInterrupt:
        pass
    stop.set()
    bus.shutdown()
    print("bench_sim stopped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
