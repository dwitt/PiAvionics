# Plan: show module failures on the EFIS and the backup display

Written 2026-10-17. Follows `Sensors/FAILURE_DETECTION_PLAN.md` (the modules now detect their own failures and say so in
a CAN heartbeat and the LED). This plan makes the **EFIS** and the **backup display** act on that. Nothing here is built
yet; it needs David's decisions (end of this file) before coding.

## What exists today (checked in the code)

**Sensor modules** (IMU_AHRS 1.0.15, AIR_Module 1.0.34, GPS_Module 1.0.8)
- Send heartbeats 0x701 IMU, 0x702 AIR, 0x703 GPS once a second: version, state (0 start, 1 OK, 2 degraded, 3 failed),
  sensors mask, uptime.
- When a sensor is invalid they keep sending data in an interim form, because the EFIS cannot read markers yet:
  airspeed 0 and differential pressure 0 (pitot lost); altitude held and vertical speed 0 (static lost); no 0x2A frame
  (OAT lost); attitude held (accel/gyro lost); no 0x63/0x64 position frames (GPS silent).

**EFIS server** (`EFIS/aio_server.py`)
- Has no handler for 0x701 to 0x703 (they are ignored).
- `monitor_timeout()` clears only the AHRS fields (0x48/0x49) after 0.2 s. Airspeed, altitude, vertical speed, OAT and the GPS
  fields never time out: if the AIR module dies the display keeps showing the last altitude and airspeed. (It also
  spins with `sleep(0)`, ISSUES.md #2.)
- Broadcasts one flat JSON object at 20 Hz; a missing value is `null`.
- Does not send a heartbeat (0x700).

**EFIS display** (`EFIS/support/efis-display.js`)
- No concept of an invalid value. Instruments are fed `dataObject.x` directly (`attitudeIndicator.pitch = dataObject.pitch`),
  so `null` goes straight into the drawing code. Start-up defaults are 0, so "no data yet" looks like real zeros.
- No failure flags, no annunciator area, no place to see module versions or states.

**Backup display** (`CAN_Display`, KS0108 128x64, reads the CAN bus itself, independent of the Pi)
- Shows airspeed, altitude, VSI, heading, pitch, roll, turn rate from 0x28 and 0x48 only.
- A value is shown as `---` if its message has not arrived for 1 s. It knows nothing about heartbeats or markers, so a
  marker value would be drawn as 32767.

## Principles

1. A value that is not trustworthy is never drawn as a number. It is replaced by a red X (EFIS) or `---` (backup display).
2. Three ways a value can be untrustworthy, and all three must lead to the same display: the module sends a marker; the
   module's heartbeat says the sensor is lost; the data stops arriving (timeout).
3. The module heartbeat tells the EFIS **why** (which sensor); the data timeout covers a module that cannot even say that
   (dead, CAN cable out, bus off).
4. The EFIS decides, once, in the server, what is valid and what to tell the pilot. The display only draws that. (One place
   to test.)
5. The backup display has its own, simpler copy of the same rules, because it must work without the Pi.

## Design

### 1. Protocol: invalid-data markers and the EFIS heartbeat (`Protocol/can_messages.yaml`)
- Marker = the maximum positive value of the field's type: `0x7FFF` for int16, `0x7FFFFFFF` for int32. Check that no real value
  can reach it (AHRS values are degrees x10 up to 3600; altitude and latitude/longitude are int32; GPS altitude is int16 feet,
  where 32767 ft is above this aircraft's ceiling).
- Add an `invalid:` entry to each field in the YAML and regenerate `CAN_PROTOCOL.md` / `CAN_BUS_MAP.md`
  (`Protocol/.venv/bin/python Protocol/generate_can_docs.py`).
- Which fields can carry a marker, by what fails:

| Failure | Message | Fields set to the marker |
|---|---|---|
| Pitot lost | 0x28, 0x2B | airspeed; differential pressure |
| Static lost | 0x28, 0x2B | altitude (int32), vertical speed; static pressure |
| OAT lost | 0x2A | OAT |
| Accel/gyro lost | 0x48, 0x49 | yaw, pitch, roll, turn rate; all three accelerations |
| Magnetometer lost | 0x48 | yaw only if David decides heading is invalid (see decision 1) |
| GPS silent / no fix | 0x63, 0x64 | position, speed, altitude, track (today the frames simply stop) |

- EFIS heartbeat **0x700**, once a second, same layout as the module heartbeats (module 0 = EFIS, version, state, sensors
  mask, uptime). Mask bits = which modules the EFIS currently sees healthy (bit 0 AIR, bit 1 IMU, bit 2 GPS) and bit 3 =
  a display client is connected. State 1 all modules OK, 2 a module degraded or missing, 3 EFIS cannot produce a display.
  Purpose: the backup display (and a future bus monitor) can tell "Pi alive, but a module is missing" from "Pi dead".

### 2. EFIS server (`aio_server.py`)
1. **Heartbeat handlers** for 0x701 to 0x703 fill `data.modules`: for each module its name, version, state, sensors mask,
   uptime and last-seen time. A module not heard for 3 s is `missing`; an uptime that goes backwards is flagged
   "restarted" (visible in the status page, and as a short caution).
2. **Validity** (`data.valid`, a dict of booleans per instrument: `airspeed`, `altitude`, `vsi`, `attitude`, `heading`,
   `turn_rate`, `oat`, `gps`). A value is valid only if its message is recent **and** is not a marker **and** the owning
   module's mask says the sensor is working. Invalid values are also set to `null`, so nothing can draw them by accident.
3. **Timeouts for every message**, in a table (replaces the AHRS-only `monitor_timeout`):

| Message | Rate | Invalid after |
|---|---|---|
| 0x28 airspeed / altitude / VSI | 10 Hz | 0.5 s |
| 0x48, 0x49 attitude, acceleration | 10 Hz | 0.5 s (was 0.2 s; to confirm with David, 0.2 s is only 2 missed frames) |
| 0x2A OAT | 5 Hz | 1.5 s |
| 0x63 to 0x65 GPS | 1 Hz | 3.5 s |
| 0x701 to 0x703 heartbeats | 1 Hz | 3 s (module missing) |

   The loop gets a real `await asyncio.sleep(0.05)` (fixes ISSUES.md #2).
4. **Annunciations**: the server builds a list in the JSON, each with a level (`caution` amber, `warning` red) and a short
   text, derived from the module states, masks and timeouts. Draft list: `NO AIR DATA` (AIR missing), `PITOT`, `STATIC`,
   `AOA`, `OAT`, `NO IMU` (IMU missing), `ATT FAIL` (accel/gyro), `HDG GYRO` (magnetometer lost, gyro-only heading),
   `GPS NO DATA`, `NO GPS FIX` (info only), `IMU NO CAL` (calibration missing), `MODULE RESTARTED`.
5. **Heartbeat 0x700** sent by a new asyncio task (1 Hz), so it stops if the server stops.
6. Decode markers in each existing message handler (value becomes `None`, validity false).

### 3. EFIS display (`efis-display.js`, `support/*.mjs`)
1. A small reusable **failure flag** (red X with a label such as `ASI`, `ALT`, `ATT`, `HDG`) drawn over an instrument when its
   `valid` flag is false; the instrument underneath stops updating and hides its numbers. Instruments covered: attitude
   indicator (also hide sky/ground), airspeed tape, altitude tape, VSI, heading/HSI rose, turn rate, OAT readout, GPS
   readouts.
2. An **annunciator strip** showing the server's list (amber / red text), always visible, not interactive.
3. **Module status page** on the menu: a table of IMU / AIR / GPS (and the EFIS itself): version, state, which sensors are
   up, uptime, last heard. This also serves the original wish to read module versions without a USB cable.
4. Replace the start-up defaults of 0 with "no data" (red X) until the first valid message arrives.
5. Guard every `dataObject.x` use against `null` (several places pass `null` into drawing code today).
- Development without hardware: `efis-test.js` / a fake websocket feed with each failure; then the real modules with
  their fault-injection commands.

### 4. Backup display (`CAN_Display/CAN_Feather`, `Display_Feather`)
1. `CAN_Feather`: treat a marker as invalid; per-field validity instead of one bit per message (airspeed can fail while altitude
   is fine, since both are in 0x28): extend `validMask` bits in `DisplayProtocol.h` (bump to 1.1.0, still one shared copy).
2. Also listen to 0x700 to 0x703; the same 3 s "missing" rule.
3. `Display_Feather`: a one-line status row (for example `I A G E` with OK / amber / fail marks, or the shortest text that
   fits 128x64) so a missing module or a dead EFIS is visible on the backup display itself.
- The two Feathers must be reflashed together (shared protocol header).

### 5. Modules: replace interim behaviour with markers (IMU_AHRS, AIR_Module, GPS_Module)
- Only after the server and backup display understand markers (see order below), because an old EFIS would draw 32767.
- AIR: pitot lost, airspeed 0 becomes marker; static lost, altitude held becomes marker; OAT lost, frame stops becomes marker
  (or keep stopping, decision 6). IMU: accel/gyro lost sends markers. GPS: see decision 7.

## Order of work (each step testable on its own)

0. **Decisions** below, and the display layout sketched on paper or as a mock-up.
1. **Protocol** (YAML + docs): markers, 0x700. No code.
2. **Server step A**: heartbeat handlers, module table, timeouts for every message, validity flags, annunciation list in the
   JSON, sleep fix. Markers not used yet. Test: unplug each module's CAN cable and use the modules' fault commands; read the
   JSON with a small websocket client; unit tests with fake frames.
3. **Display step A**: failure flags, annunciator strip, module status page, null guards, "no data" start-up. Test with the
   fake feed, then the Pi screen with the real modules.
4. **Server step B**: decode markers; EFIS heartbeat 0x700.
5. **Backup display**: markers, heartbeats, status row. Reflash both Feathers.
6. **Modules**: switch to markers (IMU, AIR, GPS), one at a time. Server first, then modules, so nothing ever shows 32767.
7. **System test table** (like the failure plan): every fault and the expected result on the EFIS and the backup display, with David
   watching, including stopping `piefis` (backup display must show EFIS missing) and pulling a module's CAN cable.
8. Update `Protocol/`, ISSUES.md #9 and #13, RESUME.md, memory.

Deploy server and display files with `EFIS/copytopiefis.sh` and `sudo systemctl restart piefis`; keep the previous version
available (git) so a bad deploy can be rolled back in a minute. Step 6 is the only step that changes the aircraft's data, and
each module can be rolled back by reflashing.

## Decisions needed from David

1. **Heading with the magnetometer lost.** The IMU keeps a gyro-only heading (drift about 0.1 deg/s). Show it with an amber
   `HDG GYRO` caution (suggested), or mark heading failed?
2. **Failure look.** Red X over the failed instrument with its name (suggested, like a G5), or just `---` where the number was?
3. **Attitude failure.** Hide the sky/ground and show a large red X (suggested), or leave the last picture with the flag?
4. **Annunciator strip position** and whether cautions should also flash for a few seconds when they first appear.
5. **Module status page** in the menu (suggested) or on a separate screen/URL only.
6. **OAT when lost:** send the marker (suggested, so "sensor failed" differs from "module silent") or stop the frame?
7. **GPS when silent / no fix:** keep the frames stopped and let the EFIS time out (simplest, suggested), or send markers?
8. **Timeouts** in the table above, especially the attitude timeout (0.2 s today; 0.5 s suggested).
9. **Restart caution.** Show a short `MODULE RESTARTED` message when a module's uptime resets (suggested for ground use, may be
   noise in flight)?
10. **EFIS heartbeat contents** as proposed (module mask + display connected), or something simpler?

## Risks and notes

- The server and display are the live instrument: change them in small steps with the previous version restorable. Do the
  server and display work before the modules switch to markers.
- The JSON shape changes (adds `modules`, `valid`, `annunciations`); existing keys keep their names, so the current display
  keeps working while the new one is built.
- The backup display has little room (128x64): the status row must stay tiny.
- ISSUES.md #9 steps 2 to 4 (commands over CAN: version query, zero, reset) are separate and can follow once the status page
  exists, since that page is a natural home for them.
