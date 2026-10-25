"""Reference conversion of RDAC XF raw values, following Protocol/engine_conversions.yaml.

Usage:  python3 engine_convert.py          (runs the self-check)
        import engine_convert as ec; ec.convert(frames)   frames = {0x202: bytes, ...}

Returns a dict with a value or None for each item (None: missing frame, open or shorted sender, or not calibrated).
"""
import math
import os
import struct

import yaml

VERSION = "1.0.1"

RDAC_BASE = 0x200
_TABLE = None


def table():
    global _TABLE
    if _TABLE is None:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "engine_conversions.yaml")) as f:
            _TABLE = yaml.safe_load(f)
    return _TABLE


def resistance(raw, channel):
    """Sender resistance in ohm from the raw divider reading, None if the input is open or shorted."""
    t = table()["resistive_input"]
    if raw is None or raw <= 0 or raw >= t["full_scale"] - 20:
        return None
    return t["pullup_ohm"][channel] * raw / (t["full_scale"] - raw)


def pressure_bar(r):
    if r is None:
        return None
    q = table()["oil_pressure"]["quadratic"]
    a, b, c = q["a"], q["b"], q["c"]
    if r <= c:
        return 0.0
    p = (-b + math.sqrt(b * b - 4 * a * (c - r))) / (2 * a) if b * b - 4 * a * (c - r) >= 0 else None
    low, high = table()["oil_pressure"]["range_bar"]
    if p is None or p > high + 0.3:
        return None                           # beyond the sender's range: a fault
    return p


def temperature_c(r):
    if r is None:
        return None
    k = table()["oil_temp"]["steinhart_hart"]
    ln = math.log(r)
    t = 1.0 / (k["A"] + k["B"] * ln + k["C"] * ln ** 3) - 273.15
    low, high = table()["oil_temp"]["range_degC"]
    return t if low <= t <= high else None


def convert(frames, ages=None):
    """frames: {CAN id: 8 data bytes} for 0x202 to 0x208 (address 32).
    ages: {CAN id: seconds since that frame arrived}. A frame older than its timeout, or a missing age, counts as
    missing, and when the RDAC is lost (0x208 too old) every value is None. Without ages nothing is timed out."""
    if ages is not None:
        t = table()["timeouts"]
        if ages.get(t["rdac_lost"]["frame"], 1e9) > t["rdac_lost"]["seconds"]:
            frames = {}
        else:
            frames = {i: d for i, d in frames.items() if ages.get(i, 1e9) <= t["frames"].get(i, 1e9)}

    def u16(i, n):
        d = frames.get(RDAC_BASE + i)
        return struct.unpack_from("<H", d, n)[0] if d is not None and len(d) >= n + 2 else None

    def s16(i, n):
        d = frames.get(RDAC_BASE + i)
        return struct.unpack_from("<h", d, n)[0] if d is not None and len(d) >= n + 2 else None

    out = {}
    cj = s16(7, 0)
    tcs = [s16(2, 0), s16(2, 2), s16(2, 4), s16(2, 6), s16(3, 0), s16(3, 2), s16(3, 4), s16(3, 6)]
    tcs = [None if (v is None or cj is None) else v + cj for v in tcs]
    out["egt"], out["cht"] = tcs[:4], tcs[4:]
    out["oil_temp"] = temperature_c(resistance(u16(5, 0), "oil_temp"))
    out["oil_pressure"] = pressure_bar(resistance(u16(5, 2), "oil_pressure"))
    raw_v = u16(7, 2)
    out["volts"] = None if raw_v is None else raw_v / 57.3758
    c = table()["current"]
    raw_c = u16(8, 6)
    out["amps"] = (None if raw_c is None or c["zero_raw"] is None or c["amps_per_count"] is None
                   else (raw_c - c["zero_raw"]) * c["amps_per_count"])
    p = table()["rpm"]
    raw_r = u16(8, 0)
    out["rpm"] = None if raw_r is None or p["pulses_per_rev"] is None else raw_r / p["pulses_per_rev"]
    return out


def _check():
    r = resistance(428, "oil_pressure")
    assert abs(pressure_bar(r) - 3.1) < 0.1, pressure_bar(r)
    assert abs(temperature_c(120.0) - 77.6) < 0.2
    assert abs(temperature_c(36.5) - 120.0) < 0.1
    assert resistance(3908, "oil_temp") is None and pressure_bar(None) is None
    assert pressure_bar(10.0) == 0.0
    assert abs(pressure_bar(184) - 5.0) < 0.1
    live = {0x202: bytes(8), 0x203: bytes(8), 0x205: bytes([0x29, 1, 0x44, 0xF, 0, 0, 0, 0]), 0x207: bytes([18, 0, 0xAF, 2]),
            0x208: bytes(8)}
    fresh = {i: 0.1 for i in live}
    assert convert(live, fresh)["volts"] is not None
    lost = convert(live, {**fresh, 0x208: 1.2})
    assert all(v is None for k, v in lost.items() if k not in ("egt", "cht")) and lost["egt"] == [None] * 4 and lost["cht"] == [None] * 4
    assert convert(live, {**fresh, 0x207: 2.0})["volts"] is None and convert(live, {**fresh, 0x207: 2.0})["egt"][0] is None
    assert convert(live, {**fresh, 0x205: 2.0})["oil_temp"] is None and convert(live, fresh)["oil_temp"] is not None
    print("engine_convert", VERSION, "self-check OK")


if __name__ == "__main__":
    _check()
