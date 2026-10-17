# Local patch to Adafruit_MCP2515 0.2.1: bounded transmit

**This is a locally modified copy of the Adafruit library.** The firmware builds use it because
`flash_remote.sh` (and the `arduino-cli compile --libraries Sensors/Libraries` command) put this folder ahead
of the copy in `~/Documents/Arduino/libraries`. The Arduino IDE does not; it would build against the stock
library (the sketches then fail to compile, because they call the new functions below, which is a useful guard).

Upstream: https://github.com/adafruit/Adafruit_MCP2515 (MIT licence, from Sandeep Mistry's arduino-CAN).
Base version 0.2.1. Patched 2026-10-17. Unmodified originals: `src/*.orig`. Diff: `local_patch.diff`.

## The problem

`Adafruit_MCP2515::endPacket()` starts a transmission and then loops `while (TXREQ is set)` until the chip
reports the frame sent. Its only way out is the chip's TXERR flag. A module alone on an **unterminated** bus
(for example its CAN cable pulled, with no 120 ohm termination resistor on its own CAN board) never gets
there: the loop never ends and the whole firmware freezes, with the LED stuck in its last colour, no sensor
processing, no heartbeat and no USB status. With a termination resistor fitted the chip raises TXERR, the
library aborts, and the module carries on. Found and diagnosed on the bench, see
`Sensors/FAILURE_DETECTION_PLAN.md` (tests C1/C2, 2026-10-17): AIR and GPS (no resistor) froze every time,
IMU (resistor fitted) never did, and a 120 ohm resistor across CANH/CANL on the AIR module stopped the freeze.

A real aircraft bus is terminated only at its two ends, so any middle module whose connector or wire fails
ends up in this state.

## What the patch does

- `endPacket()` gives up after `TxTimeoutMs` (default 10 ms; a normal frame takes about 0.5 ms). It asks the
  chip to abort the frame (CANCTRL.ABAT), waits up to 20 ms more for the abort, and if the chip still has the
  request pending it clears TXREQ by hand. It then returns 0 (failure).
- After a timeout the library enters a "down" state. While down, `endPacket()` returns 0 immediately (no
  waiting, no SPI traffic) for 500 ms, then tries one frame. The first frame that completes clears the down
  state. This keeps the module's main loop fast while the bus is dead (a module sends several frames per
  loop; waiting 10 ms for each would stall it) and means it recovers within about 0.5 s of the cable going
  back in.
- Normal behaviour is unchanged otherwise (including the library's own TXERR abort path).

New public functions: `setTxTimeoutMs(ms)`, `txDown()`, `txFault()` (sends being abandoned, or the chip's transmit error warning/passive/bus-off flags set; used for the red LED), `txTimeouts()`, `txSuppressed()`, `txErrorCount()`
(TEC register), `rxErrorCount()` (REC), `errorFlags()` (EFLG). The sketches print them in the `?` report
through `moduleInfo.setReportHook()`, e.g. `CAN tx: DOWN (no acknowledge), timeouts 3, skipped 120, TEC 248, REC 0, EFLG 0x..`.

## Re-applying after a library update

Replace `src/Adafruit_MCP2515.h` and `.cpp` of a fresh 0.2.1 with the ones in this folder, or apply
`local_patch.diff` (`patch -p0` from `src/` after matching file names). If upstream has changed, merge by
hand: the changes are confined to `endPacket()`, three small accessors, and some private fields.
