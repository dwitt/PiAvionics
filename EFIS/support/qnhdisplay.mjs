'use strict';

import { DisplayRectangle } from './displayRectangle.mjs';
import { FAILURE_LOOK } from './failureLook.mjs';

export const VERSION = '1.1.1';

// QNH range limits in inHg × 100 format (2800 = 28.00 inHg, 3100 = 31.00 inHg)
const QNH_MIN_INHG_X_100 = 2800;
const QNH_MAX_INHG_X_100 = 3100;
const NORMAL_COLOUR = 0x00ffff;
const NOT_SET_FLASH_SECONDS = 1;    // the "not set" 29.92 flashes once a second
const SYNC_FLASH_SECONDS = 2;       // after a long-press sync the value flashes quickly for this long
const SYNC_FLASH_PERIOD = 0.25;     // seconds per on/off cycle of that flash

/**     
 * Class representing the QNH display.
 */

export class QNHDisplay extends DisplayRectangle {
    /**
     * Create a QNHDisplay Object that will display the QNH value and allow it
     *     to be selected and changed by the user
     * @param {PIXI.Application} app The PixiJS application that is the EFIS display
     * @param {number} x The x position of the QNH display in pixels
     * @param {number} y The y position of the QNH display in pixels
     * @param {number} width The width of the QNH display in pixels
     * @param {number} height The height of the QNH display in pixels
     * @param {number} radius The corner radius of the QNH display in pixels
     */
    constructor (app, x, y, width, height, radius) {
        super(
            app,
            x, y,
            width, height,
            radius,
            false,          // radius not at top
            true,           // aligned from the right
            false           // no legend
        );

        // my_value stores QNH in inHg × 100 (e.g., 2992 = 29.92 inHg)
        // This format preserves two decimal places when stored as an integer
        this.my_value = 2992;
        // Track last encoder position to calculate delta for proper clamping behavior
        this.lastEncoderValue = 0;
        // QNH that makes the altitude read the GPS altitude, set by the page each frame while a sync is
        // allowed (on the ground with a good 3D fix), otherwise null. Used by sync().
        this.syncTarget = null;
        this.syncFlashUntil = 0;     // seconds (Date.now() / 1000)
        this.QNHFormat = new Intl.NumberFormat('en-US', {minimumFractionDigits: 2});

        // Set initial text values
        this.valueText.text = this.QNHFormat.format(29.92);
        this.unitText.text = "inHg";

        // Add the display objects to the container
        this.container.addChild(this.regularRectangle);
        this.container.addChild(this.valueText);
        this.container.addChild(this.unitText);
  
        app.stage.addChild(this.container);
    }
    
    /**
     * Long press on the knob while editing (userInput): set the QNH so the altitude matches the GPS altitude,
     * as setting field elevation. Only on the ground: the page leaves syncTarget null otherwise, and then the
     * long press works as a normal press. The value flashes so the change is noticed.
     */
    sync() {
        if (this.syncTarget === null || this.syncTarget === undefined) return false;
        this.value = this.syncTarget;
        this.syncFlashUntil = Date.now() / 1000 + SYNC_FLASH_SECONDS;
        return true;
    }

    callback(selected, changable, value) {
        super.callback(selected, changable, value);
        
        // Process the encoder value provided
        if (selected && changable) {
            // my_value is stored as inHg × 100 (e.g., 2992 = 29.92 inHg)
            // value is an absolute encoder position. Calculate delta from last position
            // and apply to current my_value to maintain proper clamping behavior.
            // This ensures that if we hit a limit, the encoder "sticks" at that limit
            // until rotated the other direction, rather than requiring full rotation back.
            const encoderDelta = value - this.lastEncoderValue;
            const newValue = this.my_value + encoderDelta;
            this.my_value = Math.max(QNH_MIN_INHG_X_100, Math.min(QNH_MAX_INHG_X_100, newValue));
            this.lastEncoderValue = value;
            // Divide by 100 to convert from inHg × 100 to inHg for display (shows 2 decimal places)
            this.valueText.text = this.QNHFormat.format(Math.floor(this.my_value)/100);
        }
    }

    /**
     * Show the state of the shared QNH (QNH_PLAN.md). Not set: the 29.92 flashes once a second until a person sets
     * it or a value arrives from the bus. Mismatch: the AIR module is using a different QNH, so the value is amber.
     * @param {boolean} notSet True while nobody has set the QNH
     * @param {boolean} mismatch True while the AIR module reports a different QNH
     * @param {number} nowSeconds Time in seconds, for the flashing
     */
    setStatus(notSet, mismatch, nowSeconds) {
        const colour = mismatch ? FAILURE_LOOK.cautionColour : NORMAL_COLOUR;
        this.valueText.style.fill = colour;
        this.unitText.style.fill = colour;
        let visible = !notSet || (Math.floor(nowSeconds / (NOT_SET_FLASH_SECONDS / 2)) % 2 === 0);
        if (nowSeconds < this.syncFlashUntil) {
            visible = Math.floor(nowSeconds / (SYNC_FLASH_PERIOD / 2)) % 2 === 0;
        }
        this.valueText.alpha = visible ? 1 : 0.15;
    }

    set value(new_value) {
        // new_value is expected to be in inHg × 100 format
        // Clamp value between 2800 (28.00 inHg) and 3100 (31.00 inHg)
        this.my_value = Math.max(QNH_MIN_INHG_X_100, Math.min(QNH_MAX_INHG_X_100, new_value));
        // Divide by 100 to convert from inHg × 100 to inHg for display (shows 2 decimal places)
        this.valueText.text = this.QNHFormat.format(Math.floor(this.my_value)/100);
    }

    get value() {
        // Returns QNH value in inHg × 100 format (e.g., 2992 = 29.92 inHg)
        return this.my_value;
    }
}