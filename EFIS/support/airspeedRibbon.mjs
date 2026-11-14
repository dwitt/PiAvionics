'use strict';

export const VERSION = '1.0.1';
import { Graphics } from './pixi.mjs';
import { Ribbon, Colour_Bar } from './ribbon.mjs';

// V-Speed Configuration (will be made editable via menu system)
const V_SPEEDS = {
    vso: 35,    // Stall speed, landing configuration (bottom of white arc)
    vs1: 40,    // Stall speed, clean configuration (bottom of green arc)
    vfe: 99,    // Maximum flap extended speed (top of white arc)
    vno: 108,   // Maximum structural cruising speed (green→yellow transition)
    vne: 171,   // Never exceed speed (yellow→red line)
};

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

        // Green/Yellow/Red strip (left half of bar, x=0, width=10)
        let speeds_colours = [
            {position: V_SPEEDS.vs1, colour: 0x00ff00},   // Green: Vs1 to Vno
            {position: V_SPEEDS.vno, colour: 0xffff00},   // Yellow: Vno to Vne
            {position: V_SPEEDS.vne, colour: 0xff0000},   // Red: Vne and above
        ];
        let speeds_bar = new Colour_Bar(speeds_colours, 0, 10, 300, scale, false);

        // Add a red Vne tick mark to the speeds bar graphics
        // The matrix transform handles the vertical flip and scale,
        // so positions are in knots and the tick height is in knots too
        let tickHeightInKnots = 2 / scale;  // 2 pixels converted to knot-space
        speeds_bar.graphics.fillStyle = { alpha: 1, color: 0xff0000 };
        speeds_bar.graphics.rect(-10, V_SPEEDS.vne, 30, tickHeightInKnots);
        speeds_bar.graphics.fill();

        // White strip (overlaps green on right side, x=5, width=5)
        let flaps_colours = [
            {position: V_SPEEDS.vso, colour: 0xffffff},   // White: Vso to Vfe
        ];
        let flaps_bar = new Colour_Bar(flaps_colours, 5, 5, V_SPEEDS.vfe, scale, false);

        super(app, x, y, height, width, rightSideMarks,
            majorIntervalSize, majorIntervals, minorIntervals, allowNegative,
            speeds_bar, flaps_bar);

        // Trend vector: drawn over the tick marks, under the numbers and the pointer box. Its origin is the
        // pointer (the middle of the tape); it is redrawn each frame from there up or down.
        this.trendGraphic = new Graphics();
        this.trendGraphic.visible = false;
        this.ribbon_container.addChildAt(this.trendGraphic, this.ribbon_container.getChildIndex(this.ruler) + 1);
        this.trendSpeed = null;         // knots, smoothed
        this.trendRate = 0;             // knots per second, smoothed
        this.trendLastSeconds = null;
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
            this.trendGraphic.visible = false;
            return;
        }

        const k = dt / (TREND_SMOOTHING + dt);
        const previousSpeed = this.trendSpeed;
        this.trendSpeed += (airspeed - this.trendSpeed) * k;
        this.trendRate += ((this.trendSpeed - previousSpeed) / dt - this.trendRate) * k;

        const trendKnots = this.trendRate * TREND_SECONDS;
        if (Math.abs(trendKnots) < TREND_MIN_KNOTS) {
            this.trendGraphic.visible = false;
            return;
        }
        // up the tape is negative y; the line stops at the tape's edge
        const halfHeight = this.ribbonHeight / 2;
        const end = Math.max(-halfHeight, Math.min(halfHeight, -trendKnots * this.ribbon_interval_ratio));
        this.trendGraphic.clear();
        this.trendGraphic.rect(TREND_X, Math.min(0, end), TREND_WIDTH, Math.abs(end));
        this.trendGraphic.fill({ color: TREND_COLOUR });
        this.trendGraphic.visible = true;
    }
}
