# Resume here (last saved 2026-10-22)

Tell Claude: "Read RESUME.md and carry on."

## Update 2026-10-22: the backup display is built, and every board has a watchdog
- **Backup display:** `Sensors/Backup_Display/Backup_Display.ino` 1.7.7, built and working on the bench: Flight, GPS and System pages, QNH over the
  bus, mini horizon and slip ball, status letters I A G E. Design record: `Sensors/Backup_Display/DESIGN.md`. Still open there: G on the Flight page
  (David undecided), the Engine page (MGL RDAC raw values), its own heartbeat, module versions on the System page. Not yet tried in an aircraft.
- **Firmware now:** IMU_AHRS 1.0.18, AIR_Module 1.0.39, GPS_Module 1.0.11, Backup_Display 1.7.7, all flashed. AIR 1.0.38 smooths the vertical speed over
  about 3 s and sends a steady value (the displays do no smoothing). Protocol 0.3.1 (docs regenerated).
- **Watchdog on all four boards:** the chip restarts if its main loop stops for 2 s, and an I2C transaction gives up after 25 ms. It starts at the end of a
  good `setup()`, so a failed start-up still halts with its blink code and heartbeat. The GPS module feeds it from `gpsService()` so its re-probing of a silent
  GPS is not cut off. Test commands: `!h` on the three sensors (fault injection) and `H` on the backup display hang the board on purpose; all four hang tests
  passed (frames stop for 2.7 to 4.7 s, then the board is back and logs "Restarted by the watchdog"). Set `FAULT_INJECT 0` for a flight build.
- The GPS module is back on the bus (the display no longer stands in for it). The first backup-display encoder was faulty and was replaced.
- Git is clean: the NHD-0420DZ datasheet PDF now lives in `Sensors/Backup_Display/` and the old HM2004A bench sketch is in `Legacy/CAN_Display_v1/lcd_test`.

## Where things stand (end of 2026-10-18)
- **On the Pi (all committed, git clean except David's untracked files):** server `aio_server.py` 1.6.3, `display_orientation.py` 1.0.1, `efis_heartbeat.py` 1.0.0,
  `module_status.py` 2.0.0; page `efis-display.js` 0.7.1. Firmware: IMU_AHRS 1.0.16, AIR_Module 1.0.35, GPS_Module 1.0.9. The Pi is saved in landscape
  (`~/EFIS/display_orientation.json`); server backups 1.2.1 to 1.6.2 are in `~/EFIS/backup_20261018_server120/`.
- **Done this session (details in the paragraphs below and in ISSUES.md):** laptop viewers and `?pi=` layouts; the Menu orientation switch with a timed revert; the whole failure
  display (red X flags, blanked tapes, annunciators above the slip ball, the Modules page with an EFIS row); the HSI following GPS and heading data; the module markers (AIR and
  IMU) and the EFIS heartbeat 0x700; magnetic declination from the GPS module and magnetic courses (ISSUES #15); the log rotation (ISSUES #14); the sharper Pixi drawing; look
  tweaks (wings, units, annunciator fit); Display 1 size fixes.
- **GPS display (last thing done):** the HSI's GPS box is a touch button (contracted: `3D FIX` coloured by HDOP, a green `WAAS` when corrections are in use, and `VAR 10°W`;
  expanded adds SAT, HDOP, VDOP); the track diamond is hidden below 10 kt (`TRACK_MIN_SPEED_KT` in `efis-display.js`). Confirmed by touch on the real Pi (ISSUES #16).
- **Open:** (1) the moving test of the track diamond and course pointer (ISSUES #15); (2) the backup display (built 2026-10-22, see above), then plan step 7 (the full system test
  table, including pulling a module's CAN cable and closing the Pi page to see the heartbeat mask bits clear); (3) ISSUES #18, `piefis.service` ordering against `can0.service`;
  (4) the AOA value is still always 0; (5) commands over CAN (ISSUES #9 steps 2 to 4: a command message, AIR zero, two-step reset; the Modules page is the natural home).

## State of the sensor modules: finished and committed
Firmware on the boards (all flashed, healthy, committed; 2026-10-22): IMU_AHRS 1.0.18, AIR_Module 1.0.39, GPS_Module 1.0.11 (the IMU and AIR send the invalid-data markers since 2026-10-18; all three have the watchdog since 2026-10-22).
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
  with `!`, capital = on, lower case = off: IMU `!G/!g` `!M/!m` `!z` `!x`; AIR `!P/!p` `!A/!a` `!S/!s` `!O/!o`; GPS `!N/!n`; `!h` hangs any of the three (watchdog test).
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
Previous servers are in `~/EFIS/backup_20261018_server120/` (1.2.1, 1.3.0, 1.4.0 and 1.5.0 copies).
Server step B is DONE and deployed (server 1.6.1 then, now 1.6.3, page 0.6.1, plan and status in `EFIS/docs/SERVER_STEP_B_PLAN.md`): invalid-data markers are decoded (a
marker becomes None and an invalid instrument, never a number), the EFIS heartbeat 0x700 is on the bus once a second (checked with candump), and the Modules
page has an EFIS row. Not yet tried on the real system: pulling a module's CAN cable and closing the Pi's display page, to see the heartbeat mask bits clear.
The modules now send the invalid-data markers (2026-10-18): AIR_Module 1.0.35 (airspeed on a pitot loss, altitude and vertical speed on a static loss, the OAT frame
keeps coming with a marker, the pressure message), IMU_AHRS 1.0.16 (heading, pitch, roll, turn rate and the accelerations on an accel/gyro loss; a lost magnetometer
still sends the gyro-only heading as a number). The GPS module sends none. Verified on the real bus with injected faults (`/tmp/fi.sh`, `marker_check.sh` on the Pi).
A bug found on the way, a thin red X drawn across the display when a marker nulled a value before the validity caught up, is fixed (ISSUES #17: server 1.6.2, page 0.6.1).
Log rotation is done (ISSUES #14): server 1.6.3 reports a repeating send failure once a minute, and `/etc/logrotate.d/piefis` (from `EFIS/linux/piefis.logrotate`) rotates
`/var/log/piefis.log` at 5 MB, keeping 4. The service ordering against `can0.service` is parked as ISSUES #18.
The backup display is now built (see the update at the top; this paragraph was written before it was: new design in Sensors/Backup_Display, design record in Sensors/Backup_Display/DESIGN.md; the old CAN_Feather/Display_Feather design is archived in Legacy/CAN_Display_v1; per-field validity from the
markers, listen to 0x700 to 0x703, a status row; the first heartbeat after a server restart reads state 2, so allow it a second). Only the Pi EFIS receives the markers today.
Other open items: AOA value on 0x29 is still always 0; command messages over CAN (ISSUES #9 steps 2 to 4).

Watch the bus from the Pi:
`ssh david@pi-efis.local` then
`candump -ta can0,701:7FF,702:7FF,703:7FF,48:7FF,28:7FF,2A:7FF,65:7FF`
Status query of a module: `stty -F <port> 115200 raw -echo hupcl`, write `?`, read.

Safety: stacked wings are not hot-unplugged except where David chooses to (he pulled the IMU wing live twice, and
re-insertion reboots the module). Cables, probes and antennas are fine. AIR pressure sensors share the Wire bus: pulling them
live risks a stuck bus (cold pulls were done instead).

## 2026-10-23: time sync moved, RDAC XF on the bench (Engine page planning)

**Done and committed:** TIME_SYNC moved from 0x19 to 0x66 (GPS_Module 1.0.12, Backup_Display 1.7.8, aio_server 1.7.1; protocol docs
regenerated; deployed to the Pi and checked on the bus, the backup and the EFIS time box both show UTC). Reason: 0x19 is MGL's
General Command. Bench test with David's real RDAC XF: `00 81` (request calibration) on 0x19 got a reply on 0x209; the same
frame on 0x29, 0x49 and 0x66 got none. The flash-write command (0xA0) was NOT tested.

**RDAC XF is on the bench bus** (address 32, SEL A and B off, frames 0x201 to 0x209). Manual: `~/Documents/_E-Equipment_Manuals/MGL Avionics/RDAC XF MAP/RDAC XF INSTALATION.pdf`
(text extract was in the session scratchpad, which is temporary). Baseline with nothing on the inputs: supply raw 685 (about 11.9 V),
cold junction 16 C, RPM 0, all analogue inputs about 3908 (open, pull-ups on), thermocouples about 0 uncompensated (add the cold
junction), MAP about 1522 raw, CURR ramps to about 1525 while open. DIP switches now: AUX1 and AUX2 PU on, OIP PU on, OIP PD off,
fuel P, coolant, fuel level 1 and 2 PU on, rest off. There is no OIL T pull-up switch (fixed 1500 ohm). Leave the unused pull-ups on
(steady open reading beats a floating one).

**Installation: AeroVee 2.1 (air cooled, gravity fuel, no coolant, no fuel pressure or level; sight gauge)**
- Manual: `~/Documents/Sonex/AeroVee_2point1_Manual.pdf`; CHT probe sheet in the saved site under
  `_P-Projects/Sonex_Website/saved-site/www.aeroconversions.com/support/instruction_sheets/CHT_Install.pdf`.
- Ignition: two Magnatrons (P-lead switched) plus electronic secondary. Tach options: alternator 6 pulses per rev (documented), or
  David's Littelfuse 55505-00-02-A Hall geartooth sensor on the flywheel if it has a ring gear (not in the manual, tooth count unknown,
  flywheel carries magnets so test for interference).
- Sensors David has: VDO oil pressure (ACV-P01-76, 10 to 180 ohm, to OIL P) and VDO oil temperature (ACV-P01-77, 10 to 180 ohm, to OIL T);
  MGL magnetic current sensor (Aircraft Spruce 11-10422; gain 140 = +/-24 A AWG14 folded, 85 = +/-12 A, 500 = +/-50 A; black wire to the
  RDAC reference ground which goes to the engine block; zero it with the sensor off the conductor); LM335 probe (two wire, to AUX1 with
  PU on; kelvin = volts x 100); a MAP sensor (the RDAC XF MAP internal one; probably unused). Thermocouples (type K or J and count): still
  to be checked. AeroConversions CHT probes screw to the head near the plug (#32 hole, 9/16 deep, #6 self-tapping screw), comparable to plug probes.
- Limits from the manual (for the Engine page): idle 800 to 900, cruise 3200 +/- 200, max 4000 rpm; oil T 160 min for take-off to 240 F max;
  oil P 10 psi min at idle, 100 max, 40 to 50 cruise; CHT 350 to 375 cruise, 420 (5 min) climb, 450 max; EGT max 1400 F.

**Next:** (1) David finds the thermocouples, tells me K or J and how many (EGT on TC1 to 4, CHT on TC5 to 8). (2) Bench-check OIL P with
a 100 ohm resistor to ground (expect roughly 10% of 3908 if the pull-up is 1000 ohm), then the real senders. (3) Work out the
raw-to-engineering conversions (VDO curves, LM335, current sensor scale and zero, MAP against the AIR static pressure). (4) Decide what
the backup Engine page shows. Not decided: G on the Flight page; backup heartbeat and versions; plan step 7 system test.

## 2026-10-24: thermocouple decision and RDAC wiring plan

**Decided:** type K for all 8 thermocouples: 4 EGT (one per exhaust pipe, pipe about 1-1/2 in diameter) on TC1 to TC4, 4 CHT on
TC5 to TC8. EGT probe still to choose and order: Sonex "Clamp Small, K Type" (ask Sonex for the clamp range) or Grand Rapids
Aircraft Spruce 11-09535 (1 to 2 in). Prefer 316 stainless with glass-fibre insulated leads (RDAC manual). The Blaze manual warns
against 310 stainless for 4-stroke EGT probes (the MGL bolt-in 11-05567 is 310). CHT probes: AeroConversions ACV-CHT-VW-K.
The AeroVee manual says do not trim the EGT probe wires but gives no reason (CHT wires may be trimmed).

**Primary engine monitor is the MGL Blaze EMS-2** (manual: `~/Documents/_E-Equipment_Manuals/MGL Avionics/Blaze EMS-2/Blaze EMS-2.pdf`);
the backup display Engine page is the backup. The Blaze reads the RDAC over CAN (CAN Low pin 12 purple, CAN High pin 13 pink).
With an RDAC its analogue 1 to 4 are OILT, OILP, AUX1, AUX2. Not known: what it transmits, or whether it reacts to our IDs. Plan:
one shared CAN bus (RDAC and Blaze added to the existing bus), then bench-sniff once David has the Blaze wired.

**RDAC connections (from the RDAC XF manual)**

| Terminal | Connect to | DIP switch |
|---|---|---|
| DB9 5 (and 9) GND | engine block, bonded to battery negative (not direct to battery) | |
| DB9 8 VIN | +12 V via switch and breaker | |
| DB9 6 / 7 | CAN+ (CANH) / CAN- (CANL) | |
| DB9 1 to 4 | not used | |
| TC1 to TC4 | 4 EGT, K, yellow + red - | |
| TC5 to TC8 | 4 CHT, K, yellow + red - | |
| OIL P | VDO pressure sender, single wire, case grounds via engine | OIPP PU on, PD off |
| OIL T | VDO temperature sender, single wire | none (fixed 1500 ohm) |
| AUX1 | LM335 OAT, + to AUX1, - to GND | AUX1 PU on |
| CURR | MGL current sensor green; red to RDAC supply, black to GND | none |
| RPM1 / RPM2 | Magnatron P-leads, 10 kohm in line, ballast OFF for a magneto (or the Hall pickup) | to be decided |
| FUEL P, COOL, FL1, FL2, AUX2, FF1, FF2, 5V | not used, leave open | pull-ups on |

CAN rules from the manual: 120 ohm at each end only, stubs under 20 cm, twisted pair (the RDAC is on the engine side of the
firewall, so daisy-chain the bus to it). Unchecked: which TC screw is + (check markings on the unit), and how the Littelfuse Hall
sensor connects to RPM (check its datasheet).

**Order of work:** (1) RDAC alone on the bench, LED flashing at 1 Hz, frames match baseline. (2) Resistor checks on OIL P and OIL T.
(3) Sensors one at a time: VDO senders, LM335, current sensor (zeroed), thermocouples. (4) Blaze on the bus, check it reads the
RDAC, sniff what it sends. (5) Engine install: EGTs, CHTs, then RPM.

## 2026-10-25: RDAC and Blaze bench tests (Blaze reads the RDAC)

**Blaze EMS-2 is on the shared bus with the RDAC and our modules.** It sent nothing in a 60 s capture including a power cycle (4269 frames, all known IDs, no 0x019), so it is receive-only as far as we can see. It does not disturb our IDs.

**The Blaze only reads the RDAC after Data is set to RDAC in each setup menu** (manual 11.14: there is no global enable). Before that its oil/volts/RPM readings were its own open inputs (an early claim that it was reading the RDAC was wrong). Set Data = RDAC plus the RDAC address in: analog channels (CH1 = RDAC OILT, CH2 = OILP, CH3 = AUX1, CH4 = AUX2), EGT/CHT, volts, RPM, MAP, current.

**Resistor and thermocouple checks (RDAC raw, 0x205 unless noted):**
- OIL P (1000 ohm pull-up): 120 ohm to GND reads 428 to 429 (expected 419), so pull-up about 973 ohm. Blaze with Sender VDO / 5 Bar shows 45 psi (about 3.1 bar, as expected). Open input: Blaze shows 558 psi (out of range, harmless).
- OIL T (fixed 1500 ohm pull-up): 120 ohm reads 297 (expected 290), pull-up about 1460 ohm. Blaze shows 65 C. Not checked against the VDO table.
- CURR with nothing connected floats (3908 steady now; ramped to about 1525 in the first baseline). Meaningless until the sensor is attached.
- K thermocouples (yellow +, red -) on TC1 and TC5 both read correctly (room temp = cold junction, warm fingertip rises about 10 C). Blaze mapping confirmed: TC1 to TC4 = EGT1 to 4, TC5 to TC8 = CHT1 to 4.

**GTR 205x COM radio, tailBeaconX TSO (research, not built):** the GTR 205x installation manual (on the Mac under Sonex/Manuals) documents RS-232 NMEA-style `$PGRMC` messages (9600 8N1, pins 5 out, 46 in, 26 gnd): C00 active freq, C01 standby, C02 volume, C06 request, status at 1 Hz. CAN pins are Garmin G3X only, do not wire to our bus. Plan idea: a Feather COM module (CAN + MAX3232) so both displays can tune it. The tailBeaconX TSO takes RS-232 2400 8N1, UCP-HD/Apollo, with the AV-20-E as control head (squawk is set there). Apollo `#AL` altitude format is public (Apollo SL70 manual, Appendix E); `#MD` and the UCP ICD (UAV-1002375-001, from uAvionix) were not found. The Blaze also has an RS-232 serial output (manual 4.12), unread.

**Next:** connect the real VDO senders (body to GND), then LM335 and the current sensor; Blaze oil T sender type (VDO 120 or 150); conversions for the backup Engine page; RPM source.

## Handy facts
- Boards: `/dev/serial/by-id/usb-Adafruit_*`; serials AIR 38503039350B102C, GPS 38503039350B242B,
  IMU DF625857C7793E2F. Flash: `Sensors/flash_remote.sh <Sketch> <serial>`.
- Backup display (Feather 38503039350B9941, on the Mac as /dev/cu.usbmodem101): `stty -f <port> 1200; sleep 3; picotool load -x <uf2>`
  (arduino-cli upload fails, no drive mounts). The Arduino IDE serial monitor holds the port and blocks flashing.
- Reboot a running board: `stty -F <port> 1200` then `picotool reboot --ser`. Keep hupcl on the tty.
- Regenerate protocol docs: `Protocol/.venv/bin/python Protocol/generate_can_docs.py`.
- When scripting edits to ISSUES.md, check the slice has no other '## ' or '### ' headings first.
- Memory notes: `project_sensor_modules_status.md` in the Claude project memory folder.
- macOS sometimes blocks the shell/Edit tool on repo files (quarantine/macl flags); `xattr -rd com.apple.quarantine Protocol` and the same for `com.apple.macl` fixed it, and `cd /` before `cd`-ing into the repo helps when git says it cannot read the working directory.
