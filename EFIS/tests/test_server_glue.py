"""Runs the real aio_server handlers and receive loop with a fake CAN reader.
Needs the server's own libraries (aiohttp, python-can, ...), so it is skipped where they are not
installed (for example on the Mac) and is run on the Pi:  python3 -m unittest discover -s tests -v"""
VERSION = "1.0.0"

import asyncio
import json
import os
import struct
import sys
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
try:
    import aio_server as srv
except Exception as exc:       # missing libraries: skip
    srv = None
    SKIP_REASON = f"aio_server cannot be imported here: {exc}"


class FakeMsg:
    def __init__(self, can_id, data):
        self.arbitration_id = can_id
        self.data = bytes(data)


class FakeReader:
    """Gives the receive loop its messages, then cancels it."""
    def __init__(self, msgs):
        self.msgs = list(msgs)

    async def get_message(self):
        if not self.msgs:
            raise asyncio.CancelledError()
        return self.msgs.pop(0)


def heartbeat(number, state=1, mask=0x07, uptime=100):
    return FakeMsg(0x700 + number, bytes([number, 1, 0, 15, state, mask, uptime & 255, uptime >> 8]))


def air_frame(airspeed=112, altitude=2400, vsi=0):
    return FakeMsg(0x28, struct.pack("<hih", airspeed, altitude, vsi))


def ahrs_frame(yaw=900, pitch=20, roll=-30, turn=0):
    return FakeMsg(0x48, struct.pack("<hhhh", yaw, pitch, roll, turn))


def oat_frame(oat=228):
    return FakeMsg(0x2A, struct.pack("<hBBBBBB", oat, 0, 0, 0, 0, 0, 0))


def gps_status_frame(fix_quality=2, fix_type=3, satellites=12):
    return FakeMsg(0x65, struct.pack("<BBBBhh", fix_quality, fix_type, satellites, 0, 90, 130))


def run_loop(data, times, msgs):
    try:
        asyncio.run(srv.process_can_messages(FakeReader(msgs), data, times))
    except asyncio.CancelledError:
        pass


@unittest.skipIf(srv is None, "aio_server cannot be imported here")
class ServerGlueTests(unittest.TestCase):
    def setUp(self):
        srv.NULL_INVALID_VALUES = False
        self.data = srv.AvionicsData()
        self.times = {}
        self.data._status.start = time.monotonic() - 60           # past the start-up grace period

    def healthy(self, imu_mask=0x07, air_mask=0x0F, gps_mask=0x03):
        run_loop(self.data, self.times, [
            heartbeat(1, 1, imu_mask), heartbeat(2, 1, air_mask), heartbeat(3, 1, gps_mask),
            air_frame(), ahrs_frame(), oat_frame(), gps_status_frame(),
        ])

    def test_healthy_run(self):
        self.healthy()
        snap = srv.update_status(self.data, self.times, time.monotonic())
        self.assertTrue(self.data.valid["airspeed"])
        self.assertTrue(self.data.valid["attitude"])
        self.assertEqual(self.data.airspeed, 112)
        self.assertEqual(self.data.altitude, 2400)
        self.assertEqual(self.data.modules["air"]["version"], "1.0.15")
        self.assertEqual(self.data.modules["imu"]["state"], 1)
        json.dumps({k: v for k, v in self.data.__dict__.items() if not k.startswith("_")})

    def test_pitot_lost_flags_but_keeps_the_number_while_gate_is_off(self):
        self.healthy(air_mask=0x0E)
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertFalse(self.data.valid["airspeed"])
        self.assertTrue(self.data.valid["altitude"])
        self.assertEqual(self.data.airspeed, 112)                 # old display keeps working
        self.assertFalse(self.data.modules["air"]["sensors"]["pitot"])
        self.assertTrue(self.data.modules["air"]["sensors"]["static"])

    def test_pitot_lost_clears_the_number_when_gate_is_on(self):
        srv.NULL_INVALID_VALUES = True
        self.healthy(air_mask=0x0E)
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertIsNone(self.data.airspeed)
        self.assertEqual(self.data.altitude, 2400)

    def test_accel_gyro_lost_clears_attitude(self):
        self.healthy(imu_mask=0x04)
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertFalse(self.data.valid["attitude"])
        self.assertIsNone(self.data.pitch)
        self.assertIsNone(self.data.roll)
        self.assertIsNone(self.data.yaw)

    def test_stale_attitude_clears_like_the_old_monitor(self):
        self.healthy()
        later = time.monotonic() + 0.7
        for k in self.times:                                      # everything else stays fresh
            if k != 0x48:
                self.times[k] = later
        srv.update_status(self.data, self.times, later)
        self.assertIsNone(self.data.pitch)
        self.assertTrue(self.data.valid["airspeed"])

    def test_mag_lost_keeps_the_heading(self):
        self.healthy(imu_mask=0x05)
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertEqual(self.data.yaw, 90.0)
        self.assertTrue(self.data.heading_status["suspect"])
        self.assertFalse(self.data.modules["imu"]["sensors"]["magnetometer"])
        self.assertIsNotNone(self.data.heading_status["gyro_seconds"])

    def test_monitor_status_task_updates_and_sleeps(self):
        self.healthy()

        async def run_briefly():
            task = asyncio.create_task(srv.monitor_status(self.data, self.times))
            await asyncio.sleep(0.25)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        asyncio.run(run_briefly())
        self.assertIsNotNone(self.data.valid)
        self.assertIsNotNone(self.data.data_lost)


if __name__ == "__main__":
    unittest.main()
