# Backup display: design record

Updated 2026-10-22. Sketch: `Backup_Display.ino` (version 1.7.6 when this was written). The decisions below were made with David
on the bench; the sketch is the authority on exact values.

## Hardware

- Adafruit Feather RP2040, Adafruit CAN FeatherWing (MCP2515, CS 7, 250 kbit/s).
- Adafruit I2C/SPI LCD backpack (MCP23008, 0x20, powers the display) and a Newhaven NHD-0420DL-FL-GBW 20x4 character LCD.
- Adafruit seesaw rotary encoder with push button (0x36, product 4991). Found on `Wire`. One encoder had a worn contact (a clockwise
  click sometimes read as anticlockwise); replacing it fixed that.
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

## Watchdog

The chip restarts if either core stops for 2 s. Core 0 feeds the watchdog, but only while core 1 is still looping (core 1 stamps a
time on every pass). I2C transactions give up after 25 ms instead of waiting for ever. It starts once both cores are running. A
restart takes about 2 s and the QNH comes back from the bus (it is never stored). The serial command `H` hangs core 1 to test it;
after a restart the sketch prints "Restarted by the watchdog". If the LCD backpack is missing at power-up the display restarts
every 2 s until it is reconnected. The same scheme is in the IMU (1.0.18), AIR (1.0.39) and GPS (1.0.11) modules.

## The screen

20x4 characters. A value that is missing or stale (older than its "treated as lost after" time in `Protocol/CAN_PROTOCOL.md`)
shows as dashes. There are three pages. PG is in the lower left of every page and QNH in the lower right of any page that shows
pressure altitude (the Flight page only).

| Page | State | Contents |
|---|---|---|
| 1 Flight | built | IAS, ALT, VS, HDG, OAT, G, mini horizon and slip ball in the centre, module status letters, QNH |
| 2 GPS | built | ground speed, GPS altitude, time, track, position, fix type, satellites, HDOP |
| 3 System | built | backlight field, display version, module states from the heartbeats, CAN frame rate, uptime |
| Engine | not built | RPM, MAP, oil P and T, EGT, CHT, volts, fuel flow. Would go between GPS and System. Needs a decision on the MGL RDAC raw values (see below) |

The Flight and GPS pages share one layout: values left and right, the centre block (columns 8 to 10) for the horizon on the Flight
page and for the label `GPS` over `DATA` on the GPS page.

Flight page (`|` is the edge line drawn in a custom character, see below):

```
IAS 105|HHH|ALT 4520
VS +500|HHH| HDG 270
OAT 12C|_o_|   G 1.0
PG>1   IAGE   30.30
```

- **Vertical speed:** the display shows the value the AIR module sends. The AIR module (1.0.38) smooths it over about 3 s and sends
  a steady value: it holds until the smoothed rate is 30 ft/min away, then moves to the nearest 10. The sensor module is the source
  of truth, so the displays do no smoothing of their own. VS can reach four digits.
- **G:** vertical acceleration (IMU Z) divided by standard gravity. David has not decided whether to keep it. Moving G and the
  slip filter into the IMU module is a possible later change.
- **Mini horizon:** 3x2 characters (columns 8 to 10, rows 0 and 1). The one pixel gaps between characters are counted in the
  geometry (17 x 17 pixels) but never drawn, so angled lines look right. There is no box. Ground is filled, sky is blank. Pitch is
  0.5 pixel per degree (about +/-16 degrees top to bottom; David is happy with that) and roll is limited to +/-60 degrees. A positive
  roll on the bus turns the horizon clockwise, as on the EFIS. With no attitude it shows `ATT` over `---`.
- **Slip ball:** a 5x5 pixel ball on row 2 under the horizon, moving in 13 positions along the 17 pixel track (so it can straddle two
  characters). A line runs under it on pixel row 6, level with the built-in `_` that the other two cells show. It uses IMU accel Y
  (0x49 bytes 2 and 3), low-passed (0.2 per 100 ms redraw), 0.035 g per pixel. The direction matches the EFIS. It has not yet been
  tried in an aircraft.
- **Edge lines:** the horizon and ball are bounded by a one pixel line on each side, in rows 0 to 2. They are drawn with the pixel
  column nearest the horizon (the right pixel column in the left bar, the left pixel column in the right bar), so each sits the same
  distance from its neighbour.

### Custom characters

The display has 8 slots. Slots 6 and 7 are the two edge lines and never change. The other six are given out as needed:
- horizon cells that are all sky use the built-in blank (0x20) and cells that are all ground use the built-in solid (0xFF), so only
  the cells the line crosses (at most 4 of the 6) need a custom character;
- the slip ball and its line use at most 2 more;
- the worst case is 6 of the 6, so there is no spare. The selection marker is the built-in right arrow (0x7E), not a custom character.

GPS page:

```
GS 105  GPS ALT 4500
14:32Z  DATA TRK 262
N43.6532   W079.3832
PG>2  3D 9sat HD1.1
```

Track shows dashes below 2 kt (the GPS module holds it at 0 there). With no fix the status field shows `NOFIX`. HD is the HDOP; the
GPS module's fix quality is not reliable for WAAS, so HDOP is the quality indicator. David checked this page on the display with
the real GPS module on the bus (2026-10-22).

System page:

```
BL>ON         v1.7.6
I:OK A:OK G:-- E:DG
CAN 50/s UP 0:12:34
PG>3
```

Module states: `OK`, `DG` degraded, `ST` starting, `FL` failed, `--` no heartbeat for 3 s.

## Failure annunciation (Option A, self-clearing)

A failed value shows dashes and nobody has to acknowledge anything. The status letters I A G E (IMU, AIR, GPS, EFIS) sit together in
the middle of row 3 of the Flight page and show each module's heartbeat:

| Look | Meaning |
|---|---|
| capital letter, steady | OK |
| lower case letter, steady | degraded, or starting |
| lower case letter flashing against `-` | failed, or no heartbeat for 3 s: 0.5 s each for the first 5 s, then 1.5 s each |

The letter always says which module. The lower case `e` for the EFIS means it is degraded because the GPS is missing.

## The encoder

It works like the EFIS knob. Push toggles between two modes and there is no timeout. QNH is live at power-up.

- **Adjusting** (the start state, with QNH selected): a turn changes the selected field.
- **Selecting:** a turn moves the marker to the next field.

The marker sits in the column between a field's label and its value. The right arrow (0x7E) steady means adjusting. The arrow
flashing against a blank, 0.5 s each, means selecting.

Fields in clockwise order round the screen from the lower right:

| Page | Fields |
|---|---|
| Flight | QNH (lower right), PG (lower left) |
| GPS | PG (lower left) |
| System | PG (lower left), BL (upper left) |

- **PG:** a turn changes the page (clockwise is the next page, it wraps).
- **QNH:** 0.01 inHg a click, limited to 28.00 to 31.00.
- **BL:** every click, in either direction, toggles the backlight.
- **Direction:** the seesaw counts anticlockwise as positive, so the sketch reverses it (`ENCODER_SIGN` is -1).

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
I2C clock, `M <0|1>` selects real or simulated data, `B <0|1>` sets the backlight, `A` returns to the moving simulation, and `H`
hangs core 1 to test the watchdog. Each turn of the knob prints a line with the mode, field, QNH and page. Once a second the sketch
prints the horizon update rate, the CAN frame rate and the QNH state. The Arduino IDE serial monitor keeps the port open, which
stops remote flashing.

## Open

- **G on the Flight page:** keep it or not, and whether to smooth it. A later option is to move G and the slip filter into the IMU
  module as the single source (this needs new CAN messages).
- **Engine page:** the MGL RDAC sends raw ADC values for MAP and the analogue channels. The backup would need the calibration
  built into the sketch. Decide which values the backup should show.
- **CAN ID conflict:** our 0x019 TIME_SYNC collides with MGL's GENERAL_COMMAND (an RDAC would see it). Move it before an RDAC is on the bus.
- **Not yet done:** the backup display does not send its own heartbeat, and does not show module versions on the System page.
- **Not tested:** the slip ball, G and the horizon in an aircraft; the display and the EFIS agreeing after the display itself
  restarts mid-flight with everyone else set.
