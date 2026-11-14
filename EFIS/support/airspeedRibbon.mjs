'use strict';

export const VERSION = '1.0.4';
import { Container, Graphics, Text, TextStyle } from './pixi.mjs';
import { Ribbon, Colour_Bar } from './ribbon.mjs';

// Airspeed limits in knots until the server sends the ones kept on the Pi (aircraft_settings.py, the same
// defaults: David's Sonex flight manual, 40 / 46 / 100 / 125 / 197 mph CAS)
const DEFAULT_LIMITS = {
    vso: 35,    // Stall speed, landing configuration (bottom of white arc, top of the low-speed red line)
    vs1: 40,    // Stall speed, clean configuration (bottom of green arc)
    vfe: 87,    // Maximum flap extended speed (top of white arc); was 99, from 114 mph
    vno: 108,   // Maximum structural cruising speed (green→yellow transition)
    vne: 171,   // Never exceed speed (yellow→red line, barber pole above)
};
// V-speed tags until the server sends the ones kept on the Pi (aircraft_settings.py, the same defaults)
const DEFAULT_VSPEEDS = {
    vr: 52,     // rotation
    vx: 70,     // best angle of climb
    vy: 74,     // best rate of climb
    vg: 61,     // best glide
};
const STRIP_TOP = 300;              // knots: the colour strips end here
const LOW_SPEED_COLOUR = 0xff0000;  // red line below Vso, where the white arc would be (David, 2026-11-14)
const GREEN = 0x00ff00;
const YELLOW = 0xffff00;
const RED = 0xff0000;
const WHITE = 0xffffff;
const BARBER_STRIPE = 6;            // pixels: height of each red and each white stripe above Vne

// V-speed tags (G5 2.1.1.3): a cyan letter in a black flag just right of the tape, pointing at the speed
const TAG_COLOUR = 0x00ffff;        // cyan: set by the pilot
const TAG_X = 86;                   // pixels: the flag's point, at the tape's right edge
const TAG_WIDTH = 22;               // pixels from the point to the flag's right side
const TAG_HEIGHT = 18;
const LIST_LINE = 20;               // pixels between lines of the list shown when the airspeed fails

// Airspeed trend vector (G5 2.1.1.2)
const TREND_COLOUR = 0xFF6AFF;      // light magenta, as the GPS readouts
const TREND_SECONDS = 6;            // the line ends at the speed this far ahead
const TREND_SMOOTHING = 1.0;        // seconds: time constant of the speed filter and again of the rate filter
const TREND_MIN_KNOTS = 2;          // shorter trends are not drawn
const TREND_X = 10;                 // pixels: just right of the colour strip
const TREND_WIDTH = 4;
const TREND_MAX_GAP = 0.5;          // seconds: a longer gap between frames restarts the filters

export class AirspeedRibbon extends Ribbon {
    constructor(app, x, y, height, width, rightSideMarks,
        majorIntervalSize = 100, majorIntervals = 4, minorIntervals = 4,
        allowNegative) {

        let scale = (height / majorIntervals) / majorIntervalSize;

        // The colour strips are drawn by setLimits (they change with the settings): the speeds strip (left
        // edge, 10 px: green, yellow, barber pole) and over its right half the white flap arc with the low-speed
        // red line below it.
        // Their graphics are in knots up the tape (the Colour_Bar transform flips and scales y).
        let speeds_bar = new Colour_Bar([], 0, 10, STRIP_TOP, scale, false);
        let flaps_bar = new Colour_Bar([], 5, 5, STRIP_TOP, scale, false);

        super(app, x, y, height, width, rightSideMarks,
            majorIntervalSize, majorIntervals, minorIntervals, allowNegative,
            speeds_bar, flaps_bar);

        this.knotsPerPixel = 1 / scale;
        this.limits = null;
        this.setLimits(DEFAULT_LIMITS);

        // Trend vector: drawn over the tick marks, under the numbers and the pointer box. Its origin is the
        // pointer (the middle of the tape); it is redrawn each frame from there up or down.
        this.trendGraphic = new Graphics();
        this.trendGraphic.visible = false;
        this.ribbon_container.addChildAt(this.trendGraphic, this.ribbon_container.getChildIndex(this.ruler) + 1);
        this.trendSpeed = null;         // knots, smoothed
        this.trendRate = 0;             // knots per second, smoothed
        this.trendLastSeconds = null;
        this.trendKnots = 0;            // knots the speed will change in TREND_SECONDS, 0 while the line is hidden

        // V-speed tags: their own container (they stick out of the tape, past its mask), drawn under the airspeed
        // readout box made after the tape. Its origin is the pointer at 0 kt; it moves with the airspeed.
        this.tagOriginY = y;
        this.tagContainer = new Container();
        this.tagContainer.position.set(x, y);
        this.tagMask = new Graphics();
        this.tagMask.rect(x + TAG_X - 2, y - height / 2, TAG_WIDTH + 4, height);
        this.tagMask.fill({ color: 0xff0000 });
        this.tagContainer.mask = this.tagMask;
        app.stage.addChild(this.tagContainer);
        this.vspeeds = null;
        this.vspeedList = null;         // made by createVSpeedList, over the failure flag
        this.setVSpeeds(DEFAULT_VSPEEDS);
    }

    /**
     * Make the tags for these V-speeds (knots), largest at the top of the list. Called every frame with the
     * server's settings; only remade when they change. Ignored if any is missing.
     * @param {{vr:number, vx:number, vy:number, vg:number}} vspeeds
     */
    setVSpeeds(vspeeds) {
        if (!vspeeds) return;
        const names = Object.keys(DEFAULT_VSPEEDS);
        if (names.some(n => typeof vspeeds[n] !== 'number')) return;
        if (this.vspeeds && names.every(n => this.vspeeds[n] === vspeeds[n])) return;
        this.vspeeds = Object.fromEntries(names.map(n => [n, vspeeds[n]]));

        for (const child of this.tagContainer.removeChildren()) child.destroy({ children: true });
        for (const name of names) {
            const speed = this.vspeeds[name];
            const tag = new Container();
            tag.position.set(TAG_X, -speed * this.ribbon_interval_ratio);
            const flag = new Graphics();
            const half = TAG_HEIGHT / 2;
            flag.poly([0, 0, 6, -half, TAG_WIDTH, -half, TAG_WIDTH, half, 6, half]);
            flag.fill({ color: 0x000000 });
            flag.stroke({ color: TAG_COLOUR, width: 1.5, alignment: 1 });
            tag.addChild(flag);
            const letter = new Text({
                text: name.charAt(1).toUpperCase(),
                style: new TextStyle({ fontFamily: 'Tahoma', fontSize: 15, fontWeight: 'bold', fill: TAG_COLOUR }),
            });
            letter.anchor.set(0.5, 0.5);
            letter.position.set(6 + (TAG_WIDTH - 6) / 2, 0);
            tag.addChild(letter);
            this.tagContainer.addChild(tag);
        }
        this.updateVSpeedList();
    }

    /**
     * The list of V-speeds shown at the foot of the tape when the airspeed fails (G5: the tags cannot be placed).
     * Called by the page after the failure flags are made, so it is drawn over them.
     * @param {object} app The PixiJS application
     * @param {number} x Left edge of the tape
     * @param {number} bottom Pixels: the bottom of the tape
     */
    createVSpeedList(app, x, bottom) {
        this.vspeedList = new Container();
        this.vspeedList.position.set(x, bottom);
        this.vspeedList.visible = false;
        app.stage.addChild(this.vspeedList);
        this.updateVSpeedList();
    }

    updateVSpeedList() {
        if (!this.vspeedList || !this.vspeeds) return;
        for (const child of this.vspeedList.removeChildren()) child.destroy({ children: true });
        const entries = Object.entries(this.vspeeds).sort((a, b) => b[1] - a[1]);
        const height = entries.length * LIST_LINE + 8;
        const back = new Graphics();
        back.rect(8, -height - 6, 74, height);
        back.fill({ color: 0x000000 });
        this.vspeedList.addChild(back);
        entries.forEach(([name, speed], i) => {
            const y = -height - 6 + 4 + (i + 0.5) * LIST_LINE;
            const style = { fontFamily: 'Tahoma', fontSize: 17, fill: TAG_COLOUR };
            const label = new Text({ text: 'V' + name.charAt(1), style: new TextStyle(style) });
            label.anchor.set(0, 0.5);
            label.position.set(16, y);
            const value = new Text({ text: String(speed), style: new TextStyle(style) });
            value.anchor.set(1, 0.5);
            value.position.set(74, y);
            this.vspeedList.addChild(label, value);
        });
    }

    /**
     * Move the V-speed tags with the airspeed, called every frame. With no valid airspeed the tags are hidden and
     * the list is shown instead.
     * @param {number|null} airspeed Knots, null when not valid
     * @param {boolean} shown The pilot's V-Speed Tags switch
     */
    updateTags(airspeed, shown) {
        const valid = airspeed !== null && airspeed !== undefined;
        this.tagContainer.visible = shown && valid;
        if (this.vspeedList) this.vspeedList.visible = shown && !valid;
        if (shown && valid) this.tagContainer.y = this.tagOriginY + airspeed * this.ribbon_interval_ratio;
    }

    /**
     * Draw the colour strips for these airspeed limits (knots). Called every frame with the server's settings;
     * only redraws when they change. Ignored if any is missing.
     * @param {{vso:number, vs1:number, vfe:number, vno:number, vne:number}} limits
     */
    setLimits(limits) {
        if (!limits) return;
        const names = ['vso', 'vs1', 'vfe', 'vno', 'vne'];
        if (names.some(n => typeof limits[n] !== 'number')) return;
        if (this.limits && names.every(n => this.limits[n] === limits[n])) return;
        this.limits = Object.fromEntries(names.map(n => [n, limits[n]]));
        const { vso, vs1, vfe, vno, vne } = this.limits;
        const k = this.knotsPerPixel;

        // clear() also resets the drawing transform (knots up the tape) that Colour_Bar set: set it again
        const speeds = this.colour_bar1.graphics;
        speeds.clear();
        speeds.setTransform(this.colour_bar1.matrix);
        // barber pole above Vne: white with red stripes slanting up to the right, started a stripe below Vne so
        // the slant fills the strip; the yellow drawn after it covers what is below Vne
        speeds.rect(0, vne, 10, STRIP_TOP - vne).fill({ color: WHITE });
        const slant = 10 * k;
        for (let y = vne - 2 * BARBER_STRIPE * k; y < STRIP_TOP; y += 2 * BARBER_STRIPE * k) {
            speeds.poly([0, y, 10, y + slant, 10, y + slant + BARBER_STRIPE * k, 0, y + BARBER_STRIPE * k])
                  .fill({ color: RED });
        }
        speeds.rect(0, vs1, 10, vno - vs1).fill({ color: GREEN });
        speeds.rect(0, vno, 10, vne - vno).fill({ color: YELLOW });
        // red line at Vne, 2 px, across the strip and the tick marks
        speeds.rect(-10, vne, 30, 2 * k).fill({ color: RED });

        const flaps = this.colour_bar2.graphics;
        flaps.clear();
        flaps.setTransform(this.colour_bar2.matrix);
        flaps.rect(0, 0, 5, vso).fill({ color: LOW_SPEED_COLOUR });
        flaps.rect(0, vso, 5, vfe - vso).fill({ color: WHITE });
    }

    /**
     * Overspeed cue for the airspeed readout: 'warning' at or above Vne, 'caution' when the trend vector reaches
     * Vne, otherwise null.
     * @param {number|null} airspeed Indicated airspeed in knots, null when it is not valid
     */
    overspeedLevel(airspeed) {
        if (airspeed === null || airspeed === undefined || !this.limits) return null;
        if (airspeed >= this.limits.vne) return 'warning';
        if (airspeed + this.trendKnots >= this.limits.vne) return 'caution';
        return null;
    }

    /**
     * Airspeed trend vector, called every frame: a light magenta line from the pointer to the speed TREND_SECONDS
     * ahead at the present rate of change (G5 2.1.1.2). The speed is smoothed and then its rate is smoothed again,
     * so pitot noise does not make it jump. Hidden under TREND_MIN_KNOTS and while the airspeed is not valid.
     * @param {number|null} airspeed Indicated airspeed in knots, null when it is not valid
     * @param {number} nowSeconds Time in seconds
     */
    updateTrend(airspeed, nowSeconds) {
        const dt = (this.trendLastSeconds === null) ? 0 : nowSeconds - this.trendLastSeconds;
        this.trendLastSeconds = nowSeconds;
        if (airspeed === null || airspeed === undefined || this.trendSpeed === null || dt <= 0 || dt > TREND_MAX_GAP) {
            // no airspeed, the first frame or a long gap: start again from here
            this.trendSpeed = (airspeed === null || airspeed === undefined) ? null : airspeed;
            this.trendRate = 0;
            this.trendKnots = 0;
            this.trendGraphic.visible = false;
            return;
        }

        const k = dt / (TREND_SMOOTHING + dt);
        const previousSpeed = this.trendSpeed;
        this.trendSpeed += (airspeed - this.trendSpeed) * k;
        this.trendRate += ((this.trendSpeed - previousSpeed) / dt - this.trendRate) * k;

        const trendKnots = this.trendRate * TREND_SECONDS;
        if (Math.abs(trendKnots) < TREND_MIN_KNOTS) {
            this.trendKnots = 0;
            this.trendGraphic.visible = false;
            return;
        }
        this.trendKnots = trendKnots;
        // up the tape is negative y; the line stops at the tape's edge
        const halfHeight = this.ribbonHeight / 2;
        const end = Math.max(-halfHeight, Math.min(halfHeight, -trendKnots * this.ribbon_interval_ratio));
        this.trendGraphic.clear();
        this.trendGraphic.rect(TREND_X, Math.min(0, end), TREND_WIDTH, Math.abs(end));
        this.trendGraphic.fill({ color: TREND_COLOUR });
        this.trendGraphic.visible = true;
    }
}
