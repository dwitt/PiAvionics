/** Enable strict mode for enhanced error checking */
'use strict';

export const VERSION = '1.0.1';

// GPS readouts: magenta lightened so the small digits are easy to read (pure magenta 0xFF00FF was too dark;
// the HSI waypoint text uses the same shade)
const GPS_TEXT_COLOUR = 0xFF55FF;

import { DisplayRectangle } from './displayRectangle.mjs';

/**     
 * Class representing the altitude display.
 * 
 * 
 */

 const GPS = 0;
 const PRESS = 1;
 const DEN = 2;

export class AltitudeDisplay extends DisplayRectangle {

    constructor (app, x , y, width, height, radius){
        super(
            app,
            x, y,
            width, height,
            radius,
            true,           // radius at top
            true,           // aligned from the right
            true            // has legend
        );


        this.gpsAltitudeValue = 0;
        this.pressureAltitudeValue = 0;
        this.densityAltitudeValue = 0;
        this.displayItem = GPS;

        // Set by the page: false shows --- instead of the value (the source cannot be trusted)
        this.validity = { gps: true, pressure: true, density: true };

        // Set initial text values
        this.valueText.text = "0";
        this.unitText.text = "ft";
        this.legendText.text = "GPS";


        this.applySourceColour();

        // Add the display objects to the container
        this.container.addChild(this.regularRectangle);
        this.container.addChild(this.valueText);
        this.container.addChild(this.unitText);
        this.container.addChild(this.legendText);

        app.stage.addChild(this.container);
    }

    callback(selected, changable, value){
        super.callback(selected, changable, value);
        
        // Process the encoder value provided
        if (selected && changable) {
            this.displayItem = Math.abs(value % 3);

            if (this.displayItem == GPS) {
                this.legendText.text = "GPS";
            }
            else if (this.displayItem == PRESS) {
                this.legendText.text = "PRE";
            }
            else if (this.displayItem == DEN) {
                this.legendText.text = "DEN";
            }
            this.applySourceColour();
        }
    }

    /** Colour by source: GPS altitude magenta (GPS and nav data); pressure and density altitude white
     *  (from the air data). Was chartreuse for all three. */
    applySourceColour() {
        const colour = (this.displayItem == GPS) ? GPS_TEXT_COLOUR : 0xFFFFFF;
        this.valueText.style.fill = colour;
        this.unitText.style.fill = colour;
        this.legendText.style.fill = colour;
    }

    update() {
        if (this.displayItem == GPS) {
            if (!this.validity.gps) {
                this.valueText.text = "---";
            }
            else if (this.gpsAltitudeValue !== undefined && this.gpsAltitudeValue !== null) {
                this.valueText.text = String(this.gpsAltitudeValue);
            }
        }
        else if (this.displayItem == PRESS) {
            this.valueText.text = this.validity.pressure ? String(this.pressureAltitudeValue) : "---";
        } 
        else if (this.displayItem == DEN) {
            this.valueText.text = this.validity.density ? String(this.densityAltitudeValue) : "---";
        }
    }

    set gpsAltitude(new_value) {
        this.gpsAltitudeValue = new_value;
    }

    get gpsAltitude() {
        return this.gpsAltitudeValue;
    }

    set densityAltitude(new_value) {
        this.densityAltitudeValue = new_value;
    }

    get densityAltitude() {
        return this.densityAltitudeValue;
    }

    set staticPressure(newValue) {
        this.pressureAltitudeValue = Math.round(145442 * (1 - Math.pow((newValue / 10132.5), 0.1902632)));
    }

    set temperature(newValue) {
        const LAPSERATE = 0.0019812;
        const TEMPEXPONENT = 0.234960;
        const CTOK = 273.15;
        const STDTEMPK = CTOK + 15;
        let oATempK = newValue + CTOK;
        let stdTempK = STDTEMPK - LAPSERATE * this.pressureAltitudeValue;
        this.densityAltitudeValue = Math.round(this.pressureAltitudeValue + (stdTempK/LAPSERATE) * (1 - Math.pow((stdTempK/oATempK), TEMPEXPONENT)));
    }
}