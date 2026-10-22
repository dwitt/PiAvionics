# Test tools for the sensor-module failure tests

Scratch scripts used on 2026-10-17 for `Sensors/FAILURE_DETECTION_PLAN.md`. They run **on the Pi**
(`scp Sensors/TestTools/* david@pi-efis.local:/tmp/`, then `ssh david@pi-efis.local`). They need `candump`
(can-utils) and `python3` there. The modules must be plugged into the Pi's USB (the `/dev/serial/by-id/*IMU*`,
`*AIR*`, `*GPS*` names are used).

| Script | What it does |
|---|---|
| `fi.sh IMU\|AIR\|GPS "!M"` | Send one fault-injection command to a module, wait 2 s, print its heartbeat state and mask. |
| `ftest.py "cmd@sec cmd@sec" total_sec` | IMU: send commands at the given times (e.g. `"!G@1 !g@4"`), record 701/48/49 into `/tmp/f.log`, print the `?` report lines. Then run `an.py` to see heartbeat state/mask and attitude over time. |
| `atest.py "cmd@sec ..." total_sec` | Same for the AIR module: records 702/28/2A/2B into `/tmp/a.log`, prints the report and a timeline of heartbeat, airspeed/altitude/VSI and OAT. |
| `gtest.py "cmd@sec ..." total_sec` | Same for the GPS module: 703/63/64/65, `/tmp/g.log`. |
| `an.py` | Prints the timeline for the IMU capture in `/tmp/f.log`. |

Fault commands (start with `!`, capital = on, lower case = off):
IMU `!G/!g` accel-gyro, `!M/!m` magnetometer, `!z` reset ISM330DHCX, `!x` reset LIS3MDL;
AIR `!P/!p` pitot, `!A/!a` AOA, `!S/!s` static, `!O/!o` OAT; GPS `!N/!n` silence the GPS;
all three `!h` hang the main loop on purpose (the watchdog should restart the module within about 3 s).

Lessons built into them: set the tty to `raw -echo hupcl` first (with echo on, the Pi feeds the module's own report back
to it as commands), and keep the port open while sending (open, write, close can lose the byte).
