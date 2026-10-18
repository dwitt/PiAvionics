"""Tests for display_orientation.py (standard library only; run anywhere):
    python3 -m unittest tests.test_display_orientation       (from the EFIS directory)
or  python3 -m unittest discover -s tests
The screen is never touched: the runner is a fake that records the transforms it was asked for.
"""
import asyncio
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import display_orientation as do


class FakeClock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


class FakeRunner:
    """Records the transforms; `ok` False makes it fail (like a missing compositor)."""

    def __init__(self):
        self.calls = []
        self.ok = True
        self.raise_error = False

    async def __call__(self, transform):
        self.calls.append(transform)
        if self.raise_error:
            raise OSError("no compositor")
        return self.ok


class OrientationTestCase(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.dir.name, "display_orientation.json")
        self.clock = FakeClock()
        self.runner = FakeRunner()

    def tearDown(self):
        self.dir.cleanup()

    def make(self):
        return do.DisplayOrientation(self.runner, self.path, self.clock)

    def saved_on_disk(self):
        with open(self.path) as f:
            return json.load(f)["orientation"]


class TestSavedFile(OrientationTestCase):
    def test_missing_file_is_portrait(self):
        self.assertEqual(do.load_saved(self.path), "portrait")

    def test_round_trip_every_orientation(self):
        for name, transform in do.ORIENTATIONS.items():
            do.save(name, self.path)
            self.assertEqual(do.load_saved(self.path), name)
            with open(self.path) as f:
                self.assertEqual(json.load(f)["transform"], transform)

    def test_damaged_or_unknown_is_portrait(self):
        for text in ("", "{", "[]", '{"orientation": "sideways"}', '{"orientation": 3}'):
            with open(self.path, "w") as f:
                f.write(text)
            self.assertEqual(do.load_saved(self.path), "portrait", text)

    def test_no_temp_file_left_behind(self):
        do.save("landscape", self.path)
        self.assertEqual(os.listdir(self.dir.name), ["display_orientation.json"])

    def test_transform_values(self):
        self.assertEqual(do.ORIENTATIONS, {"portrait": "normal", "landscape": "90",
                                           "portrait_flipped": "180", "landscape_flipped": "270"})


class TestTrial(OrientationTestCase):
    async def test_request_rotates_but_saves_nothing(self):
        o = self.make()
        self.assertEqual(await o.request("landscape"), (True, None))
        self.assertEqual(self.runner.calls, ["90"])
        self.assertEqual(o.current, "landscape")
        self.assertEqual(o.saved, "portrait")
        self.assertFalse(os.path.exists(self.path))
        facts = o.facts()
        self.assertEqual((facts["orientation"], facts["saved"], facts["trial"]),
                         ("landscape", "portrait", "landscape"))
        self.assertEqual(facts["trial_seconds_left"], do.TRIAL_SECONDS)

    async def test_keep_saves(self):
        o = self.make()
        await o.request("landscape_flipped")
        self.assertTrue(await o.keep())
        self.assertEqual(self.saved_on_disk(), "landscape_flipped")
        self.assertEqual((o.saved, o.current, o.trial), ("landscape_flipped", "landscape_flipped", None))
        self.assertEqual(self.runner.calls, ["270"])            # keeping does not rotate again
        self.assertEqual(self.make().saved, "landscape_flipped")  # a new run (a reboot) starts from it

    async def test_timeout_reverts_and_saves_nothing(self):
        o = self.make()
        await o.request("landscape")
        self.clock.now += do.TRIAL_SECONDS - 0.1
        await o.tick()
        self.assertEqual(o.trial["orientation"], "landscape")   # not yet
        self.clock.now += 0.2
        await o.tick()
        self.assertIsNone(o.trial)
        self.assertEqual((o.current, o.saved), ("portrait", "portrait"))
        self.assertEqual(self.runner.calls, ["90", "normal"])
        self.assertFalse(os.path.exists(self.path))

    async def test_revert_button(self):
        o = self.make()
        await o.request("portrait_flipped")
        self.assertTrue(await o.revert())
        self.assertEqual((o.current, o.saved, o.trial), ("portrait", "portrait", None))
        self.assertEqual(self.runner.calls, ["180", "normal"])
        self.assertFalse(os.path.exists(self.path))

    async def test_keep_and_revert_without_a_trial_do_nothing(self):
        o = self.make()
        self.assertFalse(await o.keep())
        self.assertFalse(await o.revert())
        self.assertEqual(self.runner.calls, [])
        self.assertFalse(os.path.exists(self.path))

    async def test_keep_after_timeout_does_nothing(self):
        o = self.make()
        await o.request("landscape")
        self.clock.now += do.TRIAL_SECONDS + 1
        await o.tick()
        self.assertFalse(await o.keep())
        self.assertEqual(o.saved, "portrait")
        self.assertFalse(os.path.exists(self.path))

    async def test_countdown_in_facts(self):
        o = self.make()
        await o.request("landscape")
        self.clock.now += 6
        self.assertEqual(o.facts()["trial_seconds_left"], do.TRIAL_SECONDS - 6)

    async def test_from_saved_landscape_trial_portrait_reverts_to_landscape(self):
        do.save("landscape", self.path)
        o = self.make()
        self.assertEqual(o.current, "landscape")
        await o.request("portrait")
        await o.revert()
        self.assertEqual((o.current, o.saved), ("landscape", "landscape"))
        self.assertEqual(self.runner.calls, ["normal", "90"])


class TestRefusals(OrientationTestCase):
    async def test_unknown_orientation(self):
        o = self.make()
        self.assertEqual(await o.request("upside-down"), (False, "unknown"))
        self.assertEqual(self.runner.calls, [])

    async def test_second_request_during_a_trial_is_busy(self):
        o = self.make()
        await o.request("landscape")
        self.assertEqual(await o.request("portrait_flipped"), (False, "busy"))
        self.assertEqual(o.trial["orientation"], "landscape")
        self.assertEqual(self.runner.calls, ["90"])

    async def test_same_as_saved_is_refused_as_already(self):
        o = self.make()
        self.assertEqual(await o.request("portrait"), (False, "already"))
        self.assertEqual(self.runner.calls, [])

    async def test_airborne_by_airspeed_or_ground_speed(self):
        for speeds in ((31, None), (None, 31), (0, 120), (200, 0)):
            o = self.make()
            self.assertEqual(await o.request("landscape", speeds), (False, "airborne"), speeds)
        self.assertEqual(self.runner.calls, [])

    async def test_slow_or_unknown_speed_is_allowed(self):
        for speeds in ((0, 0), (30, 30), (None, None), (), (12, None)):
            self.runner.calls.clear()
            o = self.make()
            self.assertEqual(await o.request("landscape", speeds), (True, None), speeds)

    async def test_refusal_is_in_the_facts_and_cleared_by_a_good_request(self):
        o = self.make()
        await o.request("landscape", (99, None))
        self.clock.now += 2
        self.assertEqual(o.facts()["refused"], {"reason": "airborne", "age": 2.0})
        await o.request("landscape", (0, 0))
        self.assertIsNone(o.facts()["refused"])

    async def test_runner_failure_starts_no_trial(self):
        self.runner.ok = False
        o = self.make()
        self.assertEqual(await o.request("landscape"), (False, "failed"))
        self.assertEqual((o.current, o.trial), ("portrait", None))

    async def test_runner_exception_is_survived(self):
        self.runner.raise_error = True
        o = self.make()
        self.assertEqual(await o.request("landscape"), (False, "failed"))
        self.assertEqual((o.current, o.trial), ("portrait", None))


class TestFailureHandling(OrientationTestCase):
    async def test_save_failure_ends_the_trial_back_to_the_old_orientation(self):
        o = self.make()
        await o.request("landscape")
        os.mkdir(self.path + ".tmp")        # the temporary file cannot be created
        self.assertFalse(await o.keep())
        self.assertEqual((o.saved, o.current, o.trial), ("portrait", "portrait", None))
        self.assertEqual(self.runner.calls, ["90", "normal"])

    async def test_failed_revert_is_retried(self):
        o = self.make()
        await o.request("landscape")
        self.runner.ok = False
        self.clock.now += do.TRIAL_SECONDS + 1
        await o.tick()                       # revert attempted, fails
        self.assertIsNone(o.trial)
        self.assertEqual(o.current, "landscape")    # the screen is still turned
        self.runner.ok = True
        self.clock.now += do.RETRY_SECONDS + 1
        await o.tick()
        self.assertEqual(o.current, "portrait")
        calls = len(self.runner.calls)
        self.clock.now += do.RETRY_SECONDS + 1
        await o.tick()
        self.assertEqual(len(self.runner.calls), calls)     # no more retries once it matches

    async def test_retry_is_not_every_tick(self):
        o = self.make()
        await o.request("landscape")
        self.runner.ok = False
        self.clock.now += do.TRIAL_SECONDS + 1
        await o.tick()
        calls = len(self.runner.calls)
        self.clock.now += 0.2
        await o.tick()
        self.assertEqual(len(self.runner.calls), calls)

    async def test_start_applies_the_saved_orientation(self):
        do.save("landscape", self.path)
        o = self.make()
        await o.start()
        self.assertEqual(self.runner.calls, ["90"])

    async def test_start_failure_leaves_current_as_saved_and_tick_retries(self):
        do.save("landscape", self.path)
        self.runner.ok = False
        o = self.make()
        await o.start()
        self.assertEqual(o.current, "landscape")


class TestCommandLine(unittest.TestCase):
    def test_transform_command(self):
        import subprocess
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "display_orientation.py")
        result = subprocess.run([sys.executable, "-I", here, "--transform"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertIn(result.stdout.strip(), ("normal", "90", "180", "270"))


if __name__ == "__main__":
    unittest.main()
