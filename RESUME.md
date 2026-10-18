# Resume here (last saved 2026-10-17, end of day)

Tell Claude: "Read RESUME.md and carry on."

## State of the sensor modules: finished and committed
Firmware on the boards (all flashed, healthy, committed): IMU_AHRS 1.0.15, AIR_Module 1.0.34, GPS_Module 1.0.8.
Libraries: ModuleInfo 1.3.0, StatusLED 1.2.0, SensorHealth 1.0.0, MS4525DO 1.1.1, SSC 1.1.1, plus two locally patched
Adafruit libraries (`Adafruit_LSM6DS`: a failed read returns false; `Adafruit_MCP2515`: bounded send, `txFault()`,
`clearErrorFlags()`), each with `PATCH_NOTES.md`.
Libraries live once in `Sensors/Libraries/` and reach each sketch through relative symlinks in `<Sketch>/src/` with quoted
includes; the Arduino IDE 2.3.5 builds all three with no setup (David verified IMU and AIR). Do NOT symlink these into
`~/Documents/Arduino/libraries`: a Library Manager upgrade of a symlinked library that is in the public index deletes the
folder the link points to (tested). See `Sensors/Libraries/README.md` and `LIBRARIES.md` (third-party versions).

## Failure detection work (ISSUES.md #13): built, tested, logged
Plan, decisions, every test result and the filled-in test tables: `Sensors/FAILURE_DETECTION_PLAN.md`.
- Each module detects its own sensor failures, reports them in its heartbeat (0x701 IMU, 0x702 AIR, 0x703 GPS: state 1 OK,
  2 degraded, 3 failed, plus a sensors mask) and on the LED (green OK, amber degraded, red failed or CAN trouble).
- A CAN cable pull no longer freezes any module (cause was missing termination on the AIR and GPS CAN boards plus an
  unbounded wait in the stock MCP2515 library).
- Fault injection over USB serial is compiled in and stays in the flight build (decision: no USB in the aircraft). Commands start
  with `!`, capital = on, lower case = off: IMU `!G/!g` `!M/!m` `!z` `!x`; AIR `!P/!p` `!A/!a` `!S/!s` `!O/!o`; GPS `!N/!n`.
  Test scripts: `Sensors/TestTools/` (run on the Pi; see its README).
- Not done and not needed yet: hot-plugging a sensor into a running module, stuck-I2C-bus recovery on the AIR Wire bus,
  cold tests A6/I4/I6/G2/C3 (listed NOT RUN in the plan tables). IMU and GPS still show EFLG 0x40 (their CAN buffers are never
  read); harmless until they get a receive path.
- While a sensor is invalid the data frames use an interim form (airspeed 0, altitude held, VSI 0, attitude held, OAT frame
  stops, GPS position frames stop). The 0x7FFF markers wait for the EFIS side.

## NEXT: EFIS and backup display (decisions made, ready to build)
`EFIS/docs/MODULE_STATUS_PLAN.md` (committed) is the plan, and **David has made all ten decisions** (recorded at its end):
gyro-only heading stays under a red X for 5 minutes then blanks; red X look; attitude failure removes sky/ground with a red X;
annunciators at top centre under the heading ribbon (flash 5 s on first appearance); module status page from the Menu; OAT sends
the 0x7FFF marker; GPS frames stay stopped (EFIS timeout); timeouts 0.5 s / 1.5 s / 3.5 s; MODULE RESTARTED caution; EFIS
heartbeat 0x700 with a mask of healthy modules plus display connected.
Done: protocol 0.2.0 (markers, timeouts, 0x700 in `Protocol/can_messages.yaml`, docs regenerated); the annunciator/red-X mock-up
(`EFIS/docs/MODULE_STATUS_PLAN.md`, `EFIS/docs/mockups/annunciator_mockup.html`, approved); and server step A.
Decision 11: the server only tracks facts and the web page decides presentation.
Server step A (`EFIS/module_status.py`, `aio_server.py` 1.1.0, tests in `EFIS/tests/`) passed a fault-injection run against the real
modules on 2026-10-18 and is DEPLOYED to the live Pi EFIS (old files saved in `~/EFIS/backup_20261018/` for rollback: copy back and
`sudo systemctl restart piefis`). It adds `modules`, `valid`, `data_lost`, `heading_status`, `system_state` to the JSON; the current
display ignores them. Do not connect a test client to the live server's websocket (it holds one, for the display); use
`EFIS/tests/live_check.py` (CAN only). Use `~/EFIS/venv/bin/python` on the Pi, not the system python3.
Before that, David asked for a multi-client / viewer capability so the EFIS can be watched from a laptop without disturbing the Pi's
screen: planned in `EFIS/docs/MULTI_CLIENT_PLAN.md` (5 decisions pending; suggested to do its steps 1 and 2 before the display work).
Do NOT open the page from another computer until it is built: the second page would knock the Pi's display off and overwrite the
QNH on the CAN bus and the screen brightness.
Next: display step A (failure flags, annunciator strip built by the page from the facts using the rules table in the plan, module
status page in the Menu, null guards, websocket-stall handling), then server step B (markers decoded, 0x700 heartbeat), the backup
display, and last the module marker changes (server and display first so nothing draws 32767).
Other open items: AOA value on 0x29 is still always 0; command messages over CAN (ISSUES #9 steps 2 to 4).

Watch the bus from the Pi:
`ssh david@pi-efis.local` then
`candump -ta can0,701:7FF,702:7FF,703:7FF,48:7FF,28:7FF,2A:7FF,65:7FF`
Status query of a module: `stty -F <port> 115200 raw -echo hupcl`, write `?`, read.

Safety: stacked wings are not hot-unplugged except where David chooses to (he pulled the IMU wing live twice, and
re-insertion reboots the module). Cables, probes and antennas are fine. AIR pressure sensors share the Wire bus: pulling them
live risks a stuck bus (cold pulls were done instead).

## Handy facts
- Boards: `/dev/serial/by-id/usb-Adafruit_*`; serials AIR 38503039350B102C, GPS 38503039350B242B,
  IMU DF625857C7793E2F. Flash: `Sensors/flash_remote.sh <Sketch> <serial>`.
- Reboot a running board: `stty -F <port> 1200` then `picotool reboot --ser`. Keep hupcl on the tty.
- Regenerate protocol docs: `Protocol/.venv/bin/python Protocol/generate_can_docs.py`.
- When scripting edits to ISSUES.md, check the slice has no other '## ' or '### ' headings first.
- Memory notes: `project_sensor_modules_status.md` in the Claude project memory folder.
- Unrelated untracked files left alone: CAN_Display/lcd_test, STL files, MotionCal SourceGuide.md,
  IMU_Calibration_Design.md.
