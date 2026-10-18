# Plan: server step B (invalid-data markers and the EFIS heartbeat)

Written 2026-10-18. Nothing is built yet; the decisions that need David are at the end. This is step 4 of the order of work in
`MODULE_STATUS_PLAN.md`. The protocol side is already done (protocol 0.2.0): the markers and message 0x700 are defined.

## Goal

1. The server understands the invalid-data **markers** (0x7FFF for 16-bit fields, 0x7FFFFFFF for 32-bit fields, 0x7F for the 8-bit
   declination), so that when the modules start sending them (step 6) a failed value shows as invalid and **never as 32767**.
2. The server sends its own **heartbeat (CAN 0x700)** once a second, so the backup display can tell "the EFIS is alive but a module is
   missing" from "the EFIS is dead", and the Modules page gets an EFIS row.

Nothing the modules do changes in this step, and nothing changes for the pilot: no module sends a marker yet, so the marker code is
exercised only by tests until step 6.

## What exists today (checked 2026-10-18)

- `module_status.py` already decides validity from the module heartbeats' sensor bits and from how fresh each message is
  (`valid`, `data_lost`, `heading_status`). The page ignores anything not valid. With `NULL_INVALID_VALUES` on (1.4.0) the server also
  sends null for untrusted values.
- The handlers that unpack the fields with markers: `process_altitude_message` (0x28 airspeed, altitude, vertical speed),
  `process_oat_message` (0x2A), `process_static_pressure_message` (0x2B static and differential pressure), `process_ahrs_orient_message`
  (0x48 heading, pitch, roll, turn rate), `process_ahrs_accel_message` (0x49). They unpack straight into the data object and scale, so a
  marker would be shown as 3276.7 or 32767.
- `module_status.update`/`aio_server.update_status` already computes `_modules_ok_mask` (bit 0 AIR, bit 1 IMU, bit 2 GPS present and OK)
  and `system_state`, "for the EFIS heartbeat".
- The server already sends QNH on the bus (`pack_can_qnh_msg`, `bus.send`) and the old log showed what a send looks like when nothing
  acknowledges: `Error sending CAN message: No buffer space available` thousands of times (ISSUES #14).
- CAN receive filters: the AIR module accepts only 0x2E (QNH), so 0x700 is ignored in hardware. The IMU and GPS modules never read their
  receive buffers (they already overflow harmlessly with the QNH frames), so one more frame a second is no change. The backup display's
  filter is part of the backup display plan.

## Design

### 1. Markers (server)
- Marker values, one place in `aio_server.py`: `MARKER_INT16 = 0x7FFF`, `MARKER_INT32 = 0x7FFFFFFF`. Each handler checks the **raw** value
  before scaling. A marker field becomes `None` (and is never scaled).
- The data object records which fields currently carry a marker: `data._markers` (a set of field names, replaced by every frame, so a
  marker clears when a normal frame arrives).
- `update_status` then makes the matching validity false, on top of the sensor bits and freshness:

| Field with a marker | Becomes invalid |
|---|---|
| airspeed | `airspeed` |
| altitude | `altitude` |
| vertical_speed | `vsi` |
| oat | `oat` |
| static_pressure | `static_pressure` |
| differential_pressure | `differential_pressure` |
| heading | `heading` |
| pitch or roll | `attitude` |
| turn_rate | `turn_rate` |
| accel_x, accel_y or accel_z | `acceleration` |

  (The page needs no change: it already treats `valid` false as untrusted. A marker is the same as a heartbeat sensor bit being clear
  or the frame not arriving: the same red X.)
- The 0x65 declination marker (0x7F) is already handled in 1.5.0.
- Real values never reach a marker (checked for protocol 0.2.0: the largest real values are heading 3600, static pressure 11000,
  differential pressure under 7000, accel about 4000, altitude far below 2 billion).

### 2. EFIS heartbeat 0x700 (server)
- A small pure module `efis_heartbeat.py` (standard library only, tested anywhere): `pack(version, state, mask, uptime)` gives the 8 bytes
  in the module heartbeat layout (module number 0; version x3; state; sensors mask; uptime u16), and `build(facts, clients, uptime)` decides state and mask.
- Mask bits: 0 AIR, 1 IMU, 2 GPS seen healthy (the existing `_modules_ok_mask`), 3 a display is connected (a **controller** page: viewers
  do not count, as decided for the multi-client work). Bits 4 to 7 are 0.
- State: 1 all three modules healthy and a display connected; 2 a module is degraded, failed or missing; 3 the EFIS cannot produce a
  display: no controller page connected (see decision 1).
- Version bytes: the server's own `VERSION` (for example 1.5.0 gives 1, 5, 0). Uptime: seconds since the server started (wraps at
  65535 s like the modules).
- A new asyncio task `send_efis_heartbeat` in `aio_server.py`: once a second, `bus.send(..., timeout=...)` so a send that cannot complete
  can never stall the loop. A failed send is counted and logged **at most once a minute** with the count (so nothing repeats the 18,000-line
  log of ISSUES #14). The task stops when the server stops, which is the point: the backup display sees the heartbeat vanish.
- The heartbeat is also put in the JSON as `efis: {version, state, mask, uptime, display_clients}` so the page can show it.

### 3. Page: an EFIS row on the Modules page
- A fourth block, EFIS: state, version, uptime and "heard" is the server's own, so show "this server"; the chips are AIR, IMU, GPS (as the
  EFIS sees them) and DISPLAY.
- **Layout problem to solve with it:** four blocks no longer fit the Modules panel on a short canvas (the panel is 426 pixels tall with
  three blocks, so it already exceeds the 400-pixel canvas of Display 1 in portrait; four blocks would be 530 pixels, more than Display 2
  portrait's 512). Make the block height and spacing follow the canvas height (a compact block when short).

## Order of work (each step testable alone)

1. **Markers:** the handlers, `_markers`, the validity rule in `update_status`. Glue tests: each marker field gives `None` and the
   right validity false; a normal frame afterwards clears it; the other fields in the same frame are unaffected; no marker anywhere in a
   healthy run. Deliberate-break checks on the rule table.
2. **`efis_heartbeat.py` and the task:** unit tests for the bytes, the mask, the three states, uptime wrap, version parse; a glue test with
   a fake bus (about one frame a second, a failing bus does not stop it or flood the log).
3. **Page:** the EFIS row and the adaptive Modules panel; check all four sizes (Display 2 portrait and landscape, Display 1 portrait and
   landscape) with the fake feed, which learns to send `efis` and marker scenarios.
4. **Deploy** (backup first; the display blanks for a few seconds), then check on the real bus: `candump can0,700:7FF` shows one frame a
   second with the right version, state and mask; the AIR, IMU and GPS modules keep sending as before; the Modules page shows EFIS.
5. **Fault checks on the real system:** with the server running, stop it (the heartbeat must stop within a second or two); pull a
   module's CAN cable (the mask bit clears and the state goes to 2); disconnect the display page (bit 3 clears).
6. Update `MODULE_STATUS_PLAN.md`, `Protocol/can_messages.yaml` (0x700 from `planned` to `active`), ISSUES.md, RESUME.md and memory.

## Risks and notes

- A new frame on the bus. One frame a second at the lowest priority is negligible, and the modules do not read it. If a module ever
  shows a bus problem after the change, stop the task (rollback below).
- The backup display and the modules are not touched here; they follow (steps 5 and 6).
- If nothing on the bus acknowledges (no module powered), the send fails; that is handled quietly as above.
- Rollback: copy back `~/EFIS/backup_.../aio_server.py` and restart `piefis`. No firmware changes.

## Decisions needed from David

1. **State 3 of the EFIS heartbeat** ("the EFIS cannot produce a display"): proposed to mean no controller page has been connected for
   more than 30 seconds (so the backup display can show that the Pi is up but its screen is not). During the first 30 seconds after
   the server starts it stays 1 or 2. On a bench with no Pi display the heartbeat would then read state 3. OK?
2. **Version bytes** = the server's version (1.5.0 gives 1, 5, 0)?
3. **Modules page** with an EFIS row and a layout that adapts to a short canvas (compact blocks)?
4. **Heartbeat send failures** logged at most once a minute with a count?
