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

### Display_Feather -- KS0108 GLCD

Standard 20-pin KS0108 header. **Verify pin *order* against your specific
board's silkscreen** -- CS1/CS2 left/right assignment is known to vary
between manufacturers of this clone; everything else below is consistent
across boards.

| GLCD pin | Signal | Feather RP2040 pin | Notes |
|---|---|---|---|
| 1 | GND | GND | |
| 2 | VCC | 5V (`USB`/`BAT`) | see logic-level note below |
| 3 | V0 (contrast) | wiper of 10-20k&#8486; trimpot between VCC and GND | not a GPIO |
| 4 | RS (DI) | A0 | |
| 5 | R/W | **GND** (tie permanently low) | write-only mode -- see logic-level note |
| 6 | E (enable) | A2 | |
| 7 | DB0 | D5 | |
| 8 | DB1 | D6 | |
| 9 | DB2 | D9 | |
| 10 | DB3 | D10 | |
| 11 | DB4 | D11 | |
| 12 | DB5 | D12 | |
| 13 | DB6 | D24 | |
| 14 | DB7 | D25 | |
| 15 | CS1 | A3 | |
| 16 | CS2 | D0 | |
| 17 | RST | D1 | |
| 18 | VEE | -- | charge-pump output, do not connect externally |
| 19 | LED+ (backlight) | 5V through a series resistor (value depends on backlight forward current, typ. ~10-22&#8486; for these modules -- confirm against your module) | omit if no backlight fitted |
| 20 | LED- (backlight) | GND | |

Leaves `SCK`/`MOSI`/`MISO` free (future expansion, e.g. SD logging) and
`D13` free (onboard red LED, no extra wiring needed for a status LED).
`SDA`/`SCL` reserved for I2C to CAN_Feather.

**Logic-level risk**: this GLCD module is a 5V-logic part (typical for
KS0108 clones of this vintage) but the Feather RP2040's GPIO are 3.3V and
**not 5V-tolerant**. Driving the panel's inputs from 3.3V logic usually still
registers a valid HIGH on 5V-CMOS inputs, but the GLCD's data-out lines (when
reading the busy flag) would drive 5V back into a 3.3V-only RP2040 pin, which
is out of spec and can damage it. **Tying R/W permanently to GND** (as
above) sidesteps this entirely -- the display is write-only, u8g2's KS0108
driver never reads the busy flag, and no pin on the GLCD ever drives back
into the Feather. If you ever want bidirectional reads, add a level-shifter
(e.g. 74HCT245) instead of wiring R/W to a GPIO directly.

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
