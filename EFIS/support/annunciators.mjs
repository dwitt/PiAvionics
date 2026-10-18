'use strict';
import { Container, Graphics, TextStyle, Text } from './pixi.mjs';
import { FAILURE_LOOK } from './failureLook.mjs';
import { WARNING } from './instrumentStatus.mjs';

export const VERSION = '1.0.0';

/*****************************************************************************
 * The annunciator strip: short red warnings and amber cautions stacked
 * upward from just above the slip/skid ball, warnings at the top of the stack.
 * A new entry flashes for a few seconds and then stays steady.
 * What to show comes from InstrumentStatus; this class only draws it.
 ****************************************************************************/
export class Annunciators {

    /*************************************************************************
     * @param {object} app - PixiJS Application to draw on
     * @param {number} bottomY - y of the bottom of the lowest line
     *************************************************************************/
    constructor(app, bottomY) {
        this.app = app;
        this.bottomY = bottomY;

        this.container = new Container();
        this.container.x = app.screen.width / 2;
        this.container.y = bottomY;
        app.stage.addChild(this.container);

        this._shown = '';           // text and level of the lines currently drawn, to spot a change
        this._lines = [];           // {text, pill, label, firstSeen}
        this._firstSeen = {};       // text -> time (ms) it first appeared, for the flashing
    }

    /**
     * Draw the current annunciations. Call every frame.
     * @param {Array} annunciations List of {text, level} with warnings first
     * @param {number} now The current time in milliseconds
     */
    update(annunciations, now) {
        const items = annunciations.slice(0, FAILURE_LOOK.annunciatorMaxLines);

        // forget entries that went away so they flash again if they come back
        const present = {};
        for (const item of annunciations) present[item.text] = true;
        for (const text of Object.keys(this._firstSeen)) {
            if (!present[text]) delete this._firstSeen[text];
        }
        for (const item of items) {
            if (!(item.text in this._firstSeen)) this._firstSeen[item.text] = now;
        }

        // rebuild the lines only when the list changed
        const key = items.map((item) => item.level + ':' + item.text).join('|');
        if (key !== this._shown) {
            this._shown = key;
            this._rebuild(items);
        }

        // flashing: a new entry switches on and off for a few seconds, then stays on
        for (const line of this._lines) {
            const age = (now - this._firstSeen[line.text]) / 1000;
            const flashing = age < FAILURE_LOOK.flashSeconds;
            const on = !flashing || (Math.floor(age * FAILURE_LOOK.flashChangesPerSecond) % 2 === 0);
            line.label.visible = on;
            line.pill.alpha = on ? 1.0 : 0.3;
        }
    }

    // ------------------------------------------------------------------------
    // --- Drawing                                                          ---
    // ------------------------------------------------------------------------
    _rebuild(items) {
        for (const line of this._lines) {
            line.pill.destroy();
            line.label.destroy();
        }
        this._lines = [];
        this.container.removeChildren();

        const lineHeight = FAILURE_LOOK.annunciatorFontSize + FAILURE_LOOK.annunciatorLineGap;
        const top = -items.length * lineHeight;

        items.forEach((item, index) => {
            const centreY = top + index * lineHeight + lineHeight / 2;

            // a TextStyle is shared by reference, so each line gets its own
            const label = new Text({
                text: item.text,
                style: new TextStyle({
                    fontFamily: 'Tahoma',
                    fontSize: FAILURE_LOOK.annunciatorFontSize,
                    fontWeight: 'bold',
                    fill: item.level === WARNING ? FAILURE_LOOK.warningColour
                                                 : FAILURE_LOOK.cautionColour,
                }),
            });
            label.anchor.set(0.5, 0.5);
            label.position.set(0, centreY);

            const pillWidth = label.width + 2 * FAILURE_LOOK.annunciatorPadding;
            const pill = new Graphics();
            pill.roundRect(-pillWidth / 2, centreY - lineHeight / 2 + 1, pillWidth, lineHeight - 2, 5);
            pill.fill({ color: 0x000000, alpha: FAILURE_LOOK.annunciatorBackAlpha });

            this.container.addChild(pill);
            this.container.addChild(label);
            this._lines.push({ text: item.text, pill: pill, label: label });
        });
    }
}
