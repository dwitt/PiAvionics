'use strict';

export const VERSION = '1.0.3';
import { Graphics } from './pixi.mjs';
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
const STRIP_TOP = 300;              // knots: the colour strips end here
const LOW_SPEED_COLOUR = 0xff0000;  // red line below Vso, where the white arc would be (David, 2026-11-14)
const GREEN = 0x00ff00;
const YELLOW = 0xffff00;
const RED = 0xff0000;
const WHITE = 0xffffff;
const BARBER_STRIPE = 6;            // pixels: height of each red and each white stripe above Vne

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
