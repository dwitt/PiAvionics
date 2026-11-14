"""Aircraft settings kept on the Pi: for now the airspeed limits, in knots (indicated, taken as calibrated until
the airspeed correction table is filled in from flight testing).

The file is aircraft_settings.json beside this module. It is not in copytopiefis.sh, so a deploy never overwrites
it; without it the defaults below are used (David's Sonex flight manual, 197 / 125 / 100 / 46 / 40 mph CAS
converted to knots). It is changed from the Menu's Airspeed Limits page (on the ground only) or over SSH; the
server picks up a change within a few seconds. A file that cannot be read or fails the checks is ignored as a whole (the log says
why) and the defaults are used, so the tape is never drawn from half a set of speeds.

    {"airspeed": {"vso": 35, "vs1": 40, "vfe": 87, "vno": 108, "vne": 171}}
"""
import json
import os

VERSION = "1.0.2"

SAVE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "aircraft_settings.json")

DEFAULT_AIRSPEED = {
    "vso": 35,      # stall, landing configuration: bottom of the white arc, top of the low-speed red line (40 mph)
    "vs1": 40,      # stall, clean: bottom of the green arc (46 mph)
    "vfe": 87,      # maximum flap extended: top of the white arc (100 mph)
    "vno": 108,     # maximum structural cruising: green to yellow (125 mph)
    "vne": 171,     # never exceed: red line and the barber pole above it (197 mph)
}
MAX_KT = 300        # the tape's colour strips go to 300
AIRBORNE_KT = 30    # a change is refused when airspeed or ground speed is above this (as display_orientation.py)


def check_airspeed(speeds):
    """Return None if the airspeed limits are usable, otherwise why not."""
    if not isinstance(speeds, dict):
        return "airspeed is not a set of speeds"
    for name in DEFAULT_AIRSPEED:
        value = speeds.get(name)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return f"{name} is missing or not a number"
        if not 0 < value < MAX_KT:
            return f"{name} {value} is outside 0 to {MAX_KT} kt"
    if not speeds["vso"] <= speeds["vs1"] < speeds["vno"] < speeds["vne"]:
        return "the speeds must be in order: vso <= vs1 < vno < vne"
    if not speeds["vso"] < speeds["vfe"] <= speeds["vne"]:
        return "vfe must be above vso and not above vne"
    return None


def load(path=SAVE_FILE):
    """The settings and a note on where they came from: (settings, note). Defaults if the file is missing or bad."""
    defaults = {"airspeed": dict(DEFAULT_AIRSPEED)}
    try:
        with open(path) as f:
            saved = json.load(f)
    except FileNotFoundError:
        return defaults, "defaults (no settings file)"
    except (OSError, ValueError) as e:
        return defaults, f"defaults ({os.path.basename(path)} could not be read: {e})"
    speeds = saved.get("airspeed") if isinstance(saved, dict) else None
    problem = check_airspeed(speeds)
    if problem:
        return defaults, f"defaults ({os.path.basename(path)} ignored: {problem})"
    return {"airspeed": {name: speeds[name] for name in DEFAULT_AIRSPEED}}, f"from {os.path.basename(path)}"


def save(settings, path=SAVE_FILE):
    """Write the settings atomically (a power cut leaves the old file or the new one, never half); the old file
    is kept as .bak. Raises ValueError if they fail the checks."""
    problem = check_airspeed(settings.get("airspeed"))
    if problem:
        raise ValueError(problem)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(settings, f, indent=2)
        f.flush()
        os.fsync(f.fileno())
    if os.path.exists(path):
        os.replace(path, path + ".bak")
    os.replace(tmp, path)


class AircraftSettings:
    """The settings the server sends to the pages, reloaded when the file changes."""

    def __init__(self, path=SAVE_FILE, log=print):
        self._path = path
        self._log = log
        self._stamp = None
        self.settings = None
        self.reload_if_changed()

    def _file_stamp(self):
        try:
            st = os.stat(self._path)
            return (st.st_mtime_ns, st.st_size)
        except OSError:
            return None

    def reload_if_changed(self):
        """Load the file at start and again whenever it changes (or appears or goes). True if it was loaded."""
        stamp = self._file_stamp()
        if self.settings is not None and stamp == self._stamp:
            return False
        self._stamp = stamp
        self.settings, note = load(self._path)
        self._log(f"aircraft settings {note}: {self.settings['airspeed']}")
        return True

    def change_airspeed(self, speeds, speeds_kt=()):
        """Save new airspeed limits (the Menu's Airspeed Limits page). `speeds_kt` are the airspeed and ground speed
        in knots (None entries ignored): any above AIRBORNE_KT refuses the change. Returns (True, None) or
        (False, reason); the reason is shown on the page."""
        if any(s is not None and s > AIRBORNE_KT for s in speeds_kt):
            return False, "Not while flying"
        problem = check_airspeed(speeds)
        if problem:
            return False, problem
        old = dict(self.settings["airspeed"])
        new = {name: speeds[name] for name in DEFAULT_AIRSPEED}
        try:
            save(dict(self.settings, airspeed=new), self._path)
        except OSError as e:
            return False, f"The file could not be written: {e}"
        changes = ", ".join(f"{n} {old[n]} -> {new[n]}" for n in DEFAULT_AIRSPEED if old[n] != new[n]) or "no change"
        self._log(f"aircraft settings: airspeed limits saved from the page ({changes})")
        self.reload_if_changed()
        return True, None
