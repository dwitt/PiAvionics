"""Tests for aircraft_settings.py (standard library only; run anywhere):
    python3 -m unittest tests.test_aircraft_settings       (from the EFIS directory)
or  python3 -m unittest discover -s tests
VERSION 1.0.2
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import aircraft_settings as ac        # noqa: E402

GOOD = {"vso": 35, "vs1": 40, "vfe": 87, "vno": 108, "vne": 171}
VS = {"vr": 52, "vx": 70, "vy": 74, "vg": 61}


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
        self.assertEqual(settings["vspeeds"], ac.DEFAULT_VSPEEDS)
        self.assertIn("no settings file", note)

    def test_good_file(self):
        self.write({"airspeed": dict(GOOD, vne=170)})
        settings, note = ac.load(self.path)
        self.assertEqual(settings["airspeed"]["vne"], 170)
        self.assertEqual(settings["vspeeds"], ac.DEFAULT_VSPEEDS)    # a file saved before the V-speeds
        self.assertIn("vspeeds defaults (not in", note)
        self.write({"airspeed": GOOD, "vspeeds": dict(VS, vy=75)})
        settings, note = ac.load(self.path)
        self.assertEqual(settings["vspeeds"]["vy"], 75)
        self.assertEqual(note, "from aircraft_settings.json")

    def test_a_bad_group_does_not_spoil_the_other(self):
        self.write({"airspeed": dict(GOOD, vne=170), "vspeeds": dict(VS, vg=0)})
        settings, note = ac.load(self.path)
        self.assertEqual(settings["airspeed"]["vne"], 170)
        self.assertEqual(settings["vspeeds"], ac.DEFAULT_VSPEEDS)
        self.assertIn("vspeeds defaults", note)
        self.assertIn("vg 0 is outside", note)

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
        ac.save({"airspeed": GOOD, "vspeeds": VS}, self.path)
        ac.save({"airspeed": dict(GOOD, vne=170), "vspeeds": VS}, self.path)
        self.assertEqual(ac.load(self.path)[0]["airspeed"]["vne"], 170)
        with open(self.path + ".bak") as f:
            self.assertEqual(json.load(f)["airspeed"]["vne"], 171)
        self.assertFalse(os.path.exists(self.path + ".tmp"))
        with self.assertRaises(ValueError):
            ac.save({"airspeed": dict(GOOD, vno=200), "vspeeds": VS}, self.path)
        with self.assertRaises(ValueError):
            ac.save({"airspeed": GOOD}, self.path)          # every group must be there
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
        self.assertEqual(settings.change("airspeed", dict(GOOD, vfe=85), (31, 0)), (False, "Not while flying"))
        self.assertEqual(settings.change("airspeed", dict(GOOD, vfe=85), (None, 40))[0], False)
        self.assertFalse(os.path.exists(self.path))
        ok, reason = settings.change("airspeed", dict(GOOD, vno=200), (0, 0))
        self.assertFalse(ok)
        self.assertIn("in order", reason)
        self.assertEqual(settings.change("airspeed", dict(GOOD, vfe=85), (20, None)), (True, None))
        self.assertEqual(settings.settings["airspeed"]["vfe"], 85)
        self.assertEqual(ac.load(self.path)[0]["airspeed"]["vfe"], 85)
        self.assertTrue(any("vfe 87 -> 85" in line for line in self.lines))
        self.assertFalse(settings.reload_if_changed())       # already loaded after the save
        self.assertEqual(settings.change("vspeeds", dict(VS, vy=75), (0, 0)), (True, None))
        saved = ac.load(self.path)[0]
        self.assertEqual((saved["vspeeds"]["vy"], saved["airspeed"]["vfe"]), (75, 85))
        self.assertEqual(settings.change("vspeeds", dict(VS, vx=None), (0, 0))[0], False)
        self.assertEqual(settings.change("engine", VS, (0, 0)), (False, "Unknown settings engine"))


if __name__ == "__main__":
    unittest.main()
