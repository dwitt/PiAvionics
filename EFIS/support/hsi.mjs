'use strict';

export const VERSION = '1.0.8';

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
        this._bugValue = 0;
        this._groundSpeed = 0;
        this._bugIncrement = 1;
        this._bugFastIncrement = 5;
        this._bugFastDeltaThreshold = 3;
        this._lastEncoderValue = 0;
        this._courseDeviation = 0;   // cross-track error in NM
        this._toFrom = null;         // "TO", "FROM", or null
        this._headingShown = true;   // false when the heading cannot be trusted (see setHeadingDisplay)
        this._gpsValid = true;       // false when the GPS data cannot be trusted (see gpsValid)
        this._trackUsable = true;    // false when the ground track means nothing, for example at low speed (see trackUsable)
        this._cdiFullScaleNM = 2;    // full-scale CDI deflection in NM
        this._desiredTrack = null;   // DTK from flight plan (overrides CRS knob)
        this._waypointDistance = null;
        this._waypointId = null;
        this.changeableFirstPass = false;
        this.selected = false;
        this.changable = false;

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
        this._createCoursePointer(); // Phase 2: needle, CDI, TO/FROM
        this._createLubberLine();
        this._createAircraftSymbol();
        this._createHeadingBox();
        this._createTrackIndicator();
        this._createBugReadout();
        this._createGroundSpeedReadout();
        this._createNavReadout();
        this._createGpsStatusBox();
        this._createSelectionRing();
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
        let course;
        if (this._desiredTrack !== null && this._desiredTrack !== undefined) {
            course = this._desiredTrack;  // flight plan DTK drives pointer
        } else {
            course = this._bugValue;      // manual CRS knob drives pointer
        }
        const offset = (course - this._heading) * Math.PI / 180;
        this.courseContainer.rotation = offset;
    }

    // -----------------------------------------------------------------------
    // Heading bug readout — fixed cyan box showing bug value
    // -----------------------------------------------------------------------
    _createBugReadout() {
        const screenX = this.app.screen.width / 2;
        const screenY = this.app.screen.height / 2;
        const margin = 10;
        const boxWidth = 100;
        const boxHeight = 26;
        const pad = 6;

        const bugBox = new Container();
        // Position in upper-right corner of HSI display window
        bugBox.position.set(screenX - margin - boxWidth / 2, -screenY + margin + boxHeight / 2);

        this.bugBoxWidth = boxWidth;
        this.bugBoxHeight = boxHeight;
        this.bugBoxBg = new Graphics();
        this._drawBugBox(0x666666);
        bugBox.addChild(this.bugBoxBg);

        const labelStyle = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: 16,
            fill: 0x00ffff,
        });
        const label = new Text({ text: 'CRS', style: labelStyle });
        label.anchor.set(0, 0.5);
        label.position.set(-boxWidth / 2 + pad, 0);
        bugBox.addChild(label);

        const valueStyle = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: 20,
            fill: 0x00ffff,
        });
        this.bugText = new Text({ text: '000°', style: valueStyle });
        this.bugText.anchor.set(1, 0.5);
        this.bugText.position.set(boxWidth / 2 - pad, 0);
        bugBox.addChild(this.bugText);

        this.fixedContainer.addChild(bugBox);
    }

    _drawBugBox(color, editing = false) {
        // Editing: 3 px outline instead of 2, black fill, as the PFD boxes (displayRectangle.mjs)
        const w = this.bugBoxWidth;
        const h = this.bugBoxHeight;
        this.bugBoxBg.clear();
        this.bugBoxBg.fillStyle = { color: 0x000000 };
        this.bugBoxBg.strokeStyle = { color: color, width: editing ? 3 : 2 };
        this.bugBoxBg.roundRect(-w / 2, -h / 2, w, h, 4);
        this.bugBoxBg.fill();
        this.bugBoxBg.stroke();
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
        this._desiredTrack = value;
        this._updateCoursePointerRotation();
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
    // Heading bug property — positions the bug triangle
    // -----------------------------------------------------------------------
    set headingBug(value) {
        value = ((value % 360) + 360) % 360;
        this._bugValue = value;
        this.bugText.text = String(Math.round(value)).padStart(3, '0') + '°';
        this._updateBugPosition();
    }

    get headingBug() {
        return this._bugValue;
    }

    _updateBugPosition() {
        const offset = (this._bugValue - this._heading) * Math.PI / 180;
        this.bugContainer.rotation = offset;
        this._updateCoursePointerRotation();
    }

    // -----------------------------------------------------------------------
    // GPS Status Box — upper-left corner of HSI display. It is a touch button:
    // contracted (the normal state) it shows the fix status and the magnetic variation;
    // a touch expands it to show the satellites, HDOP and VDOP, and another touch contracts it.
    // It starts contracted. The word WAAS beside the status is green when differential
    // corrections are in use and dim when they are not.
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
        this._gpsExpanded = false;
        this._gpsFixQuality = null;     // 0 no fix, 1 GPS, 2 differential (WAAS)
        this._gpsFixType = null;        // 1 no fix, 2 2D, 3 3D
        this._gpsHdopValue = null;

        const gpsBox = new Container();
        // the origin is the top left corner so the box grows downward when it expands
        gpsBox.position.set(-screenX + margin, -screenY + margin);

        this._gpsBoxBackground = new Graphics();
        gpsBox.addChild(this._gpsBoxBackground);

        // Row 0: the fix status (colour changes dynamically) on the left, the word WAAS next to the arrow
        // that shows the box can be touched
        this._gpsFixText = new Text({ text: '---', style: new TextStyle({
            fontFamily: 'Tahoma', fontSize: 14, fill: 0x888888, fontWeight: 'bold',
        })});
        this._gpsFixText.anchor.set(0, 0.5);
        this._gpsFixText.position.set(pad, this._gpsRowY(0));
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

        // Rows 2 to 4, only while expanded
        this._gpsSatText = this._createGpsRow(gpsBox, 'SAT', 2);
        this._gpsHdopText = this._createGpsRow(gpsBox, 'HDOP', 3);
        this._gpsVdopText = this._createGpsRow(gpsBox, 'VDOP', 4);
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

    // y of the centre of a row of the GPS box
    _gpsRowY(row) {
        return this._gpsPad + 7 + row * this._gpsLineHeight;
    }

    // A label on the left and a value on the right; returns the value's Text (the label is its .gpsLabel)
    _createGpsRow(gpsBox, label, row) {
        const labelText = new Text({ text: label, style: new TextStyle({
            fontFamily: 'Tahoma', fontSize: 13, fill: 0x888888,
        })});
        labelText.anchor.set(0, 0.5);
        labelText.position.set(this._gpsPad, this._gpsRowY(row));
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
        const height = this._gpsRowY(rows - 1) + 13;

        this._gpsBoxBackground.clear();
        this._gpsBoxBackground.fillStyle = { color: 0x222222, alpha: 0.85 };
        this._gpsBoxBackground.strokeStyle = { color: 0x666666, width: 1, alpha: 0.8 };
        this._gpsBoxBackground.roundRect(0, 0, this._gpsBoxWidth, height, 6);
        this._gpsBoxBackground.fill();
        this._gpsBoxBackground.stroke();

        for (const item of this._gpsExpandedItems) {
            item.visible = this._gpsExpanded;
        }

        // a small triangle at the right of the first row: down to expand, up to contract
        const x = this._gpsBoxWidth - this._gpsPad - 3;
        const y = this._gpsRowY(0);
        this._gpsArrow.clear();
        if (this._gpsExpanded) {
            this._gpsArrow.poly([x - 4, y + 2, x + 4, y + 2, x, y - 3]);
        } else {
            this._gpsArrow.poly([x - 4, y - 2, x + 4, y - 2, x, y + 3]);
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
    // Selection ring — circle around the rose, hidden by default
    // Cyan: thin when selected, thick when editing
    // -----------------------------------------------------------------------
    _createSelectionRing() {
        const r = this.radius;
        this.selectionRing = new Graphics();
        this.selectionRing.visible = false;
        this.fixedContainer.addChild(this.selectionRing);
    }

    _drawSelectionRing(color, editing = false) {
        const r = this.radius - 2;  // match tick mark outer edge
        this.selectionRing.clear();
        this.selectionRing.strokeStyle = { color: color, width: editing ? 4 : 2 };   // thick when editing
        this.selectionRing.circle(0, 0, r);
        this.selectionRing.stroke();
        this.selectionRing.visible = true;
    }

    // -----------------------------------------------------------------------
    // Encoder callback — matches the pattern used by headingIndicator
    // -----------------------------------------------------------------------
    callback(selected, changable, value) {
        // Handle changeable state transitions
        if (changable && !this.changable) {
            this.changable = true;
            this.changeableFirstPass = true;
            this.selected = false;
        }

        if (!changable && this.changable) {
            this.changable = false;
        }

        if (!changable && (selected && !this.selected)) {
            this.selected = true;
        }

        if (!selected && this.selected) {
            this.selected = false;
        }

        // Update selection ring and CRS box border
        if (changable) {
            this._drawSelectionRing(0x00ffff, true);    // cyan, thick — editing
            this._drawBugBox(0x00ffff, true);
        } else if (selected) {
            this._drawSelectionRing(0x00ffff);          // cyan, thin — selected (red is kept for warnings and failures)
            this._drawBugBox(0x00ffff);
        } else {
            this.selectionRing.visible = false;
            this._drawBugBox(0x666666);
        }

        // Process encoder value
        if (changable && !this.changeableFirstPass) {
            let delta = value - this._lastEncoderValue;
            this._lastEncoderValue = value;
            let increment = (Math.abs(delta) >= this._bugFastDeltaThreshold)
                ? this._bugFastIncrement : this._bugIncrement;
            this._bugValue = this._bugValue + delta * increment;
            this._bugValue = ((this._bugValue % 360) + 360) % 360;
            this.bugText.text = String(Math.round(this._bugValue)).padStart(3, '0') + '°';
            this._updateBugPosition();
        } else if (this.changeableFirstPass) {
            this._lastEncoderValue = value;
            this.changeableFirstPass = false;
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
    }

    get heading() {
        return this._heading;
    }
}
