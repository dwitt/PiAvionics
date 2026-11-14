"""Aircraft settings kept on the Pi: the airspeed limits (the speed tape's colours) and the V-speeds (its tags), in
knots (indicated, taken as calibrated until the airspeed correction table is filled in from flight testing).

The file is aircraft_settings.json beside this module. It is not in copytopiefis.sh, so a deploy never overwrites
it; without it the defaults below are used (David's Sonex flight manual, 197 / 125 / 100 / 46 / 40 mph CAS
converted to knots). It is changed from the Menu's Airspeed Limits and V-Speeds pages (on the ground only) or
over SSH; the server picks up a change within a few seconds. Each group is checked on its own: a group that is
missing or fails the checks is replaced as a whole by its defaults (the log says why), so the tape is never drawn
from half a set of speeds; a file that cannot be read gives the defaults for everything.

    {"airspeed": {"vso": 35, "vs1": 40, "vfe": 87, "vno": 108, "vne": 171},
     "vspeeds": {"vr": 52, "vx": 70, "vy": 74, "vg": 61}}
"""
import json
import os

VERSION = "1.0.3"

SAVE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "aircraft_settings.json")

DEFAULT_AIRSPEED = {
    "vso": 35,      # stall, landing configuration: bottom of the white arc, top of the low-speed red line (40 mph)
    "vs1": 40,      # stall, clean: bottom of the green arc (46 mph)
    "vfe": 87,      # maximum flap extended: top of the white arc (100 mph)
    "vno": 108,     # maximum structural cruising: green to yellow (125 mph)
    "vne": 171,     # never exceed: red line and the barber pole above it (197 mph)
}
DEFAULT_VSPEEDS = {
    "vr": 52,       # rotation (60 mph)
    "vx": 70,       # best angle of climb: the manual's climb with obstacles (80 mph)
    "vy": 74,       # best rate of climb (85 mph)
    "vg": 61,       # best glide (70 mph)
}
MAX_KT = 300        # the tape's colour strips go to 300
AIRBORNE_KT = 30    # a change is refused when airspeed or ground speed is above this (as display_orientation.py)


def check_airspeed(speeds):
    """Return None if the airspeed limits are usable, otherwise why not."""
    problem = check_speeds(speeds, DEFAULT_AIRSPEED, "airspeed")
    if problem:
        return problem
    if not speeds["vso"] <= speeds["vs1"] < speeds["vno"] < speeds["vne"]:
        return "the speeds must be in order: vso <= vs1 < vno < vne"
    if not speeds["vso"] < speeds["vfe"] <= speeds["vne"]:
        return "vfe must be above vso and not above vne"
    return None


def check_speeds(speeds, defaults, what):
    """Return None if `speeds` has every speed in `defaults` as a number in range, otherwise why not."""
    if not isinstance(speeds, dict):
        return f"{what} is not a set of speeds"
    for name in defaults:
        value = speeds.get(name)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return f"{name} is missing or not a number"
        if not 0 < value < MAX_KT:
            return f"{name} {value} is outside 0 to {MAX_KT} kt"
    return None


def check_vspeeds(speeds):
    """Return None if the V-speeds (tags) are usable, otherwise why not."""
    return check_speeds(speeds, DEFAULT_VSPEEDS, "vspeeds")


# each group: its defaults and its check
GROUPS = {
    "airspeed": (DEFAULT_AIRSPEED, check_airspeed),
    "vspeeds": (DEFAULT_VSPEEDS, check_vspeeds),
}


def defaults():
    return {group: dict(default) for group, (default, _) in GROUPS.items()}


def load(path=SAVE_FILE):
    """The settings and a note on where they came from: (settings, note). Defaults for a group that is missing or
    bad, and for everything if the file is missing or cannot be read."""
    name = os.path.basename(path)
    try:
        with open(path) as f:
            saved = json.load(f)
    except FileNotFoundError:
        return defaults(), "defaults (no settings file)"
    except (OSError, ValueError) as e:
        return defaults(), f"defaults ({name} could not be read: {e})"
    settings, notes = {}, []
    for group, (default, check) in GROUPS.items():
        if isinstance(saved, dict) and group not in saved:
            settings[group] = dict(default)
            notes.append(f"{group} defaults (not in {name})")
            continue
        speeds = saved.get(group) if isinstance(saved, dict) else None
        problem = check(speeds)
        if problem:
            settings[group] = dict(default)
            notes.append(f"{group} defaults ({name} ignored: {problem})")
        else:
            settings[group] = {n: speeds[n] for n in default}
    return settings, "; ".join(notes) or f"from {name}"


def save(settings, path=SAVE_FILE):
    """Write the settings atomically (a power cut leaves the old file or the new one, never half); the old file
    is kept as .bak. Raises ValueError if a group fails its checks."""
    for group, (_, check) in GROUPS.items():
        problem = check(settings.get(group))
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
        self._log(f"aircraft settings {note}: {self.settings}")
        return True

    def change(self, group, speeds, speeds_kt=()):
        """Save a new set of speeds for one group ("airspeed" or "vspeeds", from the Menu's pages). `speeds_kt` are
        the airspeed and ground speed in knots (None entries ignored): any above AIRBORNE_KT refuses the change.
        Returns (True, None) or (False, reason); the reason is shown on the page."""
        if group not in GROUPS:
            return False, f"Unknown settings {group}"
        if any(s is not None and s > AIRBORNE_KT for s in speeds_kt):
            return False, "Not while flying"
        default, check = GROUPS[group]
        problem = check(speeds)
        if problem:
            return False, problem
        old = dict(self.settings[group])
        new = {name: speeds[name] for name in default}
        try:
            save(dict(self.settings, **{group: new}), self._path)
        except OSError as e:
            return False, f"The file could not be written: {e}"
        changes = ", ".join(f"{n} {old[n]} -> {new[n]}" for n in default if old[n] != new[n]) or "no change"
        self._log(f"aircraft settings: {group} saved from the page ({changes})")
        self.reload_if_changed()
        return True, None
