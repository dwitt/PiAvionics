# Resume here (last saved 2026-11-15)

Tell Claude: "Read RESUME.md and carry on."

## Update 2026-11-15 (late evening, latest): ALIGNING, IMU 1.0.19, backup display GPS icon (page 0.8.38, server 1.7.11, IMU_AHRS 1.0.19, Backup_Display 1.7.43, all on the Pi / boards)
- **State:** the Mac, the Pi, the boards and GitHub match. After a page deploy David presses ctrl-shift-R.
- **Feature 11, ALIGNING, done** (details in G5_REVIEW.md): amber ALIGNING over the attitude while the IMU starts and
  for 3 s after its attitude arrives; IMU_AHRS 1.0.19 sends the invalid marker and STARTING until its filter has run
  (its first frame used to be level / north / 0 G). Seen right on the Pi and the backup display with the IMU tilted.
- **Backup_Display 1.7.43:** the GPS page's satellite icon lost cells (the G and parts of the satellite) after
  coming from the Flight page: the horizon and slip ball kept their custom-character slots. Every handed-out slot is
  now freed on a page change. David confirmed it works.
- **IMU restart on the bench without reflashing** (for start-up tests): on the Pi, `stty -F <IMU tty> 1200; sleep 4;
  sudo picotool reboot`, then `stty -F <tty> hupcl`; record with `candump -t a can0,701:7FF,048:7FF,049:7FF`.
  A flash with flash_remote.sh compiles first (about 90 s), so start any recording after it.
- **Next: G5_REVIEW.md feature 12 (message history), proposed to David, waiting for his answers.** The G5 (guide 1.7):
  a [!] that flashes while a message is unseen; Messages in the Menu opens the list. Proposal: a Messages row in the
  Menu (value like "2 new") opening a list, newest first, of UTC time, text in its colour, and how long it lasted
  ("now" if current); a [!] that flashes while unseen, steady while a problem is current; entries for every
  annunciation start and clear, plus knob not answering, GPS fix lost / regained, and WMM2025 expiry (warn from six
  months before the end of 2029). Open questions for David: (1) keep the list on the server (survives a page
  reload, shared with viewers, logged to /var/log/piefis.log; recommended) or only in the page; (2) also keep it
  across a Pi restart (a file, last day or last 100); (3) [!] next to the Menu button or in the left corner as on
  the G5; (4) record short blips under 1 s too (recommended yes).
- Wind, the track marker and the ALIGNING settle time still need a flight.
- **Mockup tools are now in the repo** (they were only in a session scratchpad): `EFIS/tests/fake_feed_knob.py`
  (fake server with knob, plan, CDI scale, heading / track controls, `imu_starting` scenario, `EFIS_DIR=<copy>`)
  and `EFIS/tests/states.py` (headless Chrome steps and screenshots). Run with a venv that has aiohttp:
  `venv/bin/python EFIS/tests/fake_feed_knob.py --port 8097 &` then
  `venv/bin/python EFIS/tests/states.py OUT yaw=90 mag_track=102 shot:a scen:imu_starting shot:b`.

## Update 2026-11-15 (evening): track marker, bench simulator (page 0.8.37, headingIndicator 1.0.10, server 1.7.11, all on the Pi)
- **State:** the Mac, the Pi and GitHub match. After a page deploy David presses ctrl-shift-R.
- **Feature 10, track marker on the PFD heading tape, done** (details in G5_REVIEW.md).
- **Bench simulator** (`Sensors/Utilities/Bench_Sim/bench_sim.py`, a copy at `~/bench_sim.py` on the Pi): stands in
  for the AIR and GPS modules on CAN so the GPS and air-data features show on the bench. Unplug the real AIR and
  GPS modules first: it refuses to start if it hears them and stops if one appears. Heartbeats say version 9.9.9.
  Flies on the real IMU's heading (turn the IMU to steer) with a set wind (default 270/20), 100 KIAS, 3500 ft,
  starting 7 NM north of CYFD; altitude follows the QNH. Commands on stdin: `ias 90`, `alt 4500`, `vs 500`,
  `wind 300/25`, `hdg 45`, `hdg imu`, `q`. To drive it from the Mac, run it on the Pi behind a fifo:
  `mkfifo ~/bench_cmd; nohup sh -c "tail -f ~/bench_cmd | ~/EFIS/venv/bin/python -u ~/bench_sim.py" > ~/bench_sim.log 2>&1 &`,
  then `echo "ias 90" > ~/bench_cmd`. Do not `pkill -f` a pattern that is also in the ssh command line (it kills
  the ssh session). First run 2026-11-15: everything valid on the EFIS; the Pi's flight plan had CYQA active, so
  pick the CYFD -> VCALT leg in Route to see the CDI move.
- **Next:** feature 11 (done since, see above), then 12.

## Update 2026-11-15: CDI scale, knob I2C fixed properly (page 0.8.36, hsi 1.0.20, server 1.7.11, all on the Pi)
- **State:** the Mac, the Pi and GitHub match (pushed 2026-11-15 with David's go-ahead). After a page deploy David
  presses ctrl-shift-R.
- **Feature 9, CDI scale, done** (details in G5_REVIEW.md): automatic 1.25 NM within 30 NM of a route airport,
  else 5.00 NM; tap the box under DTK to pick 5.00 / 1.25 / 0.25 or Auto; a manual pick stays until Auto. David
  tried it on the Pi; the log shows each choice ("CDI scale X -> Y from the page").
- **The 50 kHz I2C change broke the EFIS:** after the 15:32 reboot the EFIS kept flagging no air data, no IMU and
  GPS dropouts while the backup display was fine. CAN on the wire was perfect (10 Hz, no gap over 0.18 s). Cause:
  the knob's I2C stopped answering (ETIMEDOUT on bus 1) and each read hung about 1 s on the server's asyncio loop,
  so CAN, the status checks and the page all stalled (py-spy: half the samples in the knob's I2C write; CAN socket
  dropped messages). Air data and attitude time out at 0.5 s, GPS at 3.5 s. Turning the knob woke it. The read
  errors were not logged (only with DEBUG).
- **Fixes (David agreed to both):**
  - Server 1.7.11: `read_input` runs in its own daemon thread (`start_knob_thread`); logs "Knob not answering"
    after 3 failed or slow (> 0.2 s) reads in a row and "Knob answering again"; retries every 0.25 s meanwhile.
    Tested on the Mac with a fake knob hanging 1 s per read: asyncio loop lag under 3 ms.
  - Bus 1 is now software I2C on the same pins (GPIO 2 SDA, 3 SCL, about 100 kHz), which handles the knob's clock
    stretching: `dtparam=i2c_arm=off` and `dtoverlay=i2c-gpio,bus=1,i2c_gpio_sda=2,i2c_gpio_scl=3,i2c_gpio_delay_us=2`
    in `/boot/firmware/config.txt`. Backups on the Pi: `config.txt.bak-hwi2c-20261115` (hardware I2C, 100 kHz, undo
    = restore it and reboot), `config.txt.50k-20261115` (the failed 50 kHz), `config.txt.bak-20261115` (original).
  - First half hour after: 34 QNH changes, no "not answering", no "knob jump ignored", no re-baselines. Keep
    watching those lines.
- **GPS on the bench at 15:50:** status message said no fix, 0 satellites (no position or speed sent). The module,
  not the Pi; check the antenna if it keeps happening where it used to get a fix.
- **Next:** feature 10 (done since, see above), 11, 12. Wind still needs a flight.

## Update 2026-11-15: G5-style needle and bearing pointer, brightness logging (page 0.8.35, hsi 1.0.19, server 1.7.9, all on the Pi)
- **State:** the Mac and the Pi matched (all pushed since, see above). After a page deploy David presses ctrl-shift-R.
- **HSI restyled to the G5's Figure 2-19** (details in G5_REVIEW.md, feature 8): ring around the CDI, magenta
  head and tail outside it, deviation bar inside, cyan double-line bearing pointer that outlines the magenta
  needle when the two line up. David chose each step from Mac mockups. He has not yet seen it on the Pi with a
  flight plan and GPS fix.
- **Brightness jump (0 to full overnight, 2026-11-13/14):** server 1.7.9 logs settled brightness changes ("Brightness
  X -> Y from the page"), knob presses of 1 s or more and the 3 s hold; page 0.8.35 logs its 3 s full-brightness
  hold with the age of the last message. The night of 2026-11-14 was clean (every change was David's). Leave the
  logging on; if it happens again read `/var/log/piefis.log` (Brightness / Knob / Page lines) and
  `/sys/class/backlight/10-0045/brightness`.
- **Knob I2C glitches** ("knob jump ignored" a few times a day): the bad readings are the real position with its
  bits shifted right (55 -> 27, 59 -> 29, 67 -> 1), the Pi 4's known I2C clock-stretching bug with the knob's
  ATtiny817 seesaw (Adafruit's firmware has nothing that would do it). The bus runs at the default 100 kHz (the
  400 kHz in `aio_server.py` has no effect on the Pi). Proposed, not done: `dtparam=i2c_arm_baudrate=50000` in
  `/boot/firmware/config.txt` and a reboot (Adafruit's advice). **Done 2026-11-15 11:46** (David agreed; bus 1 has
  only the knob, the display's touch and backlight are on bus 10, CAN is SPI); backup
  `/boot/firmware/config.txt.bak-20261115`. Watch the "knob jump ignored" lines: if they go on, try 10 kHz. Also
  noticed: the seesaw library sleeps 8 ms per read on the server's main loop (about 16 ms of every 36 ms poll);
  could move the knob reads to a thread.
- **Test feed** (scratchpad `knob/fake_feed_knob.py`) now also takes `dtk=` and `xtrack=`; `EFIS_DIR=<copy>` serves a
  scratch copy of the page for mockups.
- **Next:** (David has seen the new HSI on the Pi: good) G5_REVIEW.md feature 9
  (CDI scale 5.00 / 1.25 / 0.25 NM; note the dots are now about 34 px apart), 10, 11, 12. Wind still needs a flight.

## Update 2026-11-14: bearing pointer (page 0.8.34, hsi 1.0.18, server 1.7.8, all on the Pi)
- **State:** GitHub, the Mac and the Pi match. After a page deploy David presses ctrl-shift-R.
- **Feature 8, bearing pointer, done:** cyan pointer to the active waypoint in the outer band of the rose (details
  in G5_REVIEW.md). Open question from David ("why this style?"): the colour and ring follow the Garmin convention,
  the shortened length is Claude's choice; offered to check the G5 guide figure and match it (full length across
  the rose). Wait for his view after seeing it on the Pi.
- **Test feed** (scratchpad `knob/fake_feed_knob.py`) now sends `bearing` (control `bearing=`, default 98).
- **Next:** the bearing pointer style if David wants changes; then G5_REVIEW.md feature 9 (CDI scale 5.00 / 1.25 /
  0.25 NM), 10 (track marker on the PFD heading tape), 11 (ALIGNING), 12 (message history). Wind still needs a
  flight to check.

## Update 2026-11-14 (late): wind box and GPS box on the HSI (page 0.8.33, hsi 1.0.17; since updated, see above)
- **State:** GitHub, the Mac and the Pi match (server unchanged, 1.7.8). After a page deploy David presses
  ctrl-shift-R.
- **Feature 7, wind, done** (G5_REVIEW.md has the details): box at the HSI top left. Closed: arrow, direction from
  (magnetic), speed; a touch opens HEAD/TAIL and XWIND (David's choice: option 1 opening to both). Worked out on
  the page in `windEstimate.mjs` 1.0.0 (fed from `efis-display.js`); dashes on the bench (below 30 kt). Checked
  with the fake feed against a hand calculation (297 deg / 50 kt). Not flight tested: expect error from IAS taken as
  CAS and heading error.
- **GPS box** now lower right above the Route button, fix row at the bottom, grows upward, scalloped to the HSI
  circle with an 8 px gap and rounded corners (David asked for each of these).
- **Corner standard** (David agreed): 8 px stand-alone boxes, 4 px rows / buttons inside panels.
- **Next:** feature 8 (bearing pointer), done since (see above).

## Update 2026-11-14 (end of day): kt / ft labels, wind research (wind since built, see above) (page 0.8.32, server 1.7.8, all on the Pi)
- **State:** GitHub, the Mac and the Pi match. No `aircraft_settings.json` on the Pi yet (defaults from the manual
  until the first save from Menu > Airspeed Limits or V-Speeds). After a page deploy David presses ctrl-shift-R.
- **kt / ft labels (settled with David after several tries):** both inside the foot of their tape, on the side
  towards the attitude display, level with each other just above the OAT box line (`placeUnits` in
  `airSpeedWheel.mjs` 1.0.10 and `altitudeWheel.mjs` 1.0.6, called from `efis-display.js`). White with the usual
  black outline, drawn over the tape, so a tape number passing goes behind them (David: missing a tape number does
  not matter, the boxes matter). No box behind them (tried, he did not want it). The V-speed tags no longer pass
  them. Dropped: the labels beside the readout boxes, the tags in front of "kt", and an outline-only "kt" masked
  over the tags.
- **Feature 7, wind: decided on the HSI only** (not the PFD). Research (manuals): G5 HSI page top left; G3X a
  window upper left of the HSI ("No Wind Data" when invalid); G1000 / G900X / G500 left or upper left of the HSI
  with options (components with arrows / arrow + speed / arrow + direction + speed); GI 275 on the attitude
  display above the heading; Dynon SkyView on the PFD (place not found). PiEFIS's HSI top left has the GPS status
  box.
- **Next, waiting on David:** where on the HSI (Claude's suggestion: top left as the G5 / G3X, GPS box moved to
  the bottom left, growing upward when touched; offered a mock-up first) and what it shows (arrow + direction and
  speed, components, or both). Plan from G5_REVIEW.md feature 7: wind = ground vector (GS, magnetic track) - air
  vector (TAS, magnetic heading), direction magnetic as the rose; average about 10 s; hide below about 30 kt GS /
  TAS, while the heading is gyro-only or not valid, without valid TAS or GPS; hold (not update) while turning more
  than about 2-3 deg/s. TAS is `tasDisplay.trueAirSpeedValue` (valid.trueAirspeed). A 5 deg heading error at 100 kt
  gives about 9 kt of false crosswind, and IAS is taken as CAS, so expect some error.

## Update 2026-11-14 (late night): feature 6, V-speed tags (page 0.8.28, server 1.7.8, all on the Pi)
- **Done** (tried on the Mac with screenshots, then deployed; the service was restarted):
  - Tags (`airspeedRibbon.mjs` 1.0.4): cyan R / X / Y / G in small black flags just right of the speed tape,
    pointing at Vr 52, Vx 70 (the manual's climb with obstacles), Vy 74, Vg 61 kt; they scroll with the tape,
    under the readout box. With no valid airspeed they are hidden and listed (Vy 74 ... Vr 52) at the foot of the
    blanked tape, above the OAT box.
  - Menu: "V-Speed Tags" On/Off (kept per browser, like Slip Cue, so viewers have it too) and "V-Speeds" (controller
    only): the limits panel made generic (`airspeedLimitsPanel.mjs` 1.0.1, `AIRSPEED_LIMITS` / `V_SPEEDS`).
  - `aircraft_settings.py` 1.0.3: a `vspeeds` group beside `airspeed`; each group checked on its own (a missing or
    bad group gets its defaults, the other is kept), `change(group, speeds, speeds_kt)`. Server command is now
    `{"aircraft_settings": {"group", "speeds"}, "request_id"}` (page and server must go up together).
  - Still no `aircraft_settings.json` on the Pi (defaults) until the first save from a page.
- **kt label and the tags:** superseded, see the end-of-day entry above (labels now at the foot of the tapes).
- **PixiJS v8 gotcha found:** a Graphics used as a mask but not on the stage never has its position updated
  (fine for a fixed mask like the tapes', not for a moving one); add it to the stage, it is not drawn as a mask.
- **Next:** feature 7, wind on the HSI (see above).

## Update 2026-11-14 (night): Menu > Airspeed Limits page (page 0.8.24, server 1.7.7, all on the Pi)
- **Done** (tried on the Mac with screenshots, then deployed; the service was restarted):
  - `support/airspeedLimitsPanel.mjs` (new, 1.0.0), a "Airspeed Limits" row in the Menu (controller only, as
    Orientation; David chose not to show it on remote displays, even read-only). The five speeds on the left, an
    on-screen keypad on the right (0-9, Del, Next); a USB keyboard works too (digits, Backspace, Tab / arrows,
    Enter to the next speed and on Vne to the confirm, Escape). Same checks as the server shown in amber; Save
    only when valid and changed; confirm lists old -> new; then "saved" or the server's reason.
  - Ground only: airspeed and GS both 30 kt or less (as the orientation change); in the air the values are greyed
    and the keypad ignored. The server checks again (`AircraftSettings.change_airspeed`, aircraft_settings.py
    1.0.2, tested), saves in a thread, logs each change ("vfe 87 -> 85") and answers in `aircraft_save`.
  - On the Pi there is still no `aircraft_settings.json` (defaults) until the first save from the page. The page
    needed a ctrl-shift-R after the deploy (could not send it over SSH: no Wayland socket for wtype).
- **Next:** feature 6, V-speed tags (Vr 52, Vx 70, Vy 74, Vg 61 kt from the manual), kept with the limits and
  edited on the same page or one beside it. To decide: which tags (also approach / short field?) and an on/off.
  Then feature 7, wind on the HSI.
- **Testing on the Mac:** the scratch fake feed now saves `airspeed_limits` through the real `AircraftSettings`
  (temp file) and has `gs=`; `states.py` has `key:NAME` steps (CDP key events).

## Update 2026-11-14 (late): feature 5, airspeed limits kept on the Pi (page 0.8.23, server 1.7.6, all on the Pi)
- **Done** (tried on the Mac with screenshots, then deployed; the service was restarted):
  - Airspeed limits from David's Sonex flight manual (`Documents/_A-Aviation/Sonex/01 Flight Manual & POH/Flight
    Manual Conversion/Sonex Flight Manual - searchable.pdf`; mph CAS, converted to knots): Vso 35, Vs1 40,
    **Vfe 87** (was 99), Vno 108, Vne 171; also Va 109, Vy 74, glide 61, rotate 52 kt (for feature 6 tags). The
    IAS-to-CAS and stall tables on page 32 are blank (to be filled from flight test), so IAS is taken as CAS.
    David is converting the manual to knots; limits are entered in knots.
  - `aircraft_settings.py` (new, 1.0.1, tests in `tests/test_aircraft_settings.py`): the limits in
    `~/EFIS/aircraft_settings.json` on the Pi (not copied by `copytopiefis.sh`; no file yet, so the defaults
    above are used and the log says so). Checked (numbers, in range, vso <= vs1 < vno < vne, vso < vfe <= vne;
    a bad file is ignored as a whole), saved atomically with a `.bak`. Server 1.7.6 sends them in every message
    (`aircraft`) and reloads within 2 s of a change, so for now they are changed over SSH.
  - Feature 5 (`airspeedRibbon.mjs` 1.0.3, `airSpeedWheel.mjs` 1.0.3): green Vs1-Vno, yellow Vno-Vne, red line at
    Vne, red/white barber pole above Vne, white arc Vso-Vfe on the right half, and below Vso a thin red line on the
    same side (David's choice, not the G5's full-width red band). Readout box red at or above Vne; digits amber
    (caution colour) when the trend vector reaches Vne.
- **Next:** step 2, Menu > Aircraft > Airspeed Limits: on-screen numeric keypad (first use of the keypad in
  `EFIS/docs/MENU_SYSTEM_PLAN.md`), a USB keyboard accepted too, knots, editing only on the ground (IAS < 35 kt
  and GPS GS < about 20 kt; values shown greyed in the air), the same checks, an old -> new confirm, saved by the
  server. Then features 6 onward (V-speed tags next).
- **PixiJS v8 gotcha found:** `Graphics.setTransform` is the drawing transform and `clear()` resets it; set it
  again after a clear (the colour strips draw in knots through it).

## Update 2026-11-14 (evening): feature 4, knob home, OAT and UTC (EFIS page 0.8.22, all on the Pi)
- **Done** (tried on the Mac with screenshots, then deployed):
  - Feature 4, airspeed trend vector (`airspeedRibbon.mjs` 1.0.1): light magenta line right of the colour strip
    from the pointer to the speed 6 s ahead. Speed and rate each smoothed (1 s); hidden under 2 kt and when the
    airspeed is not valid. David cannot check real pitot noise; on the Mac it stayed hidden with +/-1 kt of noise
    at 60 kt (about three times the expected pitot noise). Raise the threshold if it flickers.
  - Knob (`userInput.mjs` 1.0.9): the home position (after 10 s untouched) and start-up are now QNH **selected,
    not edited**, so a turn by mistake only moves the selection (David's choice). Push and turn to set baro; QNH
    sync from GPS needs a push first. The 3 s hold still leaves brightness in edit mode.
  - OAT and UTC both shown (`tempTimeDisplay.mjs` 1.0.2), mirroring the top of the speed tape: square OAT box
    over the bottom of the tape, UTC box (rounded bottom corners) below it. UTC only, no local time and no switch
    (David: ATC uses UTC, local is on his watch). `--:--` until the GPS gives a time. Brightness stays bottom
    left. OAT/time is off the knob: QNH, brightness, heading bug, altitude source, selected altitude.
- **Next:** feature 5 (done, see above).
- **Testing on the Mac:** the scratch fake feed now also has `rate=` (kt/s ramp), `noise=` (kt) and sends GPS UTC;
  still not in `EFIS/tests/fake_feed.py`.

## Update 2026-11-14 (later): G5 review features 1 to 3 done (EFIS page 0.8.20, server 1.7.5, all on the Pi)
- **Done** (each tried on the Mac with screenshots, then deployed):
  - Menu: Slip Cue (Both / Ball / Trapezoid), Modules, Orientation, Maintenance (test and tuning rows moved there).
  - "kts" is now "kt"; units otherwise kept; staying with Tahoma (B612 tried and rejected).
  - Feature 1, knob (`userInput.mjs`): returns to QNH in edit mode after 10 s untouched. Long press: at 1 s the value
    of the item being edited flashes if it can sync; let go before 3 s and it syncs (altitude bug to altitude rounded
    to 100 ft, heading bug to the current heading, QNH from GPS altitude on the ground only: IAS < 35 kt, 3D fix,
    VDOP <= 2.5). Held to 3 s, from anywhere: nothing syncs, brightness goes to 100 % in edit mode (to turn it down;
    home 10 s after letting go). Knob jumps over 20 clicks per update are ignored and logged.
  - Feature 2 (`selectedAltitudeDisplay.mjs`): square box under the GPS altitude box over the top of the tape, cyan
    with a bug symbol, dashes until set, cyan line up the tape while selected. Alerting as the G5: flashes 5 s within
    1,000 ft and within 200 ft; after capture, outside +/-200 ft the value turns the amber caution colour (as the QNH
    mismatch; David chose amber over the G5's yellow for one caution colour) and flashes 5 s, amber until back within
    180 ft. A new target resets it.
  - Feature 3, as the G5 (David's choice): one heading bug. PFD box at the far left of the heading tape (90 x 25,
    four rounded corners, cyan value and a horizontal bug symbol, `---°` until set); the HSI draws the same bug. The
    HSI CRS box and knob item are gone: with a flight plan a magenta DTK box, course needle and CDI show the plan's
    leg; with no plan there is no course needle. Long press on the heading bug: current heading (not DTK, so a
    heading to rejoin a track can be held).
  - TAS box (white) under the GS box, square, over the top of the speed tape; the GS/TAS switch is off the knob.
  - Knob order now: QNH, OAT/time, brightness, heading bug, altitude source, selected altitude.
  - Fixed: QNH jumped (about 40) on going back to QNH after a page reload: the first message and the reconnection
    rebase both took the encoder count. Probably the cause of the earlier 28.00 (a larger count hits the limit).
    Server logs QNH changes with source in `/var/log/piefis.log` (not the journal).
- **Next:** G5 review features 4 onward (`EFIS/docs/G5_REVIEW.md`, "Recommended features"): airspeed trend vector,
  speed bands, V-speed tags, wind, bearing pointer, CDI scale, track marker, ALIGNING, message history. Optional
  from feature 1: move rarely changed items off the knob into the Menu. Optional from feature 2: the selected
  altitude on CAN for the backup display.
- **Testing on the Mac:** the scratch fake feed (`knob/fake_feed_knob.py` in the session scratchpad, not in the repo)
  had controls added for altitude and a flight plan on/off; `EFIS/tests/fake_feed.py` has neither.

## Update 2026-11-13: EFIS cosmetic clean-up from the G5 review (in progress)
- **Where:** `EFIS/docs/G5_REVIEW.md`, section "Making the display look professional". Its status block lists what is
  done, how it was built and what is next. Everything is committed, pushed and deployed to the Pi (page 0.8.10).
- **Done:** colour code (bugs and selection cyan, GPS text light magenta `0xFF6AFF`), graded sky and ground,
  antialiasing with a 30 fps cap (defaults: cap on, PFD antialiased, HSI not), pitch ladder (75 degree scale, +/-20
  in level flight, 2.5 degree marks within +/-10), 3 px dark outline on text over the sky.
- **Next:** move the test controls (Airspeed Sweep, Encoder Debug, Frame Rate, PFD/HSI Antialias, 30 fps Cap) out of
  the pilot Menu to a maintenance page; then David decides the slip cue, "kt"/"NM" and units beside the readouts.
- **Also added to PLAN.md this round:** item 5a (14-segment status annunciator) and section H (tail logger and Pi
  EFIS logger).
- **Way of working David likes:** before/after screenshots on the Mac before anything goes to the Pi (headless Chrome
  at 576x1024, DPR 1.25, with `tests/fake_feed.py`); reload the Pi with ctrl-shift-R.

## Update 2026-10-27: alarms done, scope decided, open items are in PLAN.md
- **Read `PLAN.md` first.** It is the agreed list of open items (numbered, with the dropped ones) and the scope decisions. This file is the history.
- **Scope:** the backup display is only a backup, for both the Pi EFIS and the MGL EMS-2 (the primary engine monitor). The Pi EFIS gets no Engine page.
  `Protocol/engine_conversions.yaml` (1.1.1) and `engine_convert.py` are the reference for the backup display only. No request for the uAvionix UCP ICD
  (the serial research above stays as a note). The AV-20-E manual documents no usable serial output (pins 3 and 4, AV-XPORT, are "Reserved").
- **Backup display firmware 1.7.41** (flashed, git clean). Pages: Flight, GPS, Engine, Cylinders, System, Versions. The display starts with the PG field
  selected, so a turn changes the page. Engine page (psi, F): `OilP`, `OilT`, `RPM` in the first column, highest `EGT` and `CHT` (with the cylinder
  number) on rows 1 and 2, volts and amps right justified, `EMS` upper right. Cylinders page: `CYL` upper left, E and C rows for the four cylinders.
  GPS page: small satellite icon (five custom characters) with a small G, custom P and S borrowing slots 6 and 7, `TRK` under `ALT`. Flight page: `HDG`
  one character left.
- **Engine alarms (modelled on the EMS-2):** a caution shows `!` after the label, an alarm also flashes the value, alarms are inhibited for 10 s after
  power-up, and a knob push acknowledges a flashing alarm on the Engine or Cylinders page (a repeat alarm is new). Limits are in the sketch (bar and C):
  RPM caution 3800 and alarm 4000; oil P low alarm 0.7 bar and low caution 1.4; oil T caution 110 and alarm 116; CHT caution 216 and alarm 232; EGT alarm
  760 only (no caution, by decision, 2026-10-26). Volts and amps have no limits yet. Bench tested by David on OIL P with 3 and 4 parallel 120 ohm
  (caution and alarm and acknowledge all work). Not bench testable: the CHT and EGT alarms (no probes).
- **Known:** the low oil pressure alarm is not gated on RPM, so with the real sender on a stopped engine it flashes 10 s after power-up (PLAN item 1).
- Floating thermocouple inputs read about room temperature (0 C plus the cold junction); David chose not to add open-probe detection.

## Update 2026-10-22: the backup display is built, and every board has a watchdog
- **Backup display:** `Sensors/Backup_Display/Backup_Display.ino` 1.7.7, built and working on the bench: Flight, GPS and System pages, QNH over the
  bus, mini horizon and slip ball, status letters I A G E. Design record: `Sensors/Backup_Display/DESIGN.md`. Still open there: G on the Flight page
  (David undecided), the Engine page (MGL RDAC raw values), its own heartbeat, module versions on the System page. Not yet tried in an aircraft.
- **Firmware now:** IMU_AHRS 1.0.18, AIR_Module 1.0.39, GPS_Module 1.0.11, Backup_Display 1.7.7, all flashed. AIR 1.0.38 smooths the vertical speed over
  about 3 s and sends a steady value (the displays do no smoothing). Protocol 0.3.1 (docs regenerated).
- **Watchdog on all four boards:** the chip restarts if its main loop stops for 2 s, and an I2C transaction gives up after 25 ms. It starts at the end of a
  good `setup()`, so a failed start-up still halts with its blink code and heartbeat. The GPS module feeds it from `gpsService()` so its re-probing of a silent
  GPS is not cut off. Test commands: `!h` on the three sensors (fault injection) and `H` on the backup display hang the board on purpose; all four hang tests
  passed (frames stop for 2.7 to 4.7 s, then the board is back and logs "Restarted by the watchdog"). Set `FAULT_INJECT 0` for a flight build.
- The GPS module is back on the bus (the display no longer stands in for it). The first backup-display encoder was faulty and was replaced.
- Git is clean: the NHD-0420DZ datasheet PDF now lives in `Sensors/Backup_Display/` and the old HM2004A bench sketch is in `Legacy/CAN_Display_v1/lcd_test`.

## Where things stand (end of 2026-10-18)
- **On the Pi (all committed, git clean except David's untracked files):** server `aio_server.py` 1.6.3, `display_orientation.py` 1.0.1, `efis_heartbeat.py` 1.0.0,
  `module_status.py` 2.0.0; page `efis-display.js` 0.7.1. Firmware: IMU_AHRS 1.0.16, AIR_Module 1.0.35, GPS_Module 1.0.9. The Pi is saved in landscape
  (`~/EFIS/display_orientation.json`); server backups 1.2.1 to 1.6.2 are in `~/EFIS/backup_20261018_server120/`.
- **Done this session (details in the paragraphs below and in ISSUES.md):** laptop viewers and `?pi=` layouts; the Menu orientation switch with a timed revert; the whole failure
  display (red X flags, blanked tapes, annunciators above the slip ball, the Modules page with an EFIS row); the HSI following GPS and heading data; the module markers (AIR and
  IMU) and the EFIS heartbeat 0x700; magnetic declination from the GPS module and magnetic courses (ISSUES #15); the log rotation (ISSUES #14); the sharper Pixi drawing; look
  tweaks (wings, units, annunciator fit); Display 1 size fixes.
- **GPS display (last thing done):** the HSI's GPS box is a touch button (contracted: `3D FIX` coloured by HDOP, a green `WAAS` when corrections are in use, and `VAR 10°W`;
  expanded adds SAT, HDOP, VDOP); the track diamond is hidden below 10 kt (`TRACK_MIN_SPEED_KT` in `efis-display.js`). Confirmed by touch on the real Pi (ISSUES #16).
- **Open:** (1) the moving test of the track diamond and course pointer (ISSUES #15); (2) the backup display (built 2026-10-22, see above), then plan step 7 (the full system test
  table, including pulling a module's CAN cable and closing the Pi page to see the heartbeat mask bits clear); (3) ISSUES #18, `piefis.service` ordering against `can0.service`;
  (4) the AOA value is still always 0; (5) commands over CAN (ISSUES #9 steps 2 to 4: a command message, AIR zero, two-step reset; the Modules page is the natural home).

## State of the sensor modules: finished and committed
Firmware on the boards (all flashed, healthy, committed; 2026-10-22): IMU_AHRS 1.0.18, AIR_Module 1.0.39, GPS_Module 1.0.11 (the IMU and AIR send the invalid-data markers since 2026-10-18; all three have the watchdog since 2026-10-22).
Libraries: ModuleInfo 1.3.0, StatusLED 1.2.0, SensorHealth 1.0.0, MS4525DO 1.1.1, SSC 1.1.1, plus two locally patched
Adafruit libraries (`Adafruit_LSM6DS`: a failed read returns false; `Adafruit_MCP2515`: bounded send, `txFault()`,
`clearErrorFlags()`), each with `PATCH_NOTES.md`.
Libraries live once in `Sensors/Libraries/` and reach each sketch through relative symlinks in `<Sketch>/src/` with quoted
includes; the Arduino IDE 2.3.5 builds all three with no setup (David verified IMU and AIR). Do NOT symlink these into
`~/Documents/Arduino/libraries`: a Library Manager upgrade of a symlinked library that is in the public index deletes the
folder the link points to (tested). See `Sensors/Libraries/README.md` and `LIBRARIES.md` (third-party versions).

## Failure detection work (ISSUES.md #13): built, tested, logged
Plan, decisions, every test result and the filled-in test tables: `Sensors/FAILURE_DETECTION_PLAN.md`.
- Each module detects its own sensor failures, reports them in its heartbeat (0x701 IMU, 0x702 AIR, 0x703 GPS: state 1 OK,
  2 degraded, 3 failed, plus a sensors mask) and on the LED (green OK, amber degraded, red failed or CAN trouble).
- A CAN cable pull no longer freezes any module (cause was missing termination on the AIR and GPS CAN boards plus an
  unbounded wait in the stock MCP2515 library).
- Fault injection over USB serial is compiled in and stays in the flight build (decision: no USB in the aircraft). Commands start
  with `!`, capital = on, lower case = off: IMU `!G/!g` `!M/!m` `!z` `!x`; AIR `!P/!p` `!A/!a` `!S/!s` `!O/!o`; GPS `!N/!n`; `!h` hangs any of the three (watchdog test).
  Test scripts: `Sensors/TestTools/` (run on the Pi; see its README).
- Not done and not needed yet: hot-plugging a sensor into a running module, stuck-I2C-bus recovery on the AIR Wire bus,
  cold tests A6/I4/I6/G2/C3 (listed NOT RUN in the plan tables). IMU and GPS still show EFLG 0x40 (their CAN buffers are never
  read); harmless until they get a receive path.
- While a sensor is invalid the data frames use an interim form (airspeed 0, altitude held, VSI 0, attitude held, OAT frame
  stops, GPS position frames stop). The 0x7FFF markers wait for the EFIS side.

## NEXT: EFIS and backup display (decisions made, ready to build)
`EFIS/docs/MODULE_STATUS_PLAN.md` (committed) is the plan, and **David has made all ten decisions** (recorded at its end):
gyro-only heading stays under a red X for 5 minutes then blanks; red X look; attitude failure removes sky/ground with a red X;
annunciators at top centre under the heading ribbon (flash 5 s on first appearance); module status page from the Menu; OAT sends
the 0x7FFF marker; GPS frames stay stopped (EFIS timeout); timeouts 0.5 s / 1.5 s / 3.5 s; MODULE RESTARTED caution; EFIS
heartbeat 0x700 with a mask of healthy modules plus display connected.
Done: protocol 0.2.0 (markers, timeouts, 0x700 in `Protocol/can_messages.yaml`, docs regenerated); the annunciator/red-X mock-up
(`EFIS/docs/MODULE_STATUS_PLAN.md`, `EFIS/docs/mockups/annunciator_mockup.html`, approved); and server step A.
Decision 11: the server only tracks facts and the web page decides presentation.
Server step A (`EFIS/module_status.py`, `aio_server.py` 1.1.0, tests in `EFIS/tests/`) passed a fault-injection run against the real
modules on 2026-10-18 and is DEPLOYED to the live Pi EFIS (old files saved in `~/EFIS/backup_20261018/` for rollback: copy back and
`sudo systemctl restart piefis`). It adds `modules`, `valid`, `data_lost`, `heading_status`, `system_state` to the JSON; the current
display ignores them. Do not connect a test client to the live server's websocket (it holds one, for the display); use
`EFIS/tests/live_check.py` (CAN only). Use `~/EFIS/venv/bin/python` on the Pi, not the system python3.
Multi-client viewer support (David's request) is DONE and DEPLOYED (plan `EFIS/docs/MULTI_CLIENT_PLAN.md`): `aio_server.py` 1.2.1 and the
page (`efis-display.js` 0.2.2) are on the Pi (previous files saved in `~/EFIS/backup_20261018_server120/`, older ones in
`backup_20261018_server110/`). The Pi's own page (localhost) is the controller; any other page is a read-only viewer (max 4): open
http://192.168.5.110:8080 on a laptop, or serve the EFIS files on the Mac and use `?server=192.168.5.110:8080` (the Pi renames
`pixi-v8.6.6.mjs` to `pixi.mjs`, so a local test folder needs that name; `?view=1` forces viewer mode). A viewer shows the controller's QNH and
brightness (`controller_state`) and a "VIEW ONLY" tag. A reloaded Pi page now starts from the server's remembered QNH/brightness instead of
29.92/100 (page 0.2.2, tested on the Pi). Tested in Chrome from the Mac and Safari. The service's output is in `/var/log/piefis.log` (not the journal), see ISSUES #14.
Display orientation (2026-10-18) is DONE, deployed and tested on the Pi: Menu > Orientation (choose, confirm, then a trial with Keep/Revert
and a 15 s server-side revert; portrait, landscape and both flipped; refused above 30 kt; saved in `~/EFIS/display_orientation.json` and applied
at boot by `~/.config/labwc/autostart`). `display_orientation.py` 1.0.1, `aio_server.py` 1.3.0, page 0.3.0. Plan, decisions, tests and rollback:
`EFIS/docs/ORIENTATION_PLAN.md`; touch and rotation notes: "Display orientation and touch on the Pi" in `EFIS/README.md`; labwc config
copies: `EFIS/linux/labwc/`. David is seriously considering landscape for the aircraft panel; the display work must keep both layouts working
(the page picks its layout from the window shape and reloads when it changes). Bench note: all three modules are on the Pi's USB and restart
whenever the Pi reboots (their uptimes reset); the GPS needs a few minutes to regain satellites. David is aware and says it is fine.
Display step A (2026-10-18) is built and DEPLOYED (page 0.5.4), and David watched all seven faults on the real display ("It all looked good"):
red-X flags, blanked tapes, dimmed gyro heading, `---` readouts, annunciators above the slip/skid ball, NO DATA link state, the Menu >
Modules status page, and the HSI's GPS box, ground speed, track, CDI, TO/FROM, waypoint line and heading readout all follow the data
(blank when the GPS or heading is lost). Later the same day: Pixi now draws at the device pixel ratio (sharper; `?sharp=0` for the old way),
the wings are 33% shorter (`WING_SHORTEN`) and the kts/ft units 6 px lower, the annunciator text shrinks to fit between the tapes on a narrow
canvas, the Menu keeps a margin, and the HSI rose leaves room for the waypoint line on a short canvas. David is happy with the displays at
Display 2 (portrait and landscape) and checked Display 1 sizes (800x480 and 480x800) in Safari and Chrome. Page options: `?pi=true|portrait|
landscape` (Pi layouts), `?view=1`, `?server=host:port`, `?sharp=0`, and look overrides `xw xo fs labels` (see `EFIS/support/failureLook.mjs`).
Test log: end of `EFIS/docs/MODULE_STATUS_PLAN.md`. Tools: `EFIS/tests/fake_feed.py` + `page_shots.py` (need aiohttp in a venv) and `node --test tests/js`.
Page changes reach the Pi with rsync of `EFIS/support/*` then a reload (`wtype -M ctrl -M shift -k r -m shift -m ctrl` over ssh works).
ISSUES #15 is fixed and deployed (server 1.5.0, page 0.5.5, GPS_Module 1.0.9 flashed): the track diamond uses `mag_track`, the GPS module sends
its WMM2025 declination in 0x65 byte 3, and the server sends the desired track and bearing in magnetic degrees (held back until the declination is
known); `NULL_INVALID_VALUES` is on (untrusted values are sent as null). A moving test of the diamond and course pointer is still to do.
Previous servers are in `~/EFIS/backup_20261018_server120/` (1.2.1, 1.3.0, 1.4.0 and 1.5.0 copies).
Server step B is DONE and deployed (server 1.6.1 then, now 1.6.3, page 0.6.1, plan and status in `EFIS/docs/SERVER_STEP_B_PLAN.md`): invalid-data markers are decoded (a
marker becomes None and an invalid instrument, never a number), the EFIS heartbeat 0x700 is on the bus once a second (checked with candump), and the Modules
page has an EFIS row. Not yet tried on the real system: pulling a module's CAN cable and closing the Pi's display page, to see the heartbeat mask bits clear.
The modules now send the invalid-data markers (2026-10-18): AIR_Module 1.0.35 (airspeed on a pitot loss, altitude and vertical speed on a static loss, the OAT frame
keeps coming with a marker, the pressure message), IMU_AHRS 1.0.16 (heading, pitch, roll, turn rate and the accelerations on an accel/gyro loss; a lost magnetometer
still sends the gyro-only heading as a number). The GPS module sends none. Verified on the real bus with injected faults (`/tmp/fi.sh`, `marker_check.sh` on the Pi).
A bug found on the way, a thin red X drawn across the display when a marker nulled a value before the validity caught up, is fixed (ISSUES #17: server 1.6.2, page 0.6.1).
Log rotation is done (ISSUES #14): server 1.6.3 reports a repeating send failure once a minute, and `/etc/logrotate.d/piefis` (from `EFIS/linux/piefis.logrotate`) rotates
`/var/log/piefis.log` at 5 MB, keeping 4. The service ordering against `can0.service` is parked as ISSUES #18.
The backup display is now built (see the update at the top; this paragraph was written before it was: new design in Sensors/Backup_Display, design record in Sensors/Backup_Display/DESIGN.md; the old CAN_Feather/Display_Feather design is archived in Legacy/CAN_Display_v1; per-field validity from the
markers, listen to 0x700 to 0x703, a status row; the first heartbeat after a server restart reads state 2, so allow it a second). Only the Pi EFIS receives the markers today.
Other open items: AOA value on 0x29 is still always 0; command messages over CAN (ISSUES #9 steps 2 to 4).

Watch the bus from the Pi:
`ssh david@pi-efis.local` then
`candump -ta can0,701:7FF,702:7FF,703:7FF,48:7FF,28:7FF,2A:7FF,65:7FF`
Status query of a module: `stty -F <port> 115200 raw -echo hupcl`, write `?`, read.

Safety: stacked wings are not hot-unplugged except where David chooses to (he pulled the IMU wing live twice, and
re-insertion reboots the module). Cables, probes and antennas are fine. AIR pressure sensors share the Wire bus: pulling them
live risks a stuck bus (cold pulls were done instead).

## 2026-10-23: time sync moved, RDAC XF on the bench (Engine page planning)

**Done and committed:** TIME_SYNC moved from 0x19 to 0x66 (GPS_Module 1.0.12, Backup_Display 1.7.8, aio_server 1.7.1; protocol docs
regenerated; deployed to the Pi and checked on the bus, the backup and the EFIS time box both show UTC). Reason: 0x19 is MGL's
General Command. Bench test with David's real RDAC XF: `00 81` (request calibration) on 0x19 got a reply on 0x209; the same
frame on 0x29, 0x49 and 0x66 got none. The flash-write command (0xA0) was NOT tested.

**RDAC XF is on the bench bus** (address 32, SEL A and B off, frames 0x201 to 0x209). Manual: `~/Documents/_E-Equipment_Manuals/MGL Avionics/RDAC XF MAP/RDAC XF INSTALATION.pdf`
(text extract was in the session scratchpad, which is temporary). Baseline with nothing on the inputs: supply raw 685 (about 11.9 V),
cold junction 16 C, RPM 0, all analogue inputs about 3908 (open, pull-ups on), thermocouples about 0 uncompensated (add the cold
junction), MAP about 1522 raw, CURR ramps to about 1525 while open. DIP switches now: AUX1 and AUX2 PU on, OIP PU on, OIP PD off,
fuel P, coolant, fuel level 1 and 2 PU on, rest off. There is no OIL T pull-up switch (fixed 1500 ohm). Leave the unused pull-ups on
(steady open reading beats a floating one).

**Installation: AeroVee 2.1 (air cooled, gravity fuel, no coolant, no fuel pressure or level; sight gauge)**
- Manual: `~/Documents/Sonex/AeroVee_2point1_Manual.pdf`; CHT probe sheet in the saved site under
  `_P-Projects/Sonex_Website/saved-site/www.aeroconversions.com/support/instruction_sheets/CHT_Install.pdf`.
- Ignition: two Magnatrons (P-lead switched) plus electronic secondary. Tach options: alternator 6 pulses per rev (documented), or
  David's Littelfuse 55505-00-02-A Hall geartooth sensor on the flywheel if it has a ring gear (not in the manual, tooth count unknown,
  flywheel carries magnets so test for interference).
- Sensors David has: VDO oil pressure (ACV-P01-76, 10 to 180 ohm, to OIL P) and VDO oil temperature (ACV-P01-77, 10 to 180 ohm, to OIL T);
  MGL magnetic current sensor (Aircraft Spruce 11-10422; gain 140 = +/-24 A AWG14 folded, 85 = +/-12 A, 500 = +/-50 A; black wire to the
  RDAC reference ground which goes to the engine block; zero it with the sensor off the conductor); LM335 probe (two wire, to AUX1 with
  PU on; kelvin = volts x 100); a MAP sensor (the RDAC XF MAP internal one; probably unused). Thermocouples (type K or J and count): still
  to be checked. AeroConversions CHT probes screw to the head near the plug (#32 hole, 9/16 deep, #6 self-tapping screw), comparable to plug probes.
- Limits from the manual (for the Engine page): idle 800 to 900, cruise 3200 +/- 200, max 4000 rpm; oil T 160 min for take-off to 240 F max;
  oil P 10 psi min at idle, 100 max, 40 to 50 cruise; CHT 350 to 375 cruise, 420 (5 min) climb, 450 max; EGT max 1400 F.

**Next:** (1) David finds the thermocouples, tells me K or J and how many (EGT on TC1 to 4, CHT on TC5 to 8). (2) Bench-check OIL P with
a 100 ohm resistor to ground (expect roughly 10% of 3908 if the pull-up is 1000 ohm), then the real senders. (3) Work out the
raw-to-engineering conversions (VDO curves, LM335, current sensor scale and zero, MAP against the AIR static pressure). (4) Decide what
the backup Engine page shows. Not decided: G on the Flight page; backup heartbeat and versions; plan step 7 system test.

## 2026-10-24: thermocouple decision and RDAC wiring plan

**Decided:** type K for all 8 thermocouples: 4 EGT (one per exhaust pipe, pipe about 1-1/2 in diameter) on TC1 to TC4, 4 CHT on
TC5 to TC8. EGT probe still to choose and order: Sonex "Clamp Small, K Type" (ask Sonex for the clamp range) or Grand Rapids
Aircraft Spruce 11-09535 (1 to 2 in). Prefer 316 stainless with glass-fibre insulated leads (RDAC manual). The Blaze manual warns
against 310 stainless for 4-stroke EGT probes (the MGL bolt-in 11-05567 is 310). CHT probes: AeroConversions ACV-CHT-VW-K.
The AeroVee manual says do not trim the EGT probe wires but gives no reason (CHT wires may be trimmed).

**Primary engine monitor is the MGL Blaze EMS-2** (manual: `~/Documents/_E-Equipment_Manuals/MGL Avionics/Blaze EMS-2/Blaze EMS-2.pdf`);
the backup display Engine page is the backup. The Blaze reads the RDAC over CAN (CAN Low pin 12 purple, CAN High pin 13 pink).
With an RDAC its analogue 1 to 4 are OILT, OILP, AUX1, AUX2. Not known: what it transmits, or whether it reacts to our IDs. Plan:
one shared CAN bus (RDAC and Blaze added to the existing bus), then bench-sniff once David has the Blaze wired.

**RDAC connections (from the RDAC XF manual)**

| Terminal | Connect to | DIP switch |
|---|---|---|
| DB9 5 (and 9) GND | engine block, bonded to battery negative (not direct to battery) | |
| DB9 8 VIN | +12 V via switch and breaker | |
| DB9 6 / 7 | CAN+ (CANH) / CAN- (CANL) | |
| DB9 1 to 4 | not used | |
| TC1 to TC4 | 4 EGT, K, yellow + red - | |
| TC5 to TC8 | 4 CHT, K, yellow + red - | |
| OIL P | VDO pressure sender, single wire, case grounds via engine | OIPP PU on, PD off |
| OIL T | VDO temperature sender, single wire | none (fixed 1500 ohm) |
| AUX1 | LM335 OAT, + to AUX1, - to GND | AUX1 PU on |
| CURR | MGL current sensor green; red to RDAC supply, black to GND | none |
| RPM1 / RPM2 | Magnatron P-leads, 10 kohm in line, ballast OFF for a magneto (or the Hall pickup) | to be decided |
| FUEL P, COOL, FL1, FL2, AUX2, FF1, FF2, 5V | not used, leave open | pull-ups on |

CAN rules from the manual: 120 ohm at each end only, stubs under 20 cm, twisted pair (the RDAC is on the engine side of the
firewall, so daisy-chain the bus to it). Unchecked: which TC screw is + (check markings on the unit), and how the Littelfuse Hall
sensor connects to RPM (check its datasheet).

**Order of work:** (1) RDAC alone on the bench, LED flashing at 1 Hz, frames match baseline. (2) Resistor checks on OIL P and OIL T.
(3) Sensors one at a time: VDO senders, LM335, current sensor (zeroed), thermocouples. (4) Blaze on the bus, check it reads the
RDAC, sniff what it sends. (5) Engine install: EGTs, CHTs, then RPM.

## 2026-10-25: RDAC and Blaze bench tests (Blaze reads the RDAC)

**Blaze EMS-2 is on the shared bus with the RDAC and our modules.** It sent nothing in a 60 s capture including a power cycle (4269 frames, all known IDs, no 0x019), so it is receive-only as far as we can see. It does not disturb our IDs.

**The Blaze only reads the RDAC after Data is set to RDAC in each setup menu** (manual 11.14: there is no global enable). Before that its oil/volts/RPM readings were its own open inputs (an early claim that it was reading the RDAC was wrong). Set Data = RDAC plus the RDAC address in: analog channels (CH1 = RDAC OILT, CH2 = OILP, CH3 = AUX1, CH4 = AUX2), EGT/CHT, volts, RPM, MAP, current.

**Resistor and thermocouple checks (RDAC raw, 0x205 unless noted):**
- OIL P (1000 ohm pull-up): 120 ohm to GND reads 428 to 429 (expected 419), so pull-up about 973 ohm. Blaze with Sender VDO / 5 Bar shows 45 psi (about 3.1 bar, as expected). Open input: Blaze shows 558 psi (out of range, harmless).
- OIL T (fixed 1500 ohm pull-up): 120 ohm reads 297 (expected 290), pull-up about 1460 ohm. Blaze shows 65 C. Not checked against the VDO table.
- CURR with nothing connected floats (3908 steady now; ramped to about 1525 in the first baseline). Meaningless until the sensor is attached.
- K thermocouples (yellow +, red -) on TC1 and TC5 both read correctly (room temp = cold junction, warm fingertip rises about 10 C). Blaze mapping confirmed: TC1 to TC4 = EGT1 to 4, TC5 to TC8 = CHT1 to 4.

**GTR 205x COM radio, tailBeaconX TSO (research, not built):** the GTR 205x installation manual (on the Mac under Sonex/Manuals) documents RS-232 NMEA-style `$PGRMC` messages (9600 8N1, pins 5 out, 46 in, 26 gnd): C00 active freq, C01 standby, C02 volume, C06 request, status at 1 Hz. CAN pins are Garmin G3X only, do not wire to our bus. Plan idea: a Feather COM module (CAN + MAX3232) so both displays can tune it. The tailBeaconX TSO takes RS-232 2400 8N1, UCP-HD/Apollo, with the AV-20-E as control head (squawk is set there). Apollo `#AL` altitude format is public (Apollo SL70 manual, Appendix E); `#MD` and the UCP ICD (UAV-1002375-001, from uAvionix) were not found. The Blaze also has an RS-232 serial output (manual 4.12), unread.

**Next:** connect the real VDO senders (body to GND), then LM335 and the current sensor; Blaze oil T sender type (VDO 120 or 150); conversions for the backup Engine page; RPM source.

## Handy facts
- Boards: `/dev/serial/by-id/usb-Adafruit_*`; serials AIR 38503039350B102C, GPS 38503039350B242B,
  IMU DF625857C7793E2F. Flash: `Sensors/flash_remote.sh <Sketch> <serial>`.
- Backup display (Feather 38503039350B9941, on the Mac as /dev/cu.usbmodem101): `stty -f <port> 1200; sleep 3; picotool load -x <uf2>`
  (arduino-cli upload fails, no drive mounts). The Arduino IDE serial monitor holds the port and blocks flashing.
- Reboot a running board: `stty -F <port> 1200` then `picotool reboot --ser`. Keep hupcl on the tty.
- Regenerate protocol docs: `Protocol/.venv/bin/python Protocol/generate_can_docs.py`.
- When scripting edits to ISSUES.md, check the slice has no other '## ' or '### ' headings first.
- Memory notes: `project_sensor_modules_status.md` in the Claude project memory folder.
- macOS sometimes blocks the shell/Edit tool on repo files (quarantine/macl flags); `xattr -rd com.apple.quarantine Protocol` and the same for `com.apple.macl` fixed it, and `cd /` before `cd`-ing into the repo helps when git says it cannot read the working directory.

## 2026-10-26: backup display Engine page built (1.7.14), heartbeat and versions, EFIS BACKUP block

- Backup display 1.7.14 (commit ce97aed): heartbeat 0x704 (module 4), Versions page, RDAC XF receive and conversions, Engine page (page 3; System 4, Versions 5). Engine page layout: row 1 `OP 45psi   RPM 2450`, row 2 `OT 171F`, row 3 highest EGT and CHT with cylinder numbers, row 4 page, volts, amps. Shown in psi and degrees F (David's choice; CAN data, yaml and thresholds stay bar and C; a units setting is wanted later). Not yet looked at on the display.
- EFIS tracks 0x704 and shows a BACKUP block on the Modules page (checked). Restarting piefis does not reload the kiosk page; send F5 with `ssh david@pi-efis.local 'XDG_RUNTIME_DIR=/run/piefis WAYLAND_DISPLAY=wayland-0 wtype -k F5'`.
- Conversions and thresholds: `Protocol/engine_conversions.yaml` 1.1.0 and `Protocol/engine_convert.py` 1.0.1 (volts = raw / 57.3758; VDO 360-001 5 bar pressure curve; VDO 323-055 150 C temp, Steinhart-Hart; RDAC timeouts). Blaze oil T sender should be set to VDO 150, not 120.
- RPM and amps show dashes: `RPM_PULSES_PER_REV = 0` and `AMPS_ZERO_RAW = -1` in the sketch until the RPM source and current sensor calibration are chosen.
- **Next, one at a time:** (1) David checks page 3. (2) Cylinders page (page 4; EGT and CHT per cylinder, highest flashing; System 5, Versions 6). (3) LCD alarms (caution mark, alarm flashing, knob push acknowledges, 10 s power-up inhibit). (4) Engine page on the main EFIS second screen, selectable with the HSI (server needs the engine data). (5) Units setting. (6) DESIGN.md update for the Engine page.
- Bench: real VDO senders on OIL P and OIL T (hot-water check of the oil T curve, pressure check against a known pressure), LM335, current sensor zero and gain, thermocouples and cylinder wiring, RPM source and pulses per rev, volts limits, EGT probes.
- Parked: aircraft testing of slip ball, horizon and G on both displays; COM radio Feather; airport data (OurAirports vs NASR; waiting to hear which airport David flies from); in-aircraft network for ForeFlight transfer.

**Update later on 2026-10-26 (backup display 1.7.35, commit fc73e39):** Engine page (3) and Cylinders page (4, EGT and CHT per cylinder in F) are built and David approved both layouts. Engine page: `OilP --- psi  EMS`, `OilT 171 F EGT3   63`, `RPM ----   CHT2   63`, volts and amps right justified. GPS page: a 3 x 2 satellite icon (columns 7 to 9) with a small G in the top middle cell and custom P and S (borrowing the horizon edge line slots 6 and 7, which are reloaded on every page change); TRK under ALT; the word DATA removed. David approved these. Still next: LCD alarms, then the main EFIS Engine page, a units setting, then the bench items above.
