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

## Questions for David

1. Which of the parts above are really on connectors in your modules (RTD probe, MAX31865 board,
   EEPROM, GPS antenna, GPS wing, CAN cable, IMU wing)? That decides hot versus cold and which
   tests need fault injection.
2. Invalid data: send 0x7FFF in the affected field (my suggestion), or stop sending the frame?
   0x7FFF keeps the other fields of 0x28 alive (altitude when only the pitot has failed).
3. Is amber for degraded and red for failed right on the module LED?
4. Should the IMU keep running as a 6-axis AHRS when only the magnetometer fails (my suggestion), or
   fail the whole module?
5. OK to copy the patched `Adafruit_LSM6DS` into `Sensors/Libraries/` so it is in git?

## Test log

(Add dated entries here as tests are run: test number, action, observed heartbeat lines, time to
detect, time to recover, notes.)
