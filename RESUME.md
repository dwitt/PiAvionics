# Resume here (written 2026-10-17)

Tell Claude: "Read RESUME.md and carry on."

## Where we are
Sensor modules (Feather RP2040 on the Pi's CAN bus) are all flashed and committed:
IMU_AHRS 1.0.4, AIR_Module 1.0.23, GPS_Module 1.0.3, ModuleInfo lib 1.1.0, StatusLED lib 1.1.0,
protocol 0.1.2. Done this session: AIR zero offset, INT trace cut verified, pitot filter
(change-driven + 0.12 hPa deadband), static pressure checked, vertical speed, CAN heartbeats
0x701 IMU / 0x702 AIR / 0x703 GPS (1 Hz, state + sensors mask + uptime).

## Current task: module failure detection (ISSUES.md #13)
Plan and test tables: `Sensors/FAILURE_DETECTION_PLAN.md`. Nothing is built yet. Next is
Phase 1: baseline unplug tests on today's firmware, just watching what happens.

Watch the bus from the Pi:
`ssh david@pi-efis.local` then
`candump -ta can0,701:7FF,702:7FF,703:7FF,48:7FF,28:7FF,2A:7FF,65:7FF`

## Questions David has not answered yet
1. Which parts are on connectors in each module (RTD probe, MAX31865 board, EEPROM, GPS antenna,
   GPS wing, CAN cable, IMU wing)? Decides hot vs cold tests and what needs fault injection.
2. Invalid data: send 0x7FFF in the affected field (suggested) or stop sending the frame?
3. LED: amber = degraded, red = failed?
4. IMU keeps running as 6-axis AHRS if only the magnetometer fails (suggested)?
5. Copy the patched `Adafruit_LSM6DS` (in ~/Documents/Arduino/libraries) into `Sensors/Libraries/`?

Safety: do not hot-unplug stacked wings on a powered module. Cables, probes and antennas only;
wings get a cold test.

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
