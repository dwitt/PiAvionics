'use strict';

export const VERSION = '1.1.0';

/*****************************************************************************
 * How the failure flags (red X) and the annunciators look. Everything that
 * might need tuning on the real display is here. Sizes are in CSS pixels on
 * the primary canvas (the Pi's screen is scaled 1.25, so 1 here is 1.25
 * physical pixels).
 *
 * For quick tries without deploying, a page can override a value from the
 * address, for example  ?pi=true&xw=8&xo=70&fs=22  (see the names below).
 ****************************************************************************/

export const FAILURE_LOOK = {
    // ---- red X over a failed instrument
    xWidth: 6,                  // stroke width of the X                     (?xw=)
    xAlpha: 0.8,                // opacity of the X, 0 to 1                  (?xo= in percent)
    xColour: 0xFF2A1F,
    labelShow: true,            // small name box in the middle of the X     (?labels=0 hides)
    labelFontSize: 16,
    labelColour: 0xFF4A3F,
    labelBackAlpha: 0.75,

    // ---- instruments under a failure
    coverColour: 0x1A1C20,      // blanks a failed tape (airspeed, altitude)
    headingDimAlpha: 0.38,      // gyro-only heading: dimmed but readable under its X

    // ---- annunciators
    annunciatorFontSize: 19,                // text size                      (?fs=)
    annunciatorMinFontSize: 13,             // on a narrow canvas the text shrinks to fit, but not below this
    annunciatorMaxLines: 4,                 // more than this are not shown (warnings come first)
    annunciatorBackAlpha: 0.78,             // dark pill behind each line
    annunciatorPadding: 10,                 // space left and right of the text inside the pill
    annunciatorLineGap: 6,                  // extra height per line beyond the font size
    warningColour: 0xFF3B30,                // red
    cautionColour: 0xFFB000,                // amber
    flashSeconds: 5,                        // a new entry flashes this long, then stays steady
    flashChangesPerSecond: 3,               // the flashing line switches on/off this many times a second
};

// ----------------------------------------------------------------------------
// --- Optional overrides from the page address (for trying values quickly) ---
// ----------------------------------------------------------------------------
const params = new URLSearchParams(location.search);

function overrideNumber(name, apply) {
    const text = params.get(name);
    if (text === null) return;
    const value = parseFloat(text);
    if (!isNaN(value)) apply(value);
}

overrideNumber('xw', (v) => { FAILURE_LOOK.xWidth = v; });
overrideNumber('xo', (v) => { FAILURE_LOOK.xAlpha = v / 100; });
overrideNumber('fs', (v) => { FAILURE_LOOK.annunciatorFontSize = v; });
if (params.get('labels') === '0') {
    FAILURE_LOOK.labelShow = false;
}
