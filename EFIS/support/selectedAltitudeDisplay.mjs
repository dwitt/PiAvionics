'use strict';

export const VERSION = '1.0.1';

import { Graphics } from './pixi.mjs';
import { DisplayRectangle } from './displayRectangle.mjs';

const SELECTED_COLOUR = 0x00FFFF;   // cyan: a value the pilot sets, as the bug on the tape
const INCREMENT = 25;               // feet per knob step
const FAST_INCREMENT = 100;         // feet per step when the knob is turned quickly
const FAST_DELTA_THRESHOLD = 6;     // steps in one update at or above this use FAST_INCREMENT
const MIN_VALUE = -1000;
const MAX_VALUE = 12500;

/**
 * Selected altitude (the altitude bug), always on screen in a square box just below the GPS / PRE / DEN box at
 * the top of the altitude tape, drawn over the top of the tape. A small bug symbol right of "ft" says it is the
 * bug setting (the G5 shows one beside its selected altitude). Shows dashes until a target is set; the first turn
 * starts from the current altitude rounded to 100 ft. It is the knob item for the bug: the tape only draws it.
 *
 * While the box is selected or being edited, the tape also shows its cyan line up the side.
 *
 * The box sits 2 px up so its top outline lies on the GPS box's bottom outline (one separator line). The box
 * that is selected or being edited is drawn above the other, so its whole outline shows.
 */
export class SelectedAltitudeDisplay extends DisplayRectangle {

    /**
     * @param {PIXI.Application} app The PixiJS application that is the EFIS display
     * @param {number} x The right edge of the box in pixels
     * @param {number} y The bottom of the box in pixels
     * @param {number} width The width of the box in pixels
     * @param {number} height The height of the box in pixels
     * @param {Ribbon} ribbon The altitude tape, which draws the bug and gives the current altitude
     * @param {DisplayRectangle} boxAbove The GPS / PRE / DEN box this box sits under (drawing order)
     */
    constructor(app, x, y, width, height, ribbon, boxAbove) {
        super(
            app,
            x, y,
            width, height,
            0,              // square corners
            true,           // y is the bottom of the box
            true,           // aligned from the right
            false           // no legend
        );

        this.app = app;
        this.ribbon = ribbon;
        this.boxAbove = boxAbove;
        this.target = null;             // feet, null until set
        this.lastEncoderValue = 0;

        this.valueText.style.fill = SELECTED_COLOUR;
        this.unitText.style.fill = SELECTED_COLOUR;
        this.unitText.text = "ft";

        // Bug symbol right of "ft": the tape's bug at about 60 %, notch pointing left towards the tape's pointer
        const bugSymbol = new Graphics();
        const bugHeight = 12;
        const bugWidth = 5;
        const notch = 3;
        bugSymbol.moveTo(0, -bugHeight / 2);
        bugSymbol.lineTo(bugWidth, -bugHeight / 2);
        bugSymbol.lineTo(bugWidth, bugHeight / 2);
        bugSymbol.lineTo(0, bugHeight / 2);
        bugSymbol.lineTo(0, notch);
        bugSymbol.lineTo(notch, 0);
        bugSymbol.lineTo(0, -notch);
        bugSymbol.closePath();
        bugSymbol.fill({ color: SELECTED_COLOUR });
        bugSymbol.position.set(x - 12, y - height / 2);
        bugSymbol.zIndex = 10;

        this.container.addChild(this.regularRectangle);
        this.container.addChild(this.valueText);
        this.container.addChild(this.unitText);
        this.container.addChild(bugSymbol);

        app.stage.addChild(this.container);
        this.drawBelowBoxAbove();
        this.showTarget();
    }

    /** Show the target in the box and on the tape (dashes and no bug until it is set). */
    showTarget() {
        this.valueText.text = (this.target === null) ? "----" : String(this.target);
        this.ribbon.bugValue = this.target;
    }

    /** Draw this box under the GPS box, so the shared line is the GPS box's (and its outline when selected). */
    drawBelowBoxAbove() {
        const stage = this.app.stage;
        const aboveIndex = stage.getChildIndex(this.boxAbove.container);
        if (stage.getChildIndex(this.container) > aboveIndex) {
            stage.setChildIndex(this.container, aboveIndex);
        }
    }

    /** Draw this box over the GPS box, so its own outline shows all round while selected or being edited. */
    drawAboveBoxAbove() {
        const stage = this.app.stage;
        const aboveIndex = stage.getChildIndex(this.boxAbove.container);
        if (stage.getChildIndex(this.container) < aboveIndex) {
            stage.setChildIndex(this.container, aboveIndex);
        }
    }

    /**
     * Long press on the knob while editing (userInput): target to the current altitude rounded to 100 ft.
     * Returns false when the altitude is not known.
     */
    sync() {
        const altitude = this.ribbon.currentValue;
        if (altitude === undefined || altitude === null) return false;
        this.target = Math.max(MIN_VALUE, Math.min(MAX_VALUE, Math.round(altitude / 100) * 100));
        this.showTarget();
        return true;
    }

    callback(selected, changable, value) {
        super.callback(selected, changable, value);

        if (selected || changable) {
            this.drawAboveBoxAbove();
        } else {
            this.drawBelowBoxAbove();
        }
        // the cyan line up the side of the tape: a cue to what the knob is adjusting
        this.ribbon.showBugSelected(selected || changable);

        // Turning while editing: the value follows the change in encoder position (as the QNH box)
        if (selected && changable) {
            const delta = value - this.lastEncoderValue;
            this.lastEncoderValue = value;
            if (delta === 0) return;
            if (this.target === null) {
                // first click: the target appears at the current altitude rounded to 100 ft
                const altitude = this.ribbon.currentValue;
                this.target = (altitude === undefined || altitude === null) ? 0 : Math.round(altitude / 100) * 100;
                this.showTarget();
                return;
            }
            const increment = (Math.abs(delta) >= FAST_DELTA_THRESHOLD) ? FAST_INCREMENT : INCREMENT;
            this.target = Math.max(MIN_VALUE, Math.min(MAX_VALUE, this.target + delta * increment));
            this.showTarget();
        }
    }
}
