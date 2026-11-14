'use strict';

export const VERSION = '1.0.9';
import { Container, Graphics, TextStyle, Text, CanvasTextMetrics } from './pixi.mjs';

// The encoder debug numbers (selected item and encoder position, bottom left of the PFD) are always
// created; DEBUG only says whether they show at start. The Menu's "Encoder Debug" row shows and hides them.
const DEBUG = false;

// Home position: after HOME_TIMEOUT_MS with no turn or press, the knob goes back to the first item (QNH) selected,
// not being edited: a push and a turn sets baro, and a turn by mistake only moves the selection, so the QNH cannot
// be changed by someone who forgot it was being edited (David, 2026-11-14; it was edit mode before). Start-up is
// the same. Long press: when the knob has been held LONG_PRESS_MS
// while editing an item that can sync (canSync(), or a sync() if it has no canSync()), its value flashes: the cue
// to let go. Let go before LONG_HOLD_MS, the item's sync() runs (altitude bug to current altitude, heading bug to
// heading, CRS to the bearing to the waypoint, QNH from GPS on the ground, as the G5's push-and-hold) and it stays
// in edit mode. Long hold: held on to LONG_HOLD_MS, from anywhere, nothing is synced; onLongHold runs (the page
// sets full brightness) and the item it returns (brightness) is edited, so a screen left dark on the bench can be
// found again; it goes home to QNH HOME_TIMEOUT_MS after the knob is let go.
const HOME_TIMEOUT_MS = 10000;
// A change of more than this many clicks in one update (the page gets about 20 a second) cannot be a hand on the
// knob: it is a bad encoder read (the server re-baselines after 5 far-off reads). It is taken as a new starting
// point, so the item being edited does not jump (a jump once took the QNH to its 28.00 limit, 2026-11-14).
const MAX_CLICKS_PER_UPDATE = 20;
const LONG_PRESS_MS = 1000;
const LONG_HOLD_MS = 3000;
const SYNC_FLASH_PERIOD_MS = 250;   // one on/off cycle of the flash while a long press is held

export class UserInput {

/**
 *     The UserInput class allows the user to select between various elements using
 *     the rotary input and then provide new input to these elements. It allows 
 *     each of the elements to register a call back. 
 * 
 *     The class has two modes of operation. In the first mode, rotation of the 
 *     encoder will allow cycle through the registered call backs and indicate to 
 *     the registered element that it is selected. If the user chooses to provide 
 *     input to the element they depress the knob once and they enter a mode where
 *     the rotation of the encoder will provide data to that element.
 * 
 *     @param {PIXI.Appliation} app The current application. This is used to 
 *     allow debugging information to be displayed on the screen. This 
 *     should be removed in production.
 */
    constructor(app) {

        this.encoderPosition = 0;
        this.encoderButton = false;

        this.lastButton = this.encoderButton;
        this.lastPosition = this.encoderPosition;

        this.itemSelectionMode = false;
        this.firstPass = true;
        this.rebaseNext = false;    // see rebaseEncoder()
        this.report = null;         // optional function(text): sends a line to the server's log (set by the page)
        this.onLongHold = null;     // optional function(): run on a long hold, returns the item to edit (set by the page)

        this.lastActivity = performance.now();  // time of the last turn or press (home position)
        this.pressStart = 0;                    // when the button went down
        this.holdStage = 2;                     // 0 pressed, 1 past LONG_PRESS_MS, 2 past LONG_HOLD_MS (or done)
        this.ignoreRelease = false;             // this press was a long hold: its release does not change mode
        this.syncItem = null;                   // the item flashing, to sync when the knob is let go

        // storage for call back and virtual encoder
        this.numberOfCallbacksRegistered = 0;
        this.callbackFunction = new Array();
        this.virtualEncoderValue = new Array();
        this.currentSelection = 0;      // currently selected callback
        this.encoderAdjustment = 0;     // adjustement of encoder position
        this.virtualEncoderForSelection = 0;
        
        // Encoder debug numbers, hidden unless DEBUG or the Menu turns them on
        let displayHeight = app.screen.height;

        this.itemSelectorText = this.createTextObjectAt(90, displayHeight);

        this.encoderText = this.createTextObjectAt(140, displayHeight);

        this.setDebugVisible(DEBUG);

        app.stage.addChild(this.itemSelectorText);
        app.stage.addChild(this.encoderText);

        this.app = app;
    }

    /**
     * createTextObjectAt creates a text object on the screen for debugging 
     *     purposes. This can be removed when the debugging code is removed
     * @param {number} x The x position of the lower right corner of the text box
     * @param {number} y The y position of the lower right corner of the text box
     * @returns A PIXI.Graphics object of the text box.
     */
    createTextObjectAt(x, y) {

        let textStyle = new TextStyle({
            fontFamily: 'Tahoma',
            fontSize: '20px',
            fill: "white",
            fontWeight: "normal"
        });

        let textObject = new Text({
            text: "0",
            style: textStyle,
        });
        textObject.anchor.set(0,1);
        textObject.position.set(x, y);

        return textObject

    }


    /**
     *  Processes the encoder position and button inputs and makes calls to 
     *     the appropriate call back functions to indicate if an object is 
     *     selected, not selected, or changable and passes the encoder value
     *     to the changable object
     * @param {number} position The encoders position
     * @param {boolean} button The state of the encoder push button. True is 
     *     depressed. False is released.
     */
    rebaseEncoder() {
        // Call when the server has restarted (a new connection): its encoder position may not continue from the
        // old one. The next position seen becomes the starting point, so the jump does not turn the selected item.
        this.rebaseNext = true;
    }

    processState(position, button) {
        var encoderPosition;

        // Handle both the first time through and until there is a call back 
        //     registered.
        if (this.firstPass) {
            // Save the current encoder position into encoderAdjustment so that
            //     the actual position of the encoder does not mater on startup.

            // TODO: Saving the encoder position does not appear to be working.
            // TODO:     This needs debugging.
            this.encoderAdjustment = position;
            // This position is the starting point, so it is the last position seen and the rebase asked for on
            // the first message (rebaseEncoder) is done. Without this the rebase measured from 0 and added the
            // encoder's count a second time; it came out as a QNH jump on going back to QNH after a reload
            // (2026-11-14, about 40 clicks).
            this.lastPosition = position;
            this.rebaseNext = false;
            if (DEBUG) {
                //console.log("First Pass processing encoder");
                //console.log(this.encoderAdjustment);
                //console.log(button)
            }

            // check if we have any registered callbacks
            if (this.numberOfCallbacksRegistered > 0 && position !== undefined) {

                this.firstPass = false;
            }

        // All regular processing happens here
        } else {
            // We have at least one call back registered

            // No position (the server has just restarted), or the first one after a reconnection: take it as the
            // starting point. encoderAdjustment moves by the jump, so the value of the selected item is unchanged.
            if (position === undefined || position === null) {
                this.rebaseNext = true;
                return;
            }
            if (this.rebaseNext) {
                this.encoderAdjustment += position - this.lastPosition;
                this.lastPosition = position;
                this.rebaseNext = false;
                return;
            }

            // A jump too big for a hand on the knob: take the new position as the starting point (see above)
            if (Math.abs(position - this.lastPosition) > MAX_CLICKS_PER_UPDATE) {
                const text = "knob jump ignored: " + this.lastPosition + " -> " + position;
                console.log(text);
                if (this.report) this.report(text);
                this.encoderAdjustment += position - this.lastPosition;
                this.lastPosition = position;
                return;
            }
            
            /******************************************************************
             * Handle changes to the push button state first                  *
             ******************************************************************/

            const now = performance.now();

            // Check if the button has changed
            if (button != this.lastButton) {

                this.lastButton = button
                this.lastActivity = now;
                if (button) {
                    // pressed: time it for a long press
                    this.pressStart = now;
                    this.holdStage = 0;
                    this.ignoreRelease = false;
                } else if (this.ignoreRelease) {
                    // released after a long hold: stay on the item it went to, in edit mode
                    this.ignoreRelease = false;
                } else if (this.syncItem) {
                    // released after a long press: sync the item, stay in edit mode
                    const item = this.syncItem;
                    this.stopFlash();
                    item.sync();
                } else {
                    // The button was just released therefore the item selection
                    // mode just changed.
                    // Toggle the item selection mode
                    this.itemSelectionMode = !this.itemSelectionMode
                    
                    // Process the current selection Mode
                    if (this.itemSelectionMode) {
                        // We have entered selection mode which means we can
                        // selected between the items that have been registered

                        // Unselect previous item if there are any
                        if (this.numberOfCallbacksRegistered > 0 ) {
                            this.callbackFunction[this.currentSelection].callback(
                                true,   // The item is selected
                                false,  // The item cannot be adjusted
                                0       // The position should be ignored
                            );
                        }

                        // Save the virtual encoder position for the item that is currently selected
                        // position is the current encoder position
                        // this.virtualEncoderValue[]: holds the last virtual position
                        // this.encoderAdjustment: was the encoder value when this item was selected

                        encoderPosition = (position + 
                            this.virtualEncoderValue[this.currentSelection] -
                            this.encoderAdjustment);
                        this.virtualEncoderValue[this.currentSelection] = encoderPosition;

                    } else {
                        // We have exited selection mode which means 
                        // The currently selected item can be adjusted

                        // save the encoder position so that we can start
                        // at the same place when we return to selection mode
                        this.virtualEncoderForSelection = (position +
                            this.virtualEncoderForSelection -
                            this.encoderAdjustment);
                        
                        // Make the current selected item adjustable
                        if (this.numberOfCallbacksRegistered > 0 ) {
                            this.callbackFunction[this.currentSelection].callback(
                                true,  // The item is selected
                                true,  // The item can be adjusted
                                this.virtualEncoderValue[this.currentSelection]  // last saved encoder value
                            );
                        }

                    }
                    // save the current encoder position to be applied
                    // as the encoder makes adjustments
                    this.encoderAdjustment = position
                }
            }

            // Long press: once, when the hold reaches LONG_PRESS_MS, flash the item being edited if it can sync
            if (button && this.holdStage == 0 && now - this.pressStart >= LONG_PRESS_MS) {
                this.holdStage = 1;
                const item = this.callbackFunction[this.currentSelection];
                const canSync = (typeof item.canSync === 'function') ? item.canSync() : (typeof item.sync === 'function');
                if (!this.itemSelectionMode && canSync) {
                    this.syncItem = item;
                }
            }

            // Long hold: once, when the hold reaches LONG_HOLD_MS, stop the flash (nothing is synced) and go to the
            // item onLongHold returns, in edit mode
            if (button && this.holdStage == 1 && now - this.pressStart >= LONG_HOLD_MS) {
                this.holdStage = 2;
                if (this.syncItem) {
                    this.stopFlash();
                }
                if (this.onLongHold) {
                    const index = this.callbackFunction.indexOf(this.onLongHold());
                    if (index >= 0) {
                        this.goTo(index, position);
                    }
                    this.ignoreRelease = true;
                }
            }

            /******************************************************************
             * Handle changes to the encoder position after the button state  *
             ******************************************************************/

            // Check if the position has changed
            if (position != this.lastPosition) {
                // Handle changes in the position of the encoder
                if (this.itemSelectionMode) {
                    // We are in selection mode
                    // check if the encoder has moved

                    // calculate new encoder position

                    encoderPosition = (position + 
                                            this.virtualEncoderForSelection -
                                            this.encoderAdjustment);
                    // deslect item
                    this.callbackFunction[this.currentSelection].callback(
                        false,  // item is not selected
                        false,  // item cannot be adjusted
                        0       // should be ignored
                    );

                    let remainder = encoderPosition % this.numberOfCallbacksRegistered;
                    if (remainder < 0) {
                        remainder = remainder + this.numberOfCallbacksRegistered;
                    }
                    this.currentSelection = remainder;



                    // select item
                    this.callbackFunction[this.currentSelection].callback(
                        true,   // item is selected
                        false,  // item cannot be adjusted
                        0       // should be ignored
                    );

                    // TODO: debugging output
                    // Output the currently selected item
                    if (this.itemSelectorText.visible) {
                        this.itemSelectorText.text = this.currentSelection.toString();
                    }
                    // Calculate and display the encoder value for the item
                    let itemEncoderPosition = this.virtualEncoderValue[this.currentSelection];
                    if (this.itemSelectorText.visible) {
                        this.encoderText.text = itemEncoderPosition;
                    }

                } else {
                    // we are in adjustment mode
                    
                    // Adjust the selected item
                    
                    encoderPosition = (position + 
                        this.virtualEncoderValue[this.currentSelection] -
                        this.encoderAdjustment);

                    this.callbackFunction[this.currentSelection].callback(
                        true,   // item is selected
                        true,   // item can be adjusted
                        encoderPosition // new position
                    );

                    //TODO: debugging output to the screen
                    if (this.itemSelectorText.visible) {
                        this.encoderText.text = encoderPosition.toString();
                    }
                }

                // Save the last positoin for the next time through
                this.lastPosition = position;
                this.lastActivity = now;
            }

            // The item to sync flashes while the knob is held
            if (this.syncItem && this.syncItem.syncFlashTarget) {
                this.syncItem.syncFlashTarget.visible = Math.floor(now / (SYNC_FLASH_PERIOD_MS / 2)) % 2 === 0;
            }

            // Home position: left alone anywhere but QNH selected, go back there
            const atHome = this.itemSelectionMode && this.currentSelection == 0;
            if (!atHome && !button && now - this.lastActivity >= HOME_TIMEOUT_MS) {
                this.goHome(position);
            }
    
        }


    }

    /**
     * Go back to the first item (QNH) selected, not being edited, so a turn by mistake cannot change it. An item
     * left while being edited keeps its virtual encoder value, so it carries on from there when it is edited again.
     * @param {number} position The encoders position
     */
    goHome(position) {
        this.goTo(0, position, false);
    }

    /**
     * Go to an item, in edit mode (the long hold) or only selected (goHome). An item left while being edited keeps
     * its virtual encoder value, so it carries on from there when it is edited again.
     * @param {number} index The item, in the order registered
     * @param {number} position The encoders position
     * @param {boolean} edit True to edit the item, false to leave it selected
     */
    goTo(index, position, edit = true) {
        if (!this.itemSelectionMode) {
            this.virtualEncoderValue[this.currentSelection] = (position +
                this.virtualEncoderValue[this.currentSelection] - this.encoderAdjustment);
        }
        this.callbackFunction[this.currentSelection].callback(false, false, 0);

        this.currentSelection = index;
        this.itemSelectionMode = !edit;
        this.virtualEncoderForSelection = index;    // the next press selects this item, and turning moves on from it
        this.encoderAdjustment = position;
        this.callbackFunction[index].callback(true, edit, edit ? this.virtualEncoderValue[index] : 0);
    }

    /**
     * Stop the long press flash (let go, or the long hold).
     */
    stopFlash() {
        const item = this.syncItem;
        this.syncItem = null;
        if (item.syncFlashTarget) {
            item.syncFlashTarget.visible = true;
        }
    }

    /**
     * Register a callback function.
     *      The callback must take three parameters
     *      parameter 1 is boolean and indicates if the item is selected (true/false)
     *      parameter 2 is boolean and indicates if the item can be changed (true/false)
     *      parameter 3 is a number and indicates the virtual encoder value
     * 
     * @param {function} callbackFunction That is called when user input needs to 
     *     be directed to the element
     */

    registerCallback(callbackClass){
        this.numberOfCallbacksRegistered = this.callbackFunction.push(callbackClass);
        this.virtualEncoderValue[this.numberOfCallbacksRegistered - 1] = 0;

        if (this.numberOfCallbacksRegistered == 1) {
            // This is the first callback registered (QNH): start at the home position, selected but not being
            // edited, so a turn by mistake right after power-on cannot change it (a push and a turn sets it)
            this.itemSelectionMode = true;
            this.callbackFunction[this.currentSelection].callback(
                true,   // item is selected
                false,  // item cannot be edited until the knob is pushed
                0       // should be ignored
                );
        }
    }

    setDebugVisible(visible) {
        if (this.itemSelectorText) this.itemSelectorText.visible = visible;
        if (this.encoderText) this.encoderText.visible = visible;
    }

}