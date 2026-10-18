# PiAvionics EFIS

This is my project to create a primarily python based avionics system for experimental aircraft.

This project is inspired by http://experimentalavionics.com and by https://customgt6.com/2021/03/28/custom-digital-gauge-cluster/.

The hardware is based on various Feather boards running CircuitPython that can be found at Adafruit (https://www.adafruit.com).

## Viewing the display from another computer

The server accepts more than one display page. The page on the Pi itself (localhost) is the controller: its encoder, QNH and brightness
control the aircraft. Any other page is a read-only viewer (up to 4) and cannot change anything.

- On a laptop on the same network open `http://pi-efis.local:8080` (or `http://192.168.5.110:8080`). A "VIEW ONLY" tag shows in the lower left of the HSI and
  the QNH and brightness follow the Pi's. The page sizes itself to the window. Add `?pi=1` (`http://pi-efis.local:8080/?pi=1`) to lay it out at the
  size of the Pi's own screen (576 x 1024 CSS pixels: 720 x 1280 at the compositor's 1.25 scale) whatever the window size.
- To try page changes with the live data without deploying: serve the EFIS folder on the computer (the Pi renames `support/pixi-v8.6.6.mjs`
  to `support/pixi.mjs`, so the folder needs that name too) and open `http://127.0.0.1:<port>/?server=192.168.5.110:8080`. `?view=1` forces
  viewer mode.
- After changing page files on the Pi, reload its browser with ctrl-shift-R (a plain ctrl-r can use cached scripts). The page starts from
  the QNH and brightness the server last received.
- `tests/viewer_probe.py <host:port> [--try-command]` connects as a viewer from the command line and checks that commands are ignored.

Design and decisions: `docs/MULTI_CLIENT_PLAN.md`.

## Display orientation and touch on the Pi

The Pi's screen (Raspberry Pi Touch Display 2, panel 720 x 1280, scale 1.25) runs the kiosk browser under the labwc compositor
(`piefis-labwc.service`). Copies of its user config are in `linux/labwc/` (installed at `~/.config/labwc/`).

- **Switching from the Menu (page 0.3.0, server 1.3.0):** Menu, Orientation, choose Portrait, Landscape or a flipped version, press
  Switch. The screen rotates and the page reloads in the matching layout, then asks "Keep ...? Reverting in 15 s". Keep saves it
  (`~/EFIS/display_orientation.json`, applied at boot by the first line of `~/.config/labwc/autostart`); Revert, or pressing nothing,
  goes back to the saved orientation. The countdown is kept by the server, so it reverts even if the touch or page is unusable. A change
  is refused while airspeed or ground speed is above 30 kt, and only the Pi's own page can change it (viewers do not see the row).
  Design and tests: `docs/ORIENTATION_PLAN.md`.
- **By hand over ssh** (the display session variables are needed): `export XDG_RUNTIME_DIR=/run/piefis WAYLAND_DISPLAY=wayland-0`, then
  `wlr-randr --output DSI-1 --transform 90` (`normal`, `90`, `180`, `270`), then reload the page. This is a trial only and is not saved.
  To go back to portrait for good, delete `~/EFIS/display_orientation.json` and restart `piefis`.
- The page picks its layout when it loads (portrait: stacked; landscape: side by side) and reloads itself when its window changes
  between the two shapes (also on a laptop viewer).
- Touch follows the rotation because `rc.xml` maps the panel to the output:
  `<touch deviceName="10-005d Goodix Capacitive TouchScreen" mapToOutput="DSI-1"/>`. Tested in portrait and landscape (2026-10-18).
  Do not also set a libinput `calibrationMatrix`: labwc already rotates mapped touches, so the matrix rotates them twice.
- `rc.xml` must be one valid XML document with a `<labwc_config>` root. Several top-level elements are rejected as a parse error
  and labwc silently uses its defaults (see `sudo journalctl -u piefis-labwc | grep "rc.xml\|parser"`). Changes to the touch and
  input settings need `sudo systemctl restart piefis-labwc` (the display blanks for a few seconds); a `kill -HUP` reload did not apply them.
