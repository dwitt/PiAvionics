# Backup display: questions to settle before the layouts

Written 2026-10-20. The design so far: one Feather RP2040 and a Feather CAN wing, an Adafruit I2C/SPI LCD backpack driving a
Newhaven NHD-0420DL-FL-GBW (20x4 character LCD), and an Adafruit rotary encoder with push button (seesaw, I2C). The QNH is
shared over CAN as in `EFIS/docs/QNH_PLAN.md` (this display sends on 0x2D). The old design in this folder (`CAN_Feather`,
`Display_Feather`, `shared/DisplayProtocol.h`, the KiCad wing) is superseded.

## 1. Encoder fields

**What it is about.** The encoder is one knob and one button, and it has to control several things. Only one field is selected at
a time, and a turn changes the selected field and nothing else. This matters most for the QNH: a QNH frame goes out only while
the encoder is on the QNH field, so a bumped knob can never change the altimeter.

**How I would have it work.**
- Push moves to the next field. The selected field is highlighted on the screen.
- Turn changes the selected field.
- After about 10 s with no input it falls back to the default field.
- The default field is "page", so a casual turn only flips screens.

**Fields suggested.**

| Field | A turn does |
|---|---|
| Page | flips between layouts (flight data, engine/status, and so on) |
| QNH | changes the altimeter setting, 0.01 inHg per click, and sends it on 0x2D |
| Backlight | LCD backlight brightness |
| Contrast | LCD contrast |

**Questions for you.**
- Is that the right list? Add or drop any, for example a units toggle (inHg and hPa) or a "clear alarms" action?
- Is "page" the right default field, or should it rest on QNH?
- Is 10 s the right timeout?

**Answered (2026-10-21), replaces the proposal above:** the backup uses the EFIS model, push to enter and push to exit, with no
timeout. Like the EFIS it starts with QNH selected and adjustable, so the QNH is live all the time. A marker `>` sits
between the label and the value of the selected field: flashing while selecting (turn moves between fields), solid while
adjusting (turn changes the value; possibly a smaller custom character). To try first, then iterate if it does not work in use.
The field list and its order are still to be settled.

## 2. How failures are acknowledged

**What it is about.** When a sensor or module fails the 20x4 display must show it clearly and must not show a wrong number. The
question is what happens once you have seen it.

**Option A: it clears itself (my suggestion).**
- The failed value shows `---`.
- The status row flashes for the first few seconds, then goes steady while the fault lasts.
- When the sensor recovers the `---` becomes a number again and the row clears.
- This matches how the EFIS annunciators work, and nobody has to touch the knob in a busy moment.

**Option B: it needs a push to acknowledge.**
- The warning flashes until you push the encoder, then goes steady.
- A fault that came and went is still shown until you acknowledge it, so a brief dropout is not missed.
- The cost is an extra action while flying, and the push is also used to select fields, so the two uses would clash.

**Middle option.** Auto-clear, but keep a "fault seen" mark on the status row until the page is changed.

**Question for you.** A, B or the middle option?

**Answered:** Option A. A failure clears itself, showing `---` and a flashing status row for a few seconds.

## 3. Where the sketch goes

**What it is about.** `CAN_Display/` already holds the old design. You said it is superseded.

**My suggestion.**
- Create `CAN_Display/Backup_Display/` for the new sketch.
- Leave `CAN_Feather`, `Display_Feather` and `shared/DisplayProtocol.h` untouched.
- Add small libraries (CAN, LCD backpack, encoder) only if the sketch gets too big.

**Alternatives.**
- A top-level `Sensors/Backup_Display/`, next to the other Feather modules and using the shared `Sensors/Libraries`.
- Deleting the old files. I would not do that without your say-so.

**Questions for you.** Which location, and should the old files stay?

**Answered:** the sketch goes in `Sensors/Backup_Display/`. Whether the old files stay is still open.

## What depends on the answers

The screen layouts depend on the field list (question 1) and the failure handling (question 2). The bench sketch (CAN to LCD)
depends on the folder (question 3). If you only answer one, answer question 1.
