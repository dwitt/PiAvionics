'use strict';

export const VERSION = '1.5.0';

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
// ALIGNING (G5 1.3, 1.4.2): while the IMU starts (heartbeat STARTING, or its uptime this short), and this long after
// its attitude first arrives, so the filter can settle and the IMU's first frame (sent before its filter has run:
// level, heading north) is never shown. 3 s chosen 2026-11-15 from a bench restart; review after a flight.
const ALIGN_UPTIME_SECONDS = 5;
const ALIGN_SETTLE_MS = 3000;
const ALIGN_GIVE_UP_MS = 15000;             // a start with no attitude for this long is a failure, not aligning

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

        // true while the IMU is starting and settling: attitude, heading, turn rate and acceleration are not
        // shown, without red X or NO IMU; the page shows ALIGNING over the attitude instead
        this.aligning = false;
        this._alignStart = null;            // time (ms) an IMU start was seen, null when not aligning
        this._alignSettleEnd = null;        // time (ms) the settle ends, once the attitude has arrived
        this._alignGaveUp = false;          // gave up on this start: no ALIGNING again until the IMU stops starting
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

        // ---- which instruments can be trusted. A value that is null (the server sends null for a value it
        // cannot trust, and for a field with an invalid-data marker) is never valid, even in the moment before
        // the validity flag catches up: an instrument must never be handed a null.
        const has = (...names) => names.every((name) => data[name] !== null);
        this.valid.airspeed = v.airspeed === true && has('airspeed');
        this.valid.altitude = v.altitude === true && has('altitude');
        this.valid.vsi = v.vsi === true && has('vsi');
        this.valid.oat = v.oat === true && has('oat');
        this.valid.attitude = v.attitude === true && has('pitch', 'roll');
        this.valid.turnRate = v.turn_rate === true && has('turn_rate');
        this.valid.acceleration = v.acceleration === true && has('accx', 'accy', 'accz');
        this.valid.gps = v.gps_position === true;
        this.valid.staticPressure = v.static_pressure === true && has('static_pressure');
        // values the display works out from several sources
        this.valid.trueAirspeed = this.valid.airspeed && this.valid.staticPressure &&
                                  v.differential_pressure === true && has('differential_pressure') && this.valid.oat;
        this.valid.densityAltitude = this.valid.staticPressure && this.valid.oat;

        // ---- heading: with the magnetometer lost the number is dimmed under the X, then blanked
        const hs = data.heading_status || {};
        this.valid.heading = v.heading === true && has('yaw');
        const gyroOnly = this.valid.heading && hs.suspect === true;
        const blanked = gyroOnly && hs.gyro_seconds !== null && hs.gyro_seconds !== undefined &&
                        hs.gyro_seconds >= GYRO_HEADING_BLANK_SECONDS;
        this.heading.show = this.valid.heading && !blanked;
        this.heading.dim = gyroOnly && !blanked;
        this.heading.flag = !this.valid.heading || gyroOnly;

        // ---- ALIGNING: an IMU that is starting (or just started) is not a failure
        this._updateAligning(imu, now);
        if (this.aligning) {
            this.valid.attitude = false;
            this.valid.turnRate = false;
            this.valid.acceleration = false;
            this.valid.heading = false;
            this.heading.show = false;
            this.heading.dim = false;
            this.heading.flag = false;
        }

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

        if (this.aligning) {
            // nothing to announce: ALIGNING is shown over the attitude
        } else if (imu.missing === true || lost.attitude === true) {
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
    /**
     * Work out this.aligning. A start is seen when the IMU is present and says STARTING or has only just started
     * (uptime); aligning then lasts until ALIGN_SETTLE_MS after its attitude arrives (or ALIGN_GIVE_UP_MS without
     * attitude, when the usual failure flags take over).
     */
    _updateAligning(imu, now) {
        const present = imu.present === true && imu.missing !== true;
        const starting = present && (imu.state === 0 ||
                         (typeof imu.uptime === 'number' && imu.uptime <= ALIGN_UPTIME_SECONDS));
        if (!starting) this._alignGaveUp = false;
        if (!present) {
            this._alignStart = null;
            this._alignSettleEnd = null;
        } else if (starting && !this._alignGaveUp) {
            if (this._alignStart === null) this._alignStart = now;
            if (imu.state === 0) this._alignSettleEnd = null;       // still starting: the settle has not begun
        }
        if (this._alignStart !== null && this._alignSettleEnd === null && imu.state !== 0 && this.valid.attitude) {
            this._alignSettleEnd = now + ALIGN_SETTLE_MS;            // the attitude has arrived: settle
        }
        if (this._alignStart !== null) {
            const settled = this._alignSettleEnd !== null && now >= this._alignSettleEnd;
            const gaveUp = this._alignSettleEnd === null && now - this._alignStart >= ALIGN_GIVE_UP_MS;
            if ((settled && !starting) || gaveUp) {
                if (gaveUp) this._alignGaveUp = true;
                this._alignStart = null;
                this._alignSettleEnd = null;
            }
        }
        this.aligning = this._alignStart !== null;
    }

    _setAllInvalid(annunciations) {
        for (const name of INSTRUMENTS) {
            this.valid[name] = false;
        }
        this.heading.show = false;
        this.heading.dim = false;
        this.heading.flag = true;
        this.aligning = false;
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
    backup: [['lcd', 'LCD'], ['encoder', 'ENCODER']],
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
 * Work out what to show about the EFIS itself (the server's own heartbeat) on the module status page.
 * @param {object} efis dataObject.efis: {version, state, mask, uptime, display_clients} (may be undefined)
 * @returns {object} the same shape as describeModule()
 */
export function describeEfis(efis) {
    const known = efis !== null && efis !== undefined;
    let state = 'UNKNOWN';
    let level = 'unknown';
    if (known && efis.state === 1) {
        state = 'OK';
        level = 'ok';
    } else if (known && efis.state === 2) {
        state = 'DEGRADED';
        level = CAUTION;
    } else if (known && efis.state === 3) {
        state = 'NO DISPLAY';
        level = CAUTION;
    }
    const mask = known ? efis.mask : null;
    const bit = (b) => (mask === null ? null : (mask & b) !== 0);
    return {
        state: state,
        level: level,
        version: known && efis.version ? 'v' + efis.version : '--',
        uptime: known && efis.uptime !== null && efis.uptime !== undefined
                ? 'up ' + formatDuration(efis.uptime) : 'up --',
        heard: known ? 'this server' : 'no data',
        restarted: '',
        // what the EFIS itself sees: the three modules healthy, and a display page connected
        sensors: [
            { key: 'air', label: 'AIR', ok: bit(0x01) },
            { key: 'imu', label: 'IMU', ok: bit(0x02) },
            { key: 'gps', label: 'GPS', ok: bit(0x04) },
            { key: 'display', label: 'DISPLAY', ok: bit(0x08) },
        ],
    };
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
