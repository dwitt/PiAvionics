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

**A1b, RTD+ disconnected (hot), then reconnected**
- Unplugged: OAT went to -16.1 C then -70.0 C and stayed there, sent at 5 Hz (a false but plausible cold reading,
  unlike RTD- which froze the old value). Heartbeat state 2, mask 07, about 2.7 s later.
- Reconnected: heartbeat back to state 1, mask 0F about 0.6 s after the readings started coming back, but the OAT
  was still -23 C and took about 6 s (filter slew) to reach 22 C. So the OK bit was set while the value was wrong.
- Both tests: detection time about 2.7 to 3 s; recovery of the bit about 1 s; recovery of the value 6 to 12 s.

**A1c, RTD third wire (the doubled lead) disconnected (hot), then reconnected**
- Unplugged: heartbeat state 2, mask 07 (detected). OAT froze at 22.4 C (it normally moves between 22.2 and 22.4).
- Reconnected: first reading -16.9 C, heartbeat back to state 1, mask 0F within 0.5 s, value back to 22.3 C after about 6 s.
- RTD summary: every lead pulled is detected through bit 3 (state 2) in about 3 s. Data on 0x2A is either frozen (RTD-,
  third wire) or a false -70 C (RTD+). After reconnect the OK bit comes back before the value is right (6 to 12 s).

**A2, MAX31865 board pulled (hot), then reinserted** (RTD still on the board)
- Pulled: OAT fell to -242.0 C (about 8 s slew) and stayed there at 5 Hz. **Heartbeat never changed: state 1, mask 0F
  throughout. NOT DETECTED.** Probably the SPI bus reads all zeros with no chip, the firmware sees "raw 0, no fault"
  as a good reading, and 0 converts to about -242 C.
- Reinserted: value back to 22.2 C after about 8 s; heartbeat unchanged (never left OK).
- Needed: reject RTD readings outside a believable range (and/or read back the MAX31865 configuration register) so the
  missing chip clears bit 3 and sets state 2.

**C1/C2, AIR module CAN cable unplugged (hot), then reconnected** (watched via the USB `?` report and the LED)
- Unplugged: the module stopped sending (last frame about 15 s after the capture started) and **stopped answering `?`
  over USB** (tried twice, 8 s apart). USB still enumerated. LED stayed green (frozen). So the whole firmware loop is
  blocked, not just the CAN output: no sensor processing, no status, no LED change.
- Cause (read in the library): `Adafruit_MCP2515::endPacket()` loops `while (TXBnCTRL & TXREQ)` with no timeout; with
  nobody to ACK the frame, the MCP2515 retries forever. Exits only on the TXERR flag. Same library on IMU and GPS, so
  those very likely hang the same way (not yet tested).
- Reconnected: the module resumed by itself (heartbeat 0x702 again within about 1 s of plugging in), uptime counter
  continuous (11090 s, no reset), `?` answers again, no error frames seen on the bus. No power cycle needed.
- Needed: bound the transmit wait (abort the TX request via TXBnCTRL after a few ms, count it, set LED amber/red,
  keep the loop running), probably also use one-shot mode so a lost frame is not retried forever.

**I5, IMU EEPROM pulled while running:** no change at all (heartbeat state 1 mask 07, 0x48/0x49 at 10 Hz). As expected.

**I7, IMU wing (ISM330DHCX + LIS3MDL) pulled live, then reinserted** (IMU_AHRS 1.0.4)
- Pulled: **not detected, heartbeat stayed state 1, mask 07 throughout.** 0x49 showed the last reading for about 0.3 s then
  0, 0, 0 (kept sending at 10 Hz). 0x48 kept sending a plausible attitude that drifted steadily (yaw 19 to 7 deg,
  pitch to -48 deg, roll to 22 deg in 55 s), presumably the AHRS integrating stale gyro values with no gravity reference.
  The EFIS would show a smoothly moving horizon that is not real.
- Reinserted: 0x49 stayed 0, 0, 0 (the chip lost its configuration and the sketch only configures at start-up), 0x48
  jumped (yaw to 359 deg) and then slowly converged. **Needs a reset to recover.** After a software reboot the module
  came back normally (accel z 994 mg, level attitude).

**C1/C2, IMU module CAN cable unplugged (hot), then reconnected** (watched via the USB `?` report and the LED)
- Unplugged: nothing reached the bus (last frame 7 s into the capture) but the module did NOT freeze: it kept answering `?`
  (uptime 100 s then 116 s 16 s later) and the LED kept flashing green. So unlike the AIR module the main loop is
  still running. Why the two differ is not yet understood (same CAN library; not investigated).
- Reconnected: frames resumed about 1 s after plugging in (55.9 s gap), no burst of buffered frames, heartbeat uptime
  continuous (142 s, no reset), attitude sensible (yaw 348 deg), no error frames. No power cycle needed.
- Still needed on both: show the CAN problem (LED amber/red, `?` report) instead of looking healthy.

**C1/C2 repeat, AIR module CAN cable pulled 3 more times** (poller sent `?` every 2 s; bus gaps compared)
- Pull 1: bus silent 14.0 to 25.4 s, `?` unanswered 16 to 24 s. Pull 2: silent 30.7 to 42.0 s, unanswered 32 to 40 s.
  Pull 3: silent 46.8 to 56.9 s, unanswered 48 to 54 s. **Froze all three times** and recovered by itself each time when the
  cable went back in (about 1 s). Uptime continuous (no resets). David varied when he pulled.
- So the AIR freeze is reproducible; the idea that the IMU differed only because it was pulled between frames is not
  supported by the AIR side (3 of 3 froze). The IMU has only been pulled once; repeat to see if it ever freezes.

**AIR freeze located (test build 1.0.24 with `CAN_SEND_MARKER`: LED blue inside `endPacket()`, green when it returns)**
- Cable pulled: **LED stuck blue**, so the module is frozen inside `Adafruit_MCP2515::endPacket()`, not in the receive code.
  Cable back in: LED green immediately, bus traffic resumed (28.7 s gap).
- So the library's send loop (`while TXREQ`) never exits when nothing ACKs; the abort-on-TXERR path is evidently not triggered
  on this module. Fix: bound the wait (abort via CANCTRL ABAT after a few ms) in our own send helper.
- Why the IMU does not freeze is still unexplained. Receiving is not the cause. Open: repeat the IMU pull a few times.

**AIR freeze with the INT workaround off** (test build 1.0.25: `CAN_INT_WORKAROUND 0`, INT trace cut)
- Cable pulled: LED stuck blue again, cable back in: green at once. Same behaviour, so the cut INT trace / CANINTE
  workaround is not involved. The library polls the TX status register and never uses the INT pin.

**IMU CAN cable pulled 3 more times (poller + capture)**
- Bus gaps 4.7 s, 4.4 s and 1.6 s (shorter pulls); the `?` query was answered every 2 s throughout (21 of 21), uptime
  continuous, no reset. **The IMU never froze (4 pulls out of 4 including the first).** The AIR module froze every time (4 of 4).
- So the modules really differ. Same send code and library, so the cause is something in setup or hardware (CAN chip
  state, wing, termination?). Plan: when the bounded send is built, also log the MCP2515 error flags and counters
  (EFLG, TEC) on timeout in the `?` report on each module and compare. Not needed to proceed.

**Cause found: bus termination (David's observation, tested 2026-10-17)**
- The IMU's CAN board has the 120 ohm end-of-bus resistor fitted; the AIR module's does not.
- AIR module, cable pulled: LED stuck blue (frozen in the send). With a 120 ohm resistor connected across CANH/CANL (cable
  still out) the LED went green, i.e. the send completed and the module ran. Repeated several times, same result; cable
  then reconnected. (David reported this as "the imu"; the blue marker only exists in the AIR build, so I take it as AIR.)
- Conclusion: a node alone on an UNTERMINATED bus freezes in `endPacket()`; with termination the chip raises its error
  flag and the library aborts. In the aircraft only the two ends of the bus are terminated, so any middle module whose
  connector or wire fails ends up in exactly this state. The bounded send is therefore needed on every module regardless.
- The same applies to a module whose neighbours on both sides are lost; test the fix with the resistor removed.

**G1, GPS external antenna unplugged (hot), then replugged** (GPS_Module 1.0.3; fix on the bench at the start)
- Unplugged: the wing fell back to its internal patch antenna and kept its fix. 0x65 satellite count went 14 to 4-6, the
  accuracy field roughly tripled, once the fix type dropped to 2D (3 satellites, at about 55 s). Heartbeat unchanged:
  state 1, mask 03 throughout (a 2D fix still counts as a fix). 0x63/0x64/0x65 kept coming at 1 Hz, no gaps.
- Replugged: satellites back to 14 to 15 within about 40 s. No intervention needed.
- OK as is: degradation is visible in 0x65 and the module did not fail.

**C1/C2, GPS module CAN cable unplugged (hot), then reconnected** (watched via the `?` report)
- Unplugged: the bus went quiet and `?` went unanswered (two tries, 3 s apart): **frozen, like the AIR module**.
  LED stayed green. (David confirmed the GPS CAN board has no termination resistor, same as AIR.)
- Reconnected: it answered `?` again at once (uptime 12798 s, continuous, no reset), frames back on the bus.
- Summary of the CAN cable tests: AIR and GPS (no termination) freeze in the send when pulled; IMU (termination fitted) does not.
  Three modules, consistent with the termination explanation.

### 2026-10-17, Phase 2 step 1: bounded CAN send (AIR 1.0.26, IMU 1.0.5, GPS 1.0.4, ModuleInfo 1.2.0)

- Patched copy of Adafruit_MCP2515 in `Sensors/Libraries/Adafruit_MCP2515` (notes: `PATCH_NOTES.md`): `endPacket()` gives up
  after 10 ms, then skips sends for 500 ms between single retries; counters and TEC/REC/EFLG shown in the `?` report
  (`moduleInfo.setReportHook`). Builds from `flash_remote.sh` pick the repo copy (`--libraries`) ahead of the Arduino one.
- **AIR, no termination, cable out for about 3 minutes: no freeze.** All status queries answered, uptime continuous, `?` showed
  `CAN tx: DOWN (no acknowledge), timeouts 137, skipped 1718, TEC 0, REC 135, EFLG 0x4B`. (The chip counts receive errors from
  the floating bus but never counts transmit errors, which is why the stock library never saw TXERR.) Cable back in: traffic
  resumed (about 0.5 s), `CAN tx: ok`, timeouts 185 total, REC back to 0. EFLG 0x40 (RX0OVR) is set even when healthy: the
  receive buffer overflowed at some point; not investigated.
- Removed the temporary LED marker and restored `CAN_INT_WORKAROUND 1` in the AIR sketch.
- **GPS 1.0.4, no termination, cable out about 40 s: no freeze.** Status queries answered every 2 s, uptime continuous. After
  reconnecting: heartbeat 0x703 back at once, `CAN tx: ok, timeouts 45, skipped 135, TEC 0, REC 0, EFLG 0x40`.
- **IMU 1.0.5, terminated, cable out about 25 s: no change in behaviour.** Queries answered, data frames 0x48 resumed after
  reconnect, `CAN tx: ok, timeouts 0, skipped 0` (the terminated node still sees send errors, so the stock exit path works).
- Step 1 done: no module can freeze on a missing CAN connection any more.

### 2026-10-17, Phase 2 step 2: IMU failure handling (IMU_AHRS 1.0.11, ModuleInfo 1.3.0)

Built:
- Accel/gyro and magnetometer each have a health tracker: invalid after 20 bad samples in a row (0.2 s), valid again after 5
  good ones. A bad sample is a failed I2C read (the patched LSM6DS reports it; the magnetometer is now read by the sketch
  itself because the LIS3MDL library ignores a failed read), all-zero accel (a chip that lost power), or 100 identical
  samples in a row (a locked-up chip).
- Heartbeat follows it: accel/gyro lost = state 3, mask bit 0 clear; magnetometer lost = state 2, bit 1 clear.
- While accel/gyro are invalid the AHRS is not updated (no more integrating stale gyro data) and the last attitude is held.
  After more than 1 s without data the filter restarts (instant re-lock) and the turn rate skips the jump.
- While invalid, the chip is re-initialised (begin + settings, read back) every 2 s.
- Magnetometer missing at start-up is no longer fatal (state 2, 6-axis AHRS). With the magnetometer lost, the filter is given
  its own predicted field (its Earth-field vector rotated by its current orientation), so roll/pitch follow gravity and heading
  follows the gyro. A first version used a field learned from the magnetometer; its small mismatch with the filter's own made
  the heading drift faster and faster (0.15 to 1 deg/s in 30 s). Using the filter's own vector gives a steady 0.06 to 0.1 deg/s.
- `?` report: health, times lost, re-initialisation tries and last result, I2C read failure count, prediction error.
- Test commands over USB serial (`FAULT_INJECT 1`, needs ModuleInfo 1.3.0 command hook): `!G`/`!g` accel/gyro reads fail
  on/off, `!M`/`!m` magnetometer reads fail on/off, `!z` reset the ISM330DHCX chip, `!x` reset the LIS3MDL chip.
  They need the `!` because an early version lost its state to stray characters: the Pi tty echoed the module's own report
  back to it (echo was on until my `stty -echo`), and the report contains a "g".

Fault-injection results (I2, I3 equivalents):
- `!G` (accel/gyro reads fail) 3 s: state 3, mask 06 within 0.3 s, attitude held, state 1 mask 07 about 0.6 s after clearing.
- `!z` (ISM330DHCX reset, like a power loss; outputs zeros): state 3 mask 06 after about 1.6 s, attitude held, chip
  re-initialised by itself after about 2 s, back to state 1 mask 07; attitude continues (yaw 6.4 to 5.7 on re-lock).
- `!x` (LIS3MDL reset; stops updating): detected as stuck, state 2 mask 05 after 1.3 s, running 6-axis, re-initialised, state 1.
- `!M` (mag reads fail) 4 times: state 2 mask 05 for the injected time, state 1 mask 07 within about 1 s after.
- 6-axis with the magnetometer off, module moved by hand: turned 90 degrees (yaw 358 to 272), nose up 25 degrees (pitch 27),
  and back; roll/pitch correct, yaw drift about 0.1 deg/s afterwards; when the magnetometer came back heading eased to the
  correct value over about 5 s without a jump.

- **I7 repeat, IMU wing pulled live (IMU_AHRS 1.0.11):** detected about 1 s after the pull: heartbeat state 3, mask 04 (accel/gyro
  and magnetometer bits clear). 0x48 / 0x49 stopped changing (attitude held at the last value instead of drifting, which was
  the 1.0.4 failure). About 15 s later, when the wing was pushed back in, **the module rebooted** (uptime back to 2 s, USB
  re-enumerated, came up normally with state 1 mask 07). So the live re-insertion itself resets the Feather (supply glitch or
  contact bounce on the stacked pins); the re-initialisation path was therefore exercised only with `!z` / `!x`, not by a real
  re-insertion.
- (Done above:) act on getEvent() failure, detect the all-zero accel, mark gyro invalid, re-initialise the chip (begin and
  configure) about every 2 s while it is missing, and do not let the AHRS integrate stale gyro data.
