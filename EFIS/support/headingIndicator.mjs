'use strict';

export const VERSION = '1.0.9';

import { Container, Graphics, TextStyle, Text, CanvasTextMetrics } from './pixi.mjs';

/*****************************************************************************     
 * Class representing a Heading Indicator.
 * The indicator is designed for a height of 40 pixels.
 * The indicator is positioned at the top of the display.
 *****************************************************************************/
export class HeadingIndicator {
    
    /*************************************************************************
     * Constructor
     * @param {object} app Pixijs
     * @param {number} width the width of the horizontal ribbon
     *************************************************************************/

    constructor(app, width) {

        // TODO: Create some consistency for the screen width variable
        // get the size of the canvas used by the app
        this.displayWidth = app.screen.width;
        this.displayHeight = app.screen.height;
        
        const height = 40;            // height of heading ribbon

        // Internal parameters for the heading value
        this._value = 0;
        this._previous_value = 0;
        this._bugValue = null;              // degrees, null until the pilot sets it
        this._bugIncrement = 1;
        this._bugFastIncrement = 2;
        this._bugFastDeltaThreshold = 6;    // delta >= this uses fast increment
        this._bugSpeedSensitive = true;     // set to false to disable speed sensitivity
        this._lastEncoderValue = 0;         // holds the last encoder value to calculate deltas

        /*********************************************************************
         * Create the graphics required for the horizontal ribbon
         *
         *********************************************************************/
        
        this.headingContainer = new Container();
        
        // Centre the container on the screen horizontally
        this.headingContainer.x = this.displayWidth/2;  

        // Create the mask for the container so that everything behind only 
        // the graphics in the mask area are displayed
        const headingMask = this.createMaskGraphics(height);
        this.headingContainer.mask = headingMask;

        // Create the container with the background for the ribbon
        const headingBackground = this.createBackgroundGraphics(height);

        // create the container with the ribbon (degree marks and labels)
        // created as a property so we can manipulate it as required in
        // other class functions
        this.headingRibbon = this.createRibbonContainer(height);
        const magnifierContainer = this.createMagnifierContainer(height);
        this.headingBugContainer = this.createHeadingBugContainer(height);
        this.selectableGraphics = this.createSelectableGraphic(height);
        
        this.bugBox = this.createBugBox(height);

        /*********************************************************************
         * add the graphics to the container
         * note that the mask needs to be rendered so it must also be added
         * to the app.
         *********************************************************************/

        this.headingContainer.addChild(headingBackground);
        this.headingContainer.addChild(headingMask);

        this.headingContainer.addChild(this.headingRibbon);
        this.headingContainer.addChild(this.headingBugContainer);
        this.headingContainer.addChild(magnifierContainer);
        this.headingContainer.addChild(this.bugBox);

        this.showBug();

        /*********************************************************************
         * add the container to the display
         *********************************************************************/

        app.stage.addChild(this.headingContainer);
    }

    /*************************************************************************
     * Set the value of the heading indicator in degrees
     *************************************************************************/
    set value(new_value){

        if (new_value == this._previous_value) {
            return;
        }
        this._previous_value = new_value;
        new_value = new_value % 360;

        if (new_value < -0.5) {     // prevents 360 from being displayed
            new_value = 360 + new_value;
        }
        this._value = new_value;

        this.headingText.text = String(Math.round(this._value)).padStart(3, '0');   // 090, as on the HSI

        this.headingRibbon.x = - this._value * this.pixelsPerDegree;
        
        this.positionHeadingBugOnRibbon();

    }
    /*************************************************************************
     * Create the background for the ribbon at the top of the screen
     * Start with a solid sky blue to obscure anything behind it, then add
     * 25% black.
     * 
     * @param {number} height The height of the graphic.
     * @returns {object} A PixiJS Graphics object.
     *************************************************************************/
    createBackgroundGraphics(height){
        const backgroundGraphics = new Graphics();

        backgroundGraphics.fillStyle = {
            alpha: 1.0,         // 100%
            color: 0x0000C0,    // skyColor
        }

        // Create rectangle. (upper left, size)
        backgroundGraphics.rect(-this.displayWidth/2, 0,
                                this.displayWidth, height);
        backgroundGraphics.fill();

        backgroundGraphics.fillStyle = {
            alpha: 0.25,        // 25%
            color: 0x000000,    // black
        }

        // Create rectangle. (upper left, size)
        backgroundGraphics.rect(-this.displayWidth/2, 0,
                                this.displayWidth, height);
        backgroundGraphics.fill();

        return (backgroundGraphics);
    }
    /*************************************************************************
     * Create a mask for the space occupied by the ribbon at the top of the
     *  screen
     * 
     * @param {number} width The width of the mask.
     * @returns {object} A PixiJS Graphics object.
     *************************************************************************/
    createMaskGraphics(height){
        const maskGraphics = new Graphics();

        maskGraphics.fillStyle = {
            color: 0xff0000,    // red
        }

        maskGraphics.rect(-this.displayWidth/2, 0,
                                     this.displayWidth, height);
        maskGraphics.fill();

        return (maskGraphics);
    }

    /*************************************************************************
     * Create a graphics and container object that is a horizontal ribbon to 
     * display the current heading.
     * 
     * @param {number} height The height of the ribbon on the display.
     * @returns {object} A PixiJS Container object
     *************************************************************************/
    createRibbonContainer(height){
        const horizontalRibbonContainer = new Container();
        const horizontalRibbonGraphics = new Graphics();
        
        // TODO: make this related to screen width 
        // Object parameter so it can be used elsewhere 
        const pixelsPerTenDegrees = 80;     // ideally divisible evenly by 10  
        const pixelsPerFiveDegrees = pixelsPerTenDegrees/2;
        this.pixelsPerDegree = pixelsPerTenDegrees/10;

        // The ribbon needs to be long enough so that an extra half of the 
        // width is available at each end so that the display is filled when
        // indicating the lowest or highest value.


        const extraRibbonTenDegrees = Math.ceil((this.displayWidth/2)/pixelsPerTenDegrees);
        this.extraRibbonDegrees = extraRibbonTenDegrees * 10;
        const extraRibbonWidth = extraRibbonTenDegrees * pixelsPerTenDegrees;
        
        // The start and end of the ribbon in pixels.
        const RibbonStart = -extraRibbonWidth;
        const RibbonEnd = (360 * this.pixelsPerDegree + extraRibbonWidth);

        const lineHeightDegree = 3;       // 3 pixels
        const lineHeightFiveDegree = 6;   // 6 pixels;
        const lineHeightTenDegree = 9;    // 9 pixels;

        let textStyle = new TextStyle({
            fontFamily: "Tahoma",
            fontSize: 22,
            fill: 0xbbbbbb,             // gray
            fontWeight: "normal"  
        });

        horizontalRibbonGraphics.strokeStyle = {
            width: 1,           // 1 px
            color: 0xffffff,    // white
        }

        // loop through all the pixels required to drawing the ribbon
        // i is in pixels
        for(let i = RibbonStart; i <= RibbonEnd; i = i + this.pixelsPerDegree ){

            horizontalRibbonGraphics.moveTo(i, height);
            
            if (i % pixelsPerTenDegrees == 0 ) {
                let degrees = i / pixelsPerTenDegrees * 10;
                // adjust degrees so that it is between 0 and 359
                if (degrees < 0) {
                    degrees = degrees + 360;
                } else if (degrees > 359) {
                    degrees = degrees - 360;
                }
                // add one or two zeros to the degree string
                let degreesString = String(degrees);
                if (degreesString.length == 1) {
                    degreesString = "00" + degreesString;
                } else if (degreesString.length == 2) {
                    degreesString = "0" + degreesString;
                }

                // Create Text to display each 10 degrees
                const degreesText = new Text({
                    text: degreesString,
                    style: textStyle,
                });
                degreesText.anchor.set(0.5,.1);
                degreesText.position.set(i,4);
                horizontalRibbonContainer.addChild(degreesText);

                horizontalRibbonGraphics.lineTo(i, height - lineHeightTenDegree );
            
            } else if (i % pixelsPerFiveDegrees == 0) {
            
                horizontalRibbonGraphics.lineTo(i, height - lineHeightFiveDegree);
            
            } else {
            
                horizontalRibbonGraphics.lineTo(i, height - lineHeightDegree);
            
            }
        }
        horizontalRibbonGraphics.stroke();
 
        horizontalRibbonContainer.x = -this._value * this.pixelsPerDegree;
        horizontalRibbonContainer.addChild(horizontalRibbonGraphics)  
        return (horizontalRibbonContainer);
    }

    /*************************************************************************
     * Create the magnifier on the horizontal ribbon that will display the 
     * current heading. 
     * 
     * @param {*} height The height of the ribbon the magnifier is placed on
     * @returns {object} A PixiJS Container object
     *************************************************************************/
    createMagnifierContainer(height){
        const magnifierGraphics = new Graphics();
        const magnifierContainer = new Container();

        const lineWidth = 2;
        const magnifierWidth = 60;    // 60 pixels
        const magnifierHeight = height - 10;   // 35 pixel
        const magnifierOffset = 10 - lineWidth ;    // 8 px up from bottom of ribbon


        magnifierGraphics.strokeStyle = {
            alignment: 0,       // inside
            alpha: 1.0,         // 100%
            color: 0x666666,    // grey — was 0xffffff
            width: lineWidth,           // 2 px
        }

        magnifierGraphics.fillStyle = {
            color: 0x000000     // black
        }

        magnifierGraphics.moveTo(-magnifierWidth/2,-magnifierHeight-magnifierOffset); // top left
        magnifierGraphics.lineTo(magnifierWidth/2, -magnifierHeight-magnifierOffset); // top right
        magnifierGraphics.lineTo(magnifierWidth/2, -magnifierOffset); // bottom right
        magnifierGraphics.lineTo(magnifierOffset-2*lineWidth,-magnifierOffset);
        //magnifierGraphics.lineTo(magnifierOffset,-magnifierOffset);
        magnifierGraphics.lineTo(0,-2*lineWidth);
        magnifierGraphics.lineTo(-magnifierOffset+2*lineWidth,-magnifierOffset);
        //magnifierGraphics.lineTo(-magnifierOffset,-magnifierOffset);
        magnifierGraphics.lineTo(-magnifierWidth/2, -magnifierOffset);
        magnifierGraphics.closePath();

        magnifierGraphics.fill();
        magnifierGraphics.stroke();

        let textStyle = new TextStyle({
            fontFamily: "Tahoma",
            fontSize: 28,
            fill: "white",
            fontWeight: "bold",
            stroke: {
                color: "black",
                width: 1,
            },
        });

        let verticalCharacterCentre = this.calculateCharacterVerticalCentre(28);
        this.headingText = new Text({
            text: "0",
            style: textStyle,
        });
        this.headingText.anchor.set(0.5,verticalCharacterCentre );
        this.headingText.position.set(0, -magnifierHeight/2 - magnifierOffset);

        magnifierContainer.addChild(magnifierGraphics);
        magnifierContainer.addChild(this.headingText);

        magnifierContainer.y = height;

        return (magnifierContainer);
    }


    /*************************************************************************
     * 
     * @param {number} fontSizeToMeasure The font size to measure in points
     * @returns A ratio expressed as a number between 0 and 1 which which is 
     *    where the centre of the character is located vertically starting from
     *    the top. The value is used in an anchor setting to position the 
     *    characters vertically centered about a point
     *************************************************************************/
    
    calculateCharacterVerticalCentre(fontSizeToMeasure){

        let fontFamily = "Tahoma";          // font name for this indictor
        let fontSize = fontSizeToMeasure;   // font size for the magnifier 
        let fontWeight = "bold";            // font weight for the magnifier

        /** 
         * Height of a capital letter divided by the height of the em square. Used
         * to calculate the center of the character veritcally.
         * This is font specific.
         */
        let digitCapitalHeightRatio = 1489/2048; 

        /**
         * Create a style to use when measuring the character sizes
         */
        const measureTextStyle = new TextStyle({
            fontFamily: fontFamily,
            fontSize: String(fontSize)+"px",
            fontWeight: fontWeight,
        });

        /** Get the character metrics */
        const sampleMetrics = CanvasTextMetrics.measureText("0123456789", measureTextStyle);

        let digitAscentDistance = sampleMetrics.fontProperties.ascent;
        let overallHeight = sampleMetrics.height; 

        /** Calculate where the center of the character is vertically
         *  as a ratio of the height of the sample message. (value between 0 and 1)
         *  This is used for the anchor command.
         *  Then add 2 pixels
         */
        var verticalCharacterCentre = ( digitAscentDistance - ((digitCapitalHeightRatio * fontSize)/2)) / (overallHeight);

        return(verticalCharacterCentre);
    }


    /*************************************************************************
     * Create a PixiJS Graphics that draws the heading bug. The bug is drawn
     *     with it's default position at 0,0.
     * @param {number} ribbonHeight The height of the ribbon in pixels
     * @returns A PixiJS Graphics object
     *************************************************************************/
    createHeadingBugContainer(ribbonHeight) {

        const bugHeight = 6;             
        const bugWidth = 24;
        const bugLineColour = 0x00FFFF;   // cyan - a value the pilot sets, as on the HSI
        const bugOutlineWidth = 1;
        const bugOutlineAlpha = 1;
        const bugOutlineAlignment = 0; // Inner
        const bugFillColour = 0x00FFFF;   // cyan
        const bugFillAlpha = 1;
        const bugTriangle = 6;
    
        // Create the Graphics

        const bugGraphics = new Graphics();
        const bugContainer = new Container();

        // Draw the bug clockwise

        bugGraphics.strokeStyle = {
            alignment: bugOutlineAlignment,
            alpha: bugOutlineAlpha,
            color: bugLineColour,
            width: bugOutlineWidth,
        };

        bugGraphics.fillStyle = {
            alpha: bugFillAlpha,
            color: bugFillColour,
        };

        const adjustBug = 0;
        bugGraphics.moveTo(0, ribbonHeight-adjustBug);
        bugGraphics.lineTo(-bugWidth/2, ribbonHeight-adjustBug);
        bugGraphics.lineTo(-bugWidth/2, ribbonHeight-adjustBug-bugHeight);
        bugGraphics.lineTo(-bugTriangle, ribbonHeight-adjustBug-bugHeight);
        bugGraphics.lineTo(0, ribbonHeight-adjustBug-(bugHeight-bugTriangle));
        bugGraphics.lineTo(bugTriangle, ribbonHeight-adjustBug-bugHeight);
        bugGraphics.lineTo(bugWidth/2, ribbonHeight-adjustBug-bugHeight);
        bugGraphics.lineTo(bugWidth/2, ribbonHeight-adjustBug);
        bugGraphics.closePath();

        bugGraphics.fill();
        bugGraphics.stroke();

        bugContainer.addChild(bugGraphics);

        return(bugContainer);
    }

    /*************************************************************************
     * Position the heading bug on the ribbon using the class properties 
     * _value and _bugValue to locate the bug.
     *************************************************************************/
    positionHeadingBugOnRibbon(){
        var bugValue = this._bugValue;

        // no bug on the tape until it is set
        this.headingBugContainer.visible = (bugValue !== null);
        if (bugValue === null) {
            return;
        }

        // check if we are displaying the extra bit of ribbon
        if (this._value < this.extraRibbonDegrees) {
            // extra ribbon is being displayed to the left of the magnifier
            // check if the bug should be in the extra
            if (bugValue > (360 - this.extraRibbonDegrees)) {
                // the bug needs to be displayed early
                bugValue = bugValue - 360;
            }
        } else if (this._value > (360 - this.extraRibbonDegrees)) {
            // we are displaying the extra at the right of the magnifier
            // check if bug should be in the extra
            if (bugValue < this.extraRibbonDegrees) {
                // the bug needs to be displayed late
                bugValue = bugValue + 360;
            }
        }

        this.headingBugContainer.x =  (bugValue - this._value) * this.pixelsPerDegree;
    }

    /*************************************************************************
     * Create a PixiJS Graphics object that draws a horizontal line at the 
     *     bottom of the heading indicator to indicate the heading bug can
     *     be selected.
     * @param {number} height The height of the heading indicator ribbon in pixels.
     * @returns A PixiJS Graphics object that draws the selectable indicator
     * 
     *************************************************************************/
    createSelectableGraphic(height){

        const selectableGraphics = new Graphics();
        
        const horizontalLineWidth = 2

        selectableGraphics.strokeStyle = {
            alignment: 0,               // inner
            alpha: 1,                   // 100%
            color: 0x00ffff,            // cyan - selected; editing shows the bug box instead
            width: horizontalLineWidth, // 2 px
        };

        selectableGraphics.moveTo(-this.displayWidth/2, height - horizontalLineWidth);
        selectableGraphics.lineTo(this.displayWidth/2, height - horizontalLineWidth);
        selectableGraphics.stroke()
    
        return selectableGraphics;
    }

    /*************************************************************************
     * Create the heading bug box: always on screen at the far left of the
     *     tape, the size of the GS box below it (90 x 25) with four rounded
     *     corners. It shows the bug setting in cyan (025°, or ---° until it is
     *     set) and a small copy of the tape's bug, horizontal with the notch
     *     down, to say it is the bug setting. The outline is grey, 2 px cyan
     *     when selected and 3 px cyan when being edited, as the other boxes.
     * @param {number} height The height of the heading indicator ribbon in pixels.
     * @returns A PixiJS Container holding the box
     *************************************************************************/
    createBugBox(height) {

        const boxWidth = 90;              // as the GS box below it
        const boxHeight = 25;             // as the other boxes
        const boxCornerRadius = 8;        // as the GS box's top corners
        const bugFontSize = 20;           // as the other boxes' values
        const symbolWidth = 16;           // the tape's bug (24 x 6) made smaller
        const symbolHeight = 6;
        const symbolNotch = symbolWidth / 4;
        const symbolGap = 4;              // between the degree sign and the bug symbol

        const left = -this.displayWidth / 2;            // the container's x is the centre of the screen
        const top = (height - boxHeight) / 2;

        const boxContainer = new Container();

        // the three outlines; one is in the container at a time (index 0, behind the text)
        const box = (strokeStyle) => {
            const graphics = new Graphics();
            graphics.roundRect(left, top, boxWidth, boxHeight, boxCornerRadius);
            graphics.fill({ color: 0x000000 });         // black
            graphics.stroke(strokeStyle);
            return graphics;
        };
        this.regularBox = box({ alignment: 1, alpha: 0.5, color: 0xFFFFFF, width: 2 });    // grey: 50% white, inside
        this.selectedBox = box({ alignment: 1, alpha: 1, color: 0x00FFFF, width: 2 });     // cyan, 2 px inside
        this.changingBox = box({ alignment: 2/3, alpha: 1, color: 0x00FFFF, width: 3 });   // cyan, 2 px inside, 1 px out
        boxContainer.addChild(this.regularBox);

        const bugValueTextStyle = new TextStyle({
            fontFamily: "Tahoma",
            fontSize: bugFontSize,
            fill: 0x00ffff,             // cyan
            fontWeight: "normal",
        });

        // the value and the symbol are centred together in the box, using the width of "000°" so the
        // value does not move as it changes
        const textWidth = CanvasTextMetrics.measureText("000°", bugValueTextStyle).width;
        const groupLeft = left + (boxWidth - (textWidth + symbolGap + symbolWidth)) / 2;

        this.bugText = new Text({
            text: "---°",
            style: bugValueTextStyle,
        });
        // + 1: as the other boxes, the digits sit a pixel low to look centred
        this.bugText.anchor.set(1, this.calculateCharacterVerticalCentre(bugFontSize));
        this.bugText.position.set(groupLeft + textWidth, height / 2 + 1);
        boxContainer.addChild(this.bugText);

        // the bug symbol: a bar with a notch cut down from its top edge, as the tape's bug
        const symbol = new Graphics();
        symbol.moveTo(-symbolWidth / 2, 0);
        symbol.lineTo(-symbolNotch, 0);
        symbol.lineTo(0, symbolHeight);
        symbol.lineTo(symbolNotch, 0);
        symbol.lineTo(symbolWidth / 2, 0);
        symbol.lineTo(symbolWidth / 2, symbolHeight);
        symbol.lineTo(-symbolWidth / 2, symbolHeight);
        symbol.closePath();
        symbol.fill({ color: 0x00FFFF });             // cyan
        symbol.position.set(groupLeft + textWidth + symbolGap + symbolWidth / 2, height / 2 - symbolHeight / 2 + 1);
        boxContainer.addChild(symbol);

        return boxContainer;
    }

    /*************************************************************************
     * Show the bug setting in the box and on the tape (dashes and no bug
     *     until it is set).
     *************************************************************************/
    showBug() {
        this.bugText.text = (this._bugValue === null)
            ? "---°" : String(this._bugValue).padStart(3, '0') + '°';   // 025°, as the G5's selected heading
        this.positionHeadingBugOnRibbon();
    }

    /*************************************************************************
     * The heading bug in degrees, null until it is set. The page gives it to
     *     the HSI, which draws the same bug (one bug, as the G5).
     *************************************************************************/
    get bugValue() {
        return this._bugValue;
    }

    /*************************************************************************
     * Put one of the box outlines (regular, selected, changing) in the box.
     *************************************************************************/
    showBox(outline) {
        this.bugBox.removeChildAt(0);
        this.bugBox.addChildAt(outline, 0);
    }

    /*************************************************************************
     * callback will be called based on the users input to the rotary control.
     * Handles a call back from the main line to deal with selecting and
     * changing the bug value. We expect to be selected first then have
     * the changable flag added to allow changes. The value should be 
     * in sequence from where it was last.
     * @param {boolean} selected When true indicates this display element is selected 
     * @param {boolean} changable When true indicates this display element should 
     *     to the value parameter but only on the second pass through the function 
     * @param {*} value The value of the encoder when changeable is true
     *************************************************************************/
    /**
     * Long press on the knob while editing (userInput): move the heading bug to the current heading.
     */
    sync() {
        this._bugValue = Math.round(this._value) % 360;
        this.showBug();
        return true;
    }

    /** The text that flashes while a sync is held (userInput). */
    get syncFlashTarget() {
        return this.bugText;
    }

     callback(selected, changable, value){

        // Process changeable first as it should be enabled last
        // Assuming that were were last in selected state
        // TODO: add checks for selected state???
        if (changable && !this.changable) {
            // we just became changable - frist pass through the function
            this.changable = true;          // set the changable flag to true
            this.changeableFirstPass = true; 

            // clear the selected flag to allow detetion of a selected mode
            // when changable goes false
            // TODO: *** We need to remove this as we changed the logic
            this.selected = false;
            // ----------------------------------------------------------------
            // TODO: Indicate that the element is CHANGEABLE
            // TODO: We came from the SELECTED state
            //
            // the thick outline; the cyan line under the tape stays as a cue to what is being adjusted
            this.showBox(this.changingBox);
            this.headingContainer.addChild(this.selectableGraphics);
            //
            // ----------------------------------------------------------------            
        } //else REMOVED else if

        if (!changable && this.changable){
            // we just left the changable state
            this.changable = false;
            // ----------------------------------------------------------------
            // TODO: Indicate that the element is NOT CHANGEABLE
            //
            this.showBox(this.regularBox);
            //
            // ----------------------------------------------------------------
            if (selected) {
                // we just left changeable but are still selected
                // ------------------------------------------------------------
                // TODO: Indcate that we are SELECTED after leaving changeable

                this.showBox(this.selectedBox);
                this.headingContainer.addChild(this.selectableGraphics);
                //
                // ------------------------------------------------------------
            } else {
                this.headingContainer.removeChild(this.selectableGraphics);
            }
        }

        // check if selected was just set while we are not changeable
        // if changable is set we can ignore this
        if (!changable && (selected && !this.selected)) {
            // we just became selected 
            this.selected = true;

            // ----------------------------------------------------------------
            // TODO: Indicate that the element is SELECTABLE
            //
            this.showBox(this.selectedBox);
            this.headingContainer.addChild(this.selectableGraphics);


            //
            // ----------------------------------------------------------------

        } //else REMOVED else if

        if (!selected && this.selected) {
            // we left the selected state
            this.selected = false;
            // ----------------------------------------------------------------
            // TODO: Indicate that the element is NOT SELECTABLE
            // TODO: This should only occur when going to the NOT SELECTED and
            //          NOT CHANGEABLE state
            this.headingContainer.removeChild(this.selectableGraphics);
            this.showBox(this.regularBox);
        }

        if (!selected && !changable) {
            // TODO: restore the normal screen
            // TODO: Consider moving this up so that it is only done once
            
        }

        // ====================================================================
        // process the encoder value provided
        if (changable && !this.changeableFirstPass) {

            let delta = value - this._lastEncoderValue;
            this._lastEncoderValue = value;
            if (delta == 0) {
                return;
            }
            if (this._bugValue === null) {
                // first click: the bug appears at the current heading
                this._bugValue = Math.round(this._value) % 360;
                this.showBug();
                return;
            }
            let increment = (this._bugSpeedSensitive && Math.abs(delta) >= this._bugFastDeltaThreshold)
                ? this._bugFastIncrement : this._bugIncrement;
            this._bugValue = this._bugValue + delta * increment;
            // Wrap to 0-359
            this._bugValue = ((this._bugValue % 360) + 360) % 360;
            this.showBug();

        } else if (this.changeableFirstPass) {
            // Save the starting encoder value so the first delta is zero
            this._lastEncoderValue = value;
            this.changeableFirstPass = false;
        }
    }


}