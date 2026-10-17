# Sensor failure detection: plan and test log

Goal: when a sensor, breakout or wing in a module stops working (at start-up or while
running), the module notices within about 1 s, says so in its CAN heartbeat (state + sensors
mask, `Protocol/can_messages.yaml` 0x701 to 0x703), stops presenting bad data as good, and
recovers by itself if the part comes back.

Status of this document: plan agreed in outline 2026-10-17, nothing built yet. Results are
recorded in the tables as we go. Issue tracker entry: ISSUES.md #9 / #13.

## What exists today (read from the code, 2026-10-17)

| Module | Part | Detected at start-up | Detected while running | What happens today if it vanishes |
|--------|------|----------------------|------------------------|-----------------------------------|
| AIR | Pitot MS4525DO (Wire, 0x28) | yes, halts (state 3) | no | I2C read fails, driver returns count 0 as if it were a reading: pressure becomes large negative, airspeed shows 0 kt. Not flagged. |
| AIR | AOA MS4525DO (Wire1, 0x28) | yes, halts | no | Same. AOA is not used for any output yet. |
| AIR | Static SSC (Wire, 0x78) | yes, halts | no | Not checked. Altitude and vertical speed would show whatever 0 gives (not verified). |
| AIR | OAT MAX31865 + RTD (SPI, CS 24) | yes, halts | bit 3 only (no good sample for 2 s) | Last good OAT keeps being sent on 0x2A forever. Heartbeat bit 3 clears. |
| IMU | ISM330DHCX gyro/accel (I2C 0x6A) | yes, halts | no | Library (locally patched) returns false on a failed read, but the sketch ignores it and keeps using the old values. |
| IMU | LIS3MDL magnetometer (I2C 0x1C) | yes, halts | no | Library does not check the read at all: the buffer holds garbage and goes into the AHRS. |
| IMU | 24LC32 EEPROM (I2C 0x50) | yes, halts | not used while running | Nothing to detect while running. |
| GPS | GPS chip on the Ultimate GPS FeatherWing (UART) | probes, retries | yes (no valid NMEA for 5 s, then re-probe) | Fix dropped after 5 s, heartbeat state 2, mask bits clear. Built but not yet tested by unplugging. |
| all | MCP2515 CAN FeatherWing | yes, halts (cannot report) | no | Not known: does the sketch block when nobody acknowledges frames? Needs testing. |

Other facts that matter:
- The IMU and GPS use stacked FeatherWings. The three AIR pressure sensors are soldered on the
  Feather-Air PCB (not individually unpluggable). The ISM330DHCX and LIS3MDL are two chips on one
  wing. The IMU patch to `Adafruit_LSM6DS` lives in `~/Documents/Arduino/libraries`, outside the
  repo; a fresh library install would silently lose it.
- Do not hot-unplug a stacked wing on a powered module: pins short in passing. Unplug the USB or
  CAN power first, remove the part, power up again (that tests start-up absence). Cables, probes,
  STEMMA/jumper leads and antennas are fine to unplug while running.

## Design to build (for review before any code)

1. **Per-sensor health tracker** (small shared helper): counts consecutive failed reads and the time
   since the last good sample. A sensor goes *invalid* after about 0.3 s of failures and *valid*
   again after about 5 good reads in a row. The sensors mask in the heartbeat follows it.
2. **Drivers report read failure** instead of returning a number: `MS4525DO` and `SSC` return an
   error when the I2C read fails or the status bits say "fault" (status 0b11) or "command mode".
3. **Recovery**: while a sensor is invalid, try to re-initialise it about every 2 s (a part that is
   plugged back in comes back without a power cycle).
4. **Outputs while a sensor is invalid** (needs your decision, see questions):
   - invalid airspeed / altitude / vertical speed / OAT / yaw / pitch / roll sent as `0x7FFF` (the
     MGL "unknown" value already used in `mgl_can_messages.yaml`), so the EFIS can draw a red X;
   - dependent state: pitot invalid gives airspeed invalid only; static invalid gives altitude and
     vertical speed invalid; IMU gyro/accel invalid gives attitude invalid; magnetometer invalid gives
     heading invalid only (the AHRS can run without it).
5. **Module state**: 2 (degraded) when a non-essential sensor is invalid, 3 (failed) only when the
   module cannot produce anything useful. A module keeps sending its heartbeat in both.
6. **LED**: the NeoPixel shows amber while degraded and red when failed, using the existing
   `StatusLED` health colours.
7. **CAN loss**: read the MCP2515 error flags and counters (TX error count, bus-off); show red on the
   LED and in the `?` report. The module cannot tell the EFIS (no bus), so the EFIS side relies on the
   missing heartbeat.

## Test method

A Pi is on the bus: `ssh david@pi-efis.local`, `candump -ta can0,701:7FF,702:7FF,703:7FF,48:7FF,28:7FF,2A:7FF,65:7FF`.
For each test: note the state/mask/uptime lines before, during (about 10 s) and after, record the
time from the action to the first changed heartbeat, then reconnect and record the time to recover.

Phases:
1. **Baseline (today's firmware, no code changes)**: do each unplug and just watch. This shows what
   really happens (garbage, hangs, stale values) before we design around guesses.
2. **Build** the tracker, driver changes and outputs above, one module at a time.
3. **Repeat the same tests** on the new firmware.
4. **Recovery tests**: reconnect each part and check it comes back without a reset.
5. **Fault injection** for parts that cannot be unplugged (see below).

## Test list

Hot = unplug while running. Cold = remove with the module powered off, then power on.
Expected (after step 3) is what the new firmware should do; Today is filled in during phase 1.

### AIR module (0x702, data 0x28 / 0x2A / 0x2B)

| # | Action | How | Expected mask / state | Expected data | Today | Result |
|---|--------|-----|-----------------------|---------------|-------|--------|
| A1 | RTD probe disconnected from the MAX31865 | Hot | bit 3 clear, state 2 | 0x2A not sent (or 0x7FFF) | | |
| A2 | MAX31865 breakout removed (SPI chip gone) | Cold, then Hot if it is on a lead | bit 3 clear, state 2 (hot) / state 3 (cold start-up) | as A1 | | |
| A3 | Pitot sensor lost | Fault injection (soldered) | bit 0 clear, state 2 | airspeed 0x7FFF, altitude still valid | | |
| A4 | Static sensor lost | Fault injection | bit 2 clear, state 2 | altitude and vertical speed 0x7FFF, airspeed valid | | |
| A5 | AOA sensor lost | Fault injection | bit 1 clear, state 2 | nothing else changes (AOA is unused) | | |
| A6 | Whole Feather-Air wing removed | Cold | state 3, bits 0 to 2 clear | no 0x28 | | |
| A7 | Pitot tube blocked or pulled off (sensor still talks) | Hot | no change (not a sensor failure) | note only | | |
| A8 | I2C SDA of Wire shorted or held low briefly | optional, bench | recovers | | | |

### IMU module (0x701, data 0x48 / 0x49)

| # | Action | How | Expected mask / state | Expected data | Today | Result |
|---|--------|-----|-----------------------|---------------|-------|--------|
| I1 | IMU wing (ISM330DHCX + LIS3MDL) removed | Cold | state 3, bits 0 and 1 clear | no 0x48 / 0x49 | | |
| I2 | ISM330DHCX lost | Fault injection (same wing) | bit 0 clear, state 2 | attitude 0x7FFF | | |
| I3 | LIS3MDL lost | Fault injection | bit 1 clear, state 2 | yaw 0x7FFF, pitch/roll valid | | |
| I4 | EEPROM breakout lead unplugged | Cold | state 3 (halts) mask bit 2 clear | | | |
| I5 | EEPROM unplugged while running | Hot | no change (not used) | note only | | |
| I6 | Calibration missing (blank EEPROM) | Cold, optional | state 2, bit 2 clear | AHRS runs | | |
| I7 | I2C lead to the IMU wing unplugged, if it is a lead | Hot | bits 0 and 1 clear, state 2 or 3 | | | |

### GPS module (0x703, data 0x63 / 0x64 / 0x65)

| # | Action | How | Expected mask / state | Expected data | Today | Result |
|---|--------|-----|-----------------------|---------------|-------|--------|
| G1 | GPS antenna unplugged (if external) | Hot | bit 1 clear, state 1 | 0x65 shows no fix | | |
| G2 | GPS wing removed | Cold | state 2 or 3, bits 0 and 1 clear | | | |
| G3 | GPS UART lead unplugged | Hot, if it is a lead | state 2, bits clear within 5 s | no stale position | | |

### All modules, CAN wing / bus

| # | Action | How | Expected | Today | Result |
|---|--------|-----|----------|-------|--------|
| C1 | CAN cable unplugged from the module (module alone, no ACK) | Hot | module keeps running, LED red, `?` report shows the CAN fault, no hang | | |
| C2 | Same, reconnect | Hot | heartbeat and data resume without a reset | | |
| C3 | CAN wing removed | Cold | state 3 flash (cannot report), `?` shows it | | |
| C4 | Other modules' reaction to a dead one | observe on candump | the dead module's heartbeat stops after 3 s; others unaffected | | |
| C5 | Bus terminator or short | out of scope | | | |

## Recovery and safety checks (after each module is done)

- Reconnect each part: mask bit sets again within about 5 s, state returns to 1, data valid again.
- Intermittent connection: wiggle a lead; the bit must not flicker the output every 100 ms (the tracker
  needs 5 good reads to come back).
- Power cycle with the part missing, then plug it in while running: it should come up (recovery).
- No test may leave the module needing a hard reset except where noted.

## Decisions (David, 2026-10-17)

1. Connectors: on the AIR module the pitot, static and AOA sensors are each in a socket (hot unplug
   possible, so A3 to A5 are real unplug tests), the RTD probe and the MAX31865 board both unplug
   (A1, A2 hot), the CAN cable unplugs (C1, C2). IMU: the IMU wing and the EEPROM unplug; the
   ISM330DHCX and LIS3MDL are one wing, so I2 and I3 need fault injection. GPS: wing and antenna
   unplug. Stacked wings are still cold tests.
2. Invalid data marker 0x7FFF (0x7FFFFFFF for the 32-bit altitude) is accepted, but **held until the
   end**: the EFIS does not recognise it today (it would show 32767 kt etc.). It goes in with the EFIS
   heartbeat/backup display pass. Until then the data fields keep their old behaviour; the heartbeat
   state and mask carry the failure.
3. LED: amber for degraded, red for failed.
4. IMU keeps running as a 6-axis AHRS if only the magnetometer fails.
5. Patched `Adafruit_LSM6DS` is now in `Sensors/Libraries/Adafruit_LSM6DS/` with `PATCH_NOTES.md`
   (why, history, what it does and does not catch, how to re-apply) and `local_patch.diff`.
   Open: make the Arduino libraries copy a symlink to it, as for `StatusLED`.

## Test log

(Add dated entries here as tests are run: test number, action, observed heartbeat lines, time to
detect, time to recover, notes.)

### 2026-10-17, Phase 1 (today's firmware: AIR 1.0.23)

**A1, RTD- disconnected (hot), then reconnected** (Pi candump, 0x702 / 0x2A / 0x28)
- Before: state 1, mask 0F, OAT 22.3 C.
- Unplugged: OAT readings stopped varying; about 3 s later the heartbeat went to state 2, mask 07 (bit 3 clear).
  0x2A kept being sent at 5 Hz, frozen at the last value (22.3 C), no warning in the data. 0x28 unaffected (about 10 Hz).
- Reconnected: state 1, mask 0F within about 1 s. But 0x2A then sent bogus temperatures: 96.6 C, 74.4, 59.8, ... decaying
  to 22.3 C over about 12 s, with a second dip of the heartbeat to state 2 for 0.8 s and a spike of 166.9 C and 313.5 C
  on the bus (probably contact bounce while plugging in, passed through the OAT filter).
- Findings: (1) stale OAT looks valid (marker will fix this later); (2) on recovery the OAT filter should be reset and
  readings outside a sane RTD range rejected, otherwise bad values go out with the OK bit set for about 12 s.
