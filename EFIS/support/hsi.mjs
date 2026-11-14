'use strict';

export const VERSION = '1.0.18';

import { Container, Graphics, TextStyle, Text } from './pixi.mjs';

/*****************************************************************************
 * HSI — Horizontal Situation Indicator
 * Phase 1: Compass rose, heading bug, ground track, ground speed
 * Phase 2: Course pointer, CDI bar, TO/FROM indicator
 ****************************************************************************/
export class HSI {

    constructor(app, x, y, diameter) {
        this.app = app;
        this.diameter = diameter;
        this.radius = diameter / 2;
        this._heading = 0;
        this._groundTrack = 0;
        this._bugValue = null;       // the heading bug, set by the page from the PFD's (one bug, as the G5); null: not set
        this._groundSpeed = 0;
        this._courseDeviation = 0;   // cross-track error in NM
        this._toFrom = null;         // "TO", "FROM", or null
        this._headingShown = true;   // false when the heading cannot be trusted (see setHeadingDisplay)
        this._gpsValid = true;       // false when the GPS data cannot be trusted (see gpsValid)
        this._trackUsable = true;    // false when the ground track means nothing, for example at low speed (see trackUsable)
        this._cdiFullScaleNM = 2;    // full-scale CDI deflection in NM
        this._desiredTrack = null;   // DTK from the flight plan: the course needle and the DTK box show only with one
        this._waypointDistance = null;
        this._waypointId = null;
        this._bearing = null;        // magnetic bearing to the active waypoint (from the server's flight plan); null: none

        // --- Root container ---
        this.hsiContainer = new Container();
        this.hsiContainer.position.set(x, y);

        // --- Compass rose (rotates) ---
        this.roseContainer = new Container();
        this._createCompassRose();

        // --- Circular mask for the rose ---
        const mask = new Graphics();
        mask.fillStyle = { color: 0xffffff };
        mask.circle(0, 0, this.radius);
        mask.fill();
        this.roseContainer.addChild(mask);
        this.roseContainer.mask = mask;

        this.hsiContainer.addChild(this.roseContainer);

        // --- Fixed elements (don't rotate) ---
        this.fixedContainer = new Container();
        this._createHeadingBug();    // behind lubber line
        this._createBearingPointer(); // under the course needle
        this._createCoursePointer(); // Phase 2: needle, CDI, TO/FROM
        this._createLubberLine();
        this._createAircraftSymbol();
        this._createHeadingBox();
        this._createTrackIndicator();
        this._createDtkReadout();
        this._createGroundSpeedReadout();
        this._createNavReadout();
        this._createGpsStatusBox();
        this._createWindBox();
        this._applyBugCourseVisibility();
        this.hsiContainer.addChild(this.fixedContainer);

        app.stage.addChild(this.hsiContainer);
    }

    // -----------------------------------------------------------------------
    // Compass Rose — ticks and labels, built once at construction
    // -----------------------------------------------------------------------
    _createCompassRose() {
        const r = this.radius;
        const ticks = new Graphics();

        // Cardinal and numeric labels every 30°
        const labelDefs = {
            0: 'N', 30: '3', 60: '6', 90: 'E',
            120: '12', 150: '15', 180: 'S', 210: '21',
            240: '24', 270: 'W', 300: '30', 330: '33',
        };
        const cardinals = new Set([0, 90, 180, 270]);

        const numberStyle = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: 20,      // was 18
            fill: 0xbbbbbb,    // was 0xcccccc
        });
        const cardinalStyle = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: 22,
            fill: 0xffffff,
            fontWeight: 'bold',
        });

        for (let deg = 0; deg < 360; deg += 5) {
            const rad = deg * Math.PI / 180;
            const cos = Math.cos(rad);
            const sin = Math.sin(rad);

            // Determine tick length
            let tickLen;
            if (deg % 10 === 0) {
                tickLen = 16;   // major (every 10°)
            } else {
                tickLen = 10;   // minor (every 5°)
            }

            // Draw tick from outer edge inward
            const outerR = r - 2;
            const innerR = outerR - tickLen;

            // Tick color — white for major, grey for others
            const tickColor = (deg % 10 === 0) ? 0xffffff : 0x999999;
            const tickWidth = (deg % 10 === 0) ? 3 : 2;

            ticks.strokeStyle = { width: tickWidth, color: tickColor };
            // Ticks point inward from the top (0° = up = negative Y)
            ticks.moveTo(sin * outerR, -cos * outerR);
            ticks.lineTo(sin * innerR, -cos * innerR);
            ticks.stroke();

            // Labels at 30° intervals
            if (labelDefs[deg] !== undefined) {
                const labelText = labelDefs[deg];
                const isCardinal = cardinals.has(deg);
                const style = isCardinal ? cardinalStyle : numberStyle;

                const label = new Text({ text: labelText, style: style });
                label.anchor.set(0.5, 0.5);

                // Position labels inside the tick marks
                const labelR = outerR - tickLen - 16;
                label.position.set(sin * labelR, -cos * labelR);

                // Rotate label so it's upright relative to the rose
                label.rotation = rad;

                this.roseContainer.addChild(label);
            }
        }

        this.roseContainer.addChild(ticks);
    }

    // -----------------------------------------------------------------------
    // Lubber line — white triangle at 12 o'clock (fixed)
    // -----------------------------------------------------------------------
    _createLubberLine() {
        const r = this.radius;
        const g = new Graphics();
        const triHeight = 16;
        const triHalfWidth = 10;

        g.fillStyle = { color: 0xffffff };
        g.moveTo(0, -r + 2);                       // tip (inside rose edge)
        g.lineTo(-triHalfWidth, -r - triHeight + 2); // bottom left
        g.lineTo(triHalfWidth, -r - triHeight + 2);  // bottom right
        g.closePath();
        g.fill();

        this.fixedContainer.addChild(g);
    }

    // -----------------------------------------------------------------------
    // Aircraft symbol — top-down airplane silhouette at centre (fixed)
    // -----------------------------------------------------------------------
    _createAircraftSymbol() {
        const g = new Graphics();

        g.fillStyle = { color: 0xffffff };

        // Fuselage (50% longer: was -16 to 12, now -24 to 18)
        g.moveTo(0, -24);       // nose tip (was -16)
        g.lineTo(-4, -16);      // nose left (was -10)
        g.lineTo(-4, 14);       // tail body left (was 18)
        g.lineTo(-2, 20);       // tail tip left (was 18)
        g.lineTo(2, 20);        // tail tip right (was 18)
        g.lineTo(4, 14);        // tail body right (was 18)
        g.lineTo(4, -16);       // nose right (was -10)
        g.closePath();
        g.fill();

        // Main wings (swept back)
        g.moveTo(-4, -5);       // wing root left (was -3)
        g.lineTo(-22, 5);       // wingtip left
        g.lineTo(-20, 9);       // wingtip trailing left
        g.lineTo(-4, 3);        // wing trailing root left
        g.closePath();
        g.fill();

        g.moveTo(4, -5);        // wing root right (was -3)
        g.lineTo(22, 5);        // wingtip right
        g.lineTo(20, 9);        // wingtip trailing right
        g.lineTo(4, 3);         // wing trailing root right
        g.closePath();
        g.fill();

        // Horizontal stabilizer (same shape as wings, scaled down)
        g.moveTo(-3, 12);       // stab leading root left (was -4)
        g.lineTo(-14, 18);      // stab leading tip left
        g.lineTo(-13, 20);      // stab trailing tip left
        g.lineTo(-3, 17);       // stab trailing root left (was -4)
        g.closePath();
        g.fill();

        g.moveTo(3, 12);        // stab leading root right (was 4)
        g.lineTo(14, 18);       // stab leading tip right
        g.lineTo(13, 20);       // stab trailing tip right
        g.lineTo(3, 17);        // stab trailing root right (was 4)
        g.closePath();
        g.fill();

        this.fixedContainer.addChild(g);
    }

    // -----------------------------------------------------------------------
    // Heading readout box — digital heading above the rose (fixed)
    // -----------------------------------------------------------------------
    _createHeadingBox() {
        const boxWidth = 84;   // was 70; room for the degree sign
        const boxHeight = 36;  // was 30
        const r = this.radius;

        const headingBox = new Container();
        // Position so bottom edge connects to lubber triangle base (-r - 14)
        headingBox.position.set(0, -r - 14 - boxHeight / 2);

        // Box background
        const bg = new Graphics();
        bg.fillStyle = { color: 0x000000 };  // was 0x222222
        bg.strokeStyle = { color: 0x666666, width: 2 };  // was 0xffffff
        bg.roundRect(-boxWidth / 2, -boxHeight / 2, boxWidth, boxHeight, 0);  // was 4 — sharp corners like EFIS magnifier
        bg.fill();
        bg.stroke();
        headingBox.addChild(bg);

        // Heading text
        const style = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: 28,      // was 22 — matches heading magnifier (XL tier)
            fill: 0xffffff,
            fontWeight: 'bold',
        });
        this.headingText = new Text({ text: '000°', style: style });
        this.headingText.anchor.set(0.5, 0.5);
        headingBox.addChild(this.headingText);
        this.headingBox = headingBox;       // kept so the readout can be dimmed or blanked with the heading

        this.fixedContainer.addChild(headingBox);
    }

    // -----------------------------------------------------------------------
    // Ground track indicator — magenta diamond on rose edge (fixed container)
    // -----------------------------------------------------------------------
    _createTrackIndicator() {
        const r = this.radius;
        const diamondSize = 8;

        // Container that rotates around centre to position the diamond
        this.trackContainer = new Container();

        const diamond = new Graphics();
        diamond.fillStyle = { color: 0xff00ff };  // magenta
        diamond.moveTo(0, -r + 2);                // top point (on rose edge)
        diamond.lineTo(-diamondSize, -r + 2 + diamondSize);
        diamond.lineTo(0, -r + 2 + diamondSize * 2);
        diamond.lineTo(diamondSize, -r + 2 + diamondSize);
        diamond.closePath();
        diamond.fill();
        this.trackContainer.addChild(diamond);

        this.fixedContainer.addChild(this.trackContainer);
    }

    // -----------------------------------------------------------------------
    // Heading bug — rectangle with V-notch to fit the lubber triangle
    // Sits outside the rose. Bottom edge follows the compass arc.
    // -----------------------------------------------------------------------
    _createHeadingBug() {
        const r = this.radius;
        const outerR = r - 2;          // rose edge (where ticks start)
        const halfWidth = 17;          // bug half-width (20% wider than lubber)
        const bugHeight = 10;          // extends outward from rose edge
        // Notch follows lubber triangle slope (10/14 * bugHeight = 7)
        const notchHalfWidthAtTop = 7;

        this.bugContainer = new Container();

        const bug = new Graphics();
        bug.fillStyle = { color: 0x00ffff };  // cyan

        const topY = -(outerR + bugHeight);

        // Angles for the bottom arc on the rose edge circle
        const halfAngle = Math.asin(halfWidth / outerR);
        const rightAngle = -Math.PI / 2 + halfAngle;
        const leftAngle = -Math.PI / 2 - halfAngle;

        // Notch tip is at the rose edge (same as lubber tip)
        const notchTipY = -outerR;

        // Clockwise from top-left
        bug.moveTo(-halfWidth, topY);
        bug.lineTo(-notchHalfWidthAtTop, topY);  // to left notch edge
        bug.lineTo(0, notchTipY);                 // V-notch tip (at rose edge)
        bug.lineTo(notchHalfWidthAtTop, topY);    // to right notch edge
        bug.lineTo(halfWidth, topY);              // to top-right corner

        // Right side down to rose edge
        const rightArcY = -Math.sqrt(outerR * outerR - halfWidth * halfWidth);
        bug.lineTo(halfWidth, rightArcY);

        // Arc along bottom (rose edge) from right to left
        bug.arc(0, 0, outerR, rightAngle, leftAngle, true);

        // Close back to top-left
        bug.closePath();
        bug.fill();

        this.bugContainer.addChild(bug);
        this.fixedContainer.addChild(this.bugContainer);
    }

    // -----------------------------------------------------------------------
    // Course pointer — magenta needle through centre with CDI and TO/FROM
    // The entire assembly rotates with (CRS - heading)
    // -----------------------------------------------------------------------
    _createCoursePointer() {
        const r = this.radius;
        const innerLimit = 40;   // gap around centre for aircraft symbol / CDI
        const outerLimit = r - 52; // stop before compass labels
        const needleWidth = 3;
        const arrowHalfWidth = 10;
        const arrowHeight = 14;
        const tailBarHalfWidth = 8;
        const tailBarHeight = 4;

        // Container rotates with CRS relative to heading
        this.courseContainer = new Container();

        // --- Needle: top half (arrow with shaft) ---
        const topNeedle = new Graphics();
        topNeedle.fillStyle = { color: 0xff00ff };  // magenta

        // Arrow head
        topNeedle.moveTo(0, -outerLimit);                              // tip
        topNeedle.lineTo(-arrowHalfWidth, -outerLimit + arrowHeight);  // left wing
        topNeedle.lineTo(-needleWidth / 2, -outerLimit + arrowHeight); // left shaft
        topNeedle.lineTo(-needleWidth / 2, -innerLimit);               // shaft bottom-left
        topNeedle.lineTo(needleWidth / 2, -innerLimit);                // shaft bottom-right
        topNeedle.lineTo(needleWidth / 2, -outerLimit + arrowHeight);  // right shaft
        topNeedle.lineTo(arrowHalfWidth, -outerLimit + arrowHeight);   // right wing
        topNeedle.closePath();
        topNeedle.fill();
        this.courseContainer.addChild(topNeedle);

        // --- Needle: bottom half (shaft with tail bar) ---
        const bottomNeedle = new Graphics();
        bottomNeedle.fillStyle = { color: 0xff00ff };

        // Shaft
        bottomNeedle.moveTo(-needleWidth / 2, innerLimit);
        bottomNeedle.lineTo(-needleWidth / 2, outerLimit - tailBarHeight);
        // Tail bar (T-shape)
        bottomNeedle.lineTo(-tailBarHalfWidth, outerLimit - tailBarHeight);
        bottomNeedle.lineTo(-tailBarHalfWidth, outerLimit);
        bottomNeedle.lineTo(tailBarHalfWidth, outerLimit);
        bottomNeedle.lineTo(tailBarHalfWidth, outerLimit - tailBarHeight);
        bottomNeedle.lineTo(needleWidth / 2, outerLimit - tailBarHeight);
        bottomNeedle.lineTo(needleWidth / 2, innerLimit);
        bottomNeedle.closePath();
        bottomNeedle.fill();
        this.courseContainer.addChild(bottomNeedle);

        // --- CDI dots (5 dots: centre + 2 each side) ---
        const dotRadius = 3;
        const dotSpacing = 20;  // pixels between dot centres
        const cdiDots = new Graphics();
        cdiDots.fillStyle = { color: 0x666666 };
        for (let i = -2; i <= 2; i++) {
            cdiDots.circle(i * dotSpacing, 0, dotRadius);
            cdiDots.fill();
        }
        this.courseContainer.addChild(cdiDots);

        // --- CDI bar (slides left/right across dots) ---
        this.cdiBar = new Graphics();
        this._drawCdiBar(0);
        this.courseContainer.addChild(this.cdiBar);

        // --- TO/FROM triangles ---
        const toFromSize = 8;
        const toFromOffset = 26;  // distance from centre along needle axis

        this.toIndicator = new Graphics();
        this.toIndicator.fillStyle = { color: 0xff00ff };
        this.toIndicator.moveTo(0, -toFromOffset);                         // top point
        this.toIndicator.lineTo(-toFromSize, -toFromOffset + toFromSize);   // bottom-left
        this.toIndicator.lineTo(toFromSize, -toFromOffset + toFromSize);    // bottom-right
        this.toIndicator.closePath();
        this.toIndicator.fill();
        this.toIndicator.visible = false;
        this.courseContainer.addChild(this.toIndicator);

        this.fromIndicator = new Graphics();
        this.fromIndicator.fillStyle = { color: 0xff00ff };
        this.fromIndicator.moveTo(0, toFromOffset);                         // bottom point
        this.fromIndicator.lineTo(-toFromSize, toFromOffset - toFromSize);  // top-left
        this.fromIndicator.lineTo(toFromSize, toFromOffset - toFromSize);   // top-right
        this.fromIndicator.closePath();
        this.fromIndicator.fill();
        this.fromIndicator.visible = false;
        this.courseContainer.addChild(this.fromIndicator);

        this.fixedContainer.addChild(this.courseContainer);
    }

    _drawCdiBar(offsetPx) {
        const barHalfHeight = 34;  // vertical bar spans most of the centre gap
        const barThickness = 4;
        this.cdiBar.clear();
        this.cdiBar.fillStyle = { color: 0xff00ff };
        this.cdiBar.rect(offsetPx - barThickness / 2, -barHalfHeight,
                         barThickness, barHalfHeight * 2);
        this.cdiBar.fill();
    }

    _updateCoursePointerRotation() {
        if (this._desiredTrack === null || this._desiredTrack === undefined) {
            return;                       // no flight plan: no course needle (hidden)
        }
        const offset = (this._desiredTrack - this._heading) * Math.PI / 180;
        this.courseContainer.rotation = offset;
    }

    /**
     * The heading bug shows only when it is set, and the course needle (with the CDI and TO/FROM) and the DTK box
     * only with a flight plan (a DTK), as the G5: with no navigation source it has no course. Both also need the
     * heading (setHeadingDisplay).
     */
    _applyBugCourseVisibility() {
        const haveCourse = this._desiredTrack !== null && this._desiredTrack !== undefined;
        this.bugContainer.visible = this._headingShown && this._bugValue !== null;
        this.courseContainer.visible = this._headingShown && haveCourse;
        this.dtkBox.visible = haveCourse;
    }

    // -----------------------------------------------------------------------
    // Bearing pointer — a thin cyan needle that points straight at the active waypoint (G5 2.2.1.1); the course
    // needle shows the planned track, this where the waypoint is from here. It lies in the band between the rose
    // ticks and the course needle's ends, so the two never hide each other: the head (arrow and a ring on its
    // shaft) towards the waypoint, a plain tail opposite. Hidden without a bearing (no flight plan), without a
    // trusted GPS position, or without the heading.
    // -----------------------------------------------------------------------
    _createBearingPointer() {
        const r = this.radius;
        const tip = r - 8;          // just inside the rose's ticks
        const inner = r - 58;       // a little inside the course needle's ends (r - 52)
        const colour = 0x00ffff;    // cyan
        const width = 2;

        this.bearingContainer = new Container();
        const pointer = new Graphics();
        // head: the shaft, an open arrowhead and a ring on the shaft
        pointer.moveTo(0, -inner);
        pointer.lineTo(0, -tip);
        pointer.moveTo(-8, -tip + 12);
        pointer.lineTo(0, -tip);
        pointer.lineTo(8, -tip + 12);
        pointer.stroke({ color: colour, width: width, cap: 'round', join: 'round' });
        pointer.circle(0, -inner - 9, 5);
        pointer.stroke({ color: colour, width: width });
        // tail
        pointer.moveTo(0, inner);
        pointer.lineTo(0, tip);
        pointer.stroke({ color: colour, width: width, cap: 'round' });
        this.bearingContainer.addChild(pointer);
        this.bearingContainer.visible = false;
        this.fixedContainer.addChild(this.bearingContainer);
    }

    /** Magnetic bearing to the active waypoint, or null for none. */
    set bearing(value) {
        this._bearing = (value === null || value === undefined) ? null : value;
        this._updateBearingPointer();
    }

    _updateBearingPointer() {
        this.bearingContainer.visible = this._bearing !== null && this._headingShown && this._gpsValid;
        if (this._bearing !== null) {
            this.bearingContainer.rotation = (this._bearing - this._heading) * Math.PI / 180;
        }
    }

    // -----------------------------------------------------------------------
    // DTK readout — fixed box, upper right, showing the flight plan's desired track in magenta (GPS), as the G5.
    // Was the CRS box, the HSI's own course and heading bug, set on the knob.
    // -----------------------------------------------------------------------
    _createDtkReadout() {
        const screenX = this.app.screen.width / 2;
        const screenY = this.app.screen.height / 2;
        const margin = 10;
        const boxWidth = 100;
        const boxHeight = 26;
        const pad = 6;

        this.dtkBox = new Container();
        // Position in upper-right corner of HSI display window
        this.dtkBox.position.set(screenX - margin - boxWidth / 2, -screenY + margin + boxHeight / 2);

        const boxBg = new Graphics();
        boxBg.fillStyle = { color: 0x000000 };
        boxBg.strokeStyle = { color: 0x666666, width: 2 };     // grey: it is not set on the knob
        boxBg.roundRect(-boxWidth / 2, -boxHeight / 2, boxWidth, boxHeight, 8);   // 8 px, the stand-alone box standard
        boxBg.fill();
        boxBg.stroke();
        this.dtkBox.addChild(boxBg);

        // light magenta, as the other GPS text on the HSI (the needle stays pure magenta)
        const labelStyle = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: 16,
            fill: 0xff6aff,
        });
        const label = new Text({ text: 'DTK', style: labelStyle });
        label.anchor.set(0, 0.5);
        label.position.set(-boxWidth / 2 + pad, 0);
        this.dtkBox.addChild(label);

        const valueStyle = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: 20,
            fill: 0xff6aff,
        });
        this.dtkText = new Text({ text: '000°', style: valueStyle });
        this.dtkText.anchor.set(1, 0.5);
        this.dtkText.position.set(boxWidth / 2 - pad, 0);
        this.dtkBox.addChild(this.dtkText);

        this.fixedContainer.addChild(this.dtkBox);
    }

    // -----------------------------------------------------------------------
    // Ground speed readout — text below the rose
    // -----------------------------------------------------------------------
    _createGroundSpeedReadout() {
        const r = this.radius;

        const gsStyle = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: 20,
            fill: 0xff6aff,    // light magenta, GPS data (as the waypoint text and the PFD's GS box); was grey 0xbbbbbb
        });
        this.gsText = new Text({ text: 'GS: --- kt', style: gsStyle });
        this.gsText.anchor.set(0.5, 0);
        this.gsText.position.set(0, r + 12);

        this.fixedContainer.addChild(this.gsText);
    }

    // -----------------------------------------------------------------------
    // Ground speed property
    // -----------------------------------------------------------------------
    set groundSpeed(value) {
        this._groundSpeed = value;
        if (value === null || value === undefined) {
            this.gsText.text = 'GS: --- kt';
        } else {
            this.gsText.text = `GS: ${Math.round(value)} kt`;
        }
    }

    get groundSpeed() {
        return this._groundSpeed;
    }

    // -----------------------------------------------------------------------
    // Navigation readout — waypoint ID and distance below ground speed
    // -----------------------------------------------------------------------
    _createNavReadout() {
        const r = this.radius;
        const navStyle = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: 20,
            fill: 0xff6aff,  // magenta, lightened so small text is easy to read (as the PFD's GPS readouts);
                             // the course pointer and other lines stay pure magenta 0xff00ff
        });
        this.navText = new Text({ text: '', style: navStyle });
        this.navText.anchor.set(0.5, 0);
        this.navText.position.set(0, r + 34);  // below GS readout
        this.fixedContainer.addChild(this.navText);
    }

    // -----------------------------------------------------------------------
    // Desired track property — overrides course pointer when flight plan active
    // -----------------------------------------------------------------------
    set desiredTrack(value) {
        this._desiredTrack = (value === undefined) ? null : value;
        if (this._desiredTrack !== null) {
            this.dtkText.text = String(Math.round(this._desiredTrack) % 360).padStart(3, '0') + '°';
        }
        this._updateCoursePointerRotation();
        this._applyBugCourseVisibility();
    }

    get desiredTrack() {
        return this._desiredTrack;
    }

    // -----------------------------------------------------------------------
    // Waypoint distance and ID properties
    // -----------------------------------------------------------------------
    set waypointDistance(value) {
        this._waypointDistance = value;
        this._updateNavReadout();
    }

    set waypointId(value) {
        this._waypointId = value;
        this._updateNavReadout();
    }

    _updateNavReadout() {
        if (this._waypointId && this._waypointDistance !== null) {
            this.navText.text = `${this._waypointId}  ${this._waypointDistance.toFixed(1)} NM`;
        } else {
            this.navText.text = '';
        }
    }

    // -----------------------------------------------------------------------
    // Heading bug property — positions the bug triangle. Set by the page from the PFD's heading bug (one bug, as
    // the G5); null hides it (not set).
    // -----------------------------------------------------------------------
    set headingBug(value) {
        if (value !== null && value !== undefined) {
            value = ((value % 360) + 360) % 360;
        } else {
            value = null;
        }
        if (value === this._bugValue) {
            return;
        }
        this._bugValue = value;
        this._updateBugPosition();
        this._applyBugCourseVisibility();
    }

    get headingBug() {
        return this._bugValue;
    }

    _updateBugPosition() {
        if (this._bugValue !== null) {
            const offset = (this._bugValue - this._heading) * Math.PI / 180;
            this.bugContainer.rotation = offset;
        }
        this._updateCoursePointerRotation();
    }

    // -----------------------------------------------------------------------
    // Wind Box — upper-left corner of HSI display, as the G5. It is a touch button like the GPS box:
    // contracted (the normal state) it shows an arrow the way the wind blows, relative to the aircraft
    // (heading up, as the rose), and the direction it comes from (magnetic) and its speed;
    // a touch expands it downward to add the headwind (or tailwind) and crosswind, another contracts it.
    // Without a wind (see windEstimate.mjs) it shows dashes.
    // -----------------------------------------------------------------------
    _createWindBox() {
        const screenX = this.app.screen.width / 2;
        const screenY = this.app.screen.height / 2;
        const margin = 10;
        const boxWidth = 130;       // as the GPS box
        const pad = 8;

        this._windBoxWidth = boxWidth;
        this._windPad = pad;
        this._windExpanded = false;
        this._wind = null;

        const windBox = new Container();
        // the origin is the top left corner so the box grows downward when it expands
        windBox.position.set(-screenX + margin, -screenY + margin);

        this._windBoxBackground = new Graphics();
        windBox.addChild(this._windBoxBackground);

        // the arrow, at the left across the first two rows, drawn pointing up and turned with the wind
        this._windArrow = new Graphics();
        this._windArrow.moveTo(0, 13);
        this._windArrow.lineTo(0, -6);
        this._windArrow.stroke({ color: 0xffffff, width: 3 });
        this._windArrow.poly([-7, -4, 7, -4, 0, -14]);
        this._windArrow.fill({ color: 0xffffff });
        this._windArrow.position.set(pad + 15, (this._windRowY(0) + this._windRowY(1)) / 2);
        windBox.addChild(this._windArrow);

        // Row 0: the direction it comes from, Row 1: its speed (right, before the expand arrow)
        const valueRight = boxWidth - pad - 14;
        const valueStyle = () => new TextStyle({ fontFamily: 'Tahoma', fontSize: 16, fill: 0xffffff, fontWeight: 'bold' });
        this._windDirText = new Text({ text: '---°', style: valueStyle() });
        this._windDirText.anchor.set(1, 0.5);
        this._windDirText.position.set(valueRight, this._windRowY(0));
        windBox.addChild(this._windDirText);
        this._windSpeedText = new Text({ text: '-- kt', style: valueStyle() });
        this._windSpeedText.anchor.set(1, 0.5);
        this._windSpeedText.position.set(valueRight, this._windRowY(1));
        windBox.addChild(this._windSpeedText);

        this._windExpandArrow = new Graphics();
        windBox.addChild(this._windExpandArrow);

        // Rows 2 and 3, only while expanded: HEAD (or TAIL) and XWIND, each with a small triangle the way
        // that part of the wind pushes the aircraft (down: head, up: tail, left or right: across)
        this._windHeadText = this._createWindRow(windBox, 'HEAD', 2);
        this._windCrossText = this._createWindRow(windBox, 'XWIND', 3);
        this._windHeadMark = new Graphics();
        this._windCrossMark = new Graphics();
        windBox.addChild(this._windHeadMark, this._windCrossMark);
        this._windExpandedItems = [this._windHeadText, this._windHeadText.windLabel, this._windHeadMark,
                                   this._windCrossText, this._windCrossText.windLabel, this._windCrossMark];

        // A touch toggles the box
        windBox.eventMode = 'static';
        windBox.cursor = 'pointer';
        windBox.on('pointerdown', (e) => {
            e.stopPropagation();
            this._windExpanded = !this._windExpanded;
            this._drawWindBox();
        });

        this._drawWindBox();
        this._updateWind();
        this.fixedContainer.addChild(windBox);
    }

    // y of the centre of a row of the wind box, from the top
    _windRowY(row) {
        return this._windPad + 7 + row * 18;
    }

    // A label on the left and a value on the right; returns the value's Text (label: .windLabel)
    _createWindRow(windBox, label, row) {
        const labelText = new Text({ text: label, style: new TextStyle({
            fontFamily: 'Tahoma', fontSize: 13, fill: 0x888888,
        })});
        labelText.anchor.set(0, 0.5);
        labelText.position.set(this._windPad, this._windRowY(row));
        windBox.addChild(labelText);

        const valueText = new Text({ text: '-- kt', style: new TextStyle({
            fontFamily: 'Tahoma', fontSize: 14, fill: 0xffffff,
        })});
        valueText.anchor.set(1, 0.5);
        valueText.position.set(this._windBoxWidth - this._windPad, this._windRowY(row));
        windBox.addChild(valueText);
        valueText.windLabel = labelText;
        return valueText;
    }

    // Redraw the box background and expand arrow for the contracted or expanded state
    _drawWindBox() {
        const rows = this._windExpanded ? 4 : 2;
        const height = this._windRowY(rows - 1) + 13;

        this._windBoxBackground.clear();
        this._windBoxBackground.fillStyle = { color: 0x222222, alpha: 0.85 };
        this._windBoxBackground.strokeStyle = { color: 0x666666, width: 1, alpha: 0.8 };
        this._windBoxBackground.roundRect(0, 0, this._windBoxWidth, height, 8);
        this._windBoxBackground.fill();
        this._windBoxBackground.stroke();

        for (const item of this._windExpandedItems) {
            item.visible = this._windExpanded;
        }

        // a small triangle at the right of the first row: down to expand, up to contract (as the GPS box was)
        const x = this._windBoxWidth - this._windPad - 3;
        const y = this._windRowY(0);
        this._windExpandArrow.clear();
        if (this._windExpanded) {
            this._windExpandArrow.poly([x - 4, y + 2, x + 4, y + 2, x, y - 3]);
        } else {
            this._windExpandArrow.poly([x - 4, y - 2, x + 4, y - 2, x, y + 3]);
        }
        this._windExpandArrow.fill({ color: 0x888888 });
    }

    /**
     * The wind from windEstimate.mjs ({from, speed, headwind, crosswind}) or null for none.
     * Set it after the heading: the arrow is drawn relative to the heading.
     */
    set wind(value) {
        this._wind = value || null;
        this._updateWind();
    }

    _updateWind() {
        const w = this._wind;
        const speed = w ? Math.round(w.speed) : null;
        // no arrow or direction for no wind, or a calm one (its direction means nothing)
        this._windArrow.visible = speed !== null && speed >= 1;
        if (this._windArrow.visible) {
            this._windArrow.rotation = (w.from + 180 - this._heading) * Math.PI / 180;
            let dir = Math.round(w.from) % 360;
            if (dir === 0) dir = 360;
            this._windDirText.text = String(dir).padStart(3, '0') + '°';
        } else {
            this._windDirText.text = '---°';
        }
        this._windSpeedText.text = speed !== null ? speed + ' kt' : '-- kt';

        const head = w ? Math.round(w.headwind) : null;
        const cross = w ? Math.round(w.crosswind) : null;
        this._windHeadText.windLabel.text = head !== null && head < 0 ? 'TAIL' : 'HEAD';
        this._windHeadText.text = head !== null ? Math.abs(head) + ' kt' : '-- kt';
        this._windCrossText.text = cross !== null ? Math.abs(cross) + ' kt' : '-- kt';

        // the triangles sit after the labels: HEAD down, TAIL up; XWIND from the right points left
        const markX = this._windPad + 58;
        let y = this._windRowY(2);
        this._windHeadMark.clear();
        if (head !== null && head !== 0) {
            if (head > 0) this._windHeadMark.poly([markX - 5, y - 3, markX + 5, y - 3, markX, y + 4]);
            else this._windHeadMark.poly([markX - 5, y + 3, markX + 5, y + 3, markX, y - 4]);
            this._windHeadMark.fill({ color: 0xffffff });
        }
        y = this._windRowY(3);
        this._windCrossMark.clear();
        if (cross !== null && cross !== 0) {
            if (cross > 0) this._windCrossMark.poly([markX + 4, y - 5, markX + 4, y + 5, markX - 3, y]);
            else this._windCrossMark.poly([markX - 3, y - 5, markX - 3, y + 5, markX + 4, y]);
            this._windCrossMark.fill({ color: 0xffffff });
        }
    }

    // -----------------------------------------------------------------------
    // GPS Status Box — lower-right corner of HSI display, just above the Route button. It is a touch button:
    // contracted (the normal state) it shows the fix status (bottom row) and the magnetic variation above it;
    // a touch expands it upward to show the satellites, HDOP and VDOP above those, and another touch contracts it.
    // The bottom edge stays put. The side towards the rose is scalloped to follow the HSI circle, and the
    // labels move in where it does. It starts contracted. The word WAAS beside the status is green when
    // differential corrections are in use and dim when they are not.
    // -----------------------------------------------------------------------
    _createGpsStatusBox() {
        const screenX = this.app.screen.width / 2;
        const screenY = this.app.screen.height / 2;
        const margin = 10;
        const boxWidth = 130;       // wide enough for "3D FIX WAAS" and the arrow
        const pad = 8;

        this._gpsBoxWidth = boxWidth;
        this._gpsPad = pad;
        this._gpsLineHeight = 18;
        this._gpsCornerRadius = 8;      // the stand-alone box standard (rows and buttons inside panels: 4)
        this._gpsExpanded = false;
        this._gpsFixQuality = null;     // 0 no fix, 1 GPS, 2 differential (WAAS)
        this._gpsFixType = null;        // 1 no fix, 2 2D, 3 3D
        this._gpsHdopValue = null;

        const gpsBox = new Container();
        // the origin is the bottom left corner (rows go up from it); the Route button takes the bottom 40 px
        gpsBox.position.set(screenX - margin - boxWidth, screenY - 40 - 6);
        // the scallop: the HSI circle (its centre is the HSI's, the origin of fixedContainer) plus an 8 px gap,
        // in the box's coordinates
        this._gpsScallop = { x: -gpsBox.x, y: -gpsBox.y, r: this.radius + 8 };

        this._gpsBoxBackground = new Graphics();
        gpsBox.addChild(this._gpsBoxBackground);

        // Row 0 (bottom): the fix status (colour changes dynamically) on the left, the word WAAS next to the
        // arrow that shows the box can be touched
        this._gpsFixText = new Text({ text: '---', style: new TextStyle({
            fontFamily: 'Tahoma', fontSize: 14, fill: 0x888888, fontWeight: 'bold',
        })});
        this._gpsFixText.anchor.set(0, 0.5);
        this._gpsFixText.position.set(this._gpsLabelX(0), this._gpsRowY(0));
        gpsBox.addChild(this._gpsFixText);

        this._gpsWaasText = new Text({ text: 'WAAS', style: new TextStyle({
            fontFamily: 'Tahoma', fontSize: 13, fill: 0x555555, fontWeight: 'bold',
        })});
        this._gpsWaasText.anchor.set(1, 0.5);
        this._gpsWaasText.position.set(boxWidth - pad - 12, this._gpsRowY(0));
        gpsBox.addChild(this._gpsWaasText);

        this._gpsArrow = new Graphics();
        gpsBox.addChild(this._gpsArrow);

        // Row 1: VAR, the magnetic variation (declination) the GPS module calculated, for example 10°W
        this._gpsVarText = this._createGpsRow(gpsBox, 'VAR', 1);

        // Rows 2 to 4, only while expanded, read top down: SAT, HDOP, VDOP
        this._gpsSatText = this._createGpsRow(gpsBox, 'SAT', 4);
        this._gpsHdopText = this._createGpsRow(gpsBox, 'HDOP', 3);
        this._gpsVdopText = this._createGpsRow(gpsBox, 'VDOP', 2);
        this._gpsExpandedItems = [];
        for (const row of [this._gpsSatText, this._gpsHdopText, this._gpsVdopText]) {
            this._gpsExpandedItems.push(row, row.gpsLabel);
        }

        // A touch toggles the box
        gpsBox.eventMode = 'static';
        gpsBox.cursor = 'pointer';
        gpsBox.on('pointerdown', (e) => {
            e.stopPropagation();
            this._gpsExpanded = !this._gpsExpanded;
            this._drawGpsBox();
        });

        this._drawGpsBox();
        this.fixedContainer.addChild(gpsBox);
    }

    // y of the centre of a row of the GPS box, counted up from the bottom row (0); the box's bottom is y = 0
    _gpsRowY(row) {
        return -(this._gpsPad + 5 + row * this._gpsLineHeight);
    }

    // x of the box's left edge at height y, for a box `height` tall: the straight side with rounded corners, or
    // where the HSI circle cuts in, the scallop with a rounded corner (a fillet of the same radius) where it
    // meets the top and where it meets the straight side
    _gpsEdgeX(y, height) {
        const r = this._gpsCornerRadius;
        const c = this._gpsScallop;
        const scallop = (yy) => {
            const dy = yy - c.y;
            return Math.abs(dy) >= c.r ? 0 : Math.max(0, c.x + Math.sqrt(c.r * c.r - dy * dy));
        };
        const arc = (dy) => Math.sqrt(Math.max(0, r * r - dy * dy));
        // the plain rounded corners, top left and bottom left
        let corner = 0;
        if (y < -height + r) corner = r - arc(y + height - r);
        else if (y > -r) corner = r - arc(y + r);
        if (scallop(-height) <= 0) {
            return Math.max(corner, scallop(y));    // the circle does not reach the top: no fillets
        }
        // a fillet circle touches the scallop when its centre is r + the scallop's radius from the HSI's centre;
        // the point where they touch is on the line between the two centres
        const touch = c.r / (c.r + r);
        // top fillet: centre r below the top edge
        const y1 = -height + r;
        const x1 = c.x + Math.sqrt((c.r + r) ** 2 - (y1 - c.y) ** 2);
        const p1y = c.y + (y1 - c.y) * touch;
        if (y <= p1y) return x1 - arc(y - y1);
        // lower fillet: centre r in from the straight side
        const d = (c.r + r) ** 2 - (r - c.x) ** 2;
        if (d > 0) {
            const y2 = c.y + Math.sqrt(d);
            const p2y = c.y + (y2 - c.y) * touch;
            if (y2 < -r) {
                if (y >= p2y && y <= y2) return r - arc(y - y2);
                if (y > y2) return corner;
            }
        }
        return Math.max(corner, scallop(y));
    }

    // x of a row's label: the padding in from the edge, taken over the height of the text
    _gpsLabelX(row) {
        const y = this._gpsRowY(row);
        const open = 5 * this._gpsLineHeight + 10;      // the labels are placed for the open box
        return Math.max(this._gpsEdgeX(y - 8, open), this._gpsEdgeX(y + 8, open)) + this._gpsPad;
    }

    // A label on the left and a value on the right; returns the value's Text (the label is its .gpsLabel)
    _createGpsRow(gpsBox, label, row) {
        const labelText = new Text({ text: label, style: new TextStyle({
            fontFamily: 'Tahoma', fontSize: 13, fill: 0x888888,
        })});
        labelText.anchor.set(0, 0.5);
        labelText.position.set(this._gpsLabelX(row), this._gpsRowY(row));
        gpsBox.addChild(labelText);

        const valueText = new Text({ text: '--', style: new TextStyle({
            fontFamily: 'Tahoma', fontSize: 14, fill: 0xbbbbbb,
        })});
        valueText.anchor.set(1, 0.5);
        valueText.position.set(this._gpsBoxWidth - this._gpsPad, this._gpsRowY(row));
        gpsBox.addChild(valueText);
        valueText.gpsLabel = labelText;
        return valueText;
    }

    // Redraw the box background and arrow for the contracted or expanded state
    _drawGpsBox() {
        const rows = this._gpsExpanded ? 5 : 2;
        const height = rows * this._gpsLineHeight + 10;
        const width = this._gpsBoxWidth;
        const r = this._gpsCornerRadius;

        // the outline: the right side with rounded corners, then up the left side, which follows the larger of
        // a rounded corner and the scallop
        const points = [width - r, 0];
        for (let a = 1; a <= 4; a++) {          // bottom right corner
            const t = a * Math.PI / 8;
            points.push(width - r + r * Math.sin(t), -r + r * Math.cos(t));
        }
        for (let a = 0; a <= 4; a++) {          // top right corner
            const t = a * Math.PI / 8;
            points.push(width - r + r * Math.cos(t), -height + r - r * Math.sin(t));
        }
        for (let y = -height; y <= 0; y += 0.5) {   // the left side, top to bottom
            points.push(this._gpsEdgeX(y, height), y);
        }

        this._gpsBoxBackground.clear();
        this._gpsBoxBackground.poly(points);
        this._gpsBoxBackground.fill({ color: 0x222222, alpha: 0.85 });
        this._gpsBoxBackground.stroke({ color: 0x666666, width: 1, alpha: 0.8 });

        for (const item of this._gpsExpandedItems) {
            item.visible = this._gpsExpanded;
        }

        // a small triangle at the right of the bottom row: up to expand (the box grows upward), down to contract
        const x = width - this._gpsPad - 3;
        const y = this._gpsRowY(0);
        this._gpsArrow.clear();
        if (this._gpsExpanded) {
            this._gpsArrow.poly([x - 4, y - 2, x + 4, y - 2, x, y + 3]);
        } else {
            this._gpsArrow.poly([x - 4, y + 2, x + 4, y + 2, x, y - 3]);
        }
        this._gpsArrow.fill({ color: 0x888888 });
    }

    // The status word and its colour: NO FIX is red, 2D FIX yellow, 3D FIX the colour of the HDOP quality
    _updateGpsStatus() {
        let text = '---';
        let colour = 0x888888;
        if (this._gpsFixType === 3) {
            text = '3D FIX';
            colour = this._gpsHdopValue !== null ? this._dopColor(this._gpsHdopValue) : 0xbbbbbb;
        } else if (this._gpsFixType === 2) {
            text = '2D FIX';
            colour = 0xffff00;
        } else if (this._gpsFixType !== null) {
            text = 'NO FIX';
            colour = 0xff0000;
        } else if (this._gpsFixQuality === 0) {
            text = 'NO FIX';                    // an older server that only sends the fix quality
            colour = 0xff0000;
        } else if (this._gpsFixQuality !== null) {
            text = 'FIX';
            colour = 0xbbbbbb;
        }
        this._gpsFixText.text = text;
        this._gpsFixText.style.fill = colour;
    }

    /**
     * Fix quality from the GPS module: 0 no fix, 1 GPS, 2 differential (WAAS). The word WAAS is green when
     * the corrections are in use and dim when they are not.
     */
    set gpsFixQuality(v) {
        this._gpsFixQuality = (v === null || v === undefined) ? null : v;
        this._gpsWaasText.style.fill = this._gpsFixQuality === 2 ? 0x00ff00 : 0x555555;
        this._updateGpsStatus();
    }

    /**
     * Fix type from the GPS module: 1 no fix, 2 2D, 3 3D. Gives the status word.
     */
    set gpsFixType(v) {
        this._gpsFixType = (v === null || v === undefined) ? null : v;
        this._updateGpsStatus();
    }

    set gpsSatellites(v) {
        this._gpsSatText.text = (v !== null && v !== undefined) ? String(v) : '--';
    }

    _dopColor(v) {
        if (v < 1.5) return 0x00ff00;   // green — excellent
        if (v < 3.0) return 0x00ffff;   // cyan — good
        if (v < 5.0) return 0xffff00;   // yellow — moderate
        return 0xff4444;                 // red — poor
    }

    set gpsHdop(v) {
        this._gpsHdopValue = (v !== null && v !== undefined) ? v : null;
        if (this._gpsHdopValue !== null) {
            this._gpsHdopText.text = v.toFixed(1);
            this._gpsHdopText.style.fill = this._dopColor(v);
        } else {
            this._gpsHdopText.text = '--';
            this._gpsHdopText.style.fill = 0xbbbbbb;
        }
        this._updateGpsStatus();
    }

    /**
     * Magnetic declination in whole degrees, East positive (null if the GPS module has not given one)
     */
    set gpsDeclination(v) {
        if (v === null || v === undefined) {
            this._gpsVarText.text = '--';
        } else {
            this._gpsVarText.text = Math.abs(v) + '°' + (v < 0 ? 'W' : (v > 0 ? 'E' : ''));
        }
    }

    set gpsVdop(v) {
        if (v !== null && v !== undefined) {
            this._gpsVdopText.text = v.toFixed(1);
            this._gpsVdopText.style.fill = this._dopColor(v);
        } else {
            this._gpsVdopText.text = '--';
            this._gpsVdopText.style.fill = 0xbbbbbb;
        }
    }

    // -----------------------------------------------------------------------
    // Course deviation property — positions the CDI bar
    // -----------------------------------------------------------------------
    set courseDeviation(nm) {
        this._courseDeviation = nm;
        // Convert NM to pixels: full-scale (±cdiFullScaleNM) maps to ±2 dots (40px)
        const dotSpacing = 20;
        const maxOffsetPx = 2 * dotSpacing;  // 40px = full scale
        let offsetPx = (nm / this._cdiFullScaleNM) * maxOffsetPx;
        // Clamp to full-scale deflection
        offsetPx = Math.max(-maxOffsetPx, Math.min(maxOffsetPx, offsetPx));
        this._drawCdiBar(Math.round(offsetPx));
    }

    get courseDeviation() {
        return this._courseDeviation;
    }

    // -----------------------------------------------------------------------
    // TO/FROM property — shows/hides TO and FROM triangles
    // -----------------------------------------------------------------------
    set toFrom(value) {
        this._toFrom = value;
        if (!this._gpsValid) {
            this.toIndicator.visible = false;
            this.fromIndicator.visible = false;
        } else if (value === 'TO') {
            this.toIndicator.visible = true;
            this.fromIndicator.visible = false;
        } else if (value === 'FROM') {
            this.toIndicator.visible = false;
            this.fromIndicator.visible = true;
        } else {
            this.toIndicator.visible = false;
            this.fromIndicator.visible = false;
        }
    }

    get toFrom() {
        return this._toFrom;
    }

    // -----------------------------------------------------------------------
    // Ground track property — positions the track diamond
    // -----------------------------------------------------------------------
    set groundTrack(value) {
        value = ((value % 360) + 360) % 360;
        this._groundTrack = value;

        // Rotate the track container relative to current heading
        // (track - heading) gives the angular offset from the lubber line
        const offset = (value - this._heading) * Math.PI / 180;
        this.trackContainer.rotation = offset;
    }

    get groundTrack() {
        return this._groundTrack;
    }

    // -----------------------------------------------------------------------
    // Heading property — rotates the rose and updates the readout
    // -----------------------------------------------------------------------
    /**
     * Show, dim or hide everything that depends on the heading (compass rose, ground track,
     * heading bug, course pointer and the digital readout, which shows --- when hidden). Hidden when the heading cannot be trusted, dimmed when
     * it comes from the gyro only. The page puts a red X over the HSI.
     * @param {boolean} show true to draw them
     * @param {number} alpha opacity to draw them with (1 is normal)
     */
    setHeadingDisplay(show, alpha) {
        this._headingShown = show;
        const dependents = [this.roseContainer, this.bugContainer, this.courseContainer];
        for (const container of dependents) {
            container.visible = show;
            container.alpha = alpha;
        }
        this._applyBugCourseVisibility();      // the bug only when set, the course needle only with a DTK
        this.trackContainer.alpha = alpha;
        this.headingBox.alpha = alpha;
        if (!show) {
            this.headingText.text = '---';      // never leave the last heading on the readout
        }
        this._applyGpsVisibility();
    }

    // -----------------------------------------------------------------------
    // GPS validity — hides what only makes sense with a GPS position
    // -----------------------------------------------------------------------
    /**
     * Set by the page: false when the GPS position or speed cannot be trusted (module missing, no
     * data, no fix). The ground track diamond, the cross track bar and the TO/FROM triangles are then
     * hidden; the page passes null for the ground speed and the waypoint readout.
     * @param {boolean} isValid
     */
    set gpsValid(isValid) {
        this._gpsValid = isValid;
        this._applyGpsVisibility();
    }

    /**
     * Set by the page: false when the GPS ground track is not worth drawing (the aircraft is moving too slowly,
     * so the track is noise). The ground track diamond is hidden then.
     * @param {boolean} isUsable
     */
    set trackUsable(isUsable) {
        this._trackUsable = isUsable;
        this._applyGpsVisibility();
    }

    _applyGpsVisibility() {
        this.trackContainer.visible = this._headingShown && this._gpsValid && this._trackUsable;
        this.cdiBar.visible = this._gpsValid;
        this.toFrom = this._toFrom;     // shows or hides the TO/FROM triangles for the current state
        this._updateBearingPointer();
    }

    set heading(value) {
        // Normalise to 0-360
        value = ((value % 360) + 360) % 360;
        this._heading = value;

        // Rotate rose (negative because heading-up means rotating card opposite)
        this.roseContainer.rotation = -value * Math.PI / 180;

        // Update digital readout (zero-padded 3 digits)
        this.headingText.text = String(Math.round(value)).padStart(3, '0') + '°';   // 256°, as on the G5's HSI

        // Reposition ground track indicator relative to new heading
        const trackOffset = (this._groundTrack - value) * Math.PI / 180;
        this.trackContainer.rotation = trackOffset;

        // Reposition heading bug relative to new heading
        this._updateBugPosition();
        this._updateBearingPointer();
    }

    get heading() {
        return this._heading;
    }
}
