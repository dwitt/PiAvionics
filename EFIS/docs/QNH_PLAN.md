# Plan: share the QNH (altimeter setting) between all devices on the CAN bus

Written 2026-10-20. NOT built. Nothing in the code has changed. Decisions at the end are marked open or decided.

## Goal

Today the Pi EFIS is the only device that can set the QNH, and the AIR module is the only device that uses it. The backup display
(CAN_Display, with a rotary encoder) needs to set and show it too, and it must keep working when the Pi is dead. So:

- Any device with a QNH control can change the QNH. Every device that shows or uses it follows the change.
- Every device holds the value, so no single device failing loses it.
- The value is only ever changed by a person. No device invents one, and a restored value is shown as unconfirmed until a person
  confirms it.
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
4. **Never invent.** A device sends only a value that a person entered, or one it adopted from the bus. A restored (unconfirmed)
   value is never sent.
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
- byte 7: flags, bit 0 = restored from flash and not yet confirmed by a frame from the bus; other bits 0

Sent at the 0x2B rate (200 ms). The AIR module still sends no frame in the 0x2C to 0x2F block (rule 4).

### Power-up and storage

1. Listen for 2 s. Any valid frame: adopt it (that value is confirmed).
2. Nothing heard: use the stored last value, or 29.92 if none has ever been stored. This value is **unconfirmed**: the display
   flags it (flashing or `?`, exact look decided in the backup display design and the page). The altitude is computed with it. It
   is not sent.
3. The first change by a person in QNH mode confirms it and starts sending. A frame heard from the bus also confirms and replaces it.
4. Storage: every device stores whatever it holds (set or adopted), after the value has been steady for 2 s, and only when it
   differs from what is already stored. So all devices hold the same last value for the next cold start. Where:
   - **Pi server:** `/home/david/EFIS/qnh.json`, next to `display_orientation.json`, written the same way (temp file, fsync,
     `os.replace`, so a power cut leaves the old file or the new one). `copytopiefis.sh` copies only the files it lists and does not
     delete anything, so a deploy never touches it. The new `qnh_state.py` must be added to that script.
   - **AIR module and backup display (Feather RP2040):** the board has no EEPROM, but arduino-pico (core 6.3.0 installed) emulates
     one in the last 4 KB sector of the 8 MB flash, outside the program, so uploading a sketch does not erase it (to be verified on
     the bench). Use `EEPROM.begin(256)`, `EEPROM.put`/`get` of a small struct (value, inverse of the value as a check, a version
     byte), and `EEPROM.commit()` only when steady for 2 s and different. A bad check means "never stored": 2992.
     - Cost: `commit()` turns interrupts off and halts the other core while it erases and programs, a few tens of ms. At most one
       late 0x28 frame; the sensor health logic (20 bad samples) must not trip. To be measured on the bench before accepting.
     - Wear: about 100,000 erases; a settled change is rare, so this is not a concern.
     - Fallback if the stall is a problem: the external Adafruit 24LC32 I2C EEPROM (address 0x50) that the IMU_AHRS already uses
       for its calibration (`Adafruit_EEPROM_I2C`). No stall, but extra hardware on the board.

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
- Boot: restore `qnh` from flash (default 2992). Write it back after 2 s steady, only if it changed.
- It does not send in the QNH block (it has no person-operated control), so rules 3 and 4 do not apply to it.
- Report the QNH in use in 0x2B bytes 5-7 (see "AIR report").
- Keep the existing "discard unwanted packet" read loop.
- Heartbeat and LED unchanged. Add the stored/adopted value to the DEBUG serial output.

### 3. Server (`EFIS/aio_server.py`, minor version up)

- Dispatch the whole block 0x2C to 0x2F to `process_qnh_message`.
- Add a small QNH state object (value, confirmed flag, time of last frame heard, current silence delay) in a new file
  `EFIS/qnh_state.py` so it can be unit tested without CAN or a websocket.
  - `on_can(id, hpa, inhg_x4, now)`: checks, adopt, set `controller_state["qnh"]`, mark confirmed, reset the silence timer, schedule saving.
  - `on_user(inhg_x100, now)`: from the controller page. Counts as a user change **only if it differs from the previous value the
    page reported** (not from the server's state), so an old page that keeps repeating a stale value cannot override a bus value.
    Clamp to 2800..3100, adopt, send 0x2E at once, confirm, schedule saving.
  - `tick(now)`: if confirmed and silent for the drawn delay, send the held value on 0x2E and draw a new delay.
  - Persistence file (`/home/david/EFIS/qnh.json`, load at start as unconfirmed, save when steady 2 s and different).
  - Read the AIR module's reported QNH (0x2B bytes 5-7), compare it with the held value, and send `qnh_mismatch` in the JSON.
- Add `qnh_state.py` to `copytopiefis.sh`.
- The websocket JSON gets `qnh_confirmed` next to `controller_state`.
- `process_qnh` as it is today (send at least once a second, whether or not changed) is replaced by the object above. The send
  failure reporting (`RepeatedErrorReporter`) stays.
- Listen first for 2 s at start before using the stored value.

### 4. Page (`EFIS/support/efis-display.js`, `qnhdisplay.mjs`)

- Controller: adopt `controller_state.qnh` continuously (not only once), unless the QNH is currently selected and being changed by
  the encoder. Set `last_qnh` when adopting, so an adopted value is not echoed back as a user change.
- Stop the once-a-second QNH send. Send `qnh` when it changes; brightness keeps its own logic.
- Show the unconfirmed state from `qnh_confirmed` (flashing or a mark), cleared by the first turn in QNH mode.
- Show the baro mismatch from `qnh_mismatch` (look to be decided).
- Viewers already show `controller_state.qnh`; they get the continuous updates for free.
- Page version up one.

### 5. Backup display

Separate design (the next job). It uses this protocol: listen to the block, adopt, send on its own ID 0x2D on a change, keep-alive on silence.

## Order of work (each step testable alone, and compatible with the one before it)

1. **Protocol and docs.** No code. Review.
2. **AIR module.** An unchanged EFIS keeps sending 0x2E and everything works as now. Test on the bench: `cansend can0 02D#...` and `02C#...` frames change the altitude; out-of-range and mismatched frames are ignored; the value survives a power cycle; a hard reset restores it; the EFIS 0x2E still works.
3. **Server and page together** (they must deploy together). Unit tests for `qnh_state.py` (every rule, including the race, the silence takeover, and an old page repeating a stale value). Then the real Pi: change on the page and watch `candump`; inject 0x2D frames and watch the page follow; stop the sender and watch another take over (a second test node or `cansend` in a loop); restart the server and check it comes back with the stored value flagged unconfirmed.
4. **Jitter fix** (separate step, see below).
5. **Backup display** uses the finished protocol.

Rollback for 2 and 3: keep the old files as for earlier steps (`~/EFIS/backup_<date>/` on the Pi, previous .uf2 or source for the AIR module).

## Tests to run before calling it done

- Two senders (the page and `cansend` standing in for the backup display): the last change wins on every device; the other side stops keeping alive.
- The sender stops: another holder takes over within about 1 s; nothing changes on the displays.
- Out of range (27.99, 31.01), mismatched hPa and inHg, short frames: all ignored, no change anywhere.
- A device restarted while the others keep running: adopts the bus value within 2 s, no flag.
- Everything restarted at once: all restore the same stored value, all flag it, none send until a person turns the dial.
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
- Restored values start unconfirmed; a turn in QNH mode confirms.
- Jitter: flat +/- 20 ms about the nominal period, drawn once per send.

- ID block 0x2C to 0x2F, EFIS on 0x2E, backup display on 0x2D, 0x2C and 0x2F spare.
- The AIR module reports the QNH it is using, in 0x2B bytes 5-7, because its altitude is computed from it.
- The flat +/- 20 ms applies to every periodic message, GPS included.

Open:
- Flash EEPROM emulation or the external 24LC32 on the AIR module and backup display: measure the `commit()` stall on the bench first (proposed: flash).
- How the unconfirmed state and the baro mismatch look on the EFIS page and the backup display.
- The GPS time-sync message (0x19) with +/- 20 ms: the server only seems to build a datetime from it, but check that nothing uses its arrival time.
