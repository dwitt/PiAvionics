# Plan: more than one display client (view the EFIS from a laptop)

Written 2026-10-18. Decisions made 2026-10-18 (below); steps 1 and 2 built, step 3 (the page) still to do.

## Goal

Open the EFIS page from another computer on the local network (for example a laptop) and see the same live display, **without
disturbing the display on the Pi** and without being able to change the aircraft's settings by accident. It also gives a way to
develop and test display changes with live module data while the Pi's own screen carries on unchanged.

## How it works today (checked in `aio_server.py` and `efis-display.js`)

- The server listens on all network addresses, port 8080 (`web.TCPSite(runner)`), and serves `index.html` and `/support/`. The
  page connects back to `ws://<same host>/ws`, so a laptop can already load and run the page.
- `MyWebSocketResponse` holds **one** websocket (`self._ws`). When a new client connects the handler **closes the old one**
  (`handler()`), and `send_json()` sends only to `self._ws`.
- The page reconnects 1 s after a close (`WEBSOCKET_RECONNECT_MS`). So two pages would keep knocking each other off.
- The page **sends state back** to the server: `{qnh, ticker, brightness}` (continuously, and at least every `CAN_QNH_PERIOD`),
  plus commands from the route overlay (`active_leg`, `direct_to`, `cancel_direct_to`).
  - `qnh` goes onto the CAN bus to the AIR module, so it changes the altitude the AIR module computes.
  - `brightness` is written to the Pi's backlight.
  - The route commands change the active leg or Direct-To.
- The page holds the QNH and brightness values itself (`qnhDisplay.value`, `brightness.value`), changed by the encoder position
  that the server sends in every message (`position`, `pressed`). A freshly loaded page starts with 29.92 inHg and 100%.

## What would go wrong today if a laptop opened the page

1. The Pi's display and the laptop would take turns losing their connection (a blink about once a second).
2. The laptop page would send its default QNH (29.92) and brightness (100) to the server, which would overwrite the aircraft's
   altimeter setting on the CAN bus and the Pi's screen brightness.
3. Its route-overlay buttons could change the active leg.

So a second page must be **view-only**, and it must show the Pi's real QNH and brightness, not its own defaults.

## Design

### Server (`aio_server.py`)
1. **A set of clients** instead of one websocket. Each connection is added when it opens and removed when it closes. A new client
   no longer closes the others.
2. **Roles.** A connection is the **controller** if it comes from this computer (`request.remote` is `127.0.0.1` or `::1`: the Pi's
   own Chromium). Every other connection is a **viewer**. Only the controller's messages are processed (`process_json_data`);
   a viewer's `json...` messages are ignored (a viewer may still send `ready` and `close`). Optional later: `?control=1` to let
   a trusted laptop act as a controller (decision 1).
3. **Broadcast.** `send_json()` sends the same JSON to every open client. Each send gets a short timeout (about 0.5 s) and
   runs without waiting on the others, so a slow or half-dead laptop connection cannot delay the Pi's display. A client that
   fails or times out repeatedly is dropped.
4. **Controller state echoed to viewers.** The server remembers the last `qnh`, `brightness` (and `ticker`) the controller sent and
   adds them to the JSON as `controller_state: {qnh, brightness}`. Viewers use it to show the real values.
5. **Display connected.** The planned 0x700 heartbeat bit 3 ("a display is connected") counts the controller only. The server
   also exposes `clients: {controller: n, viewers: n}` in the JSON for the status page.
6. Log (not print) when a client connects or disconnects, with its role and address, so the journal shows who is watching.

### Page (`efis-display.js`, `userInput.mjs`)
1. **Viewer mode** when the page is not on the Pi's own address. Detected from `location.hostname` not being `localhost` /
   `127.0.0.1`, or forced with `?view=1`. In viewer mode the page:
   - sends nothing to the server (no `json...` messages);
   - ignores the encoder `position` / `pressed` stream (does not run its own selection or adjustment logic);
   - takes the QNH and brightness values from `controller_state`;
   - disables its own touch controls that would send commands (route overlay buttons), or leaves them harmlessly inert;
   - shows a small "VIEW ONLY" tag in a corner.
2. **Server address.** `?server=host:port` overrides the websocket host, so the page can run from the Mac's files (served by a
   local web server) against the Pi's live data. Default stays `location.host`.
3. The viewer sizes itself to its window as the Pi does (portrait: top half primary and bottom half HSI; landscape: left and right).
   A laptop window about 720 wide and 1280 tall, or a mobile emulation of that size, gives the true layout.

### Not changed
- Encoder reading, CAN handling and `module_status` stay as they are.
- The Pi's own browser keeps working exactly as today (it is the controller).

## Order of work

1. **Server: clients set, roles, broadcast with timeouts, controller_state, view-only enforcement.** Unit/glue tests with fake
   websocket clients (aiohttp test client on the Pi under `~/EFIS/venv/bin/python`):
   - two clients both receive the stream;
   - the second connection does not close the first;
   - a viewer's `json` message (QNH, brightness, `active_leg`) changes nothing; the controller's does;
   - the controller reconnecting does not drop viewers;
   - a client that never reads does not slow the others (the send loop keeps its rate);
   - disconnecting removes the client; no growth over repeated connect/disconnect.
2. **Deploy to the Pi** (backup first; display blanks for a few seconds), check the journal, then open the page from the Mac
   in a normal browser: confirm the Pi's screen is undisturbed and its QNH and brightness did not change.
3. **Page: viewer mode, `controller_state`, VIEW ONLY tag, `?server=`.** Test from the Mac against the Pi's live data.
4. **Document** in `EFIS/README.md` and the setup guide how to open a viewer, and update `MODULE_STATUS_PLAN.md` (the display work
   can then be tested from the Mac).

Step 1 and 2 alone make a laptop safe to use (it will show the data, but with its own QNH/brightness boxes until step 3).

## Risks and notes

- **Network exposure.** There is no password. The server already listens on all addresses; with view-only enforcement anyone on the
  network can watch but cannot change anything. If the Pi is ever on a shared network in the aircraft, consider an allow-list or
  binding to one interface. Not needed for bench use.
- **Load.** The JSON is about 2 KB at 20 Hz per client, about 40 KB/s each. A few clients are no problem for the Pi (the server uses
  about 6% of a core), but the Pi's Wi-Fi/Ethernet is shared with the display's own work. Suggested limit: 4 viewers.
- **Reconnect storms.** A viewer on a poor network reconnects every second (the page's rule); each connect is cheap.
- **Localhost detection** relies on the kiosk Chromium connecting to `localhost:8080`. If it is ever changed to the Pi's host name
  the page would be treated as a viewer; the `?control=1` override or a configured list covers that.
- **Existing quirk (not part of this plan):** when the Pi's own page reloads it starts with QNH 29.92 and sends it to the bus. The
  `controller_state` memory could later restore the last QNH to a reloaded controller.

## Decisions needed from David

1. **Who is the controller?** Only the Pi's own browser (suggested), or also allow a trusted laptop with `?control=1`?
2. **Viewer extras.** A "VIEW ONLY" tag on viewers (suggested)?
3. **Limit.** Maximum number of viewers (suggested 4)?
4. **Access.** Open to the whole network as it is now (suggested for the bench), or restrict to certain addresses?
5. **Order.** Do steps 1 and 2 now, before the display work (suggested), and step 3 right after, so the display work can be tested
   from the Mac?

## Decisions (David, 2026-10-18: "I agree with the proposal")

1. Only the Pi's own browser (localhost) is the controller. No `?control=1` for a laptop.
2. Viewers show a small "VIEW ONLY" tag.
3. At most 4 viewers; more are refused with HTTP 503.
4. Open to the whole network as it is now (bench use). Revisit if the Pi is ever on a shared network in the aircraft.
5. Do steps 1 and 2 (server, then deploy) before the display work, and step 3 (the page's viewer mode) right after.

## Status

- **Step 1 built** (`aio_server.py` 1.2.0): a set of clients; role by address (`is_local_address`); a new controller replaces the old
  controller but viewers stay; viewers' `json` messages are ignored; `controller_state` {qnh, brightness} echoed to everyone;
  `clients` {controller, viewers} in the JSON; each client's send is its own task with a 0.5 s timeout (a stuck client is skipped and
  then dropped, nobody waits for it); connect / disconnect / refusal lines in the journal. One existing quirk was also fixed: a route
  command (`active_leg`, `direct_to`) used to send a default QNH of 29.92 to the CAN bus because the message carried no `qnh`; QNH is now
  processed only when the message has one.
- **Tests** (`EFIS/tests/test_server_clients.py`, 12 tests, run on the Pi with `~/EFIS/venv/bin/python -m unittest discover -s tests`):
  two clients both served; viewer commands ignored; controller commands applied and shared; route command without QNH; QNH clamped; a new
  controller replaces the old one but viewers stay; viewer limit; disconnects clean up; a stuck client does not slow the others and is
  dropped; address classification. Four deliberate breaks of the code (viewers allowed to command, no viewer limit, controllers not
  replaced, default QNH on route commands) were each caught.
