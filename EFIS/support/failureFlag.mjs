'use strict';
import { Container, Graphics, TextStyle, Text } from './pixi.mjs';
import { FAILURE_LOOK } from './failureLook.mjs';

export const VERSION = '1.0.0';

/*****************************************************************************
 * A red X drawn over an instrument that cannot be trusted, with an optional
 * short name in the middle. The X is semi-transparent so anything left visible
 * underneath (the gyro-only heading) can still be read.
 ****************************************************************************/
export class FailureFlag {

    /*************************************************************************
     * @param {object} app - PixiJS Application to draw on
     * @param {number} x - left edge of the area to mark
     * @param {number} y - top edge of the area to mark
     * @param {number} width - width of the area
     * @param {number} height - height of the area
     * @param {string} label - short name shown in the middle ('' for none)
     *************************************************************************/
    constructor(app, x, y, width, height, label = '') {
        this.container = new Container();
        this.container.visible = false;

        // ---- the X (drawn with a small inset so the round ends stay inside the area)
        const inset = FAILURE_LOOK.xWidth / 2 + 1;
        const xGraphics = new Graphics();
        xGraphics.strokeStyle = {
            width: FAILURE_LOOK.xWidth,
            color: FAILURE_LOOK.xColour,
            alpha: FAILURE_LOOK.xAlpha,
            cap: 'round',
        };
        xGraphics.moveTo(x + inset, y + inset);
        xGraphics.lineTo(x + width - inset, y + height - inset);
        xGraphics.moveTo(x + width - inset, y + inset);
        xGraphics.lineTo(x + inset, y + height - inset);
        xGraphics.stroke();
        this.container.addChild(xGraphics);

        // ---- the name in the middle
        if (label !== '' && FAILURE_LOOK.labelShow) {
            const labelText = new Text({
                text: label,
                style: new TextStyle({
                    fontFamily: 'Tahoma',
                    fontSize: FAILURE_LOOK.labelFontSize,
                    fontWeight: 'bold',
                    fill: FAILURE_LOOK.labelColour,
                }),
            });
            labelText.anchor.set(0.5, 0.5);
            labelText.position.set(x + width / 2, y + height / 2 + 1);

            const boxWidth = labelText.width + 14;
            const boxHeight = labelText.height + 6;
            const boxGraphics = new Graphics();
            boxGraphics.roundRect(x + width / 2 - boxWidth / 2, y + height / 2 - boxHeight / 2,
                                  boxWidth, boxHeight, 4);
            boxGraphics.fill({ color: 0x000000, alpha: FAILURE_LOOK.labelBackAlpha });
            boxGraphics.stroke({ color: FAILURE_LOOK.xColour, width: 2 });
            this.container.addChild(boxGraphics);
            this.container.addChild(labelText);
        }

        app.stage.addChild(this.container);
    }

    /**
     * Show or hide the flag
     * @param {boolean} isShown
     */
    set shown(isShown) {
        this.container.visible = isShown;
    }

    get shown() {
        return this.container.visible;
    }
}
