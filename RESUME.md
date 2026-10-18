# Resume here (last saved 2026-10-17, end of day)

Tell Claude: "Read RESUME.md and carry on."

## State of the sensor modules: finished and committed
Firmware on the boards (all flashed, healthy, committed): IMU_AHRS 1.0.15, AIR_Module 1.0.34, GPS_Module 1.0.9.
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
Multi-client viewer support (David's request) is DONE and DEPLOYED (plan `EFIS/docs/MULTI_CLIENT_PLAN.md`): `aio_server.py` 1.2.1 and the
page (`efis-display.js` 0.2.2) are on the Pi (previous files saved in `~/EFIS/backup_20261018_server120/`, older ones in
`backup_20261018_server110/`). The Pi's own page (localhost) is the controller; any other page is a read-only viewer (max 4): open
http://192.168.5.110:8080 on a laptop, or serve the EFIS files on the Mac and use `?server=192.168.5.110:8080` (the Pi renames
`pixi-v8.6.6.mjs` to `pixi.mjs`, so a local test folder needs that name; `?view=1` forces viewer mode). A viewer shows the controller's QNH and
brightness (`controller_state`) and a "VIEW ONLY" tag. A reloaded Pi page now starts from the server's remembered QNH/brightness instead of
29.92/100 (page 0.2.2, tested on the Pi). Tested in Chrome from the Mac and Safari. The service's output is in `/var/log/piefis.log` (not the journal), see ISSUES #14.
Display orientation (2026-10-18) is DONE, deployed and tested on the Pi: Menu > Orientation (choose, confirm, then a trial with Keep/Revert
and a 15 s server-side revert; portrait, landscape and both flipped; refused above 30 kt; saved in `~/EFIS/display_orientation.json` and applied
at boot by `~/.config/labwc/autostart`). `display_orientation.py` 1.0.1, `aio_server.py` 1.3.0, page 0.3.0. Plan, decisions, tests and rollback:
`EFIS/docs/ORIENTATION_PLAN.md`; touch and rotation notes: "Display orientation and touch on the Pi" in `EFIS/README.md`; labwc config
copies: `EFIS/linux/labwc/`. David is seriously considering landscape for the aircraft panel; the display work must keep both layouts working
(the page picks its layout from the window shape and reloads when it changes). Bench note: all three modules are on the Pi's USB and restart
whenever the Pi reboots (their uptimes reset); the GPS needs a few minutes to regain satellites. David is aware and says it is fine.
Display step A (2026-10-18) is built and DEPLOYED (page 0.5.4), and David watched all seven faults on the real display ("It all looked good"):
red-X flags, blanked tapes, dimmed gyro heading, `---` readouts, annunciators above the slip/skid ball, NO DATA link state, the Menu >
Modules status page, and the HSI's GPS box, ground speed, track, CDI, TO/FROM, waypoint line and heading readout all follow the data
(blank when the GPS or heading is lost). Later the same day: Pixi now draws at the device pixel ratio (sharper; `?sharp=0` for the old way),
the wings are 33% shorter (`WING_SHORTEN`) and the kts/ft units 6 px lower, the annunciator text shrinks to fit between the tapes on a narrow
canvas, the Menu keeps a margin, and the HSI rose leaves room for the waypoint line on a short canvas. David is happy with the displays at
Display 2 (portrait and landscape) and checked Display 1 sizes (800x480 and 480x800) in Safari and Chrome. Page options: `?pi=true|portrait|
landscape` (Pi layouts), `?view=1`, `?server=host:port`, `?sharp=0`, and look overrides `xw xo fs labels` (see `EFIS/support/failureLook.mjs`).
Test log: end of `EFIS/docs/MODULE_STATUS_PLAN.md`. Tools: `EFIS/tests/fake_feed.py` + `page_shots.py` (need aiohttp in a venv) and `node --test tests/js`.
Page changes reach the Pi with rsync of `EFIS/support/*` then a reload (`wtype -M ctrl -M shift -k r -m shift -m ctrl` over ssh works).
ISSUES #15 is fixed and deployed (server 1.5.0, page 0.5.5, GPS_Module 1.0.9 flashed): the track diamond uses `mag_track`, the GPS module sends
its WMM2025 declination in 0x65 byte 3, and the server sends the desired track and bearing in magnetic degrees (held back until the declination is
known); `NULL_INVALID_VALUES` is on (untrusted values are sent as null). A moving test of the diamond and course pointer is still to do.
Previous servers are in `~/EFIS/backup_20261018_server120/` (1.2.1, 1.3.0, 1.4.0 copies).
Next: server step B (markers decoded, 0x700 heartbeat), the backup display, and last the
module marker changes (server and display first so nothing draws 32767).
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
