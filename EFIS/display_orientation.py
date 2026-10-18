"""Display orientation (portrait / landscape) for the Pi's screen: the saved setting, a trial with an
automatic revert, and the facts the web page shows.

Standard library only, so it can be tested anywhere; the screen is rotated by an injected `runner`
(an async function taking a wlr-randr transform name and returning True on success).

Flow (EFIS/docs/ORIENTATION_PLAN.md):
    request(name)  rotates the screen as a TRIAL; nothing is saved.
    keep()         saves the trial orientation.
    revert()       or no keep() within TRIAL_SECONDS (tick() does it): back to the saved orientation.
Only keep() writes the file, so a bad choice never survives a power cycle.

Also usable as a command for the compositor's autostart:
    python3 display_orientation.py --transform     prints the saved wlr-randr transform (normal if unknown)
"""

VERSION = "1.0.1"

import asyncio
import json
import os
import sys
import time

# name used by the page and the saved file -> wlr-randr --transform value
ORIENTATIONS = {
    "portrait": "normal",           # the panel's native orientation
    "landscape": "90",
    "portrait_flipped": "180",
    "landscape_flipped": "270",
}
DEFAULT_ORIENTATION = "portrait"
OUTPUT_NAME = "DSI-1"
TRIAL_SECONDS = 15              # a trial reverts by itself after this long without keep()
AIRBORNE_KT = 30                # a change is refused when airspeed or ground speed is above this
RETRY_SECONDS = 5               # how often a failed revert is tried again
SAVE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "display_orientation.json")


def load_saved(path=SAVE_FILE):
    """The saved orientation name; portrait if the file is missing, damaged or names something unknown."""
    try:
        with open(path) as f:
            name = json.load(f).get("orientation")
        return name if name in ORIENTATIONS else DEFAULT_ORIENTATION
    except (OSError, ValueError, AttributeError):
        return DEFAULT_ORIENTATION


def save(name, path=SAVE_FILE):
    """Write the orientation atomically (a power cut leaves the old file or the new one, never half)."""
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump({"orientation": name, "transform": ORIENTATIONS[name]}, f)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


class DisplayOrientation:
    """Tracks the orientation the screen is meant to be in and the trial in progress."""

    def __init__(self, runner, path=SAVE_FILE, clock=time.monotonic):
        self._runner = runner
        self._path = path
        self._clock = clock
        self._lock = asyncio.Lock()
        self.saved = load_saved(path)
        self.current = self.saved           # what the screen is meant to show now
        self.trial = None                   # {"orientation": name, "deadline": time} while a trial runs
        self._last_retry = 0.0
        self.refusal = None                 # {"reason": text, "at": time} after a refused request

    async def start(self):
        """At server start: make the screen match the saved orientation (also ends any trial lost in a restart)."""
        await self._apply(self.saved)

    async def _apply(self, name):
        """Rotate the screen. `current` follows the screen only when the runner succeeds."""
        try:
            ok = await self._runner(ORIENTATIONS[name])
        except Exception:                   # a runner must never take the server down
            ok = False
        if ok:
            self.current = name
        return ok

    def _refuse(self, reason):
        self.refusal = {"reason": reason, "at": self._clock()}
        return False, reason

    async def request(self, name, speeds_kt=()):
        """Start a trial of `name`. Returns (True, None) or (False, reason). `speeds_kt` are the airspeed and
        ground speed in knots (None entries ignored): any above AIRBORNE_KT refuses the change."""
        async with self._lock:
            if name not in ORIENTATIONS:
                return self._refuse("unknown")
            if self.trial is not None:
                return self._refuse("busy")
            if any(s is not None and s > AIRBORNE_KT for s in speeds_kt):
                return self._refuse("airborne")
            if name == self.current and name == self.saved:
                return self._refuse("already")
            if not await self._apply(name):
                return self._refuse("failed")
            self.trial = {"orientation": name, "deadline": self._clock() + TRIAL_SECONDS}
            self.refusal = None
            return True, None

    async def keep(self):
        """Save the trial orientation. True if there was a trial to keep."""
        async with self._lock:
            if self.trial is None:
                return False
            name = self.trial["orientation"]
            try:
                save(name, self._path)
            except OSError:
                # could not save: end the trial the safe way, back to the old orientation
                await self._end_trial()
                return False
            self.saved = name
            self.trial = None
            return True

    async def revert(self):
        """End the trial and go back to the saved orientation. True if there was a trial."""
        async with self._lock:
            if self.trial is None:
                return False
            await self._end_trial()
            return True

    async def _end_trial(self):
        self.trial = None
        self._last_retry = self._clock()    # a failed revert is retried RETRY_SECONDS from now, not at once
        await self._apply(self.saved)

    async def tick(self):
        """Call about every 0.2 s: reverts a trial that ran out of time, retries a revert that failed."""
        async with self._lock:
            now = self._clock()
            if self.trial is not None and now >= self.trial["deadline"]:
                await self._end_trial()
            elif self.trial is None and self.current != self.saved and now - self._last_retry >= RETRY_SECONDS:
                self._last_retry = now
                await self._apply(self.saved)

    def facts(self):
        """Plain dict for the JSON (the page decides wording and layout)."""
        now = self._clock()
        refusal = None
        if self.refusal is not None:
            refusal = {"reason": self.refusal["reason"], "age": round(now - self.refusal["at"], 1)}
        return {
            "orientation": self.current,
            "saved": self.saved,
            "trial": self.trial["orientation"] if self.trial else None,
            "trial_seconds_left": round(max(0.0, self.trial["deadline"] - now), 1) if self.trial else None,
            "options": list(ORIENTATIONS),
            "refused": refusal,
        }


async def run_wlr_randr(transform):
    """The real runner: rotate the Pi's screen with wlr-randr. True on success."""
    env = dict(os.environ, XDG_RUNTIME_DIR="/run/piefis", WAYLAND_DISPLAY="wayland-0")
    try:
        process = await asyncio.create_subprocess_exec(
            "wlr-randr", "--output", OUTPUT_NAME, "--transform", transform,
            env=env, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
    except OSError:
        return False
    try:
        return await asyncio.wait_for(process.wait(), 5) == 0
    except asyncio.TimeoutError:
        process.kill()
        return False


if __name__ == "__main__":
    if "--transform" in sys.argv:
        print(ORIENTATIONS[load_saved()])
    else:
        print(f"display_orientation {VERSION}; saved: {load_saved()}")
