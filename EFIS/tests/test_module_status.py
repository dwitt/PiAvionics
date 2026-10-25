"""Tests for module_status.py. Run from the EFIS folder:  python3 -m unittest discover -s tests -v"""
VERSION = "1.0.0"

import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import module_status as ms


def hb(module, state=1, mask=0x07, uptime=100, version=(1, 0, 15)):
    """A heartbeat payload as the modules send it."""
    return bytes([module, version[0], version[1], version[2], state, mask,
                  uptime & 0xFF, (uptime >> 8) & 0xFF])


ALL_DATA = [ms.ID_AIR_DATA, ms.ID_OAT, ms.ID_RAW_PRESSURE, ms.ID_AHRS, ms.ID_ACCEL,
            ms.ID_GPS_POSITION, ms.ID_GPS_SPEED, ms.ID_GPS_STATUS]


class Bench:
    """A tracker plus a fake clock. Everything starts healthy at t = 10 s."""

    def __init__(self):
        self.t = 0.0
        self.tracker = ms.StatusTracker(0.0)
        self.seen = {}
        self.t = 10.0
        self.heartbeats(imu=(1, 0x07), air=(1, 0x0F), gps=(1, 0x03))
        self.data(ALL_DATA)

    def heartbeats(self, imu=None, air=None, gps=None, uptime=100):
        for number, spec in ((1, imu), (2, air), (3, gps)):
            if spec is not None:
                self.tracker.on_heartbeat(0x700 + number, hb(number, spec[0], spec[1], uptime), self.t)

    def data(self, ids):
        for i in ids:
            self.seen[i] = self.t

    def result(self):
        return self.tracker.evaluate(self.t, self.seen)

    @staticmethod
    def texts(result, level=None):
        return [a["text"] for a in result["annunciations"] if level is None or a["level"] == level]


class HealthyTests(unittest.TestCase):
    def test_all_healthy(self):
        b = Bench()
        r = b.result()
        self.assertTrue(all(r["valid"].values()) or
                        all(r["valid"][k] for k in ("airspeed", "altitude", "vsi", "oat", "attitude",
                                                     "heading", "turn_rate", "acceleration", "gps_position")))
        self.assertFalse(any(r["data_lost"].values()))
        self.assertEqual(r["system_state"], ms.STATE_OK)
        self.assertEqual(r["modules_ok_mask"], 0x07)
        self.assertEqual(r["modules"]["imu"]["version"], "1.0.15")
        self.assertFalse(r["heading"]["suspect"])
        self.assertTrue(all(r["modules"]["air"]["sensors"].values()))
        self.assertEqual(set(r["modules"]["air"]["sensors"]), {"pitot", "aoa", "static", "oat"})
        self.assertEqual(set(r["modules"]["imu"]["sensors"]), {"gyro_accel", "magnetometer", "calibration"})
        self.assertEqual(set(r["modules"]["gps"]["sensors"]), {"nmea", "fix"})

    def test_json_serialisable(self):
        b = Bench()
        json.dumps(b.result())

    def test_no_display_text_is_produced(self):
        b = Bench()
        b.heartbeats(air=(2, 0x0E))
        self.assertNotIn("annunciations", b.result())


class StartupTests(unittest.TestCase):
    def test_nothing_heard_inside_grace(self):
        tr = ms.StatusTracker(0.0)
        r = tr.evaluate(2.0, {})
        self.assertFalse(any(r["valid"].values()))          # instruments are invalid straight away
        self.assertFalse(any(r["data_lost"].values()))       # but nothing is "lost" during start-up
        self.assertFalse(r["modules"]["air"]["missing"])
        self.assertIsNone(r["modules"]["air"]["sensors"])

    def test_nothing_heard_after_grace(self):
        tr = ms.StatusTracker(0.0)
        r = tr.evaluate(ms.STARTUP_GRACE + 1, {})
        self.assertTrue(all(r["modules"][n]["missing"] for n in ("imu", "air", "gps")))
        self.assertTrue(r["data_lost"]["air_data"])
        self.assertTrue(r["data_lost"]["attitude"])
        self.assertEqual(r["system_state"], ms.STATE_DEGRADED)
        self.assertEqual(r["modules_ok_mask"], 0)

    def test_starting_state_mask_is_ignored(self):
        b = Bench()
        b.heartbeats(air=(ms.STATE_STARTING, 0x00))
        r = b.result()
        self.assertTrue(r["valid"]["airspeed"])               # data is flowing, mask means nothing yet
        self.assertIsNone(r["modules"]["air"]["sensors"])


class AirTests(unittest.TestCase):
    def test_pitot_lost(self):
        b = Bench()
        b.heartbeats(air=(2, 0x0E))
        r = b.result()
        self.assertFalse(r["valid"]["airspeed"])
        self.assertFalse(r["valid"]["differential_pressure"])
        self.assertTrue(r["valid"]["altitude"])
        self.assertEqual(r["modules"]["air"]["sensors"],
                         {"pitot": False, "aoa": True, "static": True, "oat": True})
        self.assertEqual(r["modules"]["air"]["state"], 2)
        self.assertEqual(r["system_state"], ms.STATE_DEGRADED)
        self.assertFalse(r["data_lost"]["air_data"])           # the data is arriving, the sensor is lost

    def test_static_lost(self):
        b = Bench()
        b.heartbeats(air=(2, 0x0B))
        r = b.result()
        self.assertFalse(r["valid"]["altitude"])
        self.assertFalse(r["valid"]["vsi"])
        self.assertTrue(r["valid"]["airspeed"])
        self.assertFalse(r["modules"]["air"]["sensors"]["static"])

    def test_oat_lost(self):
        b = Bench()
        b.heartbeats(air=(2, 0x07))
        r = b.result()
        self.assertFalse(r["valid"]["oat"])
        self.assertTrue(r["valid"]["airspeed"])
        self.assertFalse(r["modules"]["air"]["sensors"]["oat"])

    def test_aoa_lost(self):
        b = Bench()
        b.heartbeats(air=(2, 0x0D))
        r = b.result()
        self.assertFalse(r["modules"]["air"]["sensors"]["aoa"])
        self.assertTrue(r["valid"]["airspeed"] and r["valid"]["altitude"])

    def test_air_data_timeout(self):
        b = Bench()
        b.t += 0.4
        r = b.result()
        self.assertTrue(r["valid"]["airspeed"])               # inside 0.5 s
        self.assertFalse(r["data_lost"]["air_data"])
        b.t += 0.2                                            # 0.6 s since the last 0x28
        r = b.result()
        self.assertFalse(r["valid"]["airspeed"])
        self.assertTrue(r["data_lost"]["air_data"])

    def test_oat_has_a_longer_timeout(self):
        b = Bench()
        b.data([ms.ID_AIR_DATA])
        b.t += 1.0
        b.data([ms.ID_AIR_DATA])
        self.assertTrue(b.result()["valid"]["oat"])           # 1.0 s < 1.5 s
        b.t += 1.0
        b.data([ms.ID_AIR_DATA])
        r = b.result()
        self.assertFalse(r["valid"]["oat"])                   # 2.0 s > 1.5 s
        self.assertTrue(r["data_lost"]["oat"])
        self.assertFalse(r["data_lost"]["air_data"])

    def test_air_module_goes_missing(self):
        b = Bench()
        b.t += 2.9
        b.data(ALL_DATA)
        b.heartbeats(imu=(1, 0x07), gps=(1, 0x03))              # only the AIR heartbeat has stopped
        self.assertTrue(b.result()["modules"]["air"]["present"])
        b.t += 0.2                                              # 3.1 s since the last AIR heartbeat
        b.data(ALL_DATA)
        b.heartbeats(imu=(1, 0x07), gps=(1, 0x03))
        r = b.result()
        self.assertTrue(r["modules"]["air"]["missing"])
        self.assertFalse(r["modules"]["air"]["present"])
        self.assertIsNone(r["modules"]["air"]["state"])
        self.assertIsNone(r["modules"]["air"]["sensors"])
        self.assertEqual(r["system_state"], ms.STATE_DEGRADED)
        self.assertEqual(r["modules_ok_mask"], 0x06)            # IMU and GPS still healthy


class ImuTests(unittest.TestCase):
    def test_accel_gyro_lost(self):
        b = Bench()
        b.heartbeats(imu=(3, 0x04))
        r = b.result()
        self.assertFalse(r["valid"]["attitude"])
        self.assertFalse(r["valid"]["heading"])
        self.assertFalse(r["valid"]["turn_rate"])
        self.assertFalse(r["valid"]["acceleration"])
        self.assertFalse(r["modules"]["imu"]["sensors"]["gyro_accel"])
        self.assertFalse(r["heading"]["suspect"])

    def test_magnetometer_lost_reports_how_long_the_heading_has_been_gyro_only(self):
        b = Bench()
        b.heartbeats(imu=(2, 0x05))
        r = b.result()
        self.assertTrue(r["valid"]["heading"])
        self.assertTrue(r["heading"]["suspect"])
        self.assertFalse(r["modules"]["imu"]["sensors"]["magnetometer"])
        self.assertAlmostEqual(r["heading"]["gyro_seconds"], 0.0, delta=0.2)
        for _ in range(290):                                   # 4 min 50 s later, heartbeats continuing
            b.t += 1.0
            b.heartbeats(imu=(2, 0x05), air=(1, 0x0F), gps=(1, 0x03))
            b.data(ALL_DATA)
        r = b.result()
        self.assertAlmostEqual(r["heading"]["gyro_seconds"], 290.0, delta=1.0)
        self.assertTrue(r["heading"]["suspect"])
        self.assertNotIn("blank", r["heading"])                # the page decides when to blank it

    def test_magnetometer_back_clears_the_timer(self):
        b = Bench()
        b.heartbeats(imu=(2, 0x05))
        b.t += 30
        b.data(ALL_DATA)
        b.heartbeats(imu=(1, 0x07), air=(1, 0x0F), gps=(1, 0x03))
        r = b.result()
        self.assertFalse(r["heading"]["suspect"])
        self.assertIsNone(r["heading"]["gyro_seconds"])
        b.heartbeats(imu=(2, 0x05))                            # lost again: timer starts from now
        b.t += 5
        b.data(ALL_DATA)
        b.heartbeats(imu=(2, 0x05), air=(1, 0x0F), gps=(1, 0x03))
        self.assertAlmostEqual(b.result()["heading"]["gyro_seconds"], 5.0, delta=0.2)

    def test_calibration_missing(self):
        b = Bench()
        b.heartbeats(imu=(2, 0x03))
        r = b.result()
        self.assertFalse(r["modules"]["imu"]["sensors"]["calibration"])
        self.assertTrue(r["valid"]["attitude"])

    def test_imu_missing(self):
        b = Bench()
        b.t += 3.5
        b.data(ALL_DATA)
        b.heartbeats(air=(1, 0x0F), gps=(1, 0x03))
        r = b.result()
        self.assertTrue(r["modules"]["imu"]["missing"])
        self.assertFalse(r["heading"]["suspect"])

    def test_attitude_data_stops_while_heartbeat_continues(self):
        b = Bench()
        b.t += 0.7
        b.heartbeats(imu=(1, 0x07), air=(1, 0x0F), gps=(1, 0x03))
        b.data([i for i in ALL_DATA if i != ms.ID_AHRS])
        r = b.result()
        self.assertFalse(r["valid"]["attitude"])
        self.assertTrue(r["data_lost"]["attitude"])
        self.assertFalse(r["modules"]["imu"]["missing"])


class GpsTests(unittest.TestCase):
    def test_no_fix_is_not_data_lost(self):
        b = Bench()
        b.t += 1.0
        b.heartbeats(imu=(1, 0x07), air=(1, 0x0F), gps=(1, 0x01))   # NMEA yes, fix no
        b.data([i for i in ALL_DATA if i not in (ms.ID_GPS_POSITION, ms.ID_GPS_SPEED)])
        r = b.result()
        self.assertTrue(r["valid"]["gps_position"])            # still inside 3.5 s
        self.assertEqual(r["modules"]["gps"]["sensors"], {"nmea": True, "fix": False})

    def test_position_times_out_after_3_5_s(self):
        b = Bench()
        for _ in range(4):
            b.t += 1.0
            b.heartbeats(imu=(1, 0x07), air=(1, 0x0F), gps=(1, 0x01))
            b.data([i for i in ALL_DATA if i not in (ms.ID_GPS_POSITION, ms.ID_GPS_SPEED)])
        r = b.result()
        self.assertFalse(r["valid"]["gps_position"])
        self.assertTrue(r["data_lost"]["gps_position"])
        self.assertFalse(r["data_lost"]["gps_status"])         # the GPS is talking, there is just no fix

    def test_gps_silent(self):
        b = Bench()
        b.heartbeats(imu=(1, 0x07), air=(1, 0x0F), gps=(2, 0x00))
        r = b.result()
        self.assertEqual(r["modules"]["gps"]["sensors"], {"nmea": False, "fix": False})
        self.assertFalse(r["valid"]["gps_position"])


class RestartTests(unittest.TestCase):
    def test_first_heartbeat_is_not_a_restart(self):
        b = Bench()
        self.assertIsNone(b.result()["modules"]["air"]["restarted_age"])

    def test_uptime_drop_reports_the_time_since_the_restart(self):
        b = Bench()
        b.heartbeats(air=(1, 0x0F), uptime=3)                  # was 100
        self.assertEqual(b.result()["modules"]["air"]["restarted_age"], 0.0)
        b.t += 7.0
        b.data(ALL_DATA)
        b.heartbeats(air=(1, 0x0F), uptime=10)                 # AIR continues from its new start
        b.heartbeats(imu=(1, 0x07), gps=(1, 0x03), uptime=107) # the others carry on counting up
        self.assertAlmostEqual(b.result()["modules"]["air"]["restarted_age"], 7.0, delta=0.1)
        self.assertIsNone(b.result()["modules"]["imu"]["restarted_age"])


class BackupTests(unittest.TestCase):
    def test_backup_heartbeat_is_reported(self):
        b = Bench()
        b.tracker.on_heartbeat(0x704, hb(4, 1, 0x03, 50, (1, 7, 11)), b.t)
        m = b.result()["modules"]["backup"]
        self.assertTrue(m["present"])
        self.assertEqual(m["version"], "1.7.11")
        self.assertEqual(m["sensors"], {"lcd": True, "encoder": True})

    def test_backup_never_changes_the_system_state(self):
        b = Bench()
        b.t += 60.0
        b.data(ALL_DATA)
        b.heartbeats(imu=(1, 0x07), air=(1, 0x0F), gps=(1, 0x03))
        r = b.result()
        self.assertTrue(r["modules"]["backup"]["missing"])
        self.assertEqual(r["system_state"], ms.STATE_OK)
        self.assertEqual(r["modules_ok_mask"], 0x07)
        b.tracker.on_heartbeat(0x704, hb(4, 2, 0x01), b.t)     # degraded: no encoder
        self.assertEqual(b.result()["system_state"], ms.STATE_OK)


class HeartbeatParsingTests(unittest.TestCase):
    def test_wrong_module_number_is_ignored(self):
        tr = ms.StatusTracker(0.0)
        self.assertFalse(tr.on_heartbeat(0x702, hb(1), 1.0))   # ID says AIR, payload says IMU
        self.assertIsNone(tr.modules[ms.MODULE_AIR].last_heard)

    def test_short_payload_is_ignored(self):
        tr = ms.StatusTracker(0.0)
        self.assertFalse(tr.on_heartbeat(0x701, bytes([1, 1, 0]), 1.0))

    def test_unknown_module_is_ignored(self):
        tr = ms.StatusTracker(0.0)
        self.assertFalse(tr.on_heartbeat(0x705, hb(5), 1.0))
        self.assertFalse(tr.on_heartbeat(0x700, hb(0), 1.0))

    def test_uptime_is_16_bit(self):
        tr = ms.StatusTracker(0.0)
        tr.on_heartbeat(0x702, hb(2, uptime=0x1234), 1.0)
        self.assertEqual(tr.modules[ms.MODULE_AIR].uptime, 0x1234)


if __name__ == "__main__":
    unittest.main()
