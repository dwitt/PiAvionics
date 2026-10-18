"""Fault-injection run on the Pi: starts live_check.py (read-only listener using the real server code), fires
fault commands at the real modules on a timetable through Sensors/TestTools/fi.sh, and prints one merged
timeline: each command, and each change in what the EFIS side concluded (module states, sensors lost, invalid
instruments, data lost, gyro-only heading).

Needs /tmp/fi.sh on the Pi (scp Sensors/TestTools/* david@pi-efis.local:/tmp/) and the modules on USB.
    ~/EFIS/venv/bin/python fault_run.py            (from the folder with live_check.py)
"""
VERSION = "1.0.0"

import re
import subprocess
import sys
import time

# (seconds from start, module, command, note)
TIMETABLE = [
    (4,  "AIR", "!P", "pitot lost"),            (10, "AIR", "!p", "pitot back"),
    (14, "AIR", "!S", "static lost"),           (20, "AIR", "!s", "static back"),
    (24, "AIR", "!A", "AOA lost"),              (30, "AIR", "!a", "AOA back"),
    (34, "AIR", "!O", "OAT lost"),              (42, "AIR", "!o", "OAT back"),
    (47, "IMU", "!M", "magnetometer lost"),     (55, "IMU", "!m", "magnetometer back"),
    (59, "IMU", "!G", "accel/gyro lost"),       (66, "IMU", "!g", "accel/gyro back"),
    (71, "GPS", "!N", "GPS silenced"),          (86, "GPS", "!n", "GPS back"),
]
TOTAL = 100


def key_of(line):
    line = re.sub(r"gyro_hdg=\d+s", "gyro_hdg=Ns", line)
    return line.split("  asi=")[0].split("s  ", 1)[1].strip()


def main():
    listener = subprocess.Popen([sys.executable, "-u", "live_check.py", str(TOTAL)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    start = time.monotonic()
    events = []
    for t, module, cmd, note in TIMETABLE:
        time.sleep(max(0, t - (time.monotonic() - start)))
        out = subprocess.run(["/tmp/fi.sh", module, cmd], capture_output=True, text=True).stdout.strip()
        events.append((time.monotonic() - start, f"COMMAND {module} {cmd}  ({note})  -> module says: {out}"))
    out, _ = listener.communicate()
    last = None
    for line in out.splitlines():
        m = re.match(r"\s*(\d+\.\d)s\s", line)
        if not m:
            continue
        k = key_of(line)
        if k != last:
            events.append((float(m.group(1)), "EFIS sees: " + k))
            last = k
    for t, text in sorted(events, key=lambda e: e[0]):
        print(f"{t:6.1f}s  {text}")


if __name__ == "__main__":
    main()
