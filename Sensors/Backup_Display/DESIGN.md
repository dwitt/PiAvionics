# Backup display: design record

Written 2026-10-21. Sketch: `Backup_Display.ino` (version 1.4.1 when this was written). The decisions below were made with David
on the bench; the sketch is the authority on exact values.

## Hardware

- Adafruit Feather RP2040, Adafruit CAN FeatherWing (MCP2515, CS 7, 250 kbit/s).
- Adafruit I2C/SPI LCD backpack (MCP23008, 0x20, powers the display) and a Newhaven NHD-0420DL-FL-GBW 20x4 character LCD.
- Adafruit seesaw rotary encoder with push button (0x36, product 4991). Found on `Wire`.
- I2C runs at 400 kHz. At 100 kHz one full horizon update took 150 ms; at 400 kHz it takes about 35 ms. 1 MHz is faster still (10 ms)
  but 400 kHz was chosen as safer.
- The backpack cannot set contrast (it is a pot) or backlight brightness (the backlight is on/off). So there are no brightness or
  contrast fields. The display is readable with the backlight off.
- Libraries: the patched `Adafruit_MCP2515` from `Sensors/Libraries` (symlink in `src/`), `Adafruit_LiquidCrystal`, `Adafruit_seesaw`.
- The old KS0108 design is archived in `Legacy/CAN_Display_v1/`.

## Two cores

Core 0 only receives CAN and runs the QNH logic. Core 1 does the I2C, LCD, encoder and serial commands. An LCD update takes tens of
milliseconds and would otherwise let the MCP2515's two receive buffers overflow. The encoder turns reach core 0 through the
RP2040 inter-core FIFO. Shared values are plain `volatile` variables with a signed time difference for ages (core 0 can stamp a
frame a moment after core 1 reads the clock).

## The screen

20x4 characters. A value that is missing or stale (older than its "treated as lost after" time in `Protocol/CAN_PROTOCOL.md`)
shows as dashes. There are four pages. PG is in the lower left of every page and QNH in the lower right of any page that shows
pressure altitude.

| Page | State | Contents |
|---|---|---|
| 1 Flight | built | IAS, ALT, VS, HDG, OAT, G, a 3x2 character mini horizon, module status letters, QNH |
| 2 System | built | backlight field, display version, module states from the heartbeats, CAN frame rate, uptime |
| 3 Engine | not built | RPM, MAP, oil P and T, EGT, CHT, volts, fuel flow. Needs a decision on the MGL RDAC raw values (see below) |
| 4 GPS | not built | ground speed, track, GPS altitude and time. Needs the GPS module on the bus |

Flight page:

```
IAS 105   ALT  4520
HHH VS  +500 HDG 270
HHH OAT 12C  G 1.0
PG>1   IAGE   30.30
```

- **Vertical speed:** shown in 50 ft/min steps and it stays put until the raw value is 40 ft/min away from it. The raw value drifts
  by about +/-16 ft/min at rest. A real static-pressure VSI is also damped.
- **G:** vertical acceleration (sensor Z) divided by standard gravity.
- **Mini horizon:** 3x2 custom characters. The one pixel gaps between characters are counted in the geometry (17 x 17 pixels) but
  never drawn, so angled lines look right. It has a one pixel box round the edge. Ground is filled, sky is blank. Pitch is 0.5
  pixel per degree (about +/-16 degrees top to bottom) and roll is limited to +/-60 degrees. With no attitude it shows `ATT` over `---`.
- **Custom characters:** 8 slots in all: 6 for the horizon, slot 6 for the adjusting marker, slot 7 for its reverse. Nothing is spare.

System page:

```
BL>ON         v1.4.1
I:OK A:OK G:-- E:DG
CAN 50/s UP 0:12:34
PG>2
```

Module states: `OK`, `DG` degraded, `ST` starting, `FL` failed, `--` no heartbeat for 3 s.

## Failure annunciation (Option A, self-clearing)

A failed value shows dashes and nobody has to acknowledge anything. The status letters I A G E (IMU, AIR, GPS, EFIS) in the
middle of row 3 of the Flight page show each module's heartbeat:

| Look | Meaning |
|---|---|
| capital letter, steady | OK |
| lower case letter, steady | degraded, or starting |
| lower case letter flashing against `-` | failed, or no heartbeat for 3 s: 0.5 s each for the first 5 s, then 1.5 s each |

The letter always says which module. The lower case `e` for the EFIS means it is degraded because the GPS is missing.

## The encoder

It works like the EFIS knob. Push toggles between two modes and there is no timeout.

- **Adjusting** (the start state, with QNH selected): a turn changes the selected field.
- **Selecting:** a turn moves the marker to the next field.

The marker sits in the column between a field's label and its value. A small solid triangle (steady) means adjusting. The same
triangle flashing against its reverse (a solid block with the triangle cut out, bottom row blank), 0.5 s each, means selecting.

Fields in clockwise order round the screen from the lower right:

| Page | Fields |
|---|---|
| Flight | QNH (lower right), PG (lower left) |
| System | PG (lower left), BL (upper left) |

- **PG:** a turn changes the page (clockwise is the next page, it wraps).
- **QNH:** 0.01 inHg a click, limited to 28.00 to 31.00.
- **BL:** every click, in either direction, toggles the backlight.
- **Direction:** the seesaw counts anticlockwise as positive, so the sketch reverses it (`ENCODER_SIGN`).

## QNH

The rules are those of `EFIS/docs/QNH_PLAN.md`, ported from `EFIS/qnh_state.py`.

- This display sends on 0x2D and adopts a valid frame from any ID in 0x2C to 0x2F (28.00 to 31.00 inHg and an hPa field that agrees to within 1 hPa).
- A change from the knob is sent at once, at most one frame per 20 ms.
- A set value is sent again after 1000 ms +/- 20 ms without a QNH frame on the bus. That is also the takeover when the other senders are silent.
- For 2 s after power-up it sends nothing unless the knob is turned.
- With nothing heard, the value is 29.92 followed by `?` (not set) and it is never sent.
- A `!` after the value means the AIR module (0x2B bytes 5 to 7) has reported a different QNH for more than 2 s.
- Nothing is stored. After a restart the value comes from the bus.

Tested on the real bus on 2026-10-21 (sketch 1.3.0): adopting the shared value, one frame per click on 0x2D, the `!` and the EFIS
amber a mismatch about 2 s after the AIR module was silenced, the display as the only sender (frames 0.984 to 1.019 s apart),
and `29.92?` with everyone not set until one click set all three devices.
With the EFIS and the display both in step, about a quarter of the seconds carry two keep-alives (both expired within a few ms
of each other). This is harmless.

## Serial commands (115200)

`P <deg>` and `R <deg>` hold the pitch and roll, `S <hz>` sweeps them, `U <ms>` is the horizon update period, `C <khz>` sets the
I2C clock, `M <0|1>` selects real or simulated data, `B <0|1>` sets the backlight, and `A` returns to the moving simulation. Once a
second the sketch prints the horizon update rate, the CAN frame rate and the QNH state. The Arduino IDE serial monitor keeps the
port open, which stops remote flashing.

## Open

- **Engine page:** the MGL RDAC sends raw ADC values for MAP and the analogue channels. The backup would need the calibration
  built into the sketch. Decide which values the backup should show.
- **GPS page:** needs the GPS module back on the bus. The display is plugged in where the GPS module was, for the bench.
- **CAN ID conflict:** our 0x019 TIME_SYNC collides with MGL's GENERAL_COMMAND (an RDAC would see it). Move it before an RDAC is on the bus.
- **Not yet done:** the backup display does not send its own heartbeat, and does not show module versions on the System page.
- **Not tested:** the display and the EFIS agreeing after the display itself restarts mid-flight with everyone else set.
