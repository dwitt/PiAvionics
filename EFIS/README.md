# PiAvionics EFIS

This is my project to create a primarily python based avionics system for experimental aircraft.

This project is inspired by http://experimentalavionics.com and by https://customgt6.com/2021/03/28/custom-digital-gauge-cluster/.

The hardware is based on various Feather boards running CircuitPython that can be found at Adafruit (https://www.adafruit.com).

## Viewing the display from another computer

The server accepts more than one display page. The page on the Pi itself (localhost) is the controller: its encoder, QNH and brightness
control the aircraft. Any other page is a read-only viewer (up to 4) and cannot change anything.

- On a laptop on the same network open `http://pi-efis.local:8080` (or `http://192.168.5.110:8080`). A "VIEW ONLY" tag shows in the corner and
  the QNH and brightness follow the Pi's. The page sizes itself to the window: about 720 px wide gives the Pi's proportions.
- To try page changes with the live data without deploying: serve the EFIS folder on the computer (the Pi renames `support/pixi-v8.6.6.mjs`
  to `support/pixi.mjs`, so the folder needs that name too) and open `http://127.0.0.1:<port>/?server=192.168.5.110:8080`. `?view=1` forces
  viewer mode.
- After changing page files on the Pi, reload its browser with ctrl-shift-R (a plain ctrl-r can use cached scripts). The page starts from
  the QNH and brightness the server last received.
- `tests/viewer_probe.py <host:port> [--try-command]` connects as a viewer from the command line and checks that commands are ignored.

Design and decisions: `docs/MULTI_CLIENT_PLAN.md`.

