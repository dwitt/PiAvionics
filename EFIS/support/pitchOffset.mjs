'use strict';

export const VERSION = '1.0.5';

import { Container, Graphics, Text, TextStyle } from './pixi.mjs';

const CYAN = 0x00FFFF;              // a value the pilot sets, and the knob's selected / editing outline
const STEP = 0.5;                   // degrees per knob click
const MAX_OFFSET = 5;               // degrees either way (as the G5)
const SERVER_HOLD_MS = 3000;        // after a change here, the server's value is not taken for this long
const SELECTED_EDGE = 2;            // px of cyan round the symbol's black edge when selected
const EDITING_EDGE = 3;             // and when editing
const READOUT_GAP = 10;             // px between the right wing tip and the offset label
const LABEL_BACK_ALPHA = 0.25;      // the label's black background, as the ribbons (0.25 fill, 1 px edge)
const LABEL_PAD_X = 6;              // px of background either side of the label
const LABEL_CORNER = 8;             // corner radius, the stand-alone box standard
const LABEL_PAD_Y = 1;              // and above and below it

/**
 * Pitch attitude offset (G5 review feature 14, G5 guide 2.1.7): the knob moves the yellow aircraft symbol up or
 * down, as on a mechanical attitude indicator, up to 5 degrees either way in 0.5 degree steps. The horizon and the
 * pitch ladder do not move. It is the last knob item.
 *
 * Selected: a 2 px cyan line tight round the symbol's parts (wings and dot) and the offset ("+2.0°") in cyan right of
 * the wing, right justified on a fixed-size semi-transparent black background with round corners (as the ribbons).
 * Editing: a 3 px line and the same offset in bold. Not selected: nothing shows the offset (David). A long press
 * while editing centres the symbol (sync).
 *
 * The offset is kept on the Pi (aircraft_settings.json, attitude group): the page sends each change and takes the
 * server's value at start, and from other pages, when it has not changed it itself for a moment.
 */
export class PitchOffset {

    /**
     * @param {PIXI.Application} app The PFD
     * @param {AircraftIndicator} aircraft The yellow aircraft symbol (its graphics, half width and depth)
     * @param {number} pitchRatio Pixels per degree of pitch (the attitude indicator's)
     * @param {function} send Sends a command to the server
     */
    constructor(app, aircraft, pitchRatio, send) {
        this.aircraft = aircraft;
        this.centreY = aircraft.graphics.y;
        this.pitchRatio = pitchRatio;
        this.send = send;

        this.offset = 0;                // degrees, + is up
        this.selected = false;
        this.editing = false;
        this.lastEncoderValue = 0;
        this.lastLocalChange = -Infinity;

        // everything that moves with the symbol, drawn in its coordinates (0, 0 at the dot)
        this.container = new Container();
        this.container.x = aircraft.graphics.x;
        this.container.y = this.centreY;

        // the cyan line tight round the wings and the dot: the symbol's shape drawn wider in cyan, under it
        // (David did not like a box round the whole symbol)
        this.outlines = new Container();
        this.outlines.x = aircraft.graphics.x;
        this.outlines.y = this.centreY;
        this.selectedOutline = aircraft.outline(SELECTED_EDGE);
        this.editingOutline = aircraft.outline(EDITING_EDGE);
        this.outlines.addChild(this.selectedOutline, this.editingOutline);
        app.stage.addChildAt(this.outlines, app.stage.getChildIndex(aircraft.graphics));

        const style = { fontFamily: 'Tahoma', fontSize: 20, fill: CYAN };
        // the offset beside the wing while selected or editing, bold while editing (David: no PITCH box)
        this.label = new Text({ text: '', style: new TextStyle(style) });

        // the widest the label gets (bold, five degrees either way), so the box is a fixed size and the number,
        // right justified in it, does not move as it goes through zero or turns bold (David)
        this.label.style.fontWeight = 'bold';
        let textWidth = 0;
        for (const offset of [MAX_OFFSET, -MAX_OFFSET]) {
            this.label.text = PitchOffset.format(offset);
            textWidth = Math.max(textWidth, this.label.width);
        }
        const boxLeft = aircraft.halfWidth + READOUT_GAP - LABEL_PAD_X;
        const boxWidth = textWidth + 2 * LABEL_PAD_X;
        const boxHeight = this.label.height + 2 * LABEL_PAD_Y;

        this.label.anchor.set(1, 0.5);
        this.label.x = boxLeft + boxWidth - LABEL_PAD_X;
        this.label.y = aircraft.depth / 2;

        // a semi-transparent black background behind the label, as the ribbons have, so it reads over the sky
        this.labelBack = new Graphics();
        this.labelBack.roundRect(boxLeft, this.label.y - boxHeight / 2, boxWidth, boxHeight, LABEL_CORNER);
        this.labelBack.fill({ color: 0x000000, alpha: LABEL_BACK_ALPHA });
        this.labelBack.stroke({ color: 0x000000, alpha: LABEL_BACK_ALPHA, width: 1, alignment: 0 });

        this.container.addChild(this.labelBack, this.label);
        app.stage.addChild(this.container);
        this.show();
    }

    /** Offset as shown: "+2.0°", "-1.5°", "0.0°" */
    static format(offset) {
        return (offset > 0 ? '+' : offset < 0 ? '−' : '') + Math.abs(offset).toFixed(1) + '°';
    }

    show() {
        const y = this.centreY - this.offset * this.pitchRatio;
        this.aircraft.graphics.y = y;
        this.container.y = y;
        this.outlines.y = y;
        this.selectedOutline.visible = this.selected && !this.editing;
        this.editingOutline.visible = this.editing;
        this.label.visible = this.selected;                     // selected or editing only (David)
        this.label.style.fontWeight = this.editing ? 'bold' : 'normal';
        this.label.text = PitchOffset.format(this.offset);
        this.labelBack.visible = this.selected;
    }

    setOffset(offset) {
        offset = Math.max(-MAX_OFFSET, Math.min(MAX_OFFSET, Math.round(offset / STEP) * STEP));
        if (offset === this.offset) return;
        this.offset = offset;
        this.lastLocalChange = performance.now();
        this.send({ pitch_offset: offset });
        this.show();
    }

    /**
     * The offset the server has (aircraft settings), taken unless this page changed it a moment ago (the server's
     * copy may not have caught up yet).
     * @param {object} attitude dataObject.aircraft.attitude
     */
    fromServer(attitude) {
        if (!attitude || typeof attitude.pitch_offset !== 'number') return;
        if (this.editing || performance.now() - this.lastLocalChange < SERVER_HOLD_MS) return;
        if (attitude.pitch_offset !== this.offset) {
            this.offset = attitude.pitch_offset;
            this.show();
        }
    }

    /** Long press on the knob while editing (userInput): centre the symbol. */
    sync() {
        this.setOffset(0);
        return true;
    }

    /** The text that flashes while a sync is held (userInput). */
    get syncFlashTarget() {
        return this.label;
    }

    callback(selected, changable, value) {
        const editing = selected && changable;
        if (editing && !this.editing) this.lastEncoderValue = value;    // just pressed: no change yet
        this.selected = selected;
        this.editing = editing;
        if (editing) {
            const delta = value - this.lastEncoderValue;
            this.lastEncoderValue = value;
            if (delta !== 0) this.setOffset(this.offset + delta * STEP);
        }
        this.show();
    }
}
