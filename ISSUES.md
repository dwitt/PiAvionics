# PiAvionics Issues

Running list of known problems and follow-ups. Newest first within each section.
Status: **Open**, **Monitoring** (fix applied, watching), **Fixed**.

## Open

### 1. AIR module (0x28-0x2B) not on the CAN bus
- Seen 2026-09-30: only 0x48, 0x49, 0x2E and 0x65 (plus 0x63/0x64 with a fix) were
  present. No 0x28 (airspeed/altitude/VSI), 0x29 (AoA), 0x2A (OAT), 0x2B (static pressure).
- Not yet diagnosed. Check power, connector and wiring of the AIR module first.
- Effect: the EFIS has no airspeed or altitude.

### 2. `monitor_timeout()` busy loop pins a CPU core
- `EFIS/aio_server.py` (~line 1002-1020): `await asyncio.sleep(0)` in a `while True`
  loop spins flat out. `aio_server.py` sits at ~100% of one core.
- Proposed fix: `await asyncio.sleep(0.1)`, bump the file version, deploy.
  Deploying restarts `piefis` and blanks the display briefly.
- Chromium (GPU + renderer) also uses 40-60% each; that is the 20 Hz PixiJS display
  and is a separate matter.
- Load average ~5 on 4 cores; no throttling (`throttled=0x0`), ~63 C.
- **Fixed and deployed 2026-10-18** with EFIS server 1.1.0: `monitor_timeout()` was replaced by `monitor_status()`, which sleeps 0.1 s
  between passes. `aio_server.py` now uses about 6% of one core (before: about 50% averaged over 39 hours, 20 h 26 min of CPU
  in that time). Chromium's load is unchanged. Still to do: move this entry to "Fixed" and re-measure the load average once it settles.

### 3. `can0` does not auto-recover from bus-off until next boot
- `/etc/systemd/system/can0.service` now sets `restart-ms 100` (backup saved as
  `can0.service.bak`), but the kernel refuses to change it while the interface is up.
- Takes effect at the next Pi boot, or after `sudo systemctl restart can0`.
- Restarting can0 puts the MCP2515 to sleep and needs an oscillator cold start
  (see issue 4), so check it comes back cleanly.

### 4. CAN board (Pi-CAN-Power-i2c): crystal load capacitors and BOM
- Y1 is an 18 pF-load crystal, but the BOM/schematic list it as "16Mhz 9pf", and
  C2/C3 are 15 pF. Suggested C2/C3: ~27 pF C0G/NP0 0603 (22 pF if startup margin
  is a concern). Update BOM and schematic once the crystal part number is known.
- Not confirmed as the cause of the outage below; a load mismatch mostly affects
  frequency accuracy, not startup.

### 5. Stale "Breakout" references for the GPS hardware
- The hardware is the Adafruit Ultimate GPS **FeatherWing**.
- README fixed (commit 9800d76). Still to fix: the header comment in
  `Sensors/GPS_Module/GPS_Module.ino` and `Sensors/GPS_Module/PLAN.md`.

### 9. Enhancement: module identity, version and commands over CAN
- Idea (2026-09-30): let the EFIS read each module's firmware version and send it
  commands (reset, zero) over CAN. Nothing is designed or implemented yet.
- Motivation: during the GPS/AIR debugging there was no way to tell which firmware
  a module was running, or whether a module was absent, without a USB cable. Zero
  over CAN would also fix the power-up-only limit in issue 8, and reset would let
  a module be restarted from the Pi.
- Suggested order:
  1. Version/heartbeat: each module sends module type + VERSION (major/minor/patch)
     every few seconds. Also acts as a "module present" indicator for the EFIS.
  2. A shared command message and reply (target module in byte 0, command code,
     result/ack). The IMU calibration design (`Sensors/IMU_AHRS/IMU_Calibration_Design.md`,
     CAN 0x23) already proposes commands over CAN and should use the same scheme.
  3. Zero command for the AIR module. The module refuses unless pressure is near zero
     and steady, and replies with the offset it computed.
  4. Reset command (RP2040 watchdog reboot), two-step arm/confirm so a stray frame
     cannot reboot a module in flight.
- Open decisions: command/response CAN IDs (none reserved yet; existing IDs are
  0x19, 0x23, 0x28-0x2E, 0x32, 0x48/49, 0x51-53, 0x63-65); whether commands are
  ground-only (suggested default, enforced in the module as well as the EFIS);
  where the EFIS triggers them (soft-button menu).
- Out of scope for now: reading or updating the firmware image over CAN, which would
  need a bootloader.
- Step 1 done 2026-10-17: heartbeats 0x701 (IMU), 0x702 (AIR), 0x703 (GPS) are in
  `Protocol/can_messages.yaml` and sent by IMU_AHRS 1.0.4, AIR_Module 1.0.23 and
  GPS_Module 1.0.3 (shared code in the ModuleInfo library 1.1.0; StatusLED 1.1.0 keeps the
  heartbeat going during startup and while a failure code blinks). Verified on the bus: state 0
  from uptime 0 s, then state 1 with the sensors mask, uptime resetting on a reboot.
- Still to do: handle the heartbeats in `aio_server.py` and show them (to be done with the
  EFIS display, together with David's backup text display and an EFIS heartbeat 0x700);
  detect a sensor that stops answering while running (issue 13); the command messages
  (steps 2 to 4 above).

### 13. Sensor failure detection while running
- Raised 2026-10-17. Only start-up is checked today (plus the AIR temperature sensor, and the GPS
  by NMEA silence). A pressure sensor, the IMU chips or the CAN controller that stop answering in
  flight are not detected, and their last or zero values keep being sent as if they were good.
  Design, test list and results go in `Sensors/FAILURE_DETECTION_PLAN.md`.
- Found while planning: the installed `Adafruit_LSM6DS` library has a local patch (a failed read
  returns false) kept outside the repo; the IMU sketch ignores the result; the LIS3MDL library
  does not check its read at all.
- 2026-10-17 progress (details and test log in the plan): baseline unplug tests done. Root cause
  of the AIR/GPS freeze when the CAN cable is pulled: no 120 ohm termination on their CAN boards, so
  the stock MCP2515 library waited forever for an acknowledge. Step 1 done (bounded CAN send, patched
  library in `Sensors/Libraries/Adafruit_MCP2515`; verified on all three modules). Step 2 done (IMU:
  health tracking, chip re-initialisation, 6-axis AHRS without magnetometer, fault injection; verified
  with injected faults and a real wing pull). Steps 3 (AIR), 4 (status LED) and 5 (GPS no-data path) also done and tested. Still to do: full retest against the
  plan tables and recovery tests, then the 0x7FFF invalid-data markers with the EFIS pass.
- 2026-10-18: the EFIS side (display step A, the Modules page, server step B with the marker decoding and the EFIS heartbeat 0x700) is done, and the
  modules now send the markers (AIR_Module 1.0.35, IMU_AHRS 1.0.16; the GPS module sends none by decision). Verified on the real bus with injected faults:
  pitot, static, OAT, accel/gyro and magnetometer. A marker bug found on the way (a thin red X drawn across the display) is ISSUES #17, fixed.
  Not built yet: the backup display (Sensors/Backup_Display) status row and per-field markers; David will design it next.

### 14. `/var/log/piefis.log` has no rotation and grows without limit (fixed 2026-10-18)
- Found 2026-10-18: the file was 33 MB, 369,835 lines (the service writes its output there: `StandardOutput=append:/var/log/piefis.log`). 366,280
  of those lines were `Error sending CAN message: Failed to transmit: No buffer space available [Error Code 105]`, one per second for days when nothing on the
  bus acknowledged the Pi's QNH frames; the last one was about 1,000 lines from the end, so it stopped long ago. `logrotate` was installed and its timer ran
  daily, but nothing was configured for this file.
- Fixed 2026-10-18, the cause (server 1.6.3, deployed): a failed QNH send is reported once and then at most once a minute with a count (`RepeatedErrorReporter`,
  shared with the EFIS heartbeat), so the same fault can never fill the log again. Tests: 2, with 4 deliberate breaks caught.
- Fixed 2026-10-18, the file: `/etc/logrotate.d/piefis` installed on the Pi from `EFIS/linux/piefis.logrotate` (rotate at 5 MB, keep 4, compress, `copytruncate` because
  systemd holds the file open). Run once by hand: the 33 MB file became `/var/log/piefis.log.1` and the log restarted empty, and the service kept writing to it
  (a page reload showed up in the new file). The timer runs it daily (next 00:22). The old `piefis.log.1` (33 MB of the old error lines) is compressed by the next run and
  removed after four more rotations; delete it by hand to reclaim the space sooner.
- The service-ordering finding from this investigation is ISSUES #18.

### 15. HSI ground track diamond and course pointer mixed true and magnetic (fixed 2026-10-18, one moving test left)
- Found 2026-10-18 (David asked where the HSI heading comes from). The HSI rose is rotated by `dataObject.yaw`, the IMU module's
  fused heading (CAN 0x48), which is a **magnetic** heading. The magenta track diamond was placed from the GPS's **true** track, and the
  desired track (DTK) and bearing from `compute_navigation` (flightplan.py) are also **true** degrees, so the diamond and the flight-plan
  course pointer were off by the local magnetic variation (David's area: -10 degrees, 10 West).
- **Fixed 2026-10-18, track diamond (server 1.4.0, page 0.5.5):** the server keeps `mag_track` (0x64 bytes 6-7, the GPS module's true track
  corrected with its WMM2025 declination) and the page gives it to the diamond.
- **Fixed 2026-10-18, course pointer and bearing (GPS_Module 1.0.9, server 1.5.0):** the GPS module now sends its declination in 0x65 byte
  3 (int8 whole degrees, East positive, 0x7F until calculated; it is calculated as soon as there is a fix and a date, then every minute).
  The server decodes it (`gps_declination` in the JSON), converts the flight plan's desired track and bearing to magnetic (`magnetic = true -
  declination`) and holds back dtk, bearing, cross-track and TO/FROM until the declination is known (the pointer then follows the CRS
  setting). Protocol `Protocol/can_messages.yaml` and the generated docs updated (generator 1.2.0 accepts int8 markers).
- Checked on the live system: with the GPS module on 1.0.9 the server reported a declination of -10, and the desired track changed from 15.3
  (true) to 25.3 (magnetic) and the bearing from 332.1 to 342.1.
- **Still to do:** a moving test (drive or flight) to see the track diamond and the course pointer agree with a compass. Before the GPS
  module reflash the byte read 0 (declination 0, so no correction); modules older than 1.0.9 therefore get no correction, not a wrong one.
- Idea for later: the backup display can read the same declination from 0x65 if it ever shows a course.
- Added 2026-10-18 (page 0.7.0): the track diamond is hidden below 10 knots of ground speed (`TRACK_MIN_SPEED_KT` in `efis-display.js`), because the GPS track is noise when slow
  (the module's own hold-at-0 below 2 kt, `TRACK_SPEED_THRESHOLD` in the GPS firmware, is unchanged).

### 16. Enhancement: make the HSI's GPS box a touch button that expands and contracts (done 2026-10-18)
- Asked for by David 2026-10-18. Done in page 0.7.0 (`hsi.mjs`, `_createGpsStatusBox`): the box in the top left of the HSI is a touch button. Contracted (the
  normal state, and how it starts after every reload) it shows the fix status and `VAR 10°W`; a touch expands it to also show SAT, HDOP, VDOP and DIFF (whether
  differential corrections are used); another touch contracts it. It stays as David left it (no timer). A small arrow shows it can be touched.
- The status word is now `3D FIX`, `2D FIX` or `NO FIX` (from the GPS module's fix type, `gps_fix_3d`), coloured by the HDOP quality for 3D (green below 1.5,
  cyan below 3, yellow below 5, red above), yellow for 2D, red for no fix, grey dashes when the GPS is lost. "DGPS" is no longer the headline: the word `WAAS` sits
  on the same line, green when the module reports differential (WAAS) corrections and dim when it does not (page 0.7.1; David's idea, replacing a DIFF row in the
  expanded view). (My claim that the chip's DGPS flag is "unreliable" was withdrawn: today's DGPS looked normal.)
- Checked at the Display 2 portrait size with the fake feed (contracted, expanded, poor fix, 2D, no fix, GPS lost). David confirmed on the real Pi 2026-10-18: the touch expands and contracts it, and WAAS shows green (the module reports WAAS corrections in use).

### 17. A thin red X drawn across the display when a module sent a marker (fixed 2026-10-18)
- Found 2026-10-18 by David while the modules' invalid-data markers were first tested (the pitot fault on the AIR module): "an extra X is drawn
  somewhere else when the data drops".
- Cause: a marker turns its value into `null` the moment the frame arrives, but the server re-evaluates `valid` only every 0.1 s, so for up to 100 ms
  the page got a null value that was still flagged valid and handed it to the instrument. The old classes have their own built-in red X for
  bad data (`Ribbon.badDataGraphic`, `AttitudeIndicator.badDataGraphics`); it was drawn across the middle of the display next to the new failure
  flags and the tapes scrolled to 0. (The older failures never showed it because their values stayed numbers until the validity changed.)
  Reproduced in a browser with the fake feed (`race_gyro`, `race_pitot` scenarios) and confirmed gone after the fix.
- Fixed (server 1.6.2, page 0.6.1): the sending loop applies the markers to `valid` again just before every send (`apply_markers_to_valid`), and
  `InstrumentStatus` treats an explicit `null` value as invalid at once (airspeed, altitude, vsi, oat, pitch or roll, yaw, turn rate, any
  acceleration, both pressures; a missing key from an older server is not treated as null). Tests: 2 server tests and 4 page tests; 2 and 9 deliberate breaks caught.
- Note: the old built-in X's in `Ribbon` and `AttitudeIndicator` are still there and would show if a page ever passed `undefined` (ribbons) or `null`
  (attitude) to them; the page now never does.

### 18. `piefis.service` is not ordered against the CAN interface (found 2026-10-18, parked for later)
- `EFIS/linux/piefis.service` (the deployed unit is identical) has `DefaultDependencies=false` and `After=network.target` only: no ordering against `can0.service`.
  At a reboot or a restart the CAN interface can go down before the server stops, and the server prints about 40 long tracebacks into
  `/var/log/piefis.log` (`can.exceptions.CanOperationError: Error receiving: Network is down [Error Code 100]`, from python-can's notifier). It is harmless noise,
  and the log rotation (ISSUES #14) keeps it bounded.
- Possible fix: add `After=can0.service` and `Wants=can0.service` (as `piefis-labwc.service` already does) so the server starts after the CAN interface is up and is stopped
  before it goes down, and drop `DefaultDependencies=false` so the service is stopped in order at shutdown. That changes boot and shutdown ordering a little, so test it with
  a reboot (the display, the CAN modules and the heartbeat should come up as before). Also check what happens if `can0` is slow to come up (the server has `Restart=on-failure`).

## Monitoring

### 6. MCP2515 not initialising on the Pi (`Cannot initialize MCP2515. Wrong wiring?`)
- 2026-09-30: `can0` went bus-off at 09:17:42 after ~14 h of normal running. It
  did not recover (`restart-ms 0`). Cold boots then failed with `Probe failed, err=19`
  and down/up cycles failed with `didn't enter in config mode`.
- Diagnosis: raw SPI tests (spidev) showed the chip and SPI link were healthy while
  its oscillator was already running (0 errors in 2000 read/writes at up to 4 MHz).
  It failed whenever the oscillator had to start from cold (boot, or after the
  driver put the chip to sleep); registers then read 0x00 and never recovered.
- Fix: reflowed the MCP2515, C2/C3 and the crystal. Cold boot then initialised
  normally at 10:04:07 and traffic resumed.
- Root cause is probably a bad joint on the crystal circuit; not proven.
- Watch for a repeat. If it returns, suspect Y1 and the load caps (issue 4).
- Config: `dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=25,spimaxfrequency=2000000`.

## Fixed

### 10. AIR module: INT trace on the CAN FeatherWing (cut 2026-10-17)
- Found 2026-10-16: the CAN FeatherWing INT output is on GPIO8, the same pin as
  Wire1 SDA (AOA sensor). `Electronics/Feather-Air/Feather-Air_Connections.md` and the
  AIR sketch both say the INT trace must be cut, but on the wing in the AIR module it
  is not cut (or not fully): GPIO8 read low before Wire1 started and 9 clock pulses
  did not release it.
- Effect: after any software reset (flash, picotool reboot, a watchdog reset) the
  MCP2515 kept INT asserted, the AOA sensor was "not found" and the module halted
  until a hard reset or power cycle. In normal running INT can also disturb Wire1.
- Software workaround in place: AIR_Module v1.0.4 starts CAN first (which resets the
  chip) and clears the MCP2515 CANINTE register so INT is never driven. The CAN library
  polls flags, so receiving is unaffected. This is not a substitute for the cut.
- To do: cut the INT trace on the CAN FeatherWing used in the AIR module, then check
  with a meter that there is no continuity between the wing's INT pad and GPIO8
  (D9). Check any other CAN wing that goes into a module using GPIO8 as well.
- Related: a reset command over CAN (issue 9) would hit the same problem on boards
  where the trace is not cut and the workaround is missing.
- Fixed 2026-10-17: the INT trace was cut. Tested with the software workaround off
  (v1.0.11 build): 5 soft reboots plus a flash, AOA found every time. The workaround stays
  on in v1.0.12 (`CAN_INT_WORKAROUND 1`) as a safety net for a wing whose trace is not cut.
- Still to check: any other CAN wing that goes into a module using GPIO8.

### 11. AIR module: 75 ms RTD read held the whole sensor loop to about 11 Hz
- Found 2026-10-16. The Adafruit MAX31865 library's `rtd.temperature()` waits with
  `delay(10)` (bias settling) and `delay(65)` (conversion) inside `readRTD()`. Measured on
  the module: 75.2 ms per call (`readFault()` 1.07 ms). The AIR sketch called it on every
  pass of the sensor block, which is meant to run every 10 ms, so the block ran about 34
  times in 3 s (11 Hz) instead of 100 Hz.
- Effects: CAN rates below the protocol (0x28 at 5.7 Hz instead of 10 Hz, 0x29/0x2A/0x2B
  at 3.9 Hz); the pressure, airspeed and altitude calculations got a new sample about every
  88 ms. The low-pass filters (`LowPassFilter(0.01)` static, `(0.04)` pitot) are per-sample,
  so they ran about 9 times slower than designed. OAT was also unfiltered and truncated
  instead of rounded (std dev 0.14 C, 0.5 C peak to peak).
- Fix (AIR_Module v1.0.5): `rtdUpdate()` is a non-blocking state machine (bias on, settle,
  convert, read, fault check) called every `loop()`; the OAT is sampled every 250 ms,
  smoothed with a 1.4 s low-pass and rounded. A sample with a fault is dropped and the last
  good value kept; 0x2A is not sent until the first good sample.
- Result: 0x28 9.8 Hz, 0x29/0x2A/0x2B 4.9 Hz; OAT std dev 0.035 C, range 0.1 C over 30 s.
- Pitot filtering reworked 2026-10-17 (AIR_Module v1.0.20). Raw MS4525DO readings at rest
  were measured: std dev about 4.7 Pa, fast noise, in steps of about 6 Pa; the resting
  level also moves a few Pa between boots (+2 to -5 Pa), so a deadband is needed. On release
  the raw pressure falls from about 100 Pa to rest within 0.2 s, so a slow tail is the
  filter, not the tube. Tried and dropped: no deadband (3-4 kt at rest); a filter whose
  strength follows the pressure level (5-7 kt tail after a blow); averaging 8 reads per
  sample (noise 4.8 -> 3.7 Pa only, and about 5 ms of blocking per sample).
- Kept: 0.12 hPa deadband. New low-pass: strength follows the difference between the new
  sample and the output (heavy averaging alpha 0.02 inside the noise band, alpha 0.2 for a
  real change), so it drops to zero quickly when the pressure stops. Result: steady 0 at
  rest, and 0 within 0.4-0.8 s of releasing a hard blow. Nothing is shown below about 9 kt.
- Notes: the 4.7 Pa figure is a constant in the sketch (PITOT_NOISE_HPA); re-measure it if
  the sensor or tubing changes. The same idea could be applied to the static pressure.

### 12. AIR module: vertical speed was never calculated
- Found 2026-10-17: `verticalSpeed` in 0x28 was declared and sent but never set, so it
  was always 0 (`previousStaticPressure` was saved and unused).
- Fixed in AIR_Module v1.0.22: the pressure altitude of the low-passed static pressure
  (before the smooth deadband, which would add 1-2 ft steps) is sampled every 100 ms into a
  3 s ring buffer; rate = altitude now minus 3 s ago, smoothed (alpha 0.1 per 100 ms).
- Measured: at rest mean -1 ft/min, std dev 17, range +-40 (a 1 s window gave std dev 50,
  +-100: slope noise falls steeply with window length because the raw static pressure is
  6 ft std dev in 3.3 ft steps). Lifting the module about 1 m gave +62 ft/min and
  lowering it -46 ft/min, about 2 s after the motion, and the sign is right.
- Limit: about 3 s lag and about +-40 ft/min resolution at rest. A faster VSI would need
  a quieter pressure source or an accelerometer (the IMU) blended in.

### 8. AIR module: MS4525DO zero offset was taken from a single sample
- Found 2026-09-30. `MS4525DO::zeroPressureSensor()` was meant to average 10 readings at
  start-up, but `pCount` was declared outside the loop, so one reading was counted 10 times.
  The first reading after power-up is sometimes about 14 counts off, so the resting
  differential pressure read 0 Pa on some boots and about 15 Pa (8-10 kt of airspeed with
  nothing on the pitot) on others. The deadband filter hid the jitter when the zero was right
  and showed it when it was wrong, which looked like pitot noise (reported 2026-10-16).
- Fix (AIR_Module v1.0.10): discard the first 5 readings, then average 32 good readings 5 ms
  apart (about 190 ms extra at start-up), skipping bad or stale status; gives up after 200
  reads and keeps the old offset. Affects both MS4525DO sensors (pitot and AOA).
- Result: 8 consecutive soft reboots all read 0.0 Pa and 0 kt at rest (before: 0 or 15 Pa).
- Remaining limits: the zero is still taken once at power-up and assumes equal pressure on
  both ports then (no wind, no cover with a pressure difference), and does not track
  temperature drift. A zero command over CAN (issue 9) would address that.
- Reference: TE MS45xx Auto-Zero application note in `Sensors/Reference/MS4525DO/`.

### 7. GPS module needed an RP2040 reset to pick up a fix after a full repower
- Old firmware set the GPS baud once at boot (9600 -> 115200), sent config commands
  once with no check, and hard-coded `gps_success = true`. After a full repower the
  RP2040 could end up listening at the wrong baud and never see valid NMEA while
  the GPS itself had a fix (reporting "no fix", 0 satellites).
- Fixed in `GPS_Module.ino` v1.0.1 (commit 18410a3): probes 115200 then 9600 for valid
  NMEA, switches the GPS to 115200, configures it, and has a 5 s watchdog that
  re-probes and clears a stale fix.
- Verified 2026-09-30: three full repowers (including one after a long power-off)
  each produced a fix with no reset. Times to fix: 50-67 s from first status frame.
- Exact cause of the old failure not proven (baud mismatch vs boot-time race).
  To find out, report the probe result in the reserved byte 3 of 0x65.
- The GPS FeatherWing has no backup battery, so every power-up is a cold start.
- Behaviour change: the NeoPixel is orange if the GPS is silent (CAN up); the
  flashing red is now only for CAN init failure.

## Notes
- The GPS chip's reset is not connected to the RP2040. Resetting the Feather leaves
  the GPS running and holding its fix.
- Diagnosis tip: if `can0` is missing or will not come up, check `dmesg | grep mcp251x`.
  `candump -e can0` shows frames and error frames; `ip -s -d link show can0` shows
  state and error counters.
