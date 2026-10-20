"""Tests for qnh_state.py (standard library only; run anywhere):
    python3 -m unittest discover -s tests          (from the EFIS directory)"""
VERSION = "1.0.0"

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import qnh_state as q


def frame(inhg_x100):
    """(hpa, x4) as the senders pack them"""
    return round(inhg_x100 / 2.95299875), inhg_x100 * 4


def make(rng_value=0.5):
    return q.QnhState(1000.0, rng=lambda: rng_value)


class TestChecks(unittest.TestCase):
    def test_good_frame(self):
        hpa, x4 = frame(2992)
        self.assertTrue(q.valid_frame(0x2E, hpa, x4))

    def test_range(self):
        for inhg in (2799, 3101):
            hpa, x4 = frame(inhg)
            self.assertFalse(q.valid_frame(0x2E, hpa, x4))
        for inhg in (2800, 3100):
            hpa, x4 = frame(inhg)
            self.assertTrue(q.valid_frame(0x2E, hpa, x4))

    def test_hpa_must_agree(self):
        hpa, x4 = frame(2992)
        self.assertFalse(q.valid_frame(0x2E, hpa + 2, x4))
        self.assertTrue(q.valid_frame(0x2E, hpa + 1, x4))

    def test_id_must_be_in_the_block(self):
        hpa, x4 = frame(2992)
        self.assertFalse(q.valid_frame(0x2B, hpa, x4))
        self.assertFalse(q.valid_frame(0x30, hpa, x4))
        self.assertTrue(q.valid_frame(0x2C, hpa, x4))
        self.assertTrue(q.valid_frame(0x2F, hpa, x4))


class TestHear(unittest.TestCase):
    def test_starts_not_set_at_2992(self):
        s = make()
        self.assertEqual((s.value, s.is_set), (2992, False))

    def test_adopts_from_any_id_last_wins(self):
        s = make()
        self.assertEqual(s.on_can(0x2D, *frame(3010), 1000.5), 3010)
        self.assertTrue(s.is_set)
        self.assertEqual(s.on_can(0x2E, *frame(3015), 1000.6), 3015)
        self.assertEqual(s.value, 3015)

    def test_bad_frame_ignored(self):
        s = make()
        s.on_can(0x2D, *frame(3010), 1000.5)
        self.assertIsNone(s.on_can(0x2D, *frame(3200), 1000.6))
        self.assertIsNone(s.on_can(0x2D, 1000, 12000, 1000.6))
        self.assertEqual(s.value, 3010)

    def test_adopting_does_not_send(self):
        s = make()
        s.on_can(0x2D, *frame(3010), 1003.0)
        self.assertIsNone(s.due(1003.0))


class TestUser(unittest.TestCase):
    def test_first_report_equal_to_the_held_value_is_not_a_change(self):
        s = make()
        self.assertFalse(s.on_user(2992, 1000.1))
        self.assertFalse(s.is_set)
        self.assertIsNone(s.due(1000.1))

    def test_first_report_that_differs_is_a_change(self):
        s = make()
        s.on_can(0x2D, *frame(3010), 1000.5)
        self.assertFalse(s.on_user(3010, 1000.6))      # the page followed the bus
        self.assertTrue(s.on_user(3011, 1000.7))

    def test_a_change_is_sent_at_once_and_sets(self):
        s = make()
        s.on_user(2992, 1000.0)
        self.assertTrue(s.on_user(2993, 1003.0))
        self.assertTrue(s.is_set)
        self.assertEqual(s.due(1003.0), 2993)
        self.assertIsNone(s.due(1003.001))

    def test_every_step_is_sent_with_the_20_ms_guard(self):
        s = make()
        s.on_user(3000, 1005.0)
        s.on_user(3001, 1005.1)
        self.assertEqual(s.due(1005.1), 3001)
        s.on_user(3002, 1005.11)
        self.assertIsNone(s.due(1005.11))          # inside 20 ms: waits
        self.assertEqual(s.due(1005.13), 3002)     # then sends the latest value

    def test_an_old_page_repeating_a_stale_value_cannot_override_the_bus(self):
        s = make()
        s.on_user(3000, 1005.0)                    # page reported 30.00
        s.on_can(0x2D, *frame(3010), 1005.5)       # the bus says 30.10
        self.assertFalse(s.on_user(3000, 1006.0))  # page repeats 30.00: not a change
        self.assertEqual(s.value, 3010)

    def test_a_bus_frame_cancels_a_waiting_change(self):
        s = make()
        s.on_user(3000, 1005.0)
        s.on_user(3001, 1005.01)
        s.due(1005.01)
        s.on_user(3002, 1005.015)
        s.on_can(0x2D, *frame(3050), 1005.016)
        self.assertIsNone(s.due(1005.05))
        self.assertEqual(s.value, 3050)


class TestSilence(unittest.TestCase):
    def test_nothing_is_sent_while_not_set(self):
        s = make()
        for t in (1002.5, 1010.0, 1100.0):
            self.assertIsNone(s.due(t))

    def test_set_device_speaks_after_the_drawn_silence(self):
        s = make(0.5)                              # draw 0.5 -> exactly 1.000 s
        s.on_can(0x2D, *frame(3010), 1003.0)
        self.assertIsNone(s.due(1003.99))
        self.assertEqual(s.due(1004.0), 3010)

    def test_jitter_is_plus_or_minus_20_ms(self):
        for r, delay in ((0.0, 0.98), (1.0, 1.02)):
            s = make(r)
            s.on_can(0x2D, *frame(3010), 1003.0)
            self.assertIsNone(s.due(1003.0 + delay - 0.001))
            self.assertEqual(s.due(1003.0 + delay + 0.0001), 3010)

    def test_a_heard_frame_resets_the_timer(self):
        s = make()
        s.on_can(0x2D, *frame(3010), 1003.0)
        s.on_can(0x2E, *frame(3010), 1003.8)
        self.assertIsNone(s.due(1004.5))
        self.assertEqual(s.due(1004.8), 3010)

    def test_sending_restarts_the_period(self):
        s = make()
        s.on_can(0x2D, *frame(3010), 1003.0)
        self.assertEqual(s.due(1004.0), 3010)
        self.assertIsNone(s.due(1004.5))
        self.assertEqual(s.due(1005.0), 3010)

    def test_no_keep_alive_during_the_start_listen(self):
        s = make(0.0)
        s.on_user(2992, 1000.0)
        s.on_user(2993, 1000.1)
        s.due(1000.1)                              # sent (a person turned it)
        self.assertIsNone(s.due(1001.2))           # silence 1.1 s but still inside the 2 s listen

    def test_takeover_two_holders_same_value(self):
        a, b = make(0.0), make(1.0)                # a draws 0.98 s, b draws 1.02 s
        for d in (a, b):
            d.on_can(0x2D, *frame(3010), 1003.0)
        self.assertEqual(a.due(1003.99), 3010)     # a speaks first
        b.on_can(0x2E, *frame(3010), 1003.99)      # b hears it
        self.assertIsNone(b.due(1004.01))


class TestMismatch(unittest.TestCase):
    def setUp(self):
        self.s = make()
        self.s.on_can(0x2D, *frame(3010), 1003.0)

    def test_agreement_is_no_mismatch(self):
        self.s.on_air_report(3010, 0, 1004.0)
        self.assertFalse(self.s.mismatch(1010.0 - 6))

    def test_flags_after_2_s(self):
        self.s.on_air_report(2992, 0, 1004.0)
        self.assertFalse(self.s.mismatch(1004.0))
        self.s.on_air_report(2992, 0, 1005.0)
        self.assertFalse(self.s.mismatch(1005.5))
        self.s.on_air_report(2992, 0, 1006.2)
        self.assertTrue(self.s.mismatch(1006.2))

    def test_air_not_set_counts(self):
        self.s.on_air_report(2992, 1, 1004.0)
        self.s.mismatch(1004.0)
        self.s.on_air_report(2992, 1, 1006.5)
        self.assertTrue(self.s.mismatch(1006.5))

    def test_recovery_clears_it(self):
        self.s.on_air_report(2992, 0, 1004.0)
        self.s.mismatch(1004.0)
        self.s.on_air_report(3010, 0, 1007.0)
        self.assertFalse(self.s.mismatch(1007.0))

    def test_stale_or_invalid_air_report_is_ignored(self):
        self.s.on_air_report(2992, 0, 1004.0)
        self.s.mismatch(1004.0)
        self.assertFalse(self.s.mismatch(1008.0))          # report older than 1.5 s
        self.s.on_air_report(q.INVALID_16, 0, 1009.0)
        self.assertFalse(self.s.mismatch(1012.0))

    def test_not_set_here_is_no_mismatch(self):
        s = make()
        s.on_air_report(2992, 1, 1004.0)
        s.mismatch(1004.0)
        s.on_air_report(2992, 1, 1007.0)
        self.assertFalse(s.mismatch(1007.0))


if __name__ == "__main__":
    unittest.main()
