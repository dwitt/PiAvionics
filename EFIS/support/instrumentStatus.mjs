'use strict';

export const VERSION = '1.2.0';

/*****************************************************************************
 * The server tracks the facts (module heartbeats, which values can be trusted,
 * which messages stopped); this class decides what they mean for the display:
 * which instruments get a red X, how the heading is shown, and which
 * annunciations to show. It does not draw anything.
 * See EFIS/docs/MODULE_STATUS_PLAN.md (rules table) for the reasoning.
 ****************************************************************************/

// ----------------------------------------------------------------------------
// --- Settings                                                             ---
// ----------------------------------------------------------------------------
const GYRO_HEADING_BLANK_SECONDS = 300;     // heading number blanked after this long without the magnetometer
const RESTART_CAUTION_SECONDS = 8;          // MODULE RESTARTED stays up this long after an uptime reset
const LINK_TIMEOUT_MS = 1000;               // no message from the server for this long = no data link

export const WARNING = 'warning';           // red
export const CAUTION = 'caution';           // amber

// The instruments the display can mark as failed. The names are used by efis-display.js.
const INSTRUMENTS = [
    'airspeed', 'altitude', 'vsi', 'oat', 'attitude', 'heading', 'turnRate',
    'acceleration', 'gps', 'gpsStatus', 'staticPressure', 'trueAirspeed', 'densityAltitude'
];

/**
 * Class that works out, from the server's facts, what can be trusted and what to announce.
 */
export class InstrumentStatus {

    constructor() {
        this._lastMessageTime = null;           // time (ms) the last message arrived from the server

        // true when the instrument can be trusted (everything is untrusted until data arrives)
        this.valid = {};
        for (const name of INSTRUMENTS) {
            this.valid[name] = false;
        }

        // heading: show = draw the number, dim = draw it dimmed (gyro only), flag = red X over it
        this.heading = { show: false, dim: false, flag: true };

        // list of {text, level} with warnings first, then cautions
        this.annunciations = [];

        // false when the data link to the server is down (or no data has arrived yet)
        this.linkOk = false;
    }

    /**
     * Call each time a message arrives from the server.
     * @param {number} now The current time in milliseconds
     */
    messageReceived(now) {
        this._lastMessageTime = now;
    }

    /**
     * Work out the status from the latest data object from the server.
     * @param {object} data The data object (modules, valid, data_lost, heading_status ...)
     * @param {number} now The current time in milliseconds
     */
    update(data, now) {
        this.linkOk = this._lastMessageTime !== null &&
                      (now - this._lastMessageTime) <= LINK_TIMEOUT_MS;

        if (!this.linkOk || !data || !data.valid) {
            this._setAllInvalid(this.linkOk ? [] : [{ text: 'NO DATA', level: WARNING }]);
            return;
        }

        const v = data.valid;
        const lost = data.data_lost || {};
        const modules = data.modules || {};
        const air = modules.air || {};
        const imu = modules.imu || {};
        const gps = modules.gps || {};

        // ---- which instruments can be trusted
        this.valid.airspeed = v.airspeed === true;
        this.valid.altitude = v.altitude === true;
        this.valid.vsi = v.vsi === true;
        this.valid.oat = v.oat === true;
        this.valid.attitude = v.attitude === true;
        this.valid.turnRate = v.turn_rate === true;
        this.valid.acceleration = v.acceleration === true;
        this.valid.gps = v.gps_position === true;
        this.valid.staticPressure = v.static_pressure === true;
        // values the display works out from several sources
        this.valid.trueAirspeed = v.airspeed === true && v.static_pressure === true &&
                                  v.differential_pressure === true && v.oat === true;
        this.valid.densityAltitude = v.static_pressure === true && v.oat === true;

        // ---- heading: with the magnetometer lost the number is dimmed under the X, then blanked
        const hs = data.heading_status || {};
        this.valid.heading = v.heading === true;
        const gyroOnly = this.valid.heading && hs.suspect === true;
        const blanked = gyroOnly && hs.gyro_seconds !== null && hs.gyro_seconds !== undefined &&
                        hs.gyro_seconds >= GYRO_HEADING_BLANK_SECONDS;
        this.heading.show = this.valid.heading && !blanked;
        this.heading.dim = gyroOnly && !blanked;
        this.heading.flag = !this.valid.heading || gyroOnly;

        // ---- annunciations
        const warnings = [];
        const cautions = [];
        const airSensors = air.sensors || {};
        const imuSensors = imu.sensors || {};
        const gpsSensors = gps.sensors || {};

        // The GPS receiver's own status (fix type, satellites, HDOP, VDOP) is good while the module is
        // present and has NMEA data, even with no fix: it then says "No Fix" and 0 satellites.
        // (The server's data_lost.gps_status is not used: the module only sends it between baud probes.)
        this.valid.gpsStatus = gps.present === true && gps.missing !== true && gpsSensors.nmea !== false;

        if (air.missing === true || lost.air_data === true) {
            warnings.push('NO AIR DATA');
        } else {
            if (airSensors.pitot === false) warnings.push('PITOT');
            if (airSensors.static === false) warnings.push('STATIC');
            if (airSensors.oat === false || lost.oat === true) cautions.push('OAT');
            if (airSensors.aoa === false) cautions.push('AOA');
        }

        if (imu.missing === true || lost.attitude === true) {
            warnings.push('NO IMU');
        } else {
            if (imuSensors.gyro_accel === false) warnings.push('ATT FAIL');
            else if (hs.suspect === true) cautions.push('HDG GYRO');
            if (imuSensors.calibration === false) cautions.push('IMU NO CAL');
        }
        // (with the IMU missing there is no heading information; the NO IMU warning covers it)

        // A GPS without a fix is information, not an annunciation: the readouts show ---.
        // The GPS status message is not used here because the module only sends it between baud probes.
        if (gps.missing === true || gpsSensors.nmea === false) cautions.push('GPS NO DATA');

        for (const name of Object.keys(modules)) {
            const age = modules[name].restarted_age;
            if (age !== null && age !== undefined && age < RESTART_CAUTION_SECONDS) {
                cautions.push('MODULE RESTARTED');
                break;
            }
        }

        this.annunciations = [];
        for (const text of warnings) this.annunciations.push({ text: text, level: WARNING });
        for (const text of cautions) this.annunciations.push({ text: text, level: CAUTION });
    }

    // ------------------------------------------------------------------------
    // --- Helpers                                                          ---
    // ------------------------------------------------------------------------
    _setAllInvalid(annunciations) {
        for (const name of INSTRUMENTS) {
            this.valid[name] = false;
        }
        this.heading.show = false;
        this.heading.dim = false;
        this.heading.flag = true;
        this.annunciations = annunciations;
    }
}

// ----------------------------------------------------------------------------
// --- Text for the module status page                                      ---
// ----------------------------------------------------------------------------
const STATE_NAMES = { 0: 'STARTING', 1: 'OK', 2: 'DEGRADED', 3: 'FAILED' };

// The sensors each module reports, in the order they are listed, with the label shown for each
const MODULE_SENSORS = {
    imu: [['gyro_accel', 'GYRO/ACCEL'], ['magnetometer', 'MAG'], ['calibration', 'CAL']],
    air: [['pitot', 'PITOT'], ['aoa', 'AOA'], ['static', 'STATIC'], ['oat', 'OAT']],
    gps: [['nmea', 'NMEA'], ['fix', 'FIX']],
};

/**
 * Format a number of seconds as h:mm:ss
 */
export function formatDuration(seconds) {
    const total = Math.max(0, Math.floor(seconds));
    const h = Math.floor(total / 3600);
    const m = Math.floor((total % 3600) / 60);
    const s = total % 60;
    return h + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
}

/**
 * Format an age in seconds the way the status page shows it (0.4 s, 12 s, 5 min)
 */
export function formatAge(seconds) {
    if (seconds < 10) return seconds.toFixed(1) + ' s';
    if (seconds < 100) return Math.round(seconds) + ' s';
    return Math.round(seconds / 60) + ' min';
}

/**
 * Work out what to show about one module on the module status page.
 * @param {string} name 'imu', 'air' or 'gps'
 * @param {object} info that module's entry in the server's `modules` facts (may be undefined)
 * @returns {object} {state, level, version, uptime, heard, restarted, sensors}
 *     level is 'ok', CAUTION, WARNING or 'unknown'; sensors is a list of {key, label, ok}
 *     where ok is true, false, or null when the module has not said
 */
export function describeModule(name, info) {
    info = info || {};
    let state, level;
    if (info.missing === true) {
        state = 'MISSING';
        level = WARNING;
    } else if (info.present !== true) {
        state = 'WAITING';          // not heard yet, still within the start-up grace period
        level = 'unknown';
    } else if (info.state === 1) {
        state = 'OK';
        level = 'ok';
    } else if (info.state === 2) {
        state = STATE_NAMES[2];
        level = CAUTION;
    } else if (info.state === 3) {
        state = STATE_NAMES[3];
        level = WARNING;
    } else if (info.state === 0) {
        state = STATE_NAMES[0];
        level = 'unknown';
    } else {
        state = 'UNKNOWN';
        level = 'unknown';
    }

    let heard = 'never heard';
    if (info.age !== null && info.age !== undefined) {
        heard = (info.missing === true ? 'last heard ' : 'heard ') + formatAge(info.age) + ' ago';
    }

    const sensors = (MODULE_SENSORS[name] || []).map(([key, label]) => {
        const known = info.sensors !== null && info.sensors !== undefined;
        return { key: key, label: label, ok: known ? info.sensors[key] === true : null };
    });

    return {
        state: state,
        level: level,
        version: info.version ? 'v' + info.version : '--',
        uptime: (info.present === true && info.uptime !== null && info.uptime !== undefined)
                ? 'up ' + formatDuration(info.uptime) : 'up --',
        heard: heard,
        restarted: (info.restarted_age !== null && info.restarted_age !== undefined)
                   ? 'restarted ' + formatAge(info.restarted_age) + ' ago' : '',
        sensors: sensors,
    };
}
