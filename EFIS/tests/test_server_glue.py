"""Runs the real aio_server handlers and receive loop with a fake CAN reader.
Needs the server's own libraries (aiohttp, python-can, ...), so it is skipped where they are not
installed (for example on the Mac) and is run on the Pi:  python3 -m unittest discover -s tests -v"""
VERSION = "1.5.0"

import asyncio
import contextlib
import io
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
else:
    DEFAULT_NULL_INVALID_VALUES = srv.NULL_INVALID_VALUES      # before the tests change it


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


def pressure_frame(static=9420, temperature=22, differential=620):
    return FakeMsg(0x2B, struct.pack("<hbhbbb", static, temperature, differential, 0, 0, 0))


def accel_frame(x=10, y=-20, z=981, calibration=3):
    return FakeMsg(0x49, struct.pack("<hhhh", x, y, z, calibration))


MARKER16 = 0x7FFF
MARKER32 = 0x7FFFFFFF


def gps_speed_frame(speed=112, altitude=2410, true_track=2, mag_track=352):
    return FakeMsg(0x64, struct.pack("<hhhh", speed, altitude, true_track, mag_track))


def gps_position_frame(latitude=43384272, longitude=-80104167):
    return FakeMsg(0x63, struct.pack("<ll", latitude, longitude))


def gps_status_frame(fix_quality=2, fix_type=3, satellites=12, declination=-10):
    # byte 3 is the magnetic declination, whole degrees, East positive (0x7F = not calculated yet)
    return FakeMsg(0x65, struct.pack("<BBBbhh", fix_quality, fix_type, satellites, declination, 90, 130))


def run_loop(data, times, msgs):
    try:
        asyncio.run(srv.process_can_messages(FakeReader(msgs), data, times))
    except asyncio.CancelledError:
        pass


class FakeBus:
    """Records what is sent; `error` makes every send fail like a bus nothing acknowledges."""
    def __init__(self, error=None):
        self.sent = []
        self.error = error

    def send(self, msg, timeout=None):
        if self.error is not None:
            raise self.error
        self.sent.append((msg.arbitration_id, bytes(msg.data), timeout))


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

    def test_untrusted_values_are_sent_as_null_by_default(self):
        self.assertTrue(DEFAULT_NULL_INVALID_VALUES)

    def test_mag_track_is_decoded_next_to_the_true_track(self):
        run_loop(self.data, self.times, [gps_speed_frame(speed=95, true_track=2, mag_track=352)])
        self.assertEqual(self.data.gps_speed, 95)
        self.assertEqual(self.data.true_track, 2)
        self.assertEqual(self.data.mag_track, 352)
        self.assertIn("mag_track", {k: v for k, v in self.data.__dict__.items() if not k.startswith("_")})

    def test_gps_lost_clears_position_speed_track_and_navigation(self):
        srv.NULL_INVALID_VALUES = True
        self.healthy(gps_mask=0x00)                               # GPS module present but no NMEA
        run_loop(self.data, self.times, [gps_position_frame(), gps_speed_frame()])
        self.data.xtrack, self.data.dist, self.data.bearing, self.data.to_from = 0.4, 12.3, 91.0, "TO"
        self.data._dtk_true, self.data._bearing_true, self.data.wpt_id = 92.0, 91.0, "CYFD"
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertFalse(self.data.valid["gps_position"])
        for name in ("latitude", "longitude", "gps_speed", "gps_altitude", "true_track", "mag_track",
                     "xtrack", "dist", "bearing", "to_from"):
            self.assertIsNone(getattr(self.data, name), name)
        self.assertEqual(self.data.dtk, 102.0)                    # the leg's course does not depend on the position (declination -10)
        self.assertEqual(self.data.wpt_id, "CYFD")

    def test_gps_with_a_fix_keeps_position_speed_track_and_navigation(self):
        srv.NULL_INVALID_VALUES = True
        self.healthy()
        run_loop(self.data, self.times, [gps_position_frame(), gps_speed_frame()])
        self.data.xtrack, self.data.dist, self.data.bearing, self.data.to_from = 0.4, 12.3, 91.0, "TO"
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertTrue(self.data.valid["gps_position"])
        self.assertEqual(self.data.mag_track, 352)
        self.assertEqual(self.data.true_track, 2)
        self.assertEqual((self.data.xtrack, self.data.dist, self.data.to_from), (0.4, 12.3, "TO"))
        self.assertEqual(self.data.latitude, 43384272)

    # --- magnetic declination from the GPS module and the courses converted to magnetic (ISSUES.md #15) ---

    def test_declination_is_decoded_from_the_gps_status_message(self):
        run_loop(self.data, self.times, [gps_status_frame(declination=-10)])
        self.assertEqual(self.data.gps_declination, -10)
        run_loop(self.data, self.times, [gps_status_frame(declination=7)])
        self.assertEqual(self.data.gps_declination, 7)
        run_loop(self.data, self.times, [gps_status_frame(declination=0)])
        self.assertEqual(self.data.gps_declination, 0)             # a real declination of zero is not "unknown"
        run_loop(self.data, self.times, [gps_status_frame(declination=0x7F)])
        self.assertIsNone(self.data.gps_declination)               # 0x7F = not calculated yet
        json.dumps({k: v for k, v in self.data.__dict__.items() if not k.startswith("_")})

    def test_true_to_magnetic(self):
        self.assertEqual(srv.to_magnetic(92.0, -10), 102.0)        # west declination: magnetic is larger
        self.assertEqual(srv.to_magnetic(92.0, 10), 82.0)
        self.assertEqual(srv.to_magnetic(5.0, 10), 355.0)          # wraps below 0
        self.assertEqual(srv.to_magnetic(350.0, -20), 10.0)        # and above 360
        self.assertEqual(srv.to_magnetic(92.3, -10), 102.3)
        self.assertEqual(srv.to_magnetic(0.0, 0), 0.0)
        self.assertIsNone(srv.to_magnetic(None, -10))
        self.assertIsNone(srv.to_magnetic(92.0, None))

    def test_flight_plan_courses_are_sent_in_magnetic_degrees(self):
        srv.NULL_INVALID_VALUES = True
        self.healthy()                                             # declination -10 from the status frame
        run_loop(self.data, self.times, [gps_position_frame(), gps_speed_frame()])
        self.data._dtk_true, self.data._bearing_true = 92.0, 95.5
        self.data.xtrack, self.data.to_from, self.data.dist, self.data.wpt_id = 0.4, "TO", 12.3, "CYFD"
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertEqual(self.data.dtk, 102.0)
        self.assertEqual(self.data.bearing, 105.5)
        self.assertEqual((self.data.xtrack, self.data.to_from, self.data.dist), (0.4, "TO", 12.3))

    def test_navigation_is_held_back_until_the_declination_is_known(self):
        srv.NULL_INVALID_VALUES = True
        run_loop(self.data, self.times, [
            heartbeat(1, 1, 0x07), heartbeat(2, 1, 0x0F), heartbeat(3, 1, 0x03),
            air_frame(), ahrs_frame(), oat_frame(), gps_status_frame(declination=0x7F),
            gps_position_frame(), gps_speed_frame(),
        ])
        self.data._dtk_true, self.data._bearing_true = 92.0, 95.5
        self.data.xtrack, self.data.to_from, self.data.dist, self.data.wpt_id = 0.4, "TO", 12.3, "CYFD"
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertIsNone(self.data.gps_declination)
        self.assertIsNone(self.data.dtk)                           # no usable course yet ...
        self.assertIsNone(self.data.bearing)
        self.assertIsNone(self.data.xtrack)                        # ... so no cross-track or TO/FROM either
        self.assertIsNone(self.data.to_from)
        self.assertEqual((self.data.dist, self.data.wpt_id), (12.3, "CYFD"))
        # and with no flight plan there is nothing to hold back
        self.data._dtk_true = self.data._bearing_true = None
        self.data.xtrack = 0.4
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertEqual(self.data.xtrack, 0.4)

    # --- invalid-data markers (protocol 0.2.0): a marker is never shown as a number ---

    def send(self, *frames):
        run_loop(self.data, self.times, list(frames))
        srv.update_status(self.data, self.times, time.monotonic())

    def test_no_marker_in_a_healthy_run(self):
        self.healthy()
        run_loop(self.data, self.times, [pressure_frame(), accel_frame()])
        srv.update_status(self.data, self.times, time.monotonic())
        self.assertEqual(self.data._markers, set())
        self.assertEqual(self.data.accz, 981)
        self.assertEqual(self.data.static_pressure, 9420)

    def test_airspeed_marker_gives_none_and_an_invalid_airspeed_only(self):
        self.healthy()
        self.send(air_frame(airspeed=MARKER16, altitude=2400, vsi=150))
        self.assertIsNone(self.data.airspeed)
        self.assertFalse(self.data.valid["airspeed"])
        self.assertEqual(self.data.altitude, 2400)
        self.assertEqual(self.data.vsi, 150)
        self.assertTrue(self.data.valid["altitude"])
        self.assertTrue(self.data.valid["vsi"])

    def test_altitude_and_vertical_speed_markers(self):
        self.healthy()
        self.send(air_frame(airspeed=112, altitude=MARKER32, vsi=MARKER16))
        self.assertIsNone(self.data.altitude)                     # never 2147483647
        self.assertIsNone(self.data.vsi)                          # never 32767
        self.assertFalse(self.data.valid["altitude"])
        self.assertFalse(self.data.valid["vsi"])
        self.assertTrue(self.data.valid["airspeed"])
        self.assertEqual(self.data.airspeed, 112)

    def test_a_marker_clears_when_a_normal_frame_arrives(self):
        self.healthy()
        self.send(air_frame(airspeed=MARKER16))
        self.assertFalse(self.data.valid["airspeed"])
        self.send(air_frame(airspeed=95))
        self.assertTrue(self.data.valid["airspeed"])
        self.assertEqual(self.data.airspeed, 95)
        self.assertEqual(self.data._markers, set())

    def test_marker_makes_the_value_invalid_even_with_the_heartbeat_saying_ok(self):
        self.healthy(air_mask=0x0F)
        self.send(air_frame(airspeed=MARKER16))
        self.assertTrue(self.data.modules["air"]["sensors"]["pitot"])
        self.assertFalse(self.data.valid["airspeed"])

    def test_oat_marker(self):
        self.healthy()
        self.send(oat_frame(oat=MARKER16))
        self.assertIsNone(self.data.oat)                          # never 3276.7
        self.assertFalse(self.data.valid["oat"])
        self.send(oat_frame(oat=228))
        self.assertEqual(self.data.oat, 22.8)
        self.assertTrue(self.data.valid["oat"])

    def test_pressure_markers(self):
        self.healthy()
        self.send(pressure_frame(static=MARKER16, differential=620))
        self.assertIsNone(self.data.static_pressure)
        self.assertFalse(self.data.valid["static_pressure"])
        self.assertEqual(self.data.differential_pressure, 620)
        self.assertTrue(self.data.valid["differential_pressure"])
        self.send(pressure_frame(static=9420, differential=MARKER16))
        self.assertIsNone(self.data.differential_pressure)
        self.assertFalse(self.data.valid["differential_pressure"])
        self.assertTrue(self.data.valid["static_pressure"])

    def test_heading_marker_blanks_the_heading_but_not_the_attitude(self):
        self.healthy()
        self.send(ahrs_frame(yaw=MARKER16, pitch=20, roll=-30, turn=5))
        self.assertIsNone(self.data.yaw)                          # never 3276.7
        self.assertFalse(self.data.valid["heading"])
        self.assertFalse(self.data.heading_status["valid"])
        self.assertTrue(self.data.valid["attitude"])
        self.assertEqual((self.data.pitch, self.data.roll), (2.0, -3.0))

    def test_heading_marker_with_the_magnetometer_lost_is_not_a_gyro_heading(self):
        self.healthy(imu_mask=0x05)
        self.send(ahrs_frame(yaw=MARKER16))
        self.assertFalse(self.data.valid["heading"])
        self.assertFalse(self.data.heading_status["suspect"])
        self.assertIsNone(self.data.heading_status["gyro_seconds"])

    def test_pitch_or_roll_marker_makes_the_attitude_invalid(self):
        self.healthy()
        self.send(ahrs_frame(yaw=900, pitch=MARKER16, roll=-30))
        self.assertIsNone(self.data.pitch)
        self.assertFalse(self.data.valid["attitude"])
        self.assertTrue(self.data.valid["heading"])
        self.send(ahrs_frame(yaw=900, pitch=20, roll=MARKER16))
        self.assertIsNone(self.data.roll)
        self.assertFalse(self.data.valid["attitude"])
        self.send(ahrs_frame(yaw=900, pitch=20, roll=-30))
        self.assertTrue(self.data.valid["attitude"])

    def test_turn_rate_marker(self):
        self.healthy()
        self.send(ahrs_frame(turn=MARKER16))
        self.assertIsNone(self.data.turn_rate)
        self.assertFalse(self.data.valid["turn_rate"])
        self.assertTrue(self.data.valid["attitude"])

    def test_accel_marker_on_any_axis_makes_the_acceleration_invalid(self):
        self.healthy()
        for axis, frame in (("accx", accel_frame(x=MARKER16)), ("accy", accel_frame(y=MARKER16)),
                            ("accz", accel_frame(z=MARKER16))):
            self.send(accel_frame())                              # a normal frame first
            self.assertTrue(self.data.valid["acceleration"])
            self.send(frame)
            self.assertIsNone(getattr(self.data, axis), axis)
            self.assertFalse(self.data.valid["acceleration"], axis)

    def test_a_marker_invalidates_at_once_not_only_at_the_next_status_check(self):
        self.healthy()
        srv.update_status(self.data, self.times, time.monotonic())
        run_loop(self.data, self.times, [air_frame(airspeed=MARKER16)])       # no status check yet
        self.assertIsNone(self.data.airspeed)                                  # the value is null at once ...
        self.assertTrue(self.data.valid["airspeed"])                           # ... but the flag is still the old one
        srv.apply_markers_to_valid(self.data, self.data.valid)                 # what the sending loop does
        self.assertFalse(self.data.valid["airspeed"])
        self.assertTrue(self.data.valid["altitude"])

    def test_markers_make_values_invalid_with_the_blanking_gate_off_too(self):
        srv.NULL_INVALID_VALUES = False
        self.healthy()
        self.send(air_frame(airspeed=MARKER16))
        self.assertIsNone(self.data.airspeed)                     # a marker is never kept as a number
        self.assertFalse(self.data.valid["airspeed"])

    # --- the EFIS heartbeat task (CAN 0x700) ---

    def run_heartbeat(self, bus, seconds=0.35, period=0.05, **kwargs):
        saved = {name: getattr(srv, name) for name in ["EFIS_HEARTBEAT_PERIOD"] + list(kwargs)}
        srv.EFIS_HEARTBEAT_PERIOD = period
        for name, value in kwargs.items():
            setattr(srv, name, value)
        heartbeat = srv.efis_heartbeat.EfisHeartbeat("1.6.1", time.monotonic())
        output = io.StringIO()

        async def run_briefly():
            task = asyncio.create_task(srv.send_efis_heartbeat(self.data, bus, heartbeat))
            await asyncio.sleep(seconds)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        try:
            with contextlib.redirect_stdout(output):
                asyncio.run(run_briefly())
        finally:
            for name, value in saved.items():
                setattr(srv, name, value)
        return output.getvalue()

    def test_efis_heartbeat_is_sent_repeatedly_with_the_right_content(self):
        self.healthy()
        srv.update_status(self.data, self.times, time.monotonic())
        self.data.clients = {"controller": 1, "viewers": 2}
        bus = FakeBus()
        self.run_heartbeat(bus)
        self.assertGreaterEqual(len(bus.sent), 4)
        can_id, payload, timeout = bus.sent[0]
        self.assertEqual(can_id, 0x700)
        self.assertEqual(len(payload), 8)
        self.assertEqual(payload[:6], bytes([0, 1, 6, 1, 1, 0x0F]))          # module 0, v1.6.1, state OK, all four bits
        self.assertEqual(timeout, srv.EFIS_HEARTBEAT_SEND_TIMEOUT)             # a send can never wait for long
        self.assertEqual(self.data.efis["state"], 1)
        self.assertEqual(self.data.efis["display_clients"], 1)                 # viewers are not counted
        json.dumps({k: v for k, v in self.data.__dict__.items() if not k.startswith("_")})

    def test_efis_heartbeat_shows_a_missing_module_and_no_display(self):
        self.healthy(gps_mask=0x03)
        srv.update_status(self.data, self.times, time.monotonic())
        self.data._modules_ok_mask = 0x05                                      # IMU not healthy
        self.data.clients = {"controller": 0, "viewers": 1}
        bus = FakeBus()
        self.run_heartbeat(bus, seconds=0.12)
        payload = bus.sent[0][1]
        self.assertEqual((payload[4], payload[5]), (srv.efis_heartbeat.STATE_DEGRADED, 0x05))   # no display bit
        self.assertEqual(self.data.efis["display_clients"], 0)

    def test_efis_heartbeat_with_no_bus_still_publishes_the_facts(self):
        self.data.clients = None
        self.run_heartbeat(None, seconds=0.12)
        self.assertIsNotNone(self.data.efis)
        self.assertEqual(self.data.efis["display_clients"], 0)

    def test_failed_heartbeat_sends_do_not_stop_the_task_or_flood_the_log(self):
        import can
        bus = FakeBus(error=can.CanError("No buffer space available"))
        output = self.run_heartbeat(bus, seconds=0.4)                          # about 8 failed sends
        lines = [line for line in output.splitlines() if line.strip()]
        self.assertEqual(len(lines), 1)                                        # reported once, not 8 times
        self.assertIn("send failure", lines[0])
        self.assertIn("No buffer space", lines[0])
        self.assertIsNotNone(self.data.efis)                                   # and the task kept running

    def test_failed_sends_are_reported_again_after_the_log_period_with_a_count(self):
        import can
        bus = FakeBus(error=can.CanError("no ack"))
        output = self.run_heartbeat(bus, seconds=0.5, EFIS_HEARTBEAT_LOG_PERIOD=0.2)
        lines = [line for line in output.splitlines() if line.strip()]
        self.assertGreaterEqual(len(lines), 2)
        self.assertLess(len(lines), 8)

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
