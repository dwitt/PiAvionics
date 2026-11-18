# PiEFIS vs Garmin G5 — feature and display review

Written 2026-11-06, saved here 2026-11-13. Compared the Garmin *G5 Electronic Flight Instrument Pilot's Guide for
Non-Certified Aircraft* (190-02072-00 Rev. K, software 6.10) against EFIS page 0.8.1 and `aio_server.py` 1.7.x.
Shared doc version (same content): https://claude.ai/artifact/LipVXq7T5GYnHTWXiQoe9s

## Notes for Claude Code

- Nothing in this review has been implemented yet. All line numbers are from page 0.8.1 (no EFIS page file
  changed between 2026-10-25 and 2026-11-13).
- The review was checked by running the page headless on `tests/fake_feed.py` at the Pi's size
  (`tests/page_shots.py`, `?pi=true`, device scale 1.25).
- To step the knob in the test feed, add `position` and `pressed` to its `/control` handler (this was done in a
  scratch copy only, not in this repo):
  - initial state: `state = {..., "position": 0, "pressed": False}`
  - in `message()`: `"position": state["position"], "pressed": state["pressed"],`
  - in the `/control` handler: read `position` (int) and `pressed` (`"1"` = True) from `request.query`.
  - Then `curl "localhost:<port>/control?pressed=1"`, `...?pressed=0`, `...?position=6` and so on between screenshots.
- `images/g5_review_altitude_bug_states.png` shows the altitude tape selected, editing and after the edit.

## Summary

PiEFIS already covers the core of the G5's PFD and HSI pages, and does more than the G5 in several places: the
shared QNH, the GPS/pressure/density altitude box, the route list with Direct-To, and the module health display.
The biggest gaps:

- **Targets disappear once set.** The altitude bug's cyan readout shows only while it is being edited. After that
  only a small red bug remains, and it starts at 0 ft. The G5 keeps the selected altitude on screen and alerts as
  you approach it.
- **Two separate heading bugs.** The PFD heading tape and the HSI each keep their own bug value. The HSI's value
  is also its OBS course ("CRS"), so moving the heading bug on the HSI also moves the course.
- **The knob has no home position.** After any edit the knob stays in selection mode, so the next turn moves the
  selection instead of the baro setting. On the G5, turning the knob on the PFD always sets baro.
- **Missing trend and limit cues:** airspeed trend vector, full speed bands and V-speed tags, wind, bearing pointer.

On looks, the biggest wins are one colour code (red now means "selected", "bug" and "failure"), a softer sky and
ground, antialiasing, loading the font before the HSI is drawn, and hiding the encoder debug text.

## How PiEFIS works today

One push-and-turn knob drives eight items in a fixed ring. In order (`efis-display.js` `registerCallback` calls):
QNH, OAT/time box, brightness, GS/TAS box, PFD heading bug, GPS/pressure/density altitude box, altitude bug
(`altimeter_ribbon`), and the HSI's CRS value.

1. At start-up the first item, QNH, is already in edit mode, so turning the knob sets baro.
2. A press enters selection mode. The current item gets a red outline: a red border on a box, a red line along a
   tape, or a red ring around the HSI.
3. Turning moves the red outline from item to item.
4. A press enters edit mode. The outline turns cyan, and the tapes and heading tape show a cyan readout box with the
   value. Turning changes the value, with bigger steps on fast turns (`|delta| >= 6`): 25 or 100 ft for the altitude
   bug, 1° or 2° for the PFD heading bug, 1° or 5° for the HSI.
5. A press returns to selection mode. The cyan readout disappears, and the item stays outlined in red until the
   knob moves on. Nothing times out back to QNH (`userInput.mjs` has no timer).

The G5 works differently: turning the knob always sets baro on the PFD page and the heading bug on the HSI page.
A press opens a menu of the other settings (G5 manual 1.5.2), which closes when you are done.

### What PiEFIS already does that the G5 does not

- **Shared QNH** on the CAN bus; flashes until set; amber if the AIR module uses a different value.
- **Choosable readouts:** GS or TAS, GPS/pressure/density altitude, OAT or UTC time. TAS is computed from pitot,
  static and OAT with compressibility and a probe recovery factor (`speedDisplay.mjs`).
- **Route list** (`routeOverlay.mjs`): tap a waypoint, choose Leg or Direct-To.
- **GPS status box:** fix type coloured by HDOP, WAAS; expands on touch for satellites, HDOP, VDOP and WMM2025
  variation.
- **Module health:** red X flags and blanked tapes per instrument, gyro-only heading dimmed then blanked,
  annunciators that flash 5 s when new, the Modules page, read-only viewer pages.
- **Two slip/skid cues:** a trapezoid under the roll pointer (G1000 style) and the ball.

## Feature comparison

Of 31 G5 features: PiEFIS has 10, has 10 in part, lacks 8; 3 do not apply. Section numbers are the G5 manual's.

| Feature | G5 | PiEFIS today | Status |
| --- | --- | --- | --- |
| Knob | Turn sets baro (PFD) or heading bug (HSI); press opens a menu (1.5.2) | One ring of 8 items; stays in selection mode after an edit | Partial |
| Airspeed tape and readout | 60 kt window, labels every 10 kt; ones digit rolls (2.1.1) | 80 kt window; every digit rolls | Have |
| Airspeed colour bands | Red low-speed, white, green, yellow, red/white barber pole at Vne (2.1.1) | Green, yellow, white strips, red Vne tick; Vfe placeholder 99 | Partial |
| Airspeed trend vector | Magenta line to the speed 6 s ahead (2.1.1) | None | Missing |
| Overspeed cues | Pointer red at Vne; digits yellow when trend crosses Vne (2.1.1) | None | Missing |
| V-speed markers | Cyan tags; listed at tape foot when airspeed fails (2.1.1.2) | None | Missing |
| TAS, GS and OAT | All three at once (Fig. 2-1) | GS or TAS, OAT or UTC, picked with the knob | Partial |
| Attitude | 2.5° pitch marks, roll ticks 10/20/30/45/60 (2.1.2) | 5° pitch marks, same roll ticks, slip/skid trapezoid | Have |
| Standard-rate bank pointers | Green marks on roll scale (2.1.2.2) | None | Missing |
| Pitch attitude offset | Aircraft symbol ±5° (2.1.7) | None | Missing |
| ALIGNING state | While AHRS settles (1.4.2) | Red X until valid | Partial |
| Altimeter tape and readout | 400 ft window, 20 ft ticks (2.1.3) | Same, 20 ft rolling tens drum | Have |
| Selected altitude | Cyan box above tape, always shown; bug parks at edge (2.1.3) | Red bug, 25/100 ft steps; cyan readout at tape foot only while editing; starts at 0 ft | Partial |
| Altitude alerting | Flash at 1,000 and 200 ft to go; yellow outside ±200 ft (2.1.3.2) | None | Missing |
| Baro setting | inHg, hPa or mb (2.1.3.1) | inHg, shared on CAN, flashes until set, amber on mismatch | Have |
| Vertical speed | Fixed scale, pointer (2.1.6) | Non-linear ±2,000 fpm scale, magenta bar | Have |
| PFD heading tape | Cyan bug, magenta track triangle (2.1.5) | Red bug, cyan readout while editing; separate from HSI bug; no track marker | Partial |
| Turn rate indicator | Magenta trend bar (2.1.4) | Same idea, spring-damped | Have |
| Slip/skid | Ball (2.1.2) | Ball and trapezoid | Have |
| CDI on the PFD | Dot scale under slip ball (2.3.1) | HSI only | Missing |
| HSI rose, heading bug, track | Rotating rose, cyan bug, magenta track (2.2.1) | Same; track hidden below 10 kt | Have |
| HSI course, CDI, DTK | Course and DTK readout; CDI scale label (2.2.1.2) | One value ("CRS") is both heading bug and OBS course; plan DTK overrides the needle but box still shows CRS; CDI fixed 2 NM | Partial |
| Bearing pointer | Cyan pointer to waypoint (2.2.1.1) | Server sends `bearing`; not drawn | Partial |
| Wind | Arrow + speed/direction, HSI top left (Fig. 2-17) | None | Missing |
| Distance to waypoint | HSI top right | Waypoint ID and NM under the rose | Have |
| Failure flags | Red X with name (1.4.1) | Red X with name, blanked tapes, dimmed gyro-only heading | Have |
| System messages | Flashing [!] and list (1.7) | Annunciators flash 5 s when new; Modules page; no history | Partial |
| Backlight | Auto (light sensor) or manual (1.4.4) | Manual, with the knob | Partial |
| Battery backup | Up to 4 h (2.1.8) | None; backup display covers this | N/A |
| Autopilot, FD, ESP | Sections 3 and 4 | No autopilot | N/A |
| Vertical deviation | Needs external navigator (2.3.2) | No source | N/A |

## Recommended features

### High priority

1. **Give the knob a home position.**
   - After about 10 s with no turn or press in selection mode, return to QNH edit and clear the red outline. Same
     after leaving an edit.
   - Long press (about 1 s) syncs the item being edited: altitude bug to current altitude rounded to 100 ft, heading
     bug to current heading, CRS to the bearing to the waypoint. The page already receives `pressed` in every
     message, so it can time the press. The G5 syncs the same way (2.1.3, 2.1.5).
   - Consider moving the rarely changed items (OAT/time, GS/TAS, altitude source boxes) into the Menu, leaving
     QNH, heading bug, altitude bug and course on the knob.
2. **Keep the selected altitude on screen, and add alerting.**
   - Show the cyan readout whenever a target is set, not only while editing (above the tape as on the G5, or at
     the tape foot but left up). Dashes until set.
   - Bug cyan; park it at the tape edge when off-scale; start at current altitude rounded to 100 ft, not 0.
   - Alerting per G5 2.1.3.2: flash 5 s at 1,000 ft to go and at 200 ft; after capture, yellow on black and flash
     outside ±200 ft; reset when the target changes.
   - Optional: put the target on the CAN bus like QNH so the backup display can show it.
3. **One heading bug on both instruments.** `headingIndicator.mjs` and `hsi.mjs` each keep their own `_bugValue`,
   and the HSI's is also its OBS course. Keep one heading-bug value that both draw and either can set. Give the
   course its own value, used only when no flight plan is active. With a plan active, label the box DTK in magenta.
4. **Airspeed trend vector.** Magenta line beside the colour strip to the speed 6 s ahead. Low-pass the rate
   (about 1 s). Hide under about 2 kt of change and when airspeed is invalid.
5. **Speed bands and overspeed cues.** Red low-speed band below Vs1, red/white barber pole above Vne, white flap
   arc. Pointer red at/above Vne; digits yellow when the trend crosses Vne. Move `V_SPEEDS` out of
   `airspeedRibbon.mjs` into the `airspeed-limits.json` planned in `MENU_SYSTEM_PLAN.md`; fix Vfe (99).
6. **V-speed tags.** Cyan tags for Vx, Vy, Vg, Vr. When airspeed fails, list them at the foot of the blanked tape.
7. **Wind on the HSI.** Inputs already on the page: TAS, magnetic heading, GPS GS and magnetic track. Wind = ground
   velocity − air velocity. Arrow + speed/direction top left, plus headwind/crosswind components (G5 Fig. 1-6).
   Average 10–30 s; hide below ~30 kt GS, while turning > ~2°/s, and when heading is gyro-only. 5° heading error
   at 100 kt TAS gives ~9 kt false crosswind.

### Medium priority

8. **Bearing pointer.** Draw the server's `bearing` as a thin cyan needle with a ring (2.2.1.1).
9. **CDI scale.** Replace fixed `_cdiFullScaleNM` (2 NM) with the G5 VFR scales 5.00 / 1.25 / 0.25 NM (Table 2-2);
   show the scale beside the CDI.
   **Done 2026-11-15** (hsi 1.0.20, page 0.8.36, server 1.7.10, flightplan 1.0.1; seen working on the Pi). The G5
   guide does not say how the VFR scale is chosen; the automatic rule follows the G3X Touch guide (190-02472-00
   Rev. B, CDI section): 1.25 NM within 30 NM of any airport in the route, otherwise 5.00 NM. David chose:
   automatic with a manual override that stays until he picks Auto; 0.25 NM is manual only (the G3X uses it on an
   approach leg or near the FAF/MAP, which a VFR plan from the GPS does not tell us). A box under DTK shows AUTO
   (grey) or MAN (white) and the scale; a tap opens CDI SCALE: Auto (current automatic value), 5.00, 1.25, 0.25 nm.
   The server works out `cdi_auto_nm` (`auto_cdi_scale` in `flightplan.py`) and keeps the choice in
   `controller_state['cdi_scale']` (survives a page reload, not a server restart). The DTK and scale boxes are
   116 px wide. Note: en route at 5.00 NM the bar is far less sensitive than the old fixed 2 NM.
10. **Track marker on the PFD heading tape** (magenta triangle, 2.1.5).
    **Done 2026-11-15** (headingIndicator 1.0.10, page 0.8.37, on the Pi): a 16 x 10 magenta triangle with a thin
    black edge on the tape's bottom edge, pointing up, at the GPS magnetic track; same rules as the HSI's track
    diamond (valid GPS, 10 kt or more); hidden off the edge of the tape (about +/-35 deg). David chose the bottom
    edge, hiding, and style B from Mac mockups. Seen on the Pi with the bench simulator (RESUME.md).
11. **ALIGNING state.** `instrumentStatus.mjs` knows module state 0 = STARTING. Show "ALIGNING" over the attitude
    instead of a red X while the IMU starts (1.4.2).
    **Done 2026-11-15** (instrumentStatus 1.5.0, failureFlag 1.1.0, page 0.8.38, IMU_AHRS 1.0.19; seen on the Pi).
    A bench restart showed the IMU silent about 5 s (bootloader), STARTING about 4 s, then correct attitude within
    0.1 s (the NXP filter starts from gravity and the field), but its first frame was level / north / 0 G, sent
    before the filter had run. Now:
    - page: an IMU that is present and STARTING (or uptime 5 s or less) shows an amber ALIGNING box over a blank
      attitude, with no red X and no NO IMU; attitude, heading, turn rate and the slip ball stay hidden until 3 s
      (David's choice) after the attitude arrives; a start with no attitude for 15 s falls back to the usual flags;
    - IMU_AHRS 1.0.19: until the filter has run 10 updates (0.1 s), after a start or a filter restart, the attitude
      and acceleration frames carry the invalid marker and the heartbeat says STARTING.
    Checked on the Pi with the IMU held at 20 deg bank, 10 deg nose up: marker, then the tilt at once; no level flash
    on the EFIS or the backup display. Not known: how long the attitude takes to settle after a restart while
    turning or accelerating (it starts from gravity); review the 3 s after a flight.
12. **Message history.** A Messages row in the Menu listing what happened and when, with a flashing [!] until
    viewed (1.7). Include a warning when WMM2025 expires (end of 2029).

### Low priority

13. Standard-rate bank pointers: bank = atan(TAS × 0.0524 / g), TAS in m/s; ~15° at 100 kt (2.1.2.2).
    **Done 2026-11-15** (attitude-indicator 1.0.5, page 0.8.39): two green triangles outside the bank arc with
    their points on it (like the white zero mark; David chose them over the G5's arrows along the arc), turning
    with the arc so the yellow pointer meets one at standard-rate bank. Hidden without a trusted TAS or below
    30 kt TAS. Menu row "Std Rate Marks" On/Off, kept in the browser's storage (default On).
14. Pitch attitude offset ±5° from the Menu, long press to centre (2.1.7).
    **Done 2026-11-15** (pitchOffset 1.0.0, page 0.8.40, server 1.7.12, aircraft_settings 1.0.4): on the knob,
    not the Menu (David's idea, as a mechanical attitude indicator): the last knob item. Selected: 2 px cyan outline
    tight round the wings and dot of the yellow aircraft symbol (2026-11-18, was a box; David); editing: 3 px
    line (pitchOffset 1.0.1, page 0.8.41); 0.5° per
    click, ±5°; long press while editing centres it. The symbol moves, the horizon and ladder do not. A cyan
    "+2.0°" right of the wing only while selected or editing, bold while editing (2026-11-18, David; it used to
    stay whenever not zero, and editing had a "PITCH +2.0°" box under the symbol; pitchOffset 1.0.3, page 0.8.43). Kept on the Pi (aircraft_settings.json
    "attitude" group, may be changed in the air), saved 3 s after the knob stops; viewer pages follow it.
15. ~~CDI on the PFD~~ — dropped 2026-11-15 (David): no full-screen PFD mode planned.
16. ~~Automatic backlight with an ambient light sensor~~ — dropped 2026-11-15 (David).
17. Data logging — covered by the planned CAN black-box logger; could also keep the message history.

## Making the display look professional

**Status 2026-11-13 (all deployed to the Pi):**
- Done: quick fixes 1 to 3 and the three-digit headings with degree sign; 1 (colour code, as changed below);
  2 (gradient, eased over 40 degrees, 2 px horizon); 3 (antialiasing); 4 (pitch ladder); 9 (outlines).
- 1 as built: bugs cyan; selection cyan, not white (white disappeared on the tapes and the brightness icon):
  2 px outline or line when selected, 3 px when editing (boxes grow 1 px outward), brightness icon gets a box;
  OAT number and unit white; GS and GPS altitude light magenta `0xFF6AFF` (pure magenta was too dark for small
  digits), TAS/PRE/DEN white; HSI GS and waypoint text light magenta; tape numbers stay grey (David's choice).
- 3 as built: measured on the Pi, antialiasing costs frames (PFD only 48 fps, both 34). Added a 30 fps cap (every
  other 60 Hz refresh, own loop, not Pixi's maxFPS). Defaults: cap on, PFD antialiased, HSI not (HSI antialiasing
  with the Route list open gave slow frames). All three are Menu rows. Comment in `efis-display.js` has the numbers.
- 4 as built: ladder circle 30 px inside the bank arc; pitch scale 75 degrees on the screen (was 50) so the +/-20
  marks show in level flight; white 2.5 degree marks only between -10 and +10.
- Later 2026-11-13: 11 done (Menu: Slip Cue, Modules, Orientation, Maintenance; the test and tuning rows are on
  the Maintenance page). 5: both cues kept, choice in the Menu's "Slip Cue" row (Both / Ball / Trapezoid, stored
  per browser). 6: "kts" is now "kt"; "NM" kept. 7: units kept as they are (David likes the one- and two-line
  units). Server sends Cache-Control no-cache so a reload picks up every changed file.
- 8: B612 and B612 Mono tried in screenshots; staying with Tahoma (David prefers it; B612 Mono overflowed the
  QNH box and spaced out decimals). Tahoma is loaded by the page from `support/Tahoma*.ttf`.
- Features 2026-11-14 (details in RESUME.md): 1, 2 and 3 done. Feature 1 with a long press that flashes at 1 s and
  syncs on release, and a 3 s hold for full brightness; feature 2 as a selected altitude box under the GPS box with
  G5 alerting (deviation in the amber caution colour); feature 3 as the G5: one heading bug (PFD box at the left of
  the tape, drawn on the HSI too), DTK box and course needle only with a flight plan, CRS gone. Also a TAS box under
  the GS box (speed off the knob). Knob: QNH, OAT/time, brightness, heading bug, altitude source, selected altitude.
- Later 2026-11-14: feature 4 done (trend vector, light magenta, right of the colour strip). Knob home and
  start-up are now QNH selected, not edited (a stray turn cannot change the QNH). OAT and UTC both shown at the
  foot of the speed tape (UTC only, no local time); OAT/time off the knob: QNH, brightness, heading bug, altitude
  source, selected altitude.
- Later 2026-11-14: feature 5 done. Limits from the flight manual (Vfe fixed to 87 kt) kept on the Pi in
  `aircraft_settings.json` (server sends them; Menu editing page next). Low-speed cue is a thin red line below Vso
  on the white arc's side (David's choice) rather than the G5's band; digits amber (caution colour, not yellow)
  when the trend reaches Vne.
- Night 2026-11-14: Menu > Airspeed Limits page (keypad or USB keyboard, knots, ground only, old -> new confirm,
  saved by the server). Controller only.
- Late night 2026-11-14: feature 6 done. Cyan R/X/Y/G tags right of the tape, listed at the foot of the tape when
  airspeed fails; V-Speed Tags on/off and a V-Speeds page in the Menu; kept on the Pi with the limits.
  "kt" and "ft" moved to the foot of their tapes (inner side, level, over the tape) so the tags never meet them.
- Feature 7 done 2026-11-14: wind box on the HSI top left (David: HSI only, as the G5 / G3X). Closed: arrow (the
  way it blows, heading up) with direction (magnetic) and speed; a touch opens HEAD/TAIL and XWIND rows below.
  `windEstimate.mjs`: GPS ground vector - TAS along the magnetic heading, averaged ~10 s, 5 s warm-up; dashes below
  30 kt GS/TAS, without GPS/TAS, or with a gyro-only heading; held while turning > 2 deg/s. Not yet flight tested.
- GPS box moved to the lower right above the Route button: fix at the bottom, VAR above, SAT/HDOP/VDOP above those
  when opened (it grows upward). The side towards the rose is scalloped to the HSI circle (8 px gap, rounded where
  it meets the top and the side); labels move in where it cuts.
- Corners: 8 px for every stand-alone box (readouts, panels, DTK, wind, GPS, the tape's bug edit box); 4 px for
  rows and buttons inside panels; the HSI heading box stays square.
- Feature 8 done 2026-11-14 (hsi 1.0.18, page 0.8.34): cyan bearing pointer to the active waypoint from the
  server's `bearing` (magnetic). Hidden without a bearing, a trusted GPS position or a heading.
- Restyled 2026-11-15 to the G5 figure (hsi 1.0.19, Figure 2-19 on guide page 23, pdf page 37), worked out with
  David. The G5 pointer is not full length (the 1.0.18 note above said so wrongly): it is short, outside a ring
  around the CDI. Now:
  - a grey ring (0.45 of the rose radius) around the CDI, shown with the bearing pointer;
  - the magenta needle's head and tail outside the ring, 4 px wide (was 3), the tail a plain bar (the T-bar went);
    the deviation bar is the needle's middle inside the ring, across hollow dots spaced to the ring (about 34 px,
    was 20; full scale two dots) with no centre dot; the TO/FROM triangle on the axis 44 px ahead of / behind the
    centre;
  - the cyan pointer: two 2 px lines into an open arrowhead, the two lines closed at the tail (no stub), no ring on
    the shaft. It is drawn under the needle, just outside its edges, so on course (bearing = DTK, the usual case)
    it reads as the magenta needle outlined in cyan; apart, as the G5's double-line pointer (David's reading of
    the G5 design: the double line and open head are there to frame the magenta needle when the two line up).
  - Both take their sizes from `_needleGeometry()` in `hsi.mjs`, so the outline follows any needle change.
  - At full-scale deflection the deviation bar's ends run a little past the ring, as in the G5 figure.
- Left in this list: deferred 10 (HSI corners; wind top left and GPS bottom right done). Feature 9 (CDI scale)
  done 2026-11-15 (see the feature list). Next: feature 10 onward.

1. **Colour code; red only for warnings.** Red now means selected (box borders, tape line, HSI ring), bugs, and
   failures.

   | Colour | Meaning | Change |
   | --- | --- | --- |
   | White | Measured data, scales, "selected" outline | Selected outline red → white (`displayRectangle.mjs`, `ribbon.mjs`, `headingIndicator.mjs`, `brightness.mjs`, `hsi.mjs`); OAT cyan → white; tape numbers `0xBBBBBB` → white |
   | Cyan | Pilot-set values, editing | Heading and altitude bugs red → cyan |
   | Magenta | GPS/nav data, trend lines | GS readout chartreuse → magenta |
   | Green | Active/normal | Keep |
   | Yellow/amber | Caution | Keep (annunciators, yellow arc, QNH mismatch) |
   | Red | Warnings, limits, failures | Only red X, Vne line, warnings |

2. **Softer sky and ground with a gradient.** Replace `0x0000C0` / `0x884400` in `attitude-indicator.mjs` (and the
   `0x0000C0` behind the heading tape) with gradients lightening toward the horizon, e.g. sky `#1E4FA8` → `#5E8FDB`,
   ground `#8A5A2B` → `#4A2E14`. Pixi v8 `FillGradient`. Horizon line 2 px (now 1 px).
3. **Antialiasing.** `app.init` sets no `antialias` (Pixi default off). Add `antialias: true` to both canvases;
   check Pi 4 frame rate.
4. **Pitch ladder mask.** `createAttitudeMaskGraphics` uses the roll-arc radius, so the 20° label touches the arc
   and roll pointer. Make it ~30 px smaller. Optional 2.5° marks.
5. **One slip/skid cue** — trapezoid or ball, not both.
6. **Number formats.** Headings three digits everywhere (`headingIndicator.mjs` line 103 readout and line 675 bug
   box). "kt" not "kts"; "NM".
7. **"kts"/"ft" beside the readouts** (placed deliberately). The G5 has no units beside its pointers, only a small
   stacked "KT" at the foot of the speed tape. Worth trying without.
8. **Font.** Consider B612 / B612 Mono (Airbus cockpit legibility research, SIL OFL,
   https://github.com/polarsys/b612). Equal-width digits in the Mono variant.
9. **Dark outline on all text over sky/ground** (pitch labels, VSI labels and heading readout already have one).
10. **HSI corners as on the G5:** wind top left, waypoint/distance top right, DTK/CRS bottom left, selected heading
    bottom right. GPS box could shrink to a "3D WAAS" tag that expands on touch.
11. **Test controls out of the pilot Menu:** move "Airspeed Sweep" and "Encoder Debug" to a Maintenance sub-menu or
    `?debug=1`.

## Quick fixes found in the code

| Issue | Where | Fix |
| --- | --- | --- |
| HSI labels drawn before the font loads (serif fallback on N/E/S/W, rose numbers, CRS, VAR, Route) | `efis-display.js`: HSI built line 212, RouteOverlay 223; fonts load from 301; `document.fonts.ready` (355) only delays `setup()` | Load both FontFaces at the top and `await Promise.all([...])` before creating any `Text`. Tested in a scratch copy: HSI then renders in Tahoma. |
| Stray numbers bottom left of PFD; Menu shows "Encoder Debug ON" | `userInput.mjs`: `DEBUG = true`; `createTextObjectAt` passes `stile:` not `style:` | `DEBUG = false`; fix typo |
| PFD heading "90" and bug box "25" vs HSI "090" | `headingIndicator.mjs` lines 103, 675 | Pad to three digits as `hsi.mjs` does |
| Altitude bug starts at 0 ft | `ribbon.mjs` `_bugValue = 0` | Set to altitude rounded to 100 ft on first valid altitude |
| PFD heading bug starts at 5° | `headingIndicator.mjs` `_bugValue = 5` | Done 2026-11-14: `---°` until set, first click goes to the current heading (feature 3) |
| HSI box reads CRS while needle follows plan DTK | `hsi.mjs`: needle `_desiredTrack` (line 435); box `_bugValue` (lines 588, 876) | Done 2026-11-14: DTK box in magenta only with a plan (feature 3) |
| Vfe placeholder | `airspeedRibbon.mjs` `vfe: 99` | Real Vfe; V-speeds to config |
| Jagged lines | `app.init` no `antialias` | `antialias: true` both canvases |
| Out-of-date notes | `UNIMPLEMENTED_FEATURES.md`, `EFIS_WORK_PLAN.md` (slip ball "disabled", `app2` "delete"); `app2` is the HSI canvas | Update or archive; fix the `app2` comment |

## Not worth copying

Autopilot/FD/ESP/GPSS (no servos); battery status (backup display covers it); glideslope/glidepath (no source);
auto-switch to PFD in unusual attitude (both pages always shown); sky-pointer option.

## Suggested order

1. The quick fixes, then colour code and antialiasing.
2. Knob home position and long-press sync.
3. One shared heading bug and a separate course.
4. Selected altitude that stays on screen, then alerting.
5. Speed bands, overspeed cues, V-speed tags (V-speeds to config), then the trend vector.
6. HSI: bearing pointer, CDI scale, wind, four-corner layout.
7. The rest as time allows.
