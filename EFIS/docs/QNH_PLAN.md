# Plan: share the QNH (altimeter setting) between all devices on the CAN bus

Written 2026-10-20. Status 2026-10-20: steps 1 to 3 are written, committed, AIR_Module 1.0.36 is flashed and aio_server 1.7.0 with
qnh_state.py and the page 0.8.0 are deployed to the Pi (old files in ~/EFIS/backup_20261020_qnh). Bench-tested on the real bus: block IDs
0x2C/0x2D/0x2F adopted, out-of-range / hPa-mismatch / wrong-ID frames ignored, boot sends the altitude marker for 2 s then 29.92 flagged
not set, the next frame clears the flag, 0x2B bytes 5-7 report the value, the server follows 0x2C/0x2D and keeps alive on 0x2E.
Not yet done: the page itself on the Pi screen (needs a reload), a live mismatch, the jitter fix (step 4) and the backup display
(step 5). Decisions at the end are marked open or decided.

## Goal

Today the Pi EFIS is the only device that can set the QNH, and the AIR module is the only device that uses it. The backup display
(CAN_Display, with a rotary encoder) needs to set and show it too, and it must keep working when the Pi is dead. So:

- Any device with a QNH control can change the QNH. Every device that shows or uses it follows the change.
- Every device holds the value, so no single device failing loses it.
- The value is only ever changed by a person. No device invents one. A device that starts with nothing to adopt uses 29.92 and
  shows it as **not set** until a person sets it or a value arrives from the bus. Nothing is stored in non-volatile memory (decided
  2026-10-20): a single restart recovers from the other devices, and after a total power loss everything starts at the flagged 29.92.
- The same job fixes the send jitter that never worked (last section).

## What exists (checked 2026-10-20)

- **CAN 0x2E** (`Protocol/can_messages.yaml`): hPa int16 in bytes 0-1 (informational), inHg x 400 int16 in bytes 2-3, bytes 4-7 zero.
  Sender is the EFIS only. The AIR module is the only receiver.
- **Watched on the real bus** (2026-10-20, `candump -ta can0,02E:7FF`, 99 s, 151 frames): one source only; 1 Hz keep-alive with
  an occasional 2 s gap (it is driven by the page's own timer); every change is a frame at once, up to about 20 frames a second
  while the value is dragged (steps of 0.01 to 0.09 inHg); the AIR module sends no 0x2E; bytes 4-7 always 0.
- **AIR_Module.ino**: `qnh` starts at 2992 on every boot, is never stored, accepts only 0x2E (`can.filter(CAN_QNH_MSG_ID, 0x7FF)`),
  uses it for the altitude (`pow(qnh/2992, 0.190263)`), takes `qnhx4 / 4`, ignores the hPa field, no range check.
- **aio_server.py**: the page sends `{qnh, ticker, brightness}` over the websocket about once a second and on change.
  `process_qnh` clamps to 2800..3100 and sends 0x2E when the value changed or 1 s has passed. The server already decodes
  incoming 0x2E (`process_qnh_message`, sets `data.can_qnh` and `can_qnh_hpa`) but nothing uses them. `controller_state` (qnh,
  brightness) is the last value the controlling page sent; viewers show it. It is in memory only, so a server restart loses it.
- **efis-display.js / qnhdisplay.mjs**: the controller page adopts `controller_state.qnh` once, after a (re)load
  (`restoredFromServer`). After that it only sends. It sends every second (`CAN_QNH_PERIOD`) whether or not the value changed.
- Used CAN IDs near 0x2E: 0x28, 0x29, 0x2A, 0x2B, 0x2E, 0x32, 0x38. **0x2C, 0x2D and 0x2F are unused.**
- The page code is plain JavaScript (`.js` and `.mjs`), not TypeScript.

## Design

### CAN messages

- **ID block 0x2C to 0x2F.** One device, one ID; no two devices ever send the same ID.
  - 0x2E: Pi EFIS (unchanged, so an unchanged AIR module and any tool still work)
  - 0x2D: backup display
  - 0x2C, 0x2F: spare for a future panel
  - Receivers use a single hardware filter: ID 0x2C, mask 0x7FC.
- **Payload, the same in all four IDs and unchanged from today:** bytes 0-1 hPa (int16, little-endian), bytes 2-3 inHg x 400
  (int16, little-endian), bytes 4-7 zero. DLC 8.
- **Receiver checks:** the inHg value must be 2800..3100 (28.00 to 31.00 inHg after dividing x400 by 4). The hPa field must agree
  with the inHg field to within 1 hPa of rounding (hPa = inHg x 100 / 2.95299875). A frame that fails is ignored.

### The rules (no owner, no software arbitration; CAN orders the frames)

1. **Change.** When a person changes the QNH on a device, that device sends the new value at once on its own ID. Every change is
   sent as it happens (no rate limit), so the others follow the dial live. A device does not send more often than every 20 ms
   (at most 50 frames a second), always sending the latest value, as a guard against a bouncing encoder.
2. **Hear.** When a device hears a valid QNH frame from any ID, it adopts that value. The last frame heard wins. CAN arbitration only
   decides which of two simultaneous frames goes first; both are delivered, in the same order, to every device.
3. **Speak on silence.** A device that holds a confirmed value and has heard no QNH frame for 1000 ms +/- 20 ms (drawn once for each
   silent period) sends its held value on its own ID. Every device hears it, which resets all their silence timers. So the
   keep-alive normally comes from one device a second, and if that device dies another takes over by itself. If two timers expire at
   the same moment, both frames go out in ID order with the same value. If two devices ever held different values, the next
   keep-alive makes the others adopt it, so it heals itself.
4. **Never invent.** A device sends only a value that a person entered, or one it adopted from the bus. The 29.92 a device starts
   with ("not set") is never sent.
5. **Silence is not a fault.** A quiet bus means nobody has been touched and nobody's keep-alive is running. The held value is still
   good. The displays show no failure for it.
6. **The AIR module reports the QNH it is really using** (see "AIR report" below), because the altitude is computed from it
   (`AIR_Module.ino`, `pow(qnh/2992, 0.190263)`: about 10 ft per 0.01 inHg, about 1000 ft per inHg). A display that holds a
   confirmed value which differs from the AIR module's reported value for more than 2 s flags a baro mismatch (look decided with the
   display designs). This also catches a lost frame, an AIR restart, and a module on an old value.

### AIR report

The heartbeat (0x702) has all 8 bytes used, and 0x28 is full. **0x2B has 3 spare bytes** (5-7, "always 0", unpacked by the server as
padding, so an unchanged server and the legacy format are not affected):

- bytes 5-6: QNH in use, int16, inHg x 100 (2992 is 29.92), invalid marker 0x7FFF = not valid
- byte 7: flags, bit 0 = not set (it started at 29.92 and has heard no frame from the bus yet); other bits 0

Sent at the 0x2B rate (200 ms). The AIR module still sends no frame in the 0x2C to 0x2F block (rule 4).

### Power-up (no non-volatile storage)

Decided 2026-10-20: nothing is stored on any device. The Feathers have no EEPROM (the IMU module's external 24LC32 is not on the
AIR module or the backup display) and flash emulation would cost a write stall, wear, bench testing and more ways to disagree. A
stored value from yesterday also looks plausible and could be mistaken for today's.

1. Listen for 2 s. Any valid frame: adopt it. The device is **set**. This covers a restart of one device while the others run.
2. Nothing heard: the value is 29.92 and the state is **not set**. The display flags it (flashing or `?`, exact look decided in the
   backup display design and the page). The altitude is computed with it, so it reads pressure altitude, flagged. It is not sent.
3. The first change by a person in QNH mode makes the device set and starts sending. A frame heard from the bus also makes it set
   and replaces the value.
4. After a total power loss everything starts at 29.92, not set, and a person sets it once; the first device set tells all the others.
5. The Pi server keeps the value in memory only, so a page reload gets it back from the server, and a server restart gets it from
   the bus (step 1) or starts at 29.92, not set.

### Encoder behaviour (for the backup display, and the page's own encoder)

A turn changes the QNH only while the encoder is in its QNH mode. Other modes (page, backlight and so on) never send a QNH frame.
The mode falls back to the default after a period with no input. The details belong to the backup display design.

## Changes by component

### 1. Protocol (`Protocol/can_messages.yaml`, then regenerate the docs)

- Replace the single 0x2E entry with the block 0x2C to 0x2F: senders (efis 0x2E, backup 0x2D, spares), receivers (air, efis, backup),
  the payload, the rules above and the checks. Note that the old "x100" doc text is stale (it is x400).
- 0x2B: replace the 3 padding bytes with the QNH in use (bytes 5-6) and the flags (byte 7).
- Update `Protocol/CAN_PROTOCOL.md` and `CAN Reference/Can Docs.md` through the existing generator.
- Describe the jitter correctly (last section).

### 2. AIR module (`Sensors/AIR_Module/AIR_Module.ino`, patch version up one)

- Filter: `can.filter(0x2C, 0x7FC)` instead of `(0x2E, 0x7FF)`. Check the patched `Adafruit_MCP2515` filter call handles a mask other than 0x7FF.
- Accept a frame from any ID in the block. Apply the receiver checks. Adopt the value.
- Boot: `qnh` = 2992, flagged not set (reported in 0x2B byte 7 bit 0) until a valid frame is heard. Nothing is stored.
- It does not send in the QNH block (it has no person-operated control), so rules 3 and 4 do not apply to it.
- Report the QNH in use in 0x2B bytes 5-7 (see "AIR report").
- Send the altitude invalid marker on 0x28 until the 2 s listen window ends (see Decisions).
- Recovery: a rebooted AIR module hears the next keep-alive from any set device within about 1 s, adopts it and clears not set.
- Keep the existing "discard unwanted packet" read loop.
- Heartbeat and LED unchanged. Add the adopted value and its source ID to the DEBUG serial output.

### 3. Server (`EFIS/aio_server.py`, minor version up)

- Dispatch the whole block 0x2C to 0x2F to `process_qnh_message`.
- Add a small QNH state object (value, confirmed flag, time of last frame heard, current silence delay) in a new file
  `EFIS/qnh_state.py` so it can be unit tested without CAN or a websocket.
  - `on_can(id, hpa, inhg_x4, now)`: checks, adopt, set `controller_state["qnh"]`, mark set, reset the silence timer.
  - `on_user(inhg_x100, now)`: from the controller page. Counts as a user change **only if it differs from the previous value the
    page reported** (not from the server's state), so an old page that keeps repeating a stale value cannot override a bus value.
    Clamp to 2800..3100, adopt, send 0x2E at once, mark set.
  - `tick(now)`: if set and silent for the drawn delay, send the held value on 0x2E and draw a new delay.
  - Nothing is saved to disk.
  - Read the AIR module's reported QNH (0x2B bytes 5-7), compare it with the held value, and send `qnh_mismatch` in the JSON.
- Add `qnh_state.py` to `copytopiefis.sh`.
- The websocket JSON gets `qnh_set` (false while not set) next to `controller_state`.
- `process_qnh` as it is today (send at least once a second, whether or not changed) is replaced by the object above. The send
  failure reporting (`RepeatedErrorReporter`) stays.
- Listen first for 2 s at start before settling on 29.92, not set.

### 4. Page (`EFIS/support/efis-display.js`, `qnhdisplay.mjs`)

- Controller: adopt `controller_state.qnh` continuously (not only once), unless the QNH is currently selected and being changed by
  the encoder. Set `last_qnh` when adopting, so an adopted value is not echoed back as a user change.
- Stop the once-a-second QNH send. Send `qnh` when it changes; brightness keeps its own logic.
- Show the not-set state from `qnh_set` (flashing or a mark), cleared by the first turn in QNH mode or a value from the bus.
- Show the baro mismatch from `qnh_mismatch` (look to be decided).
- Viewers already show `controller_state.qnh`; they get the continuous updates for free.
- Page version up one.

### 5. Backup display

Separate design (the next job). It uses this protocol: listen to the block, adopt, send on its own ID 0x2D on a change, keep-alive on silence.

## Order of work (each step testable alone, and compatible with the one before it)

1. **Protocol and docs.** No code. Review.
2. **AIR module.** An unchanged EFIS keeps sending 0x2E and everything works as now. Test on the bench: `cansend can0 02D#...` and `02C#...` frames change the altitude; out-of-range and mismatched frames are ignored; after a power cycle or a hard reset it starts at 29.92 flagged not set and adopts the next frame heard; 0x2B bytes 5-7 report the value and the flag; the EFIS 0x2E still works.
3. **Server and page together** (they must deploy together). Unit tests for `qnh_state.py` (every rule, including the race, the silence takeover, and an old page repeating a stale value). Then the real Pi: change on the page and watch `candump`; inject 0x2D frames and watch the page follow; stop the sender and watch another take over (a second test node or `cansend` in a loop); restart the server while the AIR module runs and check it adopts the bus value; with nothing on the bus check it starts at 29.92 flagged not set.
4. **Jitter fix** (separate step, see below).
5. **Backup display** uses the finished protocol.

Rollback for 2 and 3: keep the old files as for earlier steps (`~/EFIS/backup_<date>/` on the Pi, previous .uf2 or source for the AIR module).

## Tests to run before calling it done

- Two senders (the page and `cansend` standing in for the backup display): the last change wins on every device; the other side stops keeping alive.
- The sender stops: another holder takes over within about 1 s; nothing changes on the displays.
- Out of range (27.99, 31.01), mismatched hPa and inHg, short frames: all ignored, no change anywhere.
- A device restarted while the others keep running: adopts the bus value within 2 s, no flag.
- Everything restarted at once: all start at 29.92 flagged not set, none send until a person turns the dial, then all follow that device.
- Old EFIS (0x2E only) with the new AIR module: still works.

## Jitter fix (its own step, after the QNH work)

**Found 2026-10-20.** Every periodic message uses `if (millis() - lastSent > PERIOD + random(50))`. `random(50)` is drawn again on
every pass of `loop()`, so the frame goes out on the first pass where the draw is small, about 1 ms after the period. Measured on the
live bus (15 s): 0x28 mean 102.6 ms (nominal 100), min 100.8; 0x48 mean 102.6, spread 5.6 ms; 0x2B mean 203.2 (nominal 200). So the
jitter is about 1 to 3 ms, not 0 to 49 ms. The few long gaps (129 ms on 0x28, 270 ms on 0x2B) are the AIR loop being busy, not jitter.

**Fix (decided with David):** a flat +/- 20 ms on every message, in milliseconds, not a percentage, drawn once per send.
`interval = (PERIOD - 20) + random(41)`; send when `millis() - lastSent >= interval`; draw the next interval after sending. The mean stays at the nominal period.

| Messages | Period | Interval |
|---|---|---|
| 0x28, 0x48, 0x49 | 100 ms | 80 to 120 ms |
| 0x29, 0x2A, 0x2B | 200 ms | 180 to 220 ms |
| GPS 0x63, 0x64, 0x65, time sync | 1000 ms | 980 to 1020 ms |

- One small helper in the shared library folder (`Sensors/Libraries/`, with the usual symlinks), used by AIR_Module, IMU_AHRS and GPS_Module, so the numbers are in one place. The same helper draws the QNH silence delay.
- Check before flashing that nothing downstream assumes exactly 10 Hz (VSI and turn rate are computed inside the modules, to be confirmed).
- Each module is re-flashed and re-tested (`Sensors/TestTools/`), the bus intervals are measured again with `candump`, and `Protocol/CAN_PROTOCOL.md` and the yaml note (which now says the jitter "has little effect in practice") are corrected.
- CAN arbitration never drops a frame, so this jitter is not needed for collision handling on the periodic messages. It is kept because it is
  cheap and spreads the bus load, and the QNH silence timer needs the same spread.

## Decisions

Decided (2026-10-20, David):
- Any device may set the QNH; every device holds it; only a person changes it.
- One sender per CAN ID; a block of IDs, one per sender. CAN is the arbiter: the last frame heard wins, there is no software arbitration or priority rule.
- Every change is sent as it happens (no rate limit beyond a 20 ms guard), so others follow the dial live.
- A device keeps the value alive when it hears silence, so any holder takes over if the sender stops. Jitter on the silence timer keeps holders from sending together.
- Nothing is stored anywhere. A device with nothing to adopt starts at 29.92 flagged not set; a turn in QNH mode or a frame from the bus sets it.
- Jitter: flat +/- 20 ms about the nominal period, drawn once per send.

- ID block 0x2C to 0x2F, EFIS on 0x2E, backup display on 0x2D, 0x2C and 0x2F spare.
- The AIR module reports the QNH it is using, in 0x2B bytes 5-7, because its altitude is computed from it.
- The flat +/- 20 ms applies to every periodic message, GPS included.

- AIR module hold-off: during its 2 s listen window the AIR module sends the altitude invalid marker (0x7FFFFFFF) on 0x28, not an altitude from the unset 29.92. After the window it uses the adopted value, or 29.92 flagged not set.
- Looks: on the EFIS page a not-set QNH flashes at 1 Hz and a mismatch turns the QNH box amber. On the backup 20x4, `29.92?` is not set and `29.92!` is a mismatch (final form in the display design).
- GPS 0x19 with +/- 20 ms is safe: the server only builds a datetime from the fields and nothing uses the arrival time.

Open: none.
