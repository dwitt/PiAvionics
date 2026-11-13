/*****************************************************************************
 * EFIS Display
 * This is the main module load by the index.html file.
 * The proram uses PixiJS to support the graphics display
 * 19-May-2025
 * Current PixiJS Version 8.6.6
 * version 0.2
 * 
 *****************************************************************************/
'use strict';

import { Application, Graphics, Text, TextStyle } from './pixi.mjs';
// Alternative imports for PixiJS to support minified code --------------------
//import { Application, Graphics } from './pixi.min.js';
//import { Application, Graphics, Container } from './pixi.min.mjs';

// Import of modules required for different aspects of the EFIS display -------
// Commented out various lines as part of update to newer PixiJS version

import { Ribbon } from './ribbon.mjs';  // Altitude Ribbon

import { AirspeedRibbon } from './airspeedRibbon.mjs';
import { VsiIndicator } from './vsi-indicator.mjs';
import { AttitudeIndicator } from './attitude-indicator.mjs';
import { SlipBallIndicator } from './slipBall.mjs';
import { HeadingIndicator } from './headingIndicator.mjs';
import { QNHDisplay } from './qnhdisplay.mjs'; //
import { SpeedDisplay } from './speedDisplay.mjs';
import { AltitudeDisplay } from './altitudeDisplay.mjs'
import { TempTimeDisplay } from './tempTimeDisplay.mjs';
// import { calculateCharacterVerticalCentre } from './utilityFunctions.mjs';
import { UserInput } from './userInput.mjs';
import { AirspeedWheel } from './airSpeedWheel.mjs';
import { AltitudeWheel } from './altitudeWheel.mjs';
import { Brightness } from './brightness.mjs';
import { TurnRateIndicator } from './turnRateIndicator.mjs';

import { SoftButtons } from './softButtons.mjs';
import { MenuOverlay } from './menuOverlay.mjs';
import { HSI } from './hsi.mjs';
import { RouteOverlay } from './routeOverlay.mjs';
import { OrientationPanel } from './orientationPanel.mjs';
import { InstrumentStatus, describeModule } from './instrumentStatus.mjs';
import { ModulePanel } from './modulePanel.mjs';
import { FailureFlag } from './failureFlag.mjs';
import { Annunciators } from './annunciators.mjs';
import { FAILURE_LOOK } from './failureLook.mjs';



// ----------------------------------------------------------------------------

const VERSION = "0.8.8";

// Viewer mode: a page that is not the Pi's own display only watches. It sends
// nothing to the server, ignores the encoder, and shows the QNH and brightness
// the Pi's page reports in controller_state. The server enforces this too (it
// ignores commands from anything but the local browser).
//   ?view=1            force viewer mode
//   ?server=host:port  take the data from another EFIS server (implies viewer)
//   ?sharp=0           draw at 1x and let the browser enlarge it (the old way, for comparing)
//   ?pi=true           lay the page out at the size of the Pi's own screen, portrait (below)
//   ?pi=portrait       the same, portrait: 576 x 1024, canvases stacked
//   ?pi=landscape      the Pi's screen turned: 1024 x 576, canvases side by side
// Plan: EFIS/docs/MULTI_CLIENT_PLAN.md
const pageParams = new URLSearchParams(location.search);
const serverOverride = pageParams.get('server');
const LOCAL_HOSTNAMES = ['localhost', '127.0.0.1', '[::1]'];
const VIEW_ONLY = pageParams.get('view') === '1' || serverOverride !== null
                  || !LOCAL_HOSTNAMES.includes(location.hostname);

// A reloaded controller page starts from the QNH and brightness the server last
// received (controller_state), instead of 29.92 / 100. Until the first message from
// the server has arrived the page sends nothing, so its defaults cannot overwrite them.
// The QNH is shared with the other devices on the CAN bus (EFIS/docs/QNH_PLAN.md): the server holds it,
// so after the first pass the page keeps following controller_state.qnh, except while the person is
// turning the dial. The page sends the QNH only when the person changes it.
var gotFirstMessage = false;
var restoredFromServer = false;

// What the server's facts mean for the display: which instruments can be trusted and what to announce
const instrumentStatus = new InstrumentStatus();

var SEND_PERIOD = 1000;         // time in milliseconds between the brightness / ticker messages (no QNH in them)
var QNH_USER_HOLD = 1500;       // ms after the person's last turn during which a QNH from the server is not adopted
// last_qnh stores QNH in inHg × 100 format (e.g., 2992 = 29.92 inHg)
var last_qnh = 2992;            // default qnh (29.92 inHg × 100)
var last_brightness = null;     // force a brightness change of first pass
var send_timestamp = Date.now();        // time of the last message sent
var last_qnh_user_change = 0;           // time (ms) the person last changed the QNH on this page
var current_time_millis = Date.now();   // current time

// Turn rate spring-mass-damper filter variables
// Simulates mechanical gyroscope inertia: mass resists sudden changes,
// spring pulls toward actual value, damping prevents oscillation
var smoothedTurnRate = 0.0;     // Current displayed turn rate (position)
var turnRateVelocity = 0.0;     // Rate of change of displayed value
var turnRateSpring = 8.0;       // Spring constant - how strongly it tracks the input
var turnRateDamping = 4.5;      // Damping constant - slightly underdamped for realistic feel
                                // Critical damping = 2 * sqrt(spring) ≈ 5.66, using 4.5 for slight underdamp

// TEST: Set to true to sweep airspeed 0-200 for testing color bands
var TEST_AIRSPEED_SWEEP = false;
var testAirspeed = 0;
var testAirspeedDir = 1;

// ----------------------------------------------------------------------------
// --- Load Fonts                                                           ---
// ----------------------------------------------------------------------------

// Both Tahoma faces are loaded before any Text is made. Pixi draws a Text once and keeps the picture,
// so text made before its font has loaded (the HSI and the route overlay below) kept the fallback font.
// If a font fails to load, the page carries on with the fallback.
var tahoma_normal_font = new FontFace('Tahoma', 'url(support/Tahoma.ttf)', {weight: 400});
var tahoma_bold_font = new FontFace('Tahoma', 'url(support/Tahoma%20Bold.ttf)', {weight: 800});

try {
    const loadedFaces = await Promise.all([tahoma_normal_font.load(), tahoma_bold_font.load()]);
    for (const face of loadedFaces) document.fonts.add(face);
} catch (error) {
    console.log("Font loading failed, using the fallback font:", error);
}

// ----------------------------------------------------------------------------
// ---Create a Pixi Application                                             ---
// ----------------------------------------------------------------------------

// The Pi's screen is 720 x 1280 pixels with the compositor scaled 1.25 (labwc autostart,
// wlr-randr --scale=1.25), so the kiosk browser window is 576 x 1024 CSS pixels.
// ?pi=true or ?pi=portrait uses that size in any browser window, for an accurate view of the Pi's
// layout; ?pi=landscape uses the screen turned on its side (1024 x 576).
const PI_SCREEN_W = 576;
const PI_SCREEN_H = 1024;
const piParam = pageParams.get('pi');
const USE_PI_SIZE = piParam === 'true' || piParam === 'portrait' || piParam === 'landscape';
const PI_LANDSCAPE = piParam === 'landscape';

// The layout (stacked or side by side) is chosen once at load, so reload when the window changes shape:
// the Pi's screen turning (Menu, Orientation) or a laptop window being resized.
const loadedLandscape = window.innerWidth > window.innerHeight;
if (!USE_PI_SIZE) {
    let reloadTimer = null;
    window.addEventListener('resize', () => {
        clearTimeout(reloadTimer);
        reloadTimer = setTimeout(() => {
            if ((window.innerWidth > window.innerHeight) !== loadedLandscape) location.reload();
        }, 400);
    });
}

const screenW = USE_PI_SIZE ? (PI_LANDSCAPE ? PI_SCREEN_H : PI_SCREEN_W) : window.innerWidth;
const screenH = USE_PI_SIZE ? (PI_LANDSCAPE ? PI_SCREEN_W : PI_SCREEN_H) : window.innerHeight;

let width = screenW;
let height = screenH;

if (screenH > screenW) {
  // Portrait → top half
  height = screenH / 2;
} else {
  // Landscape → left half
  width = screenW / 2;
}
// Draw at the display's real pixel ratio (1.25 on the Pi, 2 on a retina laptop) so lines and text
// are not enlarged by the browser. The canvas keeps its width and height in CSS pixels
// (autoDensity); only the pixels drawn inside it are finer. ?sharp=0 goes back to drawing at 1x.
const SHARP_DRAWING = pageParams.get('sharp') !== '0';
const drawingOptions = SHARP_DRAWING ? { resolution: window.devicePixelRatio, autoDensity: true } : {};
// Antialiasing smooths the edges of angled lines; Pixi's default is off. Measured on the Pi 2026-11-13 with the
// Menu's Frame Rate readout: off, 60 fps and no slow frames; on for the primary canvas only, about 48 fps; on for
// both canvases, worse (30 and 20 fps). So both are off by default. Each canvas follows its Menu row, "PFD Antialias"
// and "HSI Antialias" (kept in this browser's storage; reloads the page, since antialiasing can only be chosen
// when a canvas is made).
const ANTIALIAS_KEY = 'efis.pfdAntialias';
const HSI_ANTIALIAS_KEY = 'efis.hsiAntialias';
const FRAME_RATE_KEY = 'efis.frameRateShown';
function storedFlag(key, fallback) {
    try {
        const value = localStorage.getItem(key);
        return value === null ? fallback : value === 'on';
    } catch (error) {
        return fallback;            // storage blocked: use the default
    }
}
function storeFlag(key, on) {
    try { localStorage.setItem(key, on ? 'on' : 'off'); } catch (error) { /* storage blocked: not kept */ }
}
const PFD_ANTIALIAS = storedFlag(ANTIALIAS_KEY, false);
const HSI_ANTIALIAS = storedFlag(HSI_ANTIALIAS_KEY, false);

const app = new Application();  // the applicaiton object available globally
await app.init({width: width,
                height: height,
                ...drawingOptions,
                antialias: PFD_ANTIALIAS
                }
);

// ----------------------------------------------------------------------------
// Create another application as a test
// This sets up a second canvas for display. This is just a test canvas
// TODO: Delete this section
// This will create a blank canvas that is black at the bottom or side of the
// screen.
const app2 = new Application();
await app2.init({width: width,
                height: height,
                ...drawingOptions,
                antialias: HSI_ANTIALIAS
                }
);

// ----------------------------------------------------------------------------
// --- Add the canvas that Pixi automatically created for you to the HTML   ---
// --- document                                                             ---
// ----------------------------------------------------------------------------
document.body.appendChild(app.canvas);
document.body.appendChild(app2.canvas);
if (USE_PI_SIZE) {
    // Keep the Pi's arrangement whatever the window size: stacked (primary above the HSI) in portrait,
    // side by side in landscape
    document.body.style.display = 'flex';
    document.body.style.flexDirection = PI_LANDSCAPE ? 'row' : 'column';
    document.body.style.alignItems = 'flex-start';       // keep each canvas at its own size
    app.canvas.style.display = 'block';
    app2.canvas.style.display = 'block';
}

// "VIEW ONLY" tag in the bottom left corner of the second canvas (the top left holds the GPS box)
if (VIEW_ONLY) {
    const viewTag = new Text({
        text: "VIEW ONLY",
        style: new TextStyle({fontFamily: 'Tahoma', fontSize: 16, fontWeight: 'bold',
                              fill: 0xFFB000})
    });
    viewTag.anchor.set(0, 1);
    viewTag.position.set(6, app2.screen.height - 4);
    app2.stage.addChild(viewTag);
}

// --- HSI on the second canvas ---
// The rose is 75% of the smaller side of the canvas, but the heading box above it and the ground
// speed and waypoint lines below it need about 120 pixels of height between them, which matters on a
// short canvas (400 pixels tall on a Pi Touch Display 1 in portrait).
const HSI_TEXT_ROOM = 120;

// The GPS ground track is worked out from the velocity, so it is noisy when you move slowly (about 5 degrees at
// 2 knots, 1 degree at 10 knots for this chip's typical velocity accuracy). The track diamond is only drawn at or above this ground speed.
const TRACK_MIN_SPEED_KT = 10;
let hsiDiameter = Math.min(Math.min(app2.screen.width, app2.screen.height) * 0.75,
                           app2.screen.height - HSI_TEXT_ROOM);
hsi = new HSI(app2, app2.screen.width / 2, app2.screen.height / 2, hsiDiameter);
const hsiFlag = new FailureFlag(app2, app2.screen.width / 2 - hsiDiameter / 2,
                                app2.screen.height / 2 - hsiDiameter / 2, hsiDiameter, hsiDiameter, 'HDG');

// --- Route overlay and button ---
function sendCommand(cmd) {
    if (VIEW_ONLY) return;
    if (myWebSocket.readyState == 1) {
        myWebSocket.send("json" + JSON.stringify(cmd));
    }
}
let routeOverlay = new RouteOverlay(app2, sendCommand);

// "Route" button — lower right corner of HSI canvas, matching Menu button style
let routeBtnWidth = 90;
let routeBtnHeight = 38;
let routeBtnMargin = 1;
const routeBtn = new SoftButtons(app2, {
    addContainer() {
        routeOverlay.toggle();
        routeBtn.clearNotification();
    }
}, {
    x: app2.screen.width - routeBtnWidth - routeBtnMargin,
    y: app2.screen.height - 40 + routeBtnMargin,
    width: routeBtnWidth,
    height: routeBtnHeight + 2 * routeBtnMargin,
    text: "Route"
});
let _lastRouteLen = undefined;
let _lastActiveLeg = undefined;

// ----------------------------------------------------------------------------
// --- Create a new object to hold the data object coming from the websocket---
// ----------------------------------------------------------------------------

//TODO: Not sure if this can be deleted. Need to add a pause and see what happens
var dataObject = new Object();
dataObject.altitude = 0;
dataObject.airspeed = 0;
dataObject.pitch = 0;
dataObject.roll = 0;
dataObject.qnh = 29.92;
dataObject.vsi = 0;
dataObject.accy = 0;
dataObject.yaw = 0;
dataObject.position = 0;
dataObject.pressed = false;
dataObject.magx = 0.0;
dataObject.magy = 0.0;
dataObject.magz = 0.0;

// ----------------------------------------------------------------------------
// --- Connect to the websocket to receive the data from the can bus as     ---
// --- objects.                                                             ---
// ----------------------------------------------------------------------------

var webSocketURL = "ws://" + (serverOverride || location.host) + "/ws";
var myWebSocket;
var encoderRebasePending = false;       // set on every (re)connection, cleared by the first message
const WEBSOCKET_RECONNECT_MS = 1000;    // delay between reconnect attempts

function connectWebSocket() {
    myWebSocket = new WebSocket(webSocketURL);

    // listen for the 'open' event and respond with "ready"
    myWebSocket.addEventListener('open', function(event){
        // A new connection may mean a restarted server whose encoder position does not continue from the old one:
        // userInput takes the first position it gets as a new starting point (rebaseEncoder)
        encoderRebasePending = true;
        // Let the server know we are ready for data
        myWebSocket.send("ready")
    })

    myWebSocket.onmessage = RecieveWebSocketMessage;

    // Reconnect when the connection drops (e.g. the server restarts).
    // 'close' also fires when a connection attempt fails, so this one
    // handler retries until the server is back.
    myWebSocket.addEventListener('close', function(event){
        setTimeout(connectWebSocket, WEBSOCKET_RECONNECT_MS);
    })
}
connectWebSocket();

// ----------------------------------------------------------------------------
// --- Wait for the document to report the fonts are loaded then call setup ---
// ----------------------------------------------------------------------------

// declare global variables

var attitudeIndicator,
    altitudeWheel, 
    qnhDisplay, 
    altimeter_ribbon, 
    airspeedWheel,
    airspeedRibbon,
    vsiIndicator,
    slipBallIndicator,
    headingIndicator,
    speedDisplay,
    altitudeDisplay,
    tempTimeDisplay,
    brightness,
    egtGraph,
    chtGraph,
    userInput,
    softButtons,
    menuOverlay,
    orientationPanel,
    modulePanel,
    annunciators,
    asiFlag,
    altFlag,
    vsiFlag,
    attFlag,
    hdgFlag,
    turnFlag,
    asiCover,
    altCover,
    vsiCover,
    hsi,
    magnetometerCalibrate,
    turnRateIndicator,
    turnRateDebugText;

// call setup after the fonts are loaded and ready
    
document.fonts.ready.then(function() {
    setup();
});

// ----------------------------------------------------------------------------
// --- WebSocket message handler is assigned in connectWebSocket() above    ===
// ----------------------------------------------------------------------------

// ****************************************************************************
// *** END OF SCRIPT - We are event based from this point onward            ***
// ****************************************************************************


// ----------------------------------------------------------------------------
// --- Process messages received from the websocket connection              ---
// ----------------------------------------------------------------------------

function RecieveWebSocketMessage(event) {
    // parse the event.data into a data object
    // this will contain the data from the CAN bus
    dataObject = JSON.parse(event.data);
    gotFirstMessage = true;
    if (encoderRebasePending) {
        encoderRebasePending = false;
        if (typeof userInput !== 'undefined' && userInput) userInput.rebaseEncoder();
    }
    instrumentStatus.messageReceived(Date.now());
}


/*****************************************************************************
 * @brief   The setup function is called create the objects that will be
 *          drawn on the canvas.
 *****************************************************************************/
function setup() {

    let x = app.screen.width;       // canvas width
    let y = app.screen.height;      // canvas height

    //-------------------------------------------------------------------------
    // Objects are arranged back to front on the display. Create the
    // background objects first.
    // ------------------------------------------------------------------------

    //-------------------------------------------------------------------------
    // Create the Attitude indicator
    // It is centerally located on the screen.
    //-------------------------------------------------------------------------
    attitudeIndicator = new AttitudeIndicator(app);       
    
    //-------------------------------------------------------------------------
    // Create the altimeter display ribbon (vertical tape)
    //-------------------------------------------------------------------------
    altimeter_ribbon = new Ribbon(
        app,            // the current application to draw the ribbon on
        x-35, y/2,      // the location of the ribbon
        y-140, 90,      // the height and width of the ribbon
        true,           // right side if true
        100,            // Major interval size (units)
        4,              // number of major intervals in ribbon height
        5,              // number of minor devisions per major devision
        true,           // allow negative values if true
        undefined,      // colour_bar1 
        undefined,      // colour_bar2
        true);          // show adjustable setting bug if true
    
    //-------------------------------------------------------------------------
    // Create the current altitude display as a magnifier the as each of the
    // digits display as if they are on a wheel the rotate on a axis
    // parallel to the screen
    //-------------------------------------------------------------------------    
    altitudeWheel = new AltitudeWheel(app) //, 755, 240);
    
    //------------------------------------------------------------------------- 
    // Create an qnh display, generally below the airspeed ribbon that
    // displays the current qnh value in inHg
    //------------------------------------------------------------------------- 

    qnhDisplay = new QNHDisplay(
        app,                        // the current application to draw on
        x - (35), y-140/2,          // the location of the display
        90, 25,                     // the width and heigh of the display
        8);                         // the box's corner radius

    //------------------------------------------------------------------------- 
    // Create an altitude display, generally above the altitude ribbon, that
    // can display various altitudes that are uses selected such as GPS,
    // Pressure or Density altitude
    //------------------------------------------------------------------------- 
    altitudeDisplay = new AltitudeDisplay(
        app,                    // the current appication to draw on
        x - (35), 140/2,        // location of the altitude display
        90, 25,                 // width and height of the box
        8);                     // radius of the box's top corners

    // ------------------------------------------------------------------------
    // Create the airspeed ribbon to be displayed on the left side of the 
    // EFIS display area
    // The ribbon origin is it's center left unless the right side flag is
    // true
    // ------------------------------------------------------------------------

    airspeedRibbon = new AirspeedRibbon(
        app,            // the current application to draw the ribbon on
        0, y/2,         // the x and y location of the ribbon
        y-140, 90,      // height and width of the ribbon
        false,          // left side if false
        10,             // major interval size (units)
        8,              // number of major intervals in ribbon height
        2,              // number of minor intervals per major
        false);         // don't allow negative numbers.

    airspeedWheel = new AirspeedWheel(app, 12, y/2);
    
    //-------------------------------------------------------------------------
    // Create a speed display, generally above the airspeed ribbon, that can
    // display various speeds that the user selects, such as Ground Speed, and
    // True Air Speed
    //-------------------------------------------------------------------------
    speedDisplay = new SpeedDisplay(
        app,            // the current application to draw on
        0, 140/2,       // the x and y location of the display (lower left)
        90, 25,         // the width and height of the display
        8);             // the radius of the box's top corners

    
    //-------------------------------------------------------------------------
    // Create a display below the speed ribbon to display the time or the OAT
    //-------------------------------------------------------------------------
    tempTimeDisplay = new TempTimeDisplay(
        app, 
        0, y-140/2, // was + 25 for the height
        90, 25, 
        8);

    //-------------------------------------------------------------------------
    // Create a VSI Indicator
    // Position the indicators origin 35 pixels from the left edge of the
    // screen and vertically centered on the screen.
    // The VSI indicator is 35 pixels wide and 80 pixels tall.
    //-------------------------------------------------------------------------
    vsiIndicator = new VsiIndicator(
        app,            // the current application to draw on          
        x-35, y/2,      // the x and y location of the VSI indicator
        y-80, 35);      // the width and height of the VSI indicator

     var aircraft = new AircraftIndicator(app);

    slipBallIndicator = new SlipBallIndicator(app);
    turnRateIndicator = new TurnRateIndicator(app, app.screen.width / 2, app.screen.height - 19);
    headingIndicator = new HeadingIndicator(app, x);//- 260);

    brightness = new Brightness(app);

    // ------------------------------------------------------------------------
    // The following is for an EMS display - Not currently being implemented
    // ------------------------------------------------------------------------
    // //egtGraph = new TemperatureGraph(app2, 0, 200, "EGT", 1100, 1500, 0, 1400, 1400);
    // //chtGraph = new TemperatureGraph(app2, 240, 200, "CHT", 100, 500, 200, 400, 450);
    //-------------------------------------------------------------------------

    userInput = new UserInput(app);

    // Frame rate readout, bottom left of the HSI canvas (above the VIEW ONLY tag on a viewer), shown by the
    // Menu's "Frame Rate" row (kept in this browser's storage, so it survives a reload). Both canvases are drawn
    // in the same browser frame, so one count covers both: frames in the last second, and frames that took
    // more than 20 ms (a missed 60 Hz refresh) in the last 10 s. AA says which canvases are antialiased.
    const SLOW_FRAME_MS = 20;
    const fpsText = new Text({
        text: "",
        style: new TextStyle({fontFamily: 'Tahoma', fontSize: 16, fill: 0xFFFFFF})
    });
    fpsText.anchor.set(0, 1);
    fpsText.position.set(6, app2.screen.height - (VIEW_ONLY ? 24 : 4));
    fpsText.visible = storedFlag(FRAME_RATE_KEY, false);
    app2.stage.addChild(fpsText);
    const frameTimes = [];      // time of each frame in the last second
    const slowFrames = [];      // time of each slow frame in the last 10 s
    app.ticker.add(ticker => {
        if (!fpsText.visible) return;
        const now = performance.now();
        frameTimes.push(now);
        while (now - frameTimes[0] > 1000) frameTimes.shift();
        if (ticker.deltaMS > SLOW_FRAME_MS) slowFrames.push(now);
        while (slowFrames.length > 0 && now - slowFrames[0] > 10000) slowFrames.shift();
    });
    setInterval(() => {
        if (fpsText.visible) {
            fpsText.text = frameTimes.length + " fps  slow " + slowFrames.length + "/10s  AA " +
                ([PFD_ANTIALIAS ? "PFD" : "", HSI_ANTIALIAS ? "HSI" : ""].filter(Boolean).join("+") || "off");
        }
    }, 500);

    // --- Menu overlay with toggle items ---
    const toggleDefs = [
        {
            label: "Airspeed Sweep",
            getState: () => TEST_AIRSPEED_SWEEP,
            setState: (val) => { TEST_AIRSPEED_SWEEP = val; },
        },
        {
            label: "Encoder Debug",
            getState: () => userInput.itemSelectorText
                ? userInput.itemSelectorText.visible : false,
            setState: (val) => { userInput.setDebugVisible(val); },
        },
        {
            label: "Frame Rate",
            getState: () => fpsText.visible,
            setState: (val) => {
                fpsText.visible = val;
                frameTimes.length = 0;
                slowFrames.length = 0;
                storeFlag(FRAME_RATE_KEY, val);
            },
        },
        {
            label: "PFD Antialias",
            getState: () => PFD_ANTIALIAS,
            setState: (val) => {
                if (val !== PFD_ANTIALIAS) {
                    storeFlag(ANTIALIAS_KEY, val);
                    location.reload();          // antialiasing is chosen when the canvas is made
                }
            },
        },
        {
            label: "HSI Antialias",
            getState: () => HSI_ANTIALIAS,
            setState: (val) => {
                if (val !== HSI_ANTIALIAS) {
                    storeFlag(HSI_ANTIALIAS_KEY, val);
                    location.reload();          // antialiasing is chosen when the canvas is made
                }
            },
        },
    ];

    // Screen orientation (portrait / landscape): a row in the Menu opens the panels. Only the
    // controller (the Pi's own page) can change it, so viewers do not get the row.
    // The module status page is read only, so viewers get it too.
    modulePanel = new ModulePanel(app);
    const actionDefs = [{
        label: "Modules",
        getValue: () => {
            let notOk = 0;
            for (const name of ['imu', 'air', 'gps']) {
                const modules = dataObject.modules;
                if (describeModule(name, modules ? modules[name] : undefined).level !== 'ok') notOk++;
            }
            return notOk === 0 ? 'all OK' : notOk + ' not OK';
        },
        action: () => modulePanel.show(),
    }];
    if (!VIEW_ONLY) {
        orientationPanel = new OrientationPanel(app, sendCommand);
        actionDefs.push({
            label: "Orientation",
            getValue: () => {
                const o = dataObject.display && dataObject.display.orientation;
                return o ? o.replace('_', ' ') : '...';
            },
            action: () => orientationPanel.show(),
        });
    }

    menuOverlay = new MenuOverlay(app, toggleDefs, actionDefs);

    const menuCallback = {
        addContainer() {
            menuOverlay.toggle();
        }
    };

    // Menu button — lower right corner, between ribbon bottom and canvas edge
    let btnMargin = 1;
    let btnHeight = 40 - 2 * btnMargin;  // 38px
    let btnWidth = 90;
    softButtons = new SoftButtons(app, menuCallback, {
        x: x - btnWidth - btnMargin,
        y: y - 40 + btnMargin,
        width: btnWidth,
        height: btnHeight
    });

    // Create debug text for turn rate (commented out - uncomment to debug)
    // let turnRateTextStyle = new TextStyle({
    //     fontFamily: 'Tahoma',
    //     fontSize: '20px',
    //     fill: "white",
    //     fontWeight: "normal"
    // });
    // turnRateDebugText = new Text({
    //     text: "TR: 0.0",
    //     style: turnRateTextStyle
    // });
    // turnRateDebugText.position.set(10, app.screen.height - 60);
    // app.stage.addChild(turnRateDebugText);

    // ------------------------------------------------------------------------
    // Failure flags (red X), covers for failed tapes and the annunciator strip.
    // They are created after the instruments so they are drawn over them.
    // The area of each tape is the area of its ribbon (see the ribbons above).
    // ------------------------------------------------------------------------
    const tapeTop = 70;                 // the ribbons start 70 pixels from the top ...
    const tapeHeight = y - 140;         // ... and are y - 140 pixels tall
    asiCover = createCover(app, 0, tapeTop, 90, tapeHeight);
    altCover = createCover(app, x - 125, tapeTop, 90, tapeHeight);
    vsiCover = createCover(app, x - 35, 40, 35, y - 80);

    asiFlag = new FailureFlag(app, 0, tapeTop, 90, tapeHeight, 'ASI');
    altFlag = new FailureFlag(app, x - 125, tapeTop, 90, tapeHeight, 'ALT');
    vsiFlag = new FailureFlag(app, x - 35, 40, 35, y - 80, '');
    attFlag = new FailureFlag(app, 90, tapeTop, x - 90 - 125, tapeHeight, 'ATT');
    hdgFlag = new FailureFlag(app, x / 2 - 60, 0, 120, 40, 'HDG');
    turnFlag = new FailureFlag(app, x / 2 - 108, y - 19 - 8, 216, 16, '');

    // The annunciators stack upward from just above the slip/skid ball (ball centre is 50 pixels
    // from the bottom, ball and tube about 22 pixels tall)
    // The widest line may use the space between the two tapes (90 pixels on the left, 125 on the right).
    annunciators = new Annunciators(app, y - 50 - 22, x - 90 - 125);

    userInput.registerCallback(qnhDisplay);
    userInput.registerCallback(tempTimeDisplay);
    userInput.registerCallback(brightness);
    userInput.registerCallback(speedDisplay);
    userInput.registerCallback(headingIndicator);
    userInput.registerCallback(altitudeDisplay);
    userInput.registerCallback(altimeter_ribbon);
    userInput.registerCallback(hsi);

    app.ticker.add(delta => DisplayUpdateLoop(delta));
}

// ----------------------------------------------------------------------------
// --- Update called by ticker to update the display                        ---
// ----------------------------------------------------------------------------


function DisplayUpdateLoop(delta) {

    // Work out which instruments can be trusted from the server's facts. A value that cannot be
    // trusted never reaches its instrument: the instrument is hidden or blanked and a red X is
    // drawn over it (see instrumentStatus.mjs).
    instrumentStatus.update(dataObject, Date.now());
    const valid = instrumentStatus.valid;
    updateFailureDisplay(instrumentStatus);

    // Data arrives in NED frame from Arduino, no corrections needed here
    if (valid.attitude) {
        attitudeIndicator.pitch = dataObject.pitch;
        attitudeIndicator.roll = dataObject.roll;
    }
    if (valid.acceleration) {
        attitudeIndicator.accY = dataObject.accy;
        attitudeIndicator.accZ = dataObject.accz;
    }
    // attitudeIndicator.updateSlipSkid();
    if (valid.altitude) {
        altitudeWheel.value = dataObject.altitude;
        // //qnhDisplay.value = globalThis.qnh; // dataObject.qnh; // Don't update from json
        altimeter_ribbon.value = dataObject.altitude;
    }
    // //vsiDisplay.value = dataObject.vsi;
    // //testAirspeedDisplay.value = dataObject.airspeed;
    if (TEST_AIRSPEED_SWEEP) {
        // TEST: Sweep airspeed 0→200→0 to verify color bands
        testAirspeed += testAirspeedDir * 0.3;
        if (testAirspeed >= 200) testAirspeedDir = -1;
        if (testAirspeed <= 0) testAirspeedDir = 1;
        airspeedWheel.value = testAirspeed;
        airspeedRibbon.value = testAirspeed;
    } else if (valid.airspeed) {
        airspeedWheel.value = dataObject.airspeed;
        airspeedRibbon.value = dataObject.airspeed;
    }
    if (valid.vsi) {
        vsiIndicator.value = dataObject.vsi;
    }
    // Temporary test - uncomment to test VSI indicator
    // vsiIndicator.value = 500; // Test with 500 fpm climb

    if (valid.acceleration) {
        slipBallIndicator.accZ = dataObject.accz;
        slipBallIndicator.accY = dataObject.accy;
        slipBallIndicator.update();
    }

    if (valid.heading) {
        headingIndicator.value = dataObject.yaw;
        hsi.heading = dataObject.yaw;
    }
    // What the HSI shows from the GPS (ground speed and track, cross track, TO/FROM, the waypoint readout)
    // is only shown while the GPS position can be trusted; otherwise it is hidden or shows ---
    hsi.gpsValid = valid.gps;
    // The compass rose is magnetic, so the track diamond uses the GPS module's magnetic track
    // (true track corrected with its WMM2025 declination), not the true track (ISSUES.md #15)
    const trackWorthDrawing = valid.gps && dataObject.gps_speed !== undefined && dataObject.gps_speed !== null &&
                              dataObject.gps_speed >= TRACK_MIN_SPEED_KT;
    hsi.trackUsable = trackWorthDrawing;
    if (trackWorthDrawing && dataObject.mag_track !== undefined && dataObject.mag_track !== null) {
        hsi.groundTrack = dataObject.mag_track;
    }
    hsi.groundSpeed = (valid.gps && dataObject.gps_speed !== undefined) ? dataObject.gps_speed : null;

    // The GPS receiver's own status box (fix type, satellites, HDOP, VDOP) follows the module and its NMEA data
    if (valid.gpsStatus && dataObject.gps_fix_quality !== undefined) {
        hsi.gpsFixQuality = dataObject.gps_fix_quality;
        hsi.gpsFixType = dataObject.gps_fix_3d;
        hsi.gpsSatellites = dataObject.gps_satellites;
        hsi.gpsHdop = dataObject.gps_hdop;
        hsi.gpsVdop = dataObject.gps_vdop;
        hsi.gpsDeclination = dataObject.gps_declination;
    } else {
        hsi.gpsFixQuality = null;
        hsi.gpsFixType = null;
        hsi.gpsSatellites = null;
        hsi.gpsHdop = null;
        hsi.gpsVdop = null;
        hsi.gpsDeclination = null;
    }
    if (valid.gps && dataObject.xtrack !== undefined && dataObject.xtrack !== null) {
        hsi.courseDeviation = dataObject.xtrack;
    }
    hsi.toFrom = (valid.gps && dataObject.to_from !== undefined) ? dataObject.to_from : null;
    if (dataObject.dtk !== undefined && dataObject.dtk !== null) {
        hsi.desiredTrack = dataObject.dtk;
    } else {
        hsi.desiredTrack = null;
    }
    if (valid.gps && dataObject.dist !== undefined && dataObject.dist !== null) {
        hsi.waypointDistance = dataObject.dist;
    } else {
        hsi.waypointDistance = null;
    }
    if (valid.gps && dataObject.wpt_id !== undefined && dataObject.wpt_id !== null) {
        hsi.waypointId = dataObject.wpt_id;
    } else {
        hsi.waypointId = null;
    }
    if (dataObject.route_waypoints !== undefined) {
        routeOverlay.update(dataObject.route_waypoints, dataObject.active_leg, dataObject.direct_to_active);
        // Show notification dot when route loads/changes or active leg changes
        const routeLen = dataObject.route_waypoints ? dataObject.route_waypoints.length : 0;
        if (routeLen !== _lastRouteLen || dataObject.active_leg !== _lastActiveLeg) {
            if (routeLen > 0) routeBtn.showNotification();
            _lastRouteLen = routeLen;
            _lastActiveLeg = dataObject.active_leg;
        }
    }

    // Update turn rate using spring-mass-damper model (simulates gyro inertia)
    if (!valid.turnRate) {
        // start again from zero when the turn rate comes back
        smoothedTurnRate = 0.0;
        turnRateVelocity = 0.0;
    }
    else if (dataObject.turn_rate !== undefined && dataObject.turn_rate !== null) {
        // dt from PixiJS ticker (seconds)
        var dt = delta.deltaMS / 1000.0;

        // Spring-mass-damper: F = -spring*(position - target) - damping*velocity
        var acceleration = -turnRateSpring * (smoothedTurnRate - dataObject.turn_rate)
                           - turnRateDamping * turnRateVelocity;
        turnRateVelocity += acceleration * dt;
        smoothedTurnRate += turnRateVelocity * dt;

        // Update turn rate indicator bar
        turnRateIndicator.update(smoothedTurnRate);

        // Show both raw and filtered values for comparison (commented out - uncomment to debug)
        // turnRateDebugText.text = "TR Raw: " + dataObject.turn_rate.toFixed(2) +
        //                          " | Gyro: " + smoothedTurnRate.toFixed(2);
    }

    if (dataObject.gps_speed != null) {
        speedDisplay.groundSpeed = dataObject.gps_speed;
    }
    if (dataObject.static_pressure != null &&
        dataObject.differential_pressure != null &&
        dataObject.oat != null) {
        speedDisplay.staticPressure = dataObject.static_pressure;
        speedDisplay.differentialPressure = dataObject.differential_pressure;
        speedDisplay.indicatedTemperature = dataObject.oat;
    }
    speedDisplay.validity.gs = valid.gps;
    speedDisplay.validity.tas = valid.trueAirspeed;
    speedDisplay.update();

    if (dataObject.oat != null) {
        tempTimeDisplay.temperature = dataObject.oat;
    }
    if (dataObject.tm_hour != null && dataObject.tm_min != null) {
        tempTimeDisplay.timeHour = dataObject.tm_hour;
        tempTimeDisplay.timeMinute = dataObject.tm_min;
    }
    tempTimeDisplay.validity.temperature = valid.oat;
    tempTimeDisplay.update();

    altitudeDisplay.gpsAltitude = dataObject.gps_altitude;
    if (dataObject.static_pressure != null) {
        altitudeDisplay.staticPressure = dataObject.static_pressure;
    }
    if (dataObject.oat != null) {
        altitudeDisplay.temperature = dataObject.oat;
    }
    altitudeDisplay.validity.gps = valid.gps;
    altitudeDisplay.validity.pressure = valid.staticPressure;
    altitudeDisplay.validity.density = valid.densityAltitude;
    altitudeDisplay.update();

    //magnetometerCalibrate.plotPoint(dataObject.magx,dataObject.magy,dataObject.magz);
    
    // The server's orientation facts drive the choose / confirm / Keep panels (controller only)
    if (orientationPanel) orientationPanel.update(dataObject.display);

    // The module status page (when it is open)
    modulePanel.update(dataObject.modules, dataObject.efis);

    // Controller, first pass after a (re)load: adopt the server's remembered values
    if (!VIEW_ONLY && !restoredFromServer) {
        if (!gotFirstMessage) return;       // nothing to send until we know them
        const cs = dataObject.controller_state;
        if (cs && typeof cs.qnh === 'number') {
            qnhDisplay.value = cs.qnh;
            last_qnh = qnhDisplay.value;
        }
        if (cs && typeof cs.brightness === 'number') {
            brightness.value = cs.brightness;
            last_brightness = brightness.value;
        }
        restoredFromServer = true;
    }

    // Not set / baro mismatch look of the QNH (from the server's qnh_set and qnh_mismatch)
    qnhDisplay.setStatus(dataObject.qnh_set === false, dataObject.qnh_mismatch === true, Date.now() / 1000);

    // Viewer: show the Pi's QNH and brightness; do not run the encoder or send anything
    if (VIEW_ONLY) {
        const cs = dataObject.controller_state;
        if (cs && typeof cs.qnh === 'number') qnhDisplay.value = cs.qnh;
        if (cs && typeof cs.brightness === 'number') brightness.value = cs.brightness;
        return;
    }

    // // Process any change in the user input encoder
    userInput.processState(dataObject.position, dataObject.pressed)

    current_time_millis = Date.now();

    // Follow the QNH the server holds (set from the bus or by another device), unless the person is turning the dial.
    // Set last_qnh when adopting so an adopted value is not sent back as if the person had changed it.
    const sharedQnh = dataObject.controller_state ? dataObject.controller_state.qnh : undefined;
    const qnhChanged = qnhDisplay.value != last_qnh;
    if (qnhChanged) last_qnh_user_change = current_time_millis;
    if (!qnhChanged && typeof sharedQnh === 'number' && sharedQnh != qnhDisplay.value &&
        current_time_millis > last_qnh_user_change + QNH_USER_HOLD) {
        qnhDisplay.value = sharedQnh;
        last_qnh = qnhDisplay.value;
    }

    // Send to python using the websocket and json. The QNH goes only when the person changes it; the rest
    // (brightness, ticker) also goes about once a second.
    if (qnhChanged ||
        current_time_millis > send_timestamp + SEND_PERIOD ||
        brightness.value != last_brightness) {

        last_qnh = qnhDisplay.value;
        last_brightness = brightness.value;
        send_timestamp = current_time_millis;

        // Extract values safely to avoid circular references
        // qnhValue is in inHg × 100 format (e.g., 2992 = 29.92 inHg)
        var qnhValue = 2992; // default value (29.92 inHg × 100)
        var tickerValue = 0;   // default value
        var brightnessValue = 100; // default value
        
        try {
            // Safely extract qnh value (qnhDisplay.value returns inHg × 100 format)
            if (qnhDisplay && typeof qnhDisplay.value === 'number') {
                qnhValue = qnhDisplay.value;
            }
            
            // Safely extract ticker value
            if (typeof delta === 'number') {
                tickerValue = delta;
            }
            
            // Safely extract brightness value
            if (brightness && typeof brightness.value === 'number') {
                brightnessValue = brightness.value;
            }
        } catch (e) {
            console.warn("Error extracting values:", e);
        }
        
        // Create a clean object with only primitive values
        // Note: qnh is in inHg × 100 format (e.g., 2992 = 29.92 inHg)
        var obj = {
            ticker: tickerValue,
            brightness: brightnessValue
        };
        if (qnhChanged) obj.qnh = qnhValue;

        try {
            var json = JSON.stringify(obj);
        } catch (error) {
            console.error("JSON.stringify error:", error);
            console.log("Object being serialized:", obj);
            // Fallback to a simple object if serialization fails
            // Note: qnh is in inHg × 100 format (2992 = 29.92 inHg)
            var json = JSON.stringify({ticker: 0, brightness: 100});
        }
        if (myWebSocket.readyState == 1) {
            myWebSocket.send("json" + json);}
    }

}

// ----------------------------------------------------------------------------
// --- Failure display                                                       ---
// ----------------------------------------------------------------------------

/**
 * Create a hidden rectangle that blanks a failed tape (airspeed, altitude)
 */
function createCover(app, x, y, width, height) {
    const cover = new Graphics();
    cover.rect(x, y, width, height);
    cover.fill({ color: FAILURE_LOOK.coverColour });
    cover.visible = false;
    app.stage.addChild(cover);
    return cover;
}

/**
 * Show the failure flags, blank the failed instruments and draw the annunciators
 * @param {InstrumentStatus} status what can be trusted and what to announce
 */
function updateFailureDisplay(status) {
    const valid = status.valid;

    attitudeIndicator.failed = !valid.attitude;
    attFlag.shown = !valid.attitude;

    asiCover.visible = !valid.airspeed;
    airspeedWheel.visible = valid.airspeed;
    asiFlag.shown = !valid.airspeed;
    altCover.visible = !valid.altitude;
    altitudeWheel.visible = valid.altitude;
    altFlag.shown = !valid.altitude;

    vsiIndicator.container.visible = valid.vsi;
    vsiCover.visible = !valid.vsi;
    vsiFlag.shown = !valid.vsi;

    turnRateIndicator.container.visible = valid.turnRate;
    turnFlag.shown = !valid.turnRate;

    slipBallIndicator.container.visible = valid.acceleration;

    // heading: the gyro-only heading stays visible but dimmed under its X, then is blanked
    const heading = status.heading;
    headingIndicator.headingContainer.visible = heading.show;
    headingIndicator.headingContainer.alpha = heading.dim ? FAILURE_LOOK.headingDimAlpha : 1.0;
    hdgFlag.shown = heading.flag;
    hsi.setHeadingDisplay(heading.show, heading.dim ? FAILURE_LOOK.headingDimAlpha : 1.0);
    hsiFlag.shown = heading.flag;

    annunciators.update(status.annunciations, Date.now());
}

// ----------------------------------------------------------------------------
// --- Aircraft Indicator                                        ---
// ----------------------------------------------------------------------------

function AircraftIndicator(app){

    let displayWidth = app.screen.width;
    let displayHeight = app.screen.height;

    let px4 = displayWidth/40;
    let px2 = displayWidth/25;  // offset to inside of large mark
    let px3 = displayWidth/30;

    // The wings run from the gap at the centre (1.5 * px4) out to px1. The wing used to end at
    // displayWidth/7; the wings are now shortened by WING_SHORTEN (a trial: 0.25 first, then 0.33,
    // about 5 pixels shorter again on the Pi's portrait canvas).
    const WING_SHORTEN = 0.33;
    const wingInner = 1.5 * px4;
    let px1 = wingInner + (displayWidth/7 - wingInner) * (1 - WING_SHORTEN); // outside of large mark

    let py1 = displayHeight/8;  // Not Used
    let py2 = displayHeight/40; // height of verticals

    let aircraftGraphics = new Graphics();

    aircraftGraphics.strokeStyle = {
        alignment: 0.5,
        alpha: 1,
        cap: "round",
        join: "round",
        color: 0x000000,
        width: 8,
    }

    // 90 on left
    aircraftGraphics.moveTo(-px1 , 0);
    aircraftGraphics.lineTo(-1.5 * px4, 0);
    aircraftGraphics.lineTo(-1.5 * px4, py2);

    // 90 on right
    aircraftGraphics.moveTo(px1,0);//2 * px4 , 0);
    aircraftGraphics.lineTo(1.5 * px4, 0);
    aircraftGraphics.lineTo(1.5 * px4, py2);

    aircraftGraphics.stroke();

    // draw yellow foreground
    aircraftGraphics.strokeStyle = {
        alignment: 0.5,
        alpha: 1,
        cap: "round",
        join: "round",
        color: 0xFFFF00,
        width: 4,
    }

    aircraftGraphics.moveTo(-px1 , 0);
    aircraftGraphics.lineTo(-1.5 * px4, 0);
    aircraftGraphics.lineTo(-1.5 * px4, py2);

    aircraftGraphics.moveTo(px1, 0);
    aircraftGraphics.lineTo(1.5 * px4, 0);
    aircraftGraphics.lineTo(1.5 * px4, py2);

    aircraftGraphics.stroke();

    // draw yellow Dot
    aircraftGraphics.strokeStyle = {
        alignment: 0.0,
        alpha: 1,
        cap: "round",
        join: "round",
        color: 0x000000,
        width: 2,
    }
    aircraftGraphics.fillStyle = {
        alpha: 1,
        color: 0xFFFF00,
    }

    aircraftGraphics.circle(0,0,3);

    aircraftGraphics.stroke();
    aircraftGraphics.fill();

    aircraftGraphics.x = displayWidth/2;
    aircraftGraphics.y = displayHeight/2;

    app.stage.addChild(aircraftGraphics);

}
