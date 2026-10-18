"""Tests for efis_heartbeat.py (standard library only; run anywhere):
    python3 -m unittest discover -s tests          (from the EFIS directory)"""
VERSION = "1.0.0"

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import efis_heartbeat as hb


class TestVersionAndPacking(unittest.TestCase):
    def test_parse_version(self):
        self.assertEqual(hb.parse_version("1.6.0"), (1, 6, 0))
        self.assertEqual(hb.parse_version("2.10.15"), (2, 10, 15))
        self.assertEqual(hb.parse_version("1.6"), (1, 6, 0))
        self.assertEqual(hb.parse_version("1.x.3"), (1, 0, 3))
        self.assertEqual(hb.parse_version("300.2.1"), (255, 2, 1))
        self.assertEqual(hb.parse_version(""), (0, 0, 0))

    def test_pack_layout(self):
        data = hb.pack((1, 6, 0), hb.STATE_OK, 0x0F, 0x1234)
        self.assertEqual(data, bytes([0, 1, 6, 0, 1, 0x0F, 0x34, 0x12]))
        self.assertEqual(len(data), 8)

    def test_uptime_wraps_like_the_modules(self):
        self.assertEqual(hb.pack((1, 0, 0), 1, 0, 65536 + 5)[6:], bytes([5, 0]))
        self.assertEqual(hb.pack((1, 0, 0), 1, 0, 65535)[6:], bytes([255, 255]))


class TestState(unittest.TestCase):
    def setUp(self):
        self.h = hb.EfisHeartbeat("1.6.0", 1000.0)

    def test_all_modules_healthy_with_a_display(self):
        self.assertEqual(self.h.decide(0x07, 1, 1001.0), (hb.STATE_OK, 0x0F))

    def test_a_missing_module_gives_degraded_and_clears_its_bit(self):
        self.assertEqual(self.h.decide(0x05, 1, 1001.0), (hb.STATE_DEGRADED, 0x0D))      # IMU missing
        self.assertEqual(self.h.decide(0x06, 1, 1001.0), (hb.STATE_DEGRADED, 0x0E))      # AIR missing
        self.assertEqual(self.h.decide(0x03, 1, 1001.0), (hb.STATE_DEGRADED, 0x0B))      # GPS missing
        self.assertEqual(self.h.decide(0x00, 1, 1001.0), (hb.STATE_DEGRADED, 0x08))

    def test_no_display_is_tolerated_for_30_seconds_after_start(self):
        self.assertEqual(self.h.decide(0x07, 0, 1000.0), (hb.STATE_OK, 0x07))
        self.assertEqual(self.h.decide(0x07, 0, 1030.0), (hb.STATE_OK, 0x07))           # exactly 30 s: not yet
        self.assertEqual(self.h.decide(0x07, 0, 1030.1), (hb.STATE_NO_DISPLAY, 0x07))

    def test_no_display_overrides_a_degraded_module(self):
        self.assertEqual(self.h.decide(0x01, 0, 1100.0), (hb.STATE_NO_DISPLAY, 0x01))

    def test_a_display_that_disconnects_counts_30_seconds_from_when_it_was_last_seen(self):
        self.h.decide(0x07, 1, 1100.0)                                                    # a display until 1100
        self.assertEqual(self.h.decide(0x07, 0, 1125.0)[0], hb.STATE_OK)
        self.assertEqual(self.h.decide(0x07, 0, 1131.0)[0], hb.STATE_NO_DISPLAY)
        self.assertEqual(self.h.decide(0x07, 1, 1132.0), (hb.STATE_OK, 0x0F))             # and it recovers at once

    def test_viewers_do_not_count_because_only_controllers_are_passed(self):
        # the caller passes the number of controller pages; zero controllers is "no display"
        self.assertEqual(self.h.decide(0x07, 0, 2000.0)[0], hb.STATE_NO_DISPLAY)

    def test_the_modules_mask_ignores_stray_bits(self):
        self.assertEqual(self.h.decide(0xFF, 1, 1001.0), (hb.STATE_OK, 0x0F))


class TestFrame(unittest.TestCase):
    def test_frame_and_facts(self):
        h = hb.EfisHeartbeat("1.6.0", 1000.0)
        can_id, data, facts = h.frame(0x07, 1, 1065.0)
        self.assertEqual(can_id, 0x700)
        self.assertEqual(data, bytes([0, 1, 6, 0, 1, 0x0F, 65, 0]))
        self.assertEqual(facts, {"version": "1.6.0", "state": 1, "mask": 0x0F, "uptime": 65, "display_clients": 1})

    def test_frame_in_a_degraded_system(self):
        h = hb.EfisHeartbeat("1.6.0", 0.0)
        can_id, data, facts = h.frame(0x03, 1, 300.0)
        self.assertEqual((data[4], data[5]), (hb.STATE_DEGRADED, 0x0B))
        self.assertEqual(facts["state"], 2)


if __name__ == "__main__":
    unittest.main()
