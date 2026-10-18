# Plan: switch the Pi's display between portrait and landscape from the Menu

Written 2026-10-18. Built, deployed and tested 2026-10-18; decisions and status are at the end.

## Goal

Choose portrait or landscape (side by side) from the EFIS Menu. The choice is remembered across power cycles. A change needs
more than one press to take effect and to be saved, and a bad choice can never leave the display unusable.

## What exists (checked 2026-10-18)

- The panel is 720 x 1280, scale 1.25. Portrait (normal) is native. `wlr-randr --output DSI-1 --transform 90` rotates it live.
  The rotation is **not saved**: the kiosk (`piefis-labwc.service`) starts in portrait.
- Touch follows the rotation (`mapToOutput` in `~/.config/labwc/rc.xml`), tested both ways.
- The page chooses its layout once, when it loads (portrait: stacked; landscape: side by side), so a rotation must be followed
  by a page reload.
- The server (`piefis.service`) runs as `david`, the same user as the compositor, so it can run `wlr-randr` itself with
  `XDG_RUNTIME_DIR=/run/piefis WAYLAND_DISPLAY=wayland-0`.
- The kiosk browser's profile is on tmpfs (`/run/piefis/chromium-kiosk`), so anything stored in the browser is lost at power-off.
  **The saved setting must live on the Pi's disk, written by the server.**
- The Menu (`menuOverlay.mjs`) has toggle rows (label, `getState`, `setState`). An orientation row needs its own confirm step.

## Design

### Flow (the page never rotates anything itself)

1. **Menu, then Orientation.** The row shows the current orientation. Pressing it opens a small choice panel (Portrait,
   Landscape, and any flipped variants chosen below, plus Cancel).
2. **Press 1: choose.** A confirmation panel: "Switch to Landscape? The display rotates and reloads. [Switch] [Cancel]".
3. **Press 2: Switch.** The page sends the request to the server. The server rotates the screen (a **trial**, not saved) and the
   page reloads in the new layout.
4. **Press 3: Keep.** The reloaded page shows "Keep Landscape? Reverting in 15 s [Keep] [Revert]". **Keep** saves it.
   **Revert**, or no press within 15 s, rotates back to the saved orientation and reloads.
   The countdown lives in the **server**, so it still reverts if the page or the touch is unusable (the case the matrix mistake
   would have caused). Nothing is saved until Keep.

### Server (`aio_server.py`, new small `display_orientation.py`)

- New command from the controller page: `{orientation_request: "landscape"}`, `{orientation_keep: true}`,
  `{orientation_revert: true}`. Viewers cannot send it (as for all commands).
- `display_orientation.py` (stdlib only, testable anywhere with a fake command runner): the allowed orientations and their
  `wlr-randr` transforms (normal, 90, 180, 270), the saved setting file, the trial and its timer, and the revert.
- Saved setting: `~/EFIS/display_orientation.json` (`{"orientation": "portrait", "transform": "normal"}`), written atomically
  only on Keep. A missing or damaged file means portrait.
- The JSON to every page gains `display: {orientation, trial_seconds_left}` so the page can show the Keep countdown and viewers
  can follow along.
- At boot: the saved orientation is applied. Two ways, to be chosen by testing: the labwc `autostart` reads the file and runs
  `wlr-randr` (the page then loads in the right shape the first time), or the server applies it when it starts and tells the page
  to reload. The first is cleaner because no reload flicker happens; a one-line read in `autostart`.
- Safety: refused while the aircraft appears to be flying, if wanted (decision 3).

### Page (`efis-display.js`, `menuOverlay.mjs`)

- An Orientation row in the Menu, the choice and confirm panels, the Keep/Revert panel with a 15 s countdown, all drawn by the page.
- On the server's `display.orientation` changing, or a `reload` message, the page reloads itself (`location.reload()`).
- Viewers and `?pi=1` ignore all of this (a viewer shows the orientation as information only).
- Version bumps as usual.

### Tests

- Unit tests (anywhere): saved file round trip and damaged file; trial then Keep saves; trial then timeout reverts and saves nothing;
  trial then Revert; a second request during a trial; an unknown orientation is refused; viewers cannot request; the
  airborne refusal; the command runner is a fake, so no screen is touched.
- On the Pi: the real rotation each way with touch tested, Keep, Revert, timeout (do nothing for 15 s), a reboot after Keep (comes up
  in the saved orientation), a reboot with no Keep (comes up in the old one).
- Mutation checks on the revert and the "save only on Keep" rules.

## Risks and notes

- The compositor, not the page, rotates the screen, so a server bug could rotate the display and then fail to revert. The timer
  and "saved only on Keep" keep that to one restart of `piefis-labwc`; the revert also runs `wlr-randr` directly.
- Over a page reload the display is blank for about a second.
- Landscape variants need the panel's physical position to decide between 90 and 270 (which way is up).
- The existing `autostart` file also hides the cursor and sends a window-snap shortcut; the orientation line must go **before** Chromium is up.
- The page will later carry the annunciators and red-X flags; both layouts must keep working. The Menu row is independent of that.

## Decisions needed from David

1. **Flow.** Choose, then confirm, then a trial with "Keep?" and an automatic revert after 15 s; only Keep saves (suggested). Or
   confirm and save immediately?
2. **Revert time.** 15 s (suggested)?
3. **While flying.** Refuse an orientation change when the airspeed is above about 30 kt (suggested)?
4. **Which orientations.** Portrait and Landscape only, or also the flipped ones (portrait upside down, landscape rotated the
   other way) for different mounting positions?
5. **Applying at boot.** Through `autostart` (suggested).

## Decisions (David, 2026-10-18)

1. Flow as proposed: choose, confirm, then a trial with "Keep?"; only Keep saves.
2. Revert time 15 s.
3. Refuse a change while flying (airspeed or ground speed above 30 kt).
4. All four rotations: portrait, landscape, portrait flipped, landscape flipped (`wlr-randr` transforms normal, 90, 180, 270).
5. Apply the saved orientation at boot through the compositor's `autostart`.

## Status

- **Built and deployed** (2026-10-18): `display_orientation.py` 1.0.1 (stdlib only), `aio_server.py` 1.3.0 (commands `orientation_request` /
  `orientation_keep` / `orientation_revert` from the controller only, `display` facts in the JSON, `monitor_orientation` task),
  page 0.3.0 (`orientationPanel.mjs`, an action row in `menuOverlay.mjs`, reload when the window changes between portrait and landscape
  shape), `copytopiefis.sh` sends `display_orientation.py`. Saved setting: `~/EFIS/display_orientation.json`. Boot: the first line of
  `~/.config/labwc/autostart` runs `wlr-randr ... --transform "$(python3 ~/EFIS/display_orientation.py --transform ...)"` (the previous
  autostart is `autostart.bak_before_orientation`; the copy in `EFIS/linux/labwc/` was updated).
- **Tests:** 27 unit tests of the module (`tests/test_display_orientation.py`, run anywhere; 14 deliberate breaks caught) and 4 server
  glue tests (`tests/test_server_clients.py`; 5 breaks caught). A test rig on the Mac (fake server using the real module plus headless
  Chrome driven over the DevTools protocol) checked the screens: choose, confirm, Keep (saved), no press (reverted after 15 s, nothing
  saved) and refused at 60 kt ("Not while flying").
- **On the Pi with David:** all four orientations tried, Revert and Keep, touch on every panel, the timeout revert, and a reboot with
  landscape kept (comes up in landscape).
- A test caught one real flaw in the module: a failed revert was retried at the next 0.2 s tick instead of after 5 s (fixed in 1.0.1).
- Rollback: the previous server is `~/EFIS/backup_20261018_server120/aio_server_1.2.1.py`; delete `display_orientation.json` to return
  to portrait; the previous autostart is `~/.config/labwc/autostart.bak_before_orientation`.
- Viewers do not get the Orientation row (they cannot send commands anyway).

