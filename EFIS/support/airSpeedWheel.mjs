// 1-June-2025 - Updated for PixiJS 8.6.6
'use strict';

export const VERSION = '1.0.4';

import { NumericWheelDisplay } from "./numericWheelDisplay.mjs";
import { Graphics, TextStyle, Text} from './pixi.mjs';
import { FAILURE_LOOK } from './failureLook.mjs';

/**     
 * Class representing an Airspeed Wheel Display.
 */
export class AirspeedWheel  {

    constructor(app, x, y) {

        // Create wheel elements for the digits in the airspeed

        this.airspeedHundredsWheel = new NumericWheelDisplay("Tahoma", 37, 1489/2048, 30, 2, false, 1, false, false, x, y);
       
        let airspeedTensWheelX = x + this.airspeedHundredsWheel.digitWidth;
        this.airspeedTensWheel = new NumericWheelDisplay("Tahoma", 37, 1489/2048, 30, 1, false, 1, false, false, airspeedTensWheelX, y);

        let airspeedOnesWheelX = airspeedTensWheelX + this.airspeedTensWheel.digitWidth;
        this.airspeedOnesWheel = new NumericWheelDisplay("Tahoma", 37, 1489/2048, 30, 0, false, 1, false, false, airspeedOnesWheelX, y);

        let airspeedWidth = this.airspeedOnesWheel.digitWidth + this.airspeedTensWheel.digitWidth + this.airspeedHundredsWheel.digitWidth;

        this.outline = this.airspeedWheelOutline(app, x ,y , airspeedWidth, 15, 0x000000);
        // the same box filled red, shown at or above Vne (see alert)
        this.warningOutline = this.airspeedWheelOutline(app, x ,y , airspeedWidth, 15, FAILURE_LOOK.warningColour);
        this.warningOutline.visible = false;
        this._alert = null;
        this._visible = true;

        // Add text for kt
        // Create a style to be used for the units characters
        this.style = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: '20px',  // was '18px'
            fill: "white",
            fontWeight: "normal",
            stroke: {
                color: "black",
                width: 3,       // 3 px, as the pitch labels (was 2)
                }
            }
        );

        this.IASunits = new Text({
            text: "kt",
            style: this.style
            });
        this.IASunits.anchor.set(0,.2);
        this.IASunits.position.set(x + airspeedWidth + 8, y + 16);    // 16 px below the digits' centre: just clear of a V-speed tag at the pointer

        app.stage.addChild(this.IASunits);
        app.stage.addChild(this.airspeedOnesWheel.digitContainer);
        app.stage.addChild(this.airspeedTensWheel.digitContainer);
        app.stage.addChild(this.airspeedHundredsWheel.digitContainer);

    }

    airspeedWheelOutline(app,x,y,width,height,fillColour){
        let line = new Graphics();

        line.strokeStyle = {
            color: 0x666666,    // grey — was 0xffffff
            width: 2,           // 2 px
        };

        line.fillStyle = {
            color: fillColour,  // black, or red for the overspeed warning
        };

        // strart drawing the point
        line.moveTo(x-6,y);
        line.lineTo(x-1,y-5);
        // draw box
        line.lineTo(x-1,y-(2+height));
        line.lineTo(x+(1+width),y-(2+height));
        line.lineTo(x+(1+width),y+(2+height));
        line.lineTo(x-1,y+(2+height));
        // complete the point
        line.lineTo(x-1,y+5);
        line.lineTo(x-6,y);
  
        line.closePath();

        line.fill();
        line.stroke();
    
        app.stage.addChild(line);
        return line;                // kept so the whole wheel can be hidden
    }

    /**
     * Overspeed cue (G5 2.1.1.1): 'warning' (at or above Vne) fills the box red, 'caution' (the trend vector
     * reaches Vne) turns the digits the amber caution colour, null is normal.
     * @param {string|null} level
     */
    set alert(level) {
        if (level === this._alert) return;
        this._alert = level;
        this.outline.visible = this._visible && level !== 'warning';
        this.warningOutline.visible = this._visible && level === 'warning';
        const tint = (level === 'caution') ? FAILURE_LOOK.cautionColour : 0xFFFFFF;
        this.airspeedOnesWheel.digitContainer.tint = tint;
        this.airspeedTensWheel.digitContainer.tint = tint;
        this.airspeedHundredsWheel.digitContainer.tint = tint;
    }

    /**
     * Show or hide the whole wheel (outline, digits and units). The page hides it when the airspeed
     * cannot be trusted.
     * @param {boolean} isVisible
     */
    set visible(isVisible) {
        this._visible = isVisible;
        this.outline.visible = isVisible && this._alert !== 'warning';
        this.warningOutline.visible = isVisible && this._alert === 'warning';
        this.IASunits.visible = isVisible;
        this.airspeedOnesWheel.digitContainer.visible = isVisible;
        this.airspeedTensWheel.digitContainer.visible = isVisible;
        this.airspeedHundredsWheel.digitContainer.visible = isVisible;
    }

    set value(newValue){
        this.airspeedOnesWheel.value = newValue;
        this.airspeedTensWheel.value = newValue;
        this.airspeedHundredsWheel.value = newValue;
    }

}
