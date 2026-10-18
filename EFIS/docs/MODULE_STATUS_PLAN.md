# Plan: show module failures on the EFIS and the backup display

Written 2026-10-17. Follows `Sensors/FAILURE_DETECTION_PLAN.md` (the modules now detect their own failures and say so in
a CAN heartbeat and the LED). This plan makes the **EFIS** and the **backup display** act on that. **All ten decisions are
made (see "Decisions recorded" at the end); nothing is built yet. Start at step 1 of "Order of work".**

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
4. The server tracks the facts once (module states, which sensors work, whether data is arriving) and sends them; the web page
   decides what to show and how (decision 11). The backup display makes its own simpler decisions from the same facts on the bus.
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
| OAT lost | 0x2A | OAT (the frame keeps being sent, decision 6) |
| Accel/gyro lost | 0x48, 0x49 | yaw, pitch, roll, turn rate; all three accelerations |
| Magnetometer lost | none | heading is still sent (gyro-only); the EFIS handles it from the heartbeat mask (decision 1) |
| GPS silent / no fix | 0x63, 0x64 | none: the frames stop (decision 7), the EFIS times them out after 3.5 s; 0x65 and the heartbeat say why |

- EFIS heartbeat **0x700**, once a second, same layout as the module heartbeats (module 0 = EFIS, version, state, sensors
  mask, uptime). Mask bits = which modules the EFIS currently sees healthy (bit 0 AIR, bit 1 IMU, bit 2 GPS) and bit 3 =
  a display client is connected. State 1 all modules OK, 2 a module degraded or missing, 3 EFIS cannot produce a display.
  Purpose: the backup display (and a future bus monitor) can tell "Pi alive, but a module is missing" from "Pi dead".

### 2. EFIS server (`aio_server.py`, `module_status.py`): tracks the facts, never decides what to show

Division of work (David, 2026-10-17): **the server tracks the data and sends facts; the web page decides what things look like
and how they are drawn** (wording, colours, order, flashing, how long a caution stays up, when the gyro heading is blanked).
The server needs the module state anyway for the 0x700 heartbeat, and it keeps its state when the browser reloads.

1. **Heartbeat handlers** for 0x701 to 0x703 fill a tracker (`module_status.StatusTracker`): version, state, sensors mask, uptime
   and last-heard time per module. A module not heard for 3 s is `missing`; one never heard is `missing` only after a 5 s
   start-up grace. An uptime that goes backwards records a restart time.
2. **Timeouts for every message** (replaces the AHRS-only `monitor_timeout`, which also spun without sleeping, ISSUES.md #2):

| Message | Rate | Lost after |
|---|---|---|
| 0x28 airspeed / altitude / VSI | 10 Hz | 0.5 s |
| 0x48, 0x49 attitude, acceleration | 10 Hz | 0.5 s (was 0.2 s; decision 8) |
| 0x2A OAT, 0x2B raw pressure | 5 Hz | 1.5 s |
| 0x63 to 0x65 GPS | 1 Hz | 3.5 s |
| 0x701 to 0x703 heartbeats | 1 Hz | 3 s (module missing) |

3. **Facts in the JSON** (added to the existing fields, which keep their names; evaluated every 0.1 s):

| Key | Content |
|---|---|
| `modules.{imu,air,gps}` | `present`, `missing`, `state` (0 to 3, null if not present), `version` ("1.0.15"), `uptime` (s), `age` (s since the last heartbeat), `restarted_age` (s since an uptime drop, or null), `sensors` (named booleans from the mask, or null if unknown) |
| `modules.imu.sensors` | `gyro_accel`, `magnetometer`, `calibration` |
| `modules.air.sensors` | `pitot`, `aoa`, `static`, `oat` |
| `modules.gps.sensors` | `nmea`, `fix` |
| `valid` | true/false for `airspeed`, `altitude`, `vsi`, `static_pressure`, `differential_pressure`, `oat`, `attitude`, `heading`, `turn_rate`, `acceleration`, `gps_position` |
| `data_lost` | true when a message group has stopped arriving (after the start-up grace): `air_data`, `raw_pressure`, `oat`, `attitude`, `acceleration`, `gps_position`, `gps_status` |
| `heading_status` | `valid`, `suspect` (the heading is gyro-only because the magnetometer is lost), `gyro_seconds` (how long, or null) |
| `system_state` | 1 when all three modules are present and OK, else 2 (also used for the 0x700 heartbeat) |

   `valid` is true only if the message is recent **and** the module's sensors mask says the sensor works. It is a fact about the
   data, not a style choice.
4. **Values**: attitude and acceleration are set to `null` when invalid (as the old monitor did for attitude). Airspeed, altitude,
   VSI, OAT, turn rate and GPS are only set to `null` when `NULL_INVALID_VALUES = True` in the server, which stays `False` until
   the display copes with `null` (display step A); until then they keep their last value and `valid` says they cannot be trusted.
5. **Heartbeat 0x700** from a new asyncio task (1 Hz), so it stops if the server stops (server step B).
6. Markers in the data messages are decoded into `null` and `valid = false` (server step B).

### 3. EFIS display (`efis-display.js`, `support/*.mjs`): decides and draws

The page receives the facts above and owns every presentation choice.

1. A reusable **failure flag** (the red X with its label) drawn over an instrument when its `valid` flag is false; the instrument
   stops updating and hides its numbers, except the gyro-only heading, which stays visible and dimmed under the X until the page
   decides to blank it (5 minutes, decision 1). Instruments: attitude indicator (also hides sky/ground), airspeed tape,
   altimeter, VSI, heading ribbon/HSI, turn rate, OAT readout, GPS readouts. Look as in the approved mock-up.
2. An **annunciator strip** at top centre, built by the page from the facts. Rules (the page may refine them):

| Text | Level | Shown when |
|---|---|---|
| NO AIR DATA | warning (red) | `modules.air.missing` or `data_lost.air_data` |
| PITOT | warning | AIR present and `sensors.pitot` is false |
| STATIC | warning | AIR present and `sensors.static` is false |
| OAT | caution (amber) | AIR present and (`sensors.oat` false or `data_lost.oat`) |
| AOA | caution | AIR present and `sensors.aoa` false |
| NO IMU | warning | `modules.imu.missing` or `data_lost.attitude` |
| ATT FAIL | warning | IMU present and `sensors.gyro_accel` false |
| HDG GYRO | caution | `heading_status.suspect` |
| IMU NO CAL | caution | IMU present and `sensors.calibration` false |
| GPS NO DATA | caution | `modules.gps.missing` or `data_lost.gps_status` or `sensors.nmea` false |
| MODULE RESTARTED | caution | any module `restarted_age` below 8 s |

   Warnings first, then cautions; a new entry flashes for 5 s then stays steady (decision 4); the page times the flash and
   the restart caution itself. "No fix" is information only, not an annunciation.
3. **Module status page** from the Menu (decision 5): the `modules` facts as a table.
4. Replace the start-up defaults of 0 with "no data" (red X) until the first valid message arrives.
5. Guard every `dataObject.x` use against `null`, and treat a stalled websocket (no message for about 1 s) as "everything invalid"
   with its own message, because the server cannot report its own absence.
- Development without hardware: `efis-test.js` / a fake websocket feed with each failure; then the real modules with
  their fault-injection commands. The mock-up `docs/mockups/annunciator_mockup.html` holds the same rules in `evaluate()`.

### 4. Backup display (`CAN_Display/CAN_Feather`, `Display_Feather`)
1. `CAN_Feather`: treat a marker as invalid; per-field validity instead of one bit per message (airspeed can fail while altitude
   is fine, since both are in 0x28): extend `validMask` bits in `DisplayProtocol.h` (bump to 1.1.0, still one shared copy).
2. Also listen to 0x700 to 0x703; the same 3 s "missing" rule.
3. `Display_Feather`: a one-line status row (for example `I A G E` with OK / amber / fail marks, or the shortest text that
   fits 128x64) so a missing module or a dead EFIS is visible on the backup display itself.
- The two Feathers must be reflashed together (shared protocol header).

### 5. Modules: replace interim behaviour with markers (IMU_AHRS, AIR_Module, GPS_Module)
- Only after the server and backup display understand markers (see order below), because an old EFIS would draw 32767.
- AIR: pitot lost, airspeed 0 becomes the marker; static lost, altitude held becomes the marker; OAT lost, the frame stops
  becomes the marker (decision 6). IMU: accel/gyro lost sends markers. GPS: unchanged, frames stop (decision 7).

## Order of work (each step testable on its own)

0. **Decisions**: done (below). Mock-up of the annunciator strip and red-X look: done and approved by David ("generally looks good"),
   `EFIS/docs/mockups/annunciator_mockup.html`. Defaults to build: X stroke 7 px at 80% opacity; compact X (centre) over the heading
   ribbon; annunciator text 24 px with a dark backing pill; red = PITOT, STATIC, NO AIR DATA, NO IMU, ATT FAIL; amber = OAT,
   HDG GYRO, GPS NO DATA, MODULE RESTARTED (and AOA, IMU NO CAL); flash 5 s then steady.
1. **Protocol** (YAML + docs): markers, 0x700. No code. **DONE 2026-10-17** (protocol 0.2.0): `invalid:` markers on the fields in the
   table above, `timeout_ms` on every message the EFIS watches (0x2B added at 1.5 s, same as OAT), notes with the interim behaviour,
   the heartbeat notes brought up to date, and the new planned message 0x700 EFIS_HEARTBEAT. The generator (1.1.0) adds an
   "Invalid marker" column, a "Treated as lost after" row, and checks each marker fits its type. All magnitudes checked: no real value
   reaches 0x7FFF (largest real values: heading 3600, static pressure 11000, differential pressure under 7000, accel about 4000).
2. **Server step A** (built 2026-10-17, not yet deployed to the live EFIS): `module_status.py` + changes in `aio_server.py`:
   heartbeat handlers, module table, timeouts for every message, `valid` / `data_lost` / `heading_status` facts in the JSON, sleep
   fix. No display text, no markers yet. Tests: 28 unit tests of the facts (`EFIS/tests/test_module_status.py`, run anywhere) and 7
   that run the real handlers and receive loop with a fake CAN reader (`test_server_glue.py`, run on the Pi with
   `~/EFIS/venv/bin/python -m unittest discover -s tests`); `tests/live_check.py` listens read-only to the real bus. Still to do:
   fault-inject the real modules while it listens, then deploy with `copytopiefis.sh` and restart `piefis`.
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

## Decisions that were needed (answered, see "Decisions recorded" at the end)

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

## Decisions recorded (David)

- **1. Heading with the magnetometer lost (2026-10-17).** The gyro-only heading stays on the display for 5 minutes (about 30 degrees of
  drift at 0.1 deg/s) under a red X, with the number dimmed but still readable. After 5 minutes the number is blanked and only
  the red X remains. The server counts the 5 minutes from the first heartbeat that reports the magnetometer lost
  (IMU sensors mask bit 1 clear) and resets it when the magnetometer comes back. The annunciator still shows `HDG GYRO`.
  Reason David gave: a red X that does not hide the data underneath, so the pilot can still cross-check.
- **2. Failure look (2026-10-17, implied by decision 1).** A red X, drawn with thick semi-transparent strokes, over the instrument. Where a
  value exists but is untrusted (only the gyro-only heading today) the value stays visible and dimmed under the X. Where there
  is no value at all (a marker, a lost module, a timeout) the instrument is blank under the X. To confirm with David.
- **3. Attitude failure (2026-10-17).** The sky and ground are removed and a large red X is shown. No frozen horizon is ever drawn.
- **4. Annunciators (2026-10-17).** Shown at top centre, just under the heading ribbon: red warnings first, amber cautions below,
  2 to 3 short lines. A new caution or warning flashes for about 5 seconds, then stays steady.
- **5. Module status page (2026-10-17).** A read-only page opened from the existing Menu (Menu, then Modules): IMU, AIR, GPS and the
  EFIS with version, state, which sensors are up, uptime and last heard. Later a natural home for the CAN commands (ISSUES #9).
- **6. OAT when lost (2026-10-17).** The AIR module keeps sending 0x2A with the 0x7FFF marker, so the EFIS can tell "OAT sensor failed"
  (show --- and an OAT caution) from "AIR module silent" (timeout).
- **7. GPS when silent or no fix (2026-10-17).** The position frames (0x63, 0x64) stay stopped as today; the EFIS times them out
  after 3.5 s. 0x65 and the GPS heartbeat say why (no NMEA, no fix, satellites). No markers on the GPS messages.
- **8. Timeouts (2026-10-17).** 0.5 s for attitude (0x48, 0x49), airspeed/altitude/VSI (0x28); 1.5 s for OAT (0x2A); 3.5 s for GPS
  (0x63 to 0x65); 3 s for a module heartbeat (module missing). Attitude changes from 0.2 s to 0.5 s.
- **9. Restart caution (2026-10-17).** Yes: a `MODULE RESTARTED` caution for a few seconds when a module's uptime goes backwards
  (it will also appear after a deliberate firmware flash on the ground).
- **10. EFIS heartbeat 0x700 (2026-10-17).** Same layout as the module heartbeats (module 0), with the mask bits: 0 AIR, 1 IMU, 2 GPS seen
  healthy by the EFIS, 3 a display client connected. State 1 all modules OK, 2 a module degraded or missing, 3 the EFIS cannot
  produce a display.
- **11. Division of work (2026-10-17).** The server tracks the data and sends facts (module states, named sensor flags, `valid`,
  `data_lost`, durations). The web page decides every presentation choice: wording, colours, order, flashing, how long a caution
  stays up, when the gyro-only heading is blanked, and the layout. The server does not produce display text.

## Test log

### 2026-10-18: server step A against the real modules (fault injection, `EFIS/tests/fault_run.py`)

The real server code (`module_status.py`, `aio_server.py` 1.1.0) listened read-only on can0 on the Pi while each module fault was
injected over USB (IMU_AHRS 1.0.15, AIR_Module 1.0.34, GPS_Module 1.0.8). What the EFIS side concluded:

| Fault injected | Facts the EFIS side showed | Delay | Cleared |
|---|---|---|---|
| AIR pitot lost (`!P`) | air state 2; `sensors.pitot` false; invalid: airspeed, differential_pressure | under 1 s | within 1 s of `!p` |
| AIR static lost (`!S`) | air state 2; `sensors.static` false; invalid: altitude, vsi, static_pressure | under 1 s | within 1 s |
| AIR AOA lost (`!A`) | air state 2; `sensors.aoa` false; nothing invalid | under 1 s | within 1 s |
| AIR OAT lost (`!O`) | air state 2; `sensors.oat` false; invalid: oat; `data_lost.oat` true for a few seconds (the AIR module stops sending 0x2A today; with the marker it will keep sending) | about 2 s | about 1 to 2 s |
| IMU magnetometer lost (`!M`) | imu state 2; `sensors.magnetometer` false; nothing invalid; `heading_status.suspect` true with `gyro_seconds` counting | under 1 s | within 1 s |
| IMU accel/gyro lost (`!G`) | imu state 3; `sensors.gyro_accel` false; invalid: attitude, heading, turn_rate, acceleration | under 1 s | within 1 s |
| GPS silenced (`!N`) | gps state 2; `sensors.nmea` and `fix` false; invalid: gps_position | about 4.4 s (the module's own 5 s rule) | about 6 s after `!n` (the module finishes its baud probe first) |

No fault produced a false `invalid` on an unrelated instrument, and every module returned to state 1 by itself.

Finding for the display rules: while the GPS is silent, `data_lost.gps_status` flaps true/false about every 7 s, because the GPS
module only sends 0x65 between its baud probes. `sensors.nmea` is false the whole time, so the rule "GPS NO DATA when `sensors.nmea`
is false or the GPS is missing" is stable; the page should not use `data_lost.gps_status` alone for that caution.

### 2026-10-18: server step A deployed to the live EFIS

Copied `aio_server.py` (1.1.0) and `module_status.py` to `~/EFIS` on the Pi; the previous `aio_server.py` and `flightplan.py` are saved in
`~/EFIS/backup_20261018/` (to roll back: copy them back and `sudo systemctl restart piefis`). The Pi's old `aio_server.py` was
identical to the git version before the change. `sudo systemctl restart piefis`: service active, no errors in the log, the browser
reconnected by itself. CPU use of the server: about 6% of one core (was about 50%; ISSUES #2). The server's single websocket belongs to
the display, so do not connect a test client to the live server; use `tests/live_check.py` (CAN only) instead.
