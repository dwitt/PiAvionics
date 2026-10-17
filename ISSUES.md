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
- First step when picked up: draft the protocol in `CAN Reference/Can Docs.md` for
  review before any firmware changes.

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
