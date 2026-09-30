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

### 8. AIR module: MS4525DO zero offset is taken from a single sample
- `Sensors/AIR_Module/MS4525DO.cpp` `zeroPressureSensor()` (~line 226) is meant to
  average 10 readings at start-up and store `_pZeroCountOffset = 0x2000 - average`.
  `pCount` is declared outside the loop and never reset, so the `while (pCount == 0)`
  read only runs on the first pass. The same first reading is added 10 times, so the
  offset is one noisy sample, not an average.
- Affects both MS4525DO sensors (PS1 pitot and PS2 AOA), since both use this driver.
  One count is about 1 Pa on the 1 psi range; a few counts of error at rest is a few
  Pa, roughly 3-4 kt of airspeed error at low speed. It will also matter for the AOA
  pressure ratio once that is implemented.
- Other limits: the zero is taken once at power-up and assumes equal pressure on both
  ports at that moment (no wind on the pitot, no cover with a pressure difference),
  and it does not track temperature drift afterwards.
- Proposed fix: re-read every iteration (declare `pCount` inside the loop), average
  more than 10 samples with a short delay between them, and bump the version.
  Consider an option to re-zero on the ground.
- Reference: TE MS45xx Auto-Zero application note in `Sensors/Reference/MS4525DO/`.
- Logged 2026-09-30; not yet fixed. Noted while comparing with OnSpeed Gen2, which
  stores fixed zero biases in its config instead (`PFWD_BIAS`, `P45_BIAS`).

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
