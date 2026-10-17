# Resume here (written 2026-10-17)

Tell Claude: "Read RESUME.md and carry on."

## Where we are (updated 2026-10-17, later)
Firmware on the boards: IMU_AHRS 1.0.11, AIR_Module 1.0.26, GPS_Module 1.0.4, ModuleInfo 1.3.0, StatusLED 1.1.0.
Libraries in the repo with local patches: `Sensors/Libraries/Adafruit_LSM6DS` (failed read returns false) and
`Sensors/Libraries/Adafruit_MCP2515` (bounded send); each has `PATCH_NOTES.md`. `flash_remote.sh` builds with them.

## Current task: module failure detection (ISSUES.md #13)
Plan, decisions and the full test log: `Sensors/FAILURE_DETECTION_PLAN.md`.
Build order (David approved): 1 bounded CAN send DONE (AIR, GPS, IMU all verified), 2 IMU DONE
(health tracking, re-init, 6-axis, fault injection `!G !g !M !m !z !x` over USB serial, wing pull tested),
3 AIR next (pressure drivers report read failure, RTD range check, OAT filter reset on recovery, test live
unplugging of the pressure sensors in their sockets), 4 LED amber/red, 5 GPS no-data test.
Then Phase 3/4 retests and recovery tests, and the 0x7FFF markers with the EFIS pass.
Decisions: 0x7FFF markers held until the EFIS/backup display pass; amber = degraded, red = failed;
IMU runs 6-axis if only the magnetometer fails.
Findings: no CAN termination on AIR and GPS boards (IMU has it); live re-insertion of the IMU wing
reboots the module; a Pi tty with echo on feeds the module's own report back to it (use `stty raw -echo`).
Open: FAULT_INJECT is on in IMU_AHRS (decide at the end); EFLG 0x40 (RX0OVR) set even when healthy;
symlink ~/Documents/Arduino/libraries copies of Adafruit_LSM6DS to the repo copies (asked, unanswered).

Watch the bus from the Pi:
`ssh david@pi-efis.local` then
`candump -ta can0,701:7FF,702:7FF,703:7FF,48:7FF,28:7FF,2A:7FF,65:7FF`
Status query: `stty -F <port> 115200 raw -echo hupcl`, write `?`, read.

Safety: stacked wings are not hot-unplugged except where David chooses to (he pulled the IMU wing live twice).
Cables, probes and antennas are fine. AIR pressure sensors share the Wire bus: pulling them live risks a stuck bus.

## Not done / later
- EFIS side: handle heartbeats in aio_server.py, display, EFIS heartbeat 0x700, together with
  David's backup text-only display (deliberately deferred until all sensors are done).
- AOA value on 0x29 is still always 0. Command messages (ISSUES #9 steps 2-4).
- AIR halts at startup if any sensor is missing (it now reports state 3 with the mask).
- Optional: make the static pressure filter change-driven (untested idea).

## Handy facts
- Boards: `/dev/serial/by-id/usb-Adafruit_*`; serials AIR 38503039350B102C, GPS 38503039350B242B,
  IMU DF625857C7793E2F. Flash: `Sensors/flash_remote.sh <Sketch> <serial>`.
- Reboot a running board: `stty -F <port> 1200` then `picotool reboot --ser`. Keep hupcl on the tty.
- Regenerate protocol docs: `Protocol/.venv/bin/python Protocol/generate_can_docs.py`.
- When scripting edits to ISSUES.md, check the slice has no other '## ' or '### ' headings first.
- Memory notes: `project_sensor_modules_status.md` in the Claude project memory folder.
- Unrelated untracked files left alone: CAN_Display/lcd_test, STL files, MotionCal SourceGuide.md,
  IMU_Calibration_Design.md.
