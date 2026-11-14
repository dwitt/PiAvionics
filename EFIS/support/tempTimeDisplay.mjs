// 1-June-2025 Update for PixiJS v8
'use strict';

export const VERSION = '1.0.2';

import { DisplayRectangle } from './displayRectangle.mjs';

/**
 * Class representing the outside air temperature or the UTC time box. There are two, one for each, at the foot of
 * the speed tape: the square OAT box over the bottom of the tape and the UTC box (rounded bottom corners) below it,
 * as the GS and TAS boxes at the top. Neither is on the knob (it used to switch one box between them).
 */

export const TEMP = 0;
export const TIME = 1;

export class TempTimeDisplay extends DisplayRectangle {

    /**
     * @param {PIXI.Application} app The PixiJS application that is the EFIS display
     * @param {number} x The left edge of the box in pixels
     * @param {number} y The top of the box in pixels
     * @param {number} width The width of the box in pixels
     * @param {number} height The height of the box in pixels
     * @param {number} radius The radius of the box's bottom corners (0 for square)
     * @param {number} displayItem TEMP or TIME
     */
    constructor(app, x , y, width, height, radius, displayItem = TEMP){
        super(
            app,
            x, y,
            width, height,
            radius,
            false,          // radius not at top
            false,          // not aligned from the right
            false           // no legend
        )

        this.screen_width = app.screen.width;

        this._temperature = 0;
        this._timeHour = null;          // null until the GPS gives a time
        this._timeMinute = null;

        this._displayItem = displayItem;

        // Set by the page: false shows --- instead of the temperature (the sensor cannot be trusted)
        this.validity = { temperature: true };

        this.valueText.text = (displayItem == TIME) ? "--:--" : "0";
        this.unitText.text = (displayItem == TIME) ? "UTC" : "ºC";

        // The temperature and time are measured, so white, with the unit label (ºC, UTC) to match; cyan means
        // set by the pilot (QNH, bugs)
        this.valueText.style.fill = 0xFFFFFF;
        this.unitText.style.fill = 0xFFFFFF;

        this.container.addChild(this.regularRectangle);
        this.container.addChild(this.valueText);
        this.container.addChild(this.unitText);

        app.stage.addChild(this.container);
    }

    update() {
        let text;
        if (this._displayItem == TEMP) {
            if (!this.validity.temperature) {
                text = "---";
            }
            else if (this._temperature !== undefined && this._temperature !== null) {
                text = String(this._temperature);
            }
        }
        else if (this._displayItem == TIME) {
            if (this._timeHour !== null && this._timeMinute !== null) {
                text = String(this._timeHour).padStart(2, '0') + ":" + String(this._timeMinute).padStart(2, '0');
            }
        }
        if (text !== undefined && text !== this.valueText.text) {      // a text change redraws it: only when it changes
            this.valueText.text = text;
        }
    }

    set temperature(newValue) {
        this._temperature = newValue;
    }

    set timeHour(newValue) {
        this._timeHour = newValue;
    }

    set timeMinute(newValue) {
        this._timeMinute = newValue;
    }

}
