# CAN_Display -- KS0108 Backup Flight Instrument

A small, dead-simple backup instrument display for the piavionics EFIS: a
KS0108 128x64 monochrome GLCD driven by one Adafruit Feather RP2040, fed by a
second Feather RP2040 that listens on the CAN bus. It has no dependency on
the Pi EFIS -- it reads the CAN bus directly, so it keeps working even if the
Pi dies.

Two boards because the KS0108 is a parallel (not I2C) panel, and driving it
plus decoding CAN plus running SPI on one RP2040 is more than necessary:

- **Display_Feather** -- Adafruit Feather RP2040, bit-bangs the KS0108 over
  GPIO via [u8g2](https://github.com/olikraus/u8g2), and is an **I2C
  peripheral** (address `0x42`).
- **CAN_Feather** -- Adafruit Feather RP2040 + Adafruit MCP2515 CAN Bus
  FeatherWing (same hardware/library as `AIR_Module.ino` / `IMU_AHRS.ino`
  elsewhere in this repo), listens on the bus, and pushes a compact struct
  to Display_Feather over I2C as **controller**.

`shared/DisplayProtocol.h` defines that struct and is symlinked into both
sketch folders so the two firmware images can't drift out of sync -- edit
only the copy in `shared/`.

Both sketches compile clean against `rp2040:rp2040:adafruit_feather` with
the `U8g2` and `Adafruit_MCP2515` libraries installed.

## Scope (v1)

Flight instruments only, from CAN data piavionics already actively
transmits: **Airspeed, Altitude, VSI, Heading, Pitch, Roll, Turn Rate.**

RDAC/engine data (RPM, TCs, oil, fuel, MAP) is a deliberate follow-on, not
implemented here -- see "Phase 2" below.

## CAN messages used

Verified against the actual sensor firmware (`Sensors/AIR_Module/AIR_Module.ino`,
`Sensors/IMU_AHRS/IMU_AHRS.ino`), **not just** `CAN Reference/Can Docs.md`,
which has stale entries for these messages:

| ID | Description | Format (as actually sent on the bus) |
|---|---|---|
| `0x28` | Airspeed, Altitude, VSI | int16 knots, **int32** ft (doc says 3 bytes + 1 unused -- wrong, it's a full int32, no padding), int16 ft/min |
| `0x48` | AHRS Euler (heading/pitch/roll/turn rate) | int16 heading, int16 pitch, int16 roll, int16 turnRate -- all **deg x 10** (deg/s x10 for turn rate) |

All fields little-endian, per `CAN Reference/Can Docs.md`'s general note.

## MGL RDAC conflict (found, not fixed here)

`Sensors/GPS_Module/GPS_Module.ino` broadcasts UTC time sync on CAN ID
`0x19` once per second. MGL's CAN addressing is `(7-bit device address << 4)
| 4-bit message type`; `0x19` is exactly `(host address 1 << 4) | 9`, which
MGL's protocol reserves for **General Command** (used to send
calibration/setup commands to an RDAC -- byte 0 is interpreted as a
destination RDAC index, 0-3).

It's harmless today by luck: byte 0 of the time-sync payload is `gpsYear`
(e.g. 26 for 2026), which never falls in RDAC's valid destination range 0-3.
But it's fragile -- it depends on the year offset staying outside 0-3, and if
the Pi EFIS ever gains real MGL-host command features, it collides for real.

**Before putting an MGL RDAC XF on this bus**, move the time-sync message off
`0x19` (e.g. to `0x66`, right after the GPS block at `0x63-0x65`). One-line
change in `GPS_Module.ino`. RDAC device data itself (`0x201-0x239`, see
`~/Documents/_A-Aviation/(M) MGL Manuals/July 2025/MGL CAN Protocol.pdf`) has
zero overlap with anything piavionics sends -- only the time-sync message
needs to move.

## Hardware wiring

### Reading from the GLCD: not supported, by the library, not just by choice

u8g2's KS0108 driver is write-only at the code level, independent of wiring.
Checked directly against the installed library source
(`U8x8lib.cpp:u8x8_byte_arduino_ks0108`, and the `U8G2_KS0108_128X64_F`
constructor in `U8g2lib.h`): the constructor never takes an R/W pin, and the
byte-callback only implements the write-side message set (`BYTE_SEND`,
`BYTE_INIT`, `SET_DC`, `START/END_TRANSFER`) -- there's no busy-flag poll and
no VRAM readback path on any platform. Enabling reads would mean writing a
custom driver, not extra wiring. It also buys little here: u8g2's full-buffer
mode keeps the entire 1KB framebuffer in the Feather's RAM (trivial against
264KB) and resends it each refresh; busy-flag polling only pays off at
redraw rates well beyond what a several-times-a-second backup instrument
needs. The hardware below is bidirectional-capable anyway (see "Level
shifting"), so R/W is a real GPIO -- but the current firmware just holds it
low permanently at boot and never toggles it.

### Level shifting (3.3V <-> 5V)

This GLCD module is 5V-logic (typical for KS0108 clones of this vintage) and
the Feather RP2040's GPIO are 3.3V and not 5V-tolerant. Driving 5V-CMOS
inputs directly from 3.3V is marginal on every line the RP2040 drives.

The **data bus (8 lines) is genuinely bidirectional** -- level-shifted with
one **SN74LVC4245A** dual-supply translating transceiver, so a future
custom driver could add real reads (busy-flag poll, VRAM readback) as a
pure firmware change with no board respin. u8g2's KS0108 driver doesn't use
that path today (see below), but the hardware supports it.
The **5 always-host-to-display control lines** (RS, E, CS1, CS2, RST) plus
**R/W's 5V-side copy** stay on a single-supply **74HCT541** octal buffer,
since those never reverse direction.

**I/O budget** (why there's room to spare): 8 data + RS + E + CS1 + CS2 +
RST + R/W = **14 signals**, against ~19-21 general-purpose digital pins
available on the Feather RP2040 header outside the dedicated I2C SDA/SCL
pair. Verified against the actual `rp2040:rp2040:adafruit_feather` pin
definitions, not guessed -- `D4`, `D13`, `SCK`, `MOSI`, `MISO` are left over.

#### SN74LVC4245A (data bus, bidirectional)

Verified against the TI datasheet (SCAS375K) rather than assumed, because
two things about this part are easy to get backwards on a fabricated board:

- **Port assignment is fixed by the part, not a free choice.** `V`<sub>CCA</sub>
  is specified as the 5V rail, `V`<sub>CCB</sub> as 3.3V. The GLCD (5V)
  connects to the **A-port** (pins 3-10, `A1-A8`); the Feather (3.3V)
  connects to the **B-port** (pins 14-21, `B8-B1`, note reversed pin order).
  Wiring it the other way around runs the part outside its rated operating
  conditions.
- **`DIR` maps straight to R/W, no inverter needed.** Per the datasheet's
  function table: `OE=L, DIR=L` &rarr; B-to-A (Feather writes into the
  GLCD); `OE=L, DIR=H` &rarr; A-to-B (GLCD drives back to the Feather). That's
  the same polarity as the KS0108's own R/W (low=write, high=read), so
  R/W wires directly to `DIR` with no logic gate in between.
- `DIR`/`OE` are "powered by V`<sub>CCA</sub>`" (5V) per the datasheet, but
  their recommended `V`<sub>IH</sub> is 2V min -- the Feather's 3.3V logic
  HIGH clears that with margin, so `DIR` and `OE` connect **directly** to
  the Feather, no shifting needed for those two control pins even though
  everything else on this chip is about shifting.
- **Package**: SSOP-24 / SOIC-24 / TSSOP-24 only -- no DIP option exists for
  this part (unlike the 74HCT541 below, which is a common PDIP-20 part). For
  a fabricated board this is a non-issue; SOIC-24 (1.27mm pitch) is the
  easiest of the three to hand-rework if that ever matters.

| SN74LVC4245A pin | Connects to | Notes |
|---|---|---|
| 1 `V`<sub>CCA</sub> | 5V rail | |
| 2 `DIR` | Feather A1 (= R/W), direct, no shifting | |
| 3-10 `A1-A8` | GLCD DB0-DB7 | 5V side |
| 11-13 `GND` | common GND | |
| 14-21 `B8-B1` | Feather D25,D24,D12,D11,D10,D9,D6,D5 (= DB7..DB0, reversed pin order) | 3.3V side |
| 22 `OE` (active low) | GND (always enabled) | |
| 23-24 `V`<sub>CCB</sub> | 3.3V rail | |

Plus a 0.1&#181;F ceramic decoupling cap across each VCC/GND pair, close to the chip.

#### 74HCT541 (control lines, unidirectional)

Same part/wiring as before, minus the 3 previously-spare channels -- one now
carries R/W's 5V-side copy (the GLCD's own R/W input still needs 5V-clean
logic, same as every other GLCD input, independent of the 4245's DIR pin).

| 74HCT541 (control) | A (3.3V, from Feather) | Y (5V, to GLCD) |
|---|---|---|
| ch 1 | A0 | RS (DI) |
| ch 2 | A1 (same net as the 4245's DIR) | R/W |
| ch 3 | A2 | E (enable) |
| ch 4 | A3 | CS1 |
| ch 5 | D0 | CS2 |
| ch 6 | D1 | RST |
| ch 7-8 | tie A6-A8-equivalent inputs to GND (unused CMOS inputs shouldn't float) | not connected |

`OE1`/`OE2` (pins 1, 19) tied to GND (outputs always enabled), `VCC` (pin
20) to the 5V rail, `GND` (pin 10) to common ground, plus a 0.1&#181;F
decoupling cap.

#### Firmware today: write-only regardless of the hardware

u8g2's KS0108 driver has no read code path on any platform (checked the
installed library source: the `U8G2_KS0108_128X64_F` constructor has no R/W
parameter, and its byte-callback only implements write-side messages). So
`Display_Feather.ino` drives R/W (Feather A1) low at boot and never
toggles it -- `DIR` stays fixed at B-to-A, matching write-only operation,
even though the board now has a real bidirectional path. Adding reads later
means writing a custom driver that takes over that pin, not a board change.

### Display_Feather -- KS0108 GLCD

Standard 20-pin KS0108 header. **Verify pin *order* against your specific
board's silkscreen** -- CS1/CS2 left/right assignment is known to vary
between manufacturers of this clone; everything else below is consistent
across boards. "Feather RP2040 pin" below means "through the matching
74HCT541 channel above," except VCC/GND/contrast/backlight/R-W which connect
directly (no shifting needed for static levels or the analog contrast tap).

| GLCD pin | Signal | Connects to | Notes |
|---|---|---|---|
| 1 | GND | GND | |
| 2 | VCC | 5V (`USB`/`BAT`) | |
| 3 | V0 (contrast) | wiper of 10-20k&#8486; trimpot between VCC and GND | not a GPIO |
| 4 | RS (DI) | Feather A0, via 74HCT541 ch1 | |
| 5 | R/W | Feather A1, via 74HCT541 ch2 (same net also goes direct to the 4245's DIR) | firmware holds this low today -- see below |
| 6 | E (enable) | Feather A2, via 74HCT541 ch3 | |
| 7 | DB0 | Feather D5, via SN74LVC4245A B8/A1 | |
| 8 | DB1 | Feather D6, via SN74LVC4245A B7/A2 | |
| 9 | DB2 | Feather D9, via SN74LVC4245A B6/A3 | |
| 10 | DB3 | Feather D10, via SN74LVC4245A B5/A4 | |
| 11 | DB4 | Feather D11, via SN74LVC4245A B4/A5 | |
| 12 | DB5 | Feather D12, via SN74LVC4245A B3/A6 | |
| 13 | DB6 | Feather D24, via SN74LVC4245A B2/A7 | |
| 14 | DB7 | Feather D25, via SN74LVC4245A B1/A8 | |
| 15 | CS1 | Feather A3, via 74HCT541 ch4 | |
| 16 | CS2 | Feather D0, via 74HCT541 ch5 | |
| 17 | RST | Feather D1, via 74HCT541 ch6 | |
| 18 | VEE | -- | charge-pump output, do not connect externally |
| 19 | LED+ (backlight) | 5V through a series resistor (value depends on backlight forward current, typ. ~10-22&#8486; for these modules -- confirm against your module) | omit if no backlight fitted |
| 20 | LED- (backlight) | GND | |

Leaves `SCK`/`MOSI`/`MISO` and `D13` (onboard red LED) free. `SDA`/`SCL`
reserved for I2C to CAN_Feather. Firmware pin `#define`s in
`Display_Feather.ino` are unchanged by the level shifters -- they sit
physically between the Feather and the GLCD; the GPIO numbers the code
drives are the same either way, plus the new `GLCD_RW` pin (A1).

### CAN_Feather -- MCP2515 FeatherWing

Stacks directly on the Feather header -- no wiring. SPI + CS7 auto-connect,
identical to `AIR_Module.ino` / `IMU_AHRS.ino`.

### Board-to-board I2C link

3 wires: SDA-SDA, SCL-SCL, GND-GND between the two Feathers. Feather RP2040
boards have onboard I2C pull-ups already, so no external resistors needed
for a short board-to-board run.

**Known risk**: RP2040 has a documented I2C-slave silicon errata
(RP2040-E14). arduino-pico's `Wire` peripheral-mode implementation works
around it in practice (widely used for RP2040-as-I2C-peripheral projects),
but if it proves flaky during bring-up, the fallback is a simple UART link
instead of I2C.

## I2C protocol

CAN_Feather pushes a `DisplayData` struct (see `shared/DisplayProtocol.h`)
to Display_Feather every ~200ms. `validMask` bits tell Display_Feather which
fields have fresh CAN data (vs. showing `---`); a separate link-staleness
check (based on `seq` advancing) covers the I2C link itself going down.

## Verification status

Both sketches compile clean (`arduino-cli compile --fqbn
rp2040:rp2040:adafruit_feather`) but **have not been bench-tested on real
hardware** in this session. Suggested bring-up order:

1. Display_Feather alone: temporarily fill `data` with fixed test values in
   `setup()` (bypass I2C) to check the GLCD wiring and layout on the actual
   panel before trusting the I2C link.
2. CAN_Feather alone: watch `Serial` for "CAN_Feather 1.0.0" and add a debug
   print of decoded values (mirroring the `DEBUG`/`serialPrintCANMsg` pattern
   already used in `AIR_Module.ino`/`GPS_Module.ino`) to confirm decode
   before trusting the I2C push.
3. Both boards together, watching the GLCD track real airspeed/AHRS
   broadcasts from the existing `AIR_Module` and `IMU_AHRS` boards on the bus.

The layout in `Display_Feather.ino` (2 columns x 4 rows, `u8g2_font_6x10_tf`)
is a first-pass starting point -- expect to tune exact column x-positions
once it's visible on the real panel; glyph metrics can shift things a few
pixels from what's planned on paper.

## KiCad schematic

`KiCad/CAN_Display_Wing/` has a starting-point KiCad 10 project for a
FeatherWing implementing the Display_Feather side (GLCD + both level
shifters) -- `CAN_Display_Wing.kicad_pro`/`.kicad_sch`, plus a project-local
symbol library (`CAN_Display_Wing.kicad_sym`) holding hand-authored
`SN74LVC4245A` and `KS0108_GLCD_20` symbols (verified pin-for-pin against
the datasheet and this README's GLCD table -- not from a stock library,
since neither part had a ready-made KiCad symbol). Reuses the existing
`Dave KiCad Library:Feather_RP2040` symbol for U1, and stock
`74xx:74HCT541` / `Device:*` / `Connector_Generic:*` symbols elsewhere.

All 35 nets in the schematic were generated programmatically from the exact
pin tables above (not hand-drawn), then checked with `kicad-cli sch erc`.
The only ERC violations that remain are either intentionally-spare Feather
pins (Rst, Aref, SPI bus, D4/D13, EN/BAT/USB -- matching the "leaves ...
free" note above), the two intentionally-unused 74HCT541 channels, or a
handful of pin-type false positives inherited from the shared
`Feather_RP2040` symbol (some GPIO pins are typed "input" in that symbol
even though they're driven outputs in this design) -- not actual wiring
mistakes. Component placement is a first pass (like the GLCD text layout,
expect some `endpoint_off_grid` nudging once you're arranging things in the
GUI); the electrical connectivity is what's been verified.

**Design decision baked into this schematic, not yet reflected anywhere
else**: it deliberately does *not* source 5V from the Feather's `USB` pin
(only present when a literal USB cable is connected, not from `BAT`) --
that would make the backup display go dark exactly when main-bus/USB power
is the thing that failed. Instead there's a dedicated `PWR_5V_IN` 2-pin
connector (J3) for an independent 5V feed from your aircraft power
distribution. Confirm this matches how you intend to power the Feather
itself before wiring it up.

## Phase 2 (not implemented)

- **GPS** (`0x64`: ground speed, altitude, track) -- struct/validMask have
  spare bits reserved, no decode implemented yet.
- **RDAC/EMS** (RPM, thermocouples, oil, fuel, MAP) -- needs the MGL `0x19`
  conflict resolved first (see above). RPM (`0x208`, message ID 8) and
  thermocouples (`0x202-0x204`, message IDs 2-4) are already final-unit and
  need no calibration. Oil pressure/temp, MAP, coolant, fuel level/pressure,
  and current are raw ADC counts (`0x205-0x206`, message IDs 5-6) and need
  MGL's calibration-readback handshake (`0x19` General Command `$81`,
  response on message ID 9) to convert to real units -- meaningful added
  scope, deliberately deferred.
