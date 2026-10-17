# Resume here (written 2026-10-17)

Tell Claude: "Read RESUME.md and carry on."

## Where we are (updated 2026-10-17, end of build phase)
Firmware on the boards: IMU_AHRS 1.0.15, AIR_Module 1.0.33, GPS_Module 1.0.8, ModuleInfo 1.3.0, StatusLED 1.2.0,
SensorHealth 1.0.0 (new shared library), MS4525DO 1.1.0 and SSC 1.1.0 (checked reads).
Shared libraries are in `Sensors/Libraries/` and reach each sketch through relative symlinks in `<Sketch>/src/` (see `Sensors/Libraries/README.md`; third-party versions in `LIBRARIES.md`); the Arduino IDE 2.3.5 builds them without setup (verified by David with IMU_AHRS). Libraries in the repo with local patches: `Sensors/Libraries/Adafruit_LSM6DS` (failed read returns false) and
`Sensors/Libraries/Adafruit_MCP2515` (bounded send, txFault()); each has `PATCH_NOTES.md`. `flash_remote.sh` builds with them.

## Current task: module failure detection (ISSUES.md #13)
Plan, decisions and the full test log: `Sensors/FAILURE_DETECTION_PLAN.md`.
Phase 2 build steps 1 to 5 are DONE and tested: bounded CAN send; IMU health/re-init/6-axis; AIR checked pressure reads, health,
tolerant start-up, OAT range check; status LED (green OK, amber degraded, red failed or CAN fault); GPS no-data path.
Fault injection over USB serial (`FAULT_INJECT 1` in each sketch, commands start with `!`): IMU `!G/!g` accel-gyro, `!M/!m` mag,
`!z` `!x` chip resets; AIR `!P/!p` pitot, `!A/!a` AOA, `!S/!s` static, `!O/!o` OAT; GPS `!N/!n` silence.
Remaining: Phase 3 full retest against the plan tables, Phase 4 recovery tests, Phase 5 any leftover fault injection; then the
0x7FFF invalid-data markers together with the EFIS pass (handle heartbeats in aio_server.py, backup display, EFIS heartbeat 0x700).
Findings: no CAN termination on AIR and GPS boards (IMU has it); live re-insertion of the IMU wing reboots the module; a Pi tty with
echo on feeds the module's own report back to it (use `stty raw -echo`; keep the port open while sending test commands).
Decided: FAULT_INJECT stays 1 in the flight build (no USB in the aircraft, faults are RAM only; see the plan). Open: EFLG 0x40 (RX0OVR) set even when healthy; IMU/GPS/AIR hot-plug of
a sensor into a running module not tested; stuck I2C bus (SDA held low) recovery on the AIR Wire bus not built.

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
