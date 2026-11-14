"""Tests for aircraft_settings.py (standard library only; run anywhere):
    python3 -m unittest tests.test_aircraft_settings       (from the EFIS directory)
or  python3 -m unittest discover -s tests
VERSION 1.0.1
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import aircraft_settings as ac        # noqa: E402

GOOD = {"vso": 35, "vs1": 40, "vfe": 87, "vno": 108, "vne": 171}


class TestAircraftSettings(unittest.TestCase):

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, "aircraft_settings.json")
        self.lines = []

    def write(self, content):
        with open(self.path, "w") as f:
            f.write(content if isinstance(content, str) else json.dumps(content))

    def test_defaults_without_a_file(self):
        settings, note = ac.load(self.path)
        self.assertEqual(settings["airspeed"], ac.DEFAULT_AIRSPEED)
        self.assertIn("no settings file", note)

    def test_good_file(self):
        self.write({"airspeed": dict(GOOD, vne=170)})
        settings, note = ac.load(self.path)
        self.assertEqual(settings["airspeed"]["vne"], 170)
        self.assertIn("from", note)

    def test_bad_files_use_the_defaults(self):
        for content, why in [("{not json", "could not be read"),
                             ({"airspeed": dict(GOOD, vfe="87")}, "vfe is missing or not a number"),
                             ({"airspeed": dict(GOOD, vne=None)}, "vne is missing"),
                             ({"airspeed": dict(GOOD, vno=180)}, "in order"),
                             ({"airspeed": dict(GOOD, vfe=30)}, "vfe must be above vso"),
                             ({"airspeed": dict(GOOD, vso=0)}, "outside"),
                             ({"airspeed": dict(GOOD, vs1=True)}, "not a number"),
                             ([1, 2], "not a set of speeds")]:
            self.write(content)
            settings, note = ac.load(self.path)
            self.assertEqual(settings["airspeed"], ac.DEFAULT_AIRSPEED, content)
            self.assertIn(why, note, content)

    def test_save_is_atomic_keeps_a_backup_and_refuses_bad_speeds(self):
        ac.save({"airspeed": GOOD}, self.path)
        ac.save({"airspeed": dict(GOOD, vne=170)}, self.path)
        self.assertEqual(ac.load(self.path)[0]["airspeed"]["vne"], 170)
        with open(self.path + ".bak") as f:
            self.assertEqual(json.load(f)["airspeed"]["vne"], 171)
        self.assertFalse(os.path.exists(self.path + ".tmp"))
        with self.assertRaises(ValueError):
            ac.save({"airspeed": dict(GOOD, vno=200)}, self.path)
        self.assertEqual(ac.load(self.path)[0]["airspeed"]["vne"], 170)

    def test_reload_only_when_the_file_changes(self):
        settings = ac.AircraftSettings(self.path, log=self.lines.append)
        self.assertEqual(settings.settings["airspeed"]["vfe"], 87)
        self.assertFalse(settings.reload_if_changed())
        self.write({"airspeed": dict(GOOD, vfe=80)})
        self.assertTrue(settings.reload_if_changed())
        self.assertEqual(settings.settings["airspeed"]["vfe"], 80)
        self.assertFalse(settings.reload_if_changed())
        os.remove(self.path)
        self.assertTrue(settings.reload_if_changed())
        self.assertEqual(settings.settings["airspeed"]["vfe"], 87)
        self.assertEqual(len(self.lines), 3)

    def test_change_airspeed_from_the_page(self):
        settings = ac.AircraftSettings(self.path, log=self.lines.append)
        self.assertEqual(settings.change_airspeed(dict(GOOD, vfe=85), (31, 0)), (False, "Not while flying"))
        self.assertEqual(settings.change_airspeed(dict(GOOD, vfe=85), (None, 40))[0], False)
        self.assertFalse(os.path.exists(self.path))
        ok, reason = settings.change_airspeed(dict(GOOD, vno=200), (0, 0))
        self.assertFalse(ok)
        self.assertIn("in order", reason)
        self.assertEqual(settings.change_airspeed(dict(GOOD, vfe=85), (20, None)), (True, None))
        self.assertEqual(settings.settings["airspeed"]["vfe"], 85)
        self.assertEqual(ac.load(self.path)[0]["airspeed"]["vfe"], 85)
        self.assertTrue(any("vfe 87 -> 85" in line for line in self.lines))
        self.assertFalse(settings.reload_if_changed())       # already loaded after the save


if __name__ == "__main__":
    unittest.main()
