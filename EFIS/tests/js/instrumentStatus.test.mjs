'use strict';
// Tests for support/instrumentStatus.mjs (no browser needed). From the EFIS directory:
//     node --test tests/js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { InstrumentStatus, WARNING, CAUTION, describeModule, describeEfis, formatDuration, formatAge }
    from '../../support/instrumentStatus.mjs';

// What the server sends when everything is healthy (the shape of module_status.py's facts)
function goodFacts() {
    return {
        modules: {
            imu: { present: true, missing: false, state: 1, restarted_age: null,
                   sensors: { gyro_accel: true, magnetometer: true, calibration: true } },
            air: { present: true, missing: false, state: 1, restarted_age: null,
                   sensors: { pitot: true, aoa: true, static: true, oat: true } },
            gps: { present: true, missing: false, state: 1, restarted_age: null,
                   sensors: { nmea: true, fix: true } },
        },
        valid: { airspeed: true, altitude: true, vsi: true, static_pressure: true,
                 differential_pressure: true, oat: true, attitude: true, heading: true,
                 turn_rate: true, acceleration: true, gps_position: true },
        data_lost: { air_data: false, raw_pressure: false, oat: false, attitude: false,
                     acceleration: false, gps_position: false, gps_status: false },
        heading_status: { valid: true, suspect: false, gyro_seconds: null },
    };
}

// Status with a message just received, evaluated for the given facts
function evaluate(facts, now = 10000) {
    const status = new InstrumentStatus();
    status.messageReceived(now);
    status.update(facts, now);
    return status;
}

const texts = (status) => status.annunciations.map((a) => a.text);
const levelOf = (status, text) => status.annunciations.find((a) => a.text === text).level;

test('everything healthy: all valid, no annunciations, heading normal', () => {
    const s = evaluate(goodFacts());
    assert.ok(s.linkOk);
    for (const name of Object.keys(s.valid)) assert.equal(s.valid[name], true, name);
    assert.deepEqual(s.annunciations, []);
    assert.deepEqual(s.heading, { show: true, dim: false, flag: false });
});

test('before any message, and with the link down, everything is invalid with NO DATA', () => {
    const fresh = new InstrumentStatus();
    fresh.update(goodFacts(), 5000);
    assert.equal(fresh.linkOk, false);
    assert.deepEqual(texts(fresh), ['NO DATA']);
    assert.equal(levelOf(fresh, 'NO DATA'), WARNING);
    assert.ok(Object.values(fresh.valid).every((x) => x === false));
    assert.deepEqual(fresh.heading, { show: false, dim: false, flag: true });

    const s = new InstrumentStatus();
    s.messageReceived(1000);
    s.update(goodFacts(), 1999);
    assert.equal(s.linkOk, true);
    s.update(goodFacts(), 2001);                 // more than 1 s since the last message
    assert.equal(s.linkOk, false);
    assert.deepEqual(texts(s), ['NO DATA']);
    assert.equal(s.valid.airspeed, false);
});

test('the data link recovers when messages return', () => {
    const s = new InstrumentStatus();
    s.messageReceived(1000);
    s.update(goodFacts(), 3000);
    assert.equal(s.linkOk, false);
    s.messageReceived(3100);
    s.update(goodFacts(), 3150);
    assert.equal(s.linkOk, true);
    assert.deepEqual(s.annunciations, []);
});

test('no valid facts from the server: all invalid, no annunciations', () => {
    const f = goodFacts();
    delete f.valid;
    const s = evaluate(f);
    assert.ok(Object.values(s.valid).every((x) => x === false));
    assert.deepEqual(s.annunciations, []);
});

test('pitot lost: PITOT warning, airspeed and true airspeed invalid, altitude fine', () => {
    const f = goodFacts();
    f.modules.air.sensors.pitot = false;
    f.valid.airspeed = false;
    f.valid.differential_pressure = false;
    const s = evaluate(f);
    assert.deepEqual(texts(s), ['PITOT']);
    assert.equal(levelOf(s, 'PITOT'), WARNING);
    assert.equal(s.valid.airspeed, false);
    assert.equal(s.valid.trueAirspeed, false);
    assert.equal(s.valid.altitude, true);
    assert.equal(s.valid.densityAltitude, true);
});

test('static lost: STATIC warning, altitude, VSI, pressure altitude and density altitude invalid', () => {
    const f = goodFacts();
    f.modules.air.sensors.static = false;
    f.valid.altitude = f.valid.vsi = f.valid.static_pressure = false;
    const s = evaluate(f);
    assert.deepEqual(texts(s), ['STATIC']);
    assert.equal(s.valid.altitude, false);
    assert.equal(s.valid.vsi, false);
    assert.equal(s.valid.staticPressure, false);
    assert.equal(s.valid.densityAltitude, false);
    assert.equal(s.valid.trueAirspeed, false);
    assert.equal(s.valid.airspeed, true);
});

test('OAT lost (sensor bit or stopped frames): OAT caution; AOA lost: AOA caution', () => {
    let f = goodFacts();
    f.modules.air.sensors.oat = false;
    f.valid.oat = false;
    let s = evaluate(f);
    assert.deepEqual(texts(s), ['OAT']);
    assert.equal(levelOf(s, 'OAT'), CAUTION);
    assert.equal(s.valid.oat, false);
    assert.equal(s.valid.trueAirspeed, false);
    assert.equal(s.valid.densityAltitude, false);

    f = goodFacts();
    f.data_lost.oat = true;
    f.valid.oat = false;
    assert.deepEqual(texts(evaluate(f)), ['OAT']);

    f = goodFacts();
    f.modules.air.sensors.aoa = false;
    s = evaluate(f);
    assert.deepEqual(texts(s), ['AOA']);
    assert.equal(levelOf(s, 'AOA'), CAUTION);
});

test('AIR module missing or air data stopped: NO AIR DATA only', () => {
    let f = goodFacts();
    f.modules.air = { present: false, missing: true, state: null, restarted_age: null, sensors: null };
    f.valid.airspeed = f.valid.altitude = f.valid.vsi = f.valid.oat = false;
    f.valid.static_pressure = f.valid.differential_pressure = false;
    f.data_lost.air_data = true;
    f.data_lost.oat = true;
    let s = evaluate(f);
    assert.deepEqual(texts(s), ['NO AIR DATA']);
    assert.equal(levelOf(s, 'NO AIR DATA'), WARNING);
    assert.equal(s.valid.airspeed, false);
    assert.equal(s.valid.oat, false);

    f = goodFacts();
    f.data_lost.air_data = true;                 // heartbeat still present but 0x28 stopped
    f.valid.airspeed = f.valid.altitude = f.valid.vsi = false;
    assert.deepEqual(texts(evaluate(f)), ['NO AIR DATA']);
});

test('IMU missing: NO IMU; attitude and heading invalid', () => {
    const f = goodFacts();
    f.modules.imu = { present: false, missing: true, state: null, restarted_age: null, sensors: null };
    f.valid.attitude = f.valid.heading = f.valid.turn_rate = f.valid.acceleration = false;
    f.data_lost.attitude = true;
    const s = evaluate(f);
    assert.deepEqual(texts(s), ['NO IMU']);
    assert.equal(levelOf(s, 'NO IMU'), WARNING);
    assert.equal(s.valid.attitude, false);
    assert.deepEqual(s.heading, { show: false, dim: false, flag: true });
});

test('accel/gyro lost: ATT FAIL warning (not NO IMU), attitude and heading invalid', () => {
    const f = goodFacts();
    f.modules.imu.sensors.gyro_accel = false;
    f.valid.attitude = f.valid.heading = f.valid.turn_rate = f.valid.acceleration = false;
    const s = evaluate(f);
    assert.deepEqual(texts(s), ['ATT FAIL']);
    assert.equal(levelOf(s, 'ATT FAIL'), WARNING);
    assert.equal(s.valid.attitude, false);
    assert.equal(s.valid.turnRate, false);
    assert.equal(s.valid.acceleration, false);
    assert.deepEqual(s.heading, { show: false, dim: false, flag: true });
});

test('magnetometer lost: HDG GYRO caution, heading shown dimmed under the X', () => {
    const f = goodFacts();
    f.modules.imu.sensors.magnetometer = false;
    f.heading_status = { valid: true, suspect: true, gyro_seconds: 42.0 };
    const s = evaluate(f);
    assert.deepEqual(texts(s), ['HDG GYRO']);
    assert.equal(levelOf(s, 'HDG GYRO'), CAUTION);
    assert.deepEqual(s.heading, { show: true, dim: true, flag: true });
    assert.equal(s.valid.attitude, true);
});

test('gyro-only heading is blanked after 5 minutes, X stays, caution stays', () => {
    const f = goodFacts();
    f.heading_status = { valid: true, suspect: true, gyro_seconds: 299.9 };
    assert.deepEqual(evaluate(f).heading, { show: true, dim: true, flag: true });
    f.heading_status = { valid: true, suspect: true, gyro_seconds: 300.0 };
    const s = evaluate(f);
    assert.deepEqual(s.heading, { show: false, dim: false, flag: true });
    assert.deepEqual(texts(s), ['HDG GYRO']);
});

test('IMU not calibrated: IMU NO CAL caution', () => {
    const f = goodFacts();
    f.modules.imu.sensors.calibration = false;
    const s = evaluate(f);
    assert.deepEqual(texts(s), ['IMU NO CAL']);
    assert.equal(levelOf(s, 'IMU NO CAL'), CAUTION);
});

test('GPS module missing or NMEA lost: GPS NO DATA caution, GPS readouts invalid', () => {
    let f = goodFacts();
    f.modules.gps = { present: false, missing: true, state: null, restarted_age: null, sensors: null };
    f.valid.gps_position = false;
    let s = evaluate(f);
    assert.deepEqual(texts(s), ['GPS NO DATA']);
    assert.equal(levelOf(s, 'GPS NO DATA'), CAUTION);
    assert.equal(s.valid.gps, false);

    f = goodFacts();
    f.modules.gps.sensors.nmea = false;
    f.valid.gps_position = false;
    assert.deepEqual(texts(evaluate(f)), ['GPS NO DATA']);
});

test('GPS with no fix is not annunciated (readouts show ---), and a flapping status message is ignored', () => {
    const f = goodFacts();
    f.modules.gps.sensors.fix = false;
    f.valid.gps_position = false;
    f.data_lost.gps_position = true;
    let s = evaluate(f);
    assert.deepEqual(s.annunciations, []);
    assert.equal(s.valid.gps, false);

    f.data_lost.gps_status = true;               // flaps while the module probes baud rates
    s = evaluate(f);
    assert.deepEqual(s.annunciations, []);
});

test('MODULE RESTARTED while a restart is recent (under 8 s), not after', () => {
    const f = goodFacts();
    f.modules.gps.restarted_age = 7.9;
    assert.deepEqual(texts(evaluate(f)), ['MODULE RESTARTED']);
    assert.equal(levelOf(evaluate(f), 'MODULE RESTARTED'), CAUTION);
    f.modules.gps.restarted_age = 8.0;
    assert.deepEqual(texts(evaluate(f)), []);
    f.modules.gps.restarted_age = 0;
    assert.deepEqual(texts(evaluate(f)), ['MODULE RESTARTED']);
});

test('MODULE RESTARTED appears once even when several modules restarted', () => {
    const f = goodFacts();
    for (const name of ['imu', 'air', 'gps']) f.modules[name].restarted_age = 1.0;
    assert.deepEqual(texts(evaluate(f)), ['MODULE RESTARTED']);
});

test('warnings come before cautions', () => {
    const f = goodFacts();
    f.modules.air.sensors.oat = false;          // caution
    f.modules.air.sensors.pitot = false;        // warning
    f.modules.gps.sensors.nmea = false;         // caution
    f.modules.imu.sensors.gyro_accel = false;   // warning
    f.valid.airspeed = f.valid.attitude = f.valid.heading = false;
    const s = evaluate(f);
    assert.deepEqual(texts(s), ['PITOT', 'ATT FAIL', 'OAT', 'GPS NO DATA']);
    assert.deepEqual(s.annunciations.map((a) => a.level), [WARNING, WARNING, CAUTION, CAUTION]);
});

test('unknown sensors (null) never raise an annunciation by themselves', () => {
    const f = goodFacts();
    for (const name of ['imu', 'air', 'gps']) f.modules[name].sensors = null;
    assert.deepEqual(texts(evaluate(f)), []);
});

test('a value of null or missing in the facts is treated as not valid', () => {
    const f = goodFacts();
    f.valid.altitude = null;
    delete f.valid.vsi;
    const s = evaluate(f);
    assert.equal(s.valid.altitude, false);
    assert.equal(s.valid.vsi, false);
    assert.equal(s.valid.airspeed, true);
});

test('with accel/gyro lost the HDG GYRO caution is not added as well', () => {
    const f = goodFacts();
    f.modules.imu.sensors.gyro_accel = false;
    f.modules.imu.sensors.magnetometer = false;
    f.heading_status = { valid: false, suspect: true, gyro_seconds: 10 };
    f.valid.attitude = f.valid.heading = f.valid.turn_rate = f.valid.acceleration = false;
    assert.deepEqual(texts(evaluate(f)), ['ATT FAIL']);
});

// ---- module status page text

test('formatDuration gives h:mm:ss', () => {
    assert.equal(formatDuration(0), '0:00:00');
    assert.equal(formatDuration(59), '0:00:59');
    assert.equal(formatDuration(61), '0:01:01');
    assert.equal(formatDuration(3600), '1:00:00');
    assert.equal(formatDuration(3725.9), '1:02:05');
    assert.equal(formatDuration(-5), '0:00:00');
});

test('formatAge gives 0.4 s, 12 s or 5 min', () => {
    assert.equal(formatAge(0.44), '0.4 s');
    assert.equal(formatAge(9.96), '10.0 s');
    assert.equal(formatAge(12.4), '12 s');
    assert.equal(formatAge(99.4), '99 s');
    assert.equal(formatAge(300), '5 min');
});

test('describeModule: a healthy module', () => {
    const f = goodFacts().modules.gps;
    f.version = '1.0.8';
    f.uptime = 3725;
    f.age = 0.44;
    const d = describeModule('gps', f);
    assert.equal(d.state, 'OK');
    assert.equal(d.level, 'ok');
    assert.equal(d.version, 'v1.0.8');
    assert.equal(d.uptime, 'up 1:02:05');
    assert.equal(d.heard, 'heard 0.4 s ago');
    assert.equal(d.restarted, '');
    assert.deepEqual(d.sensors, [{ key: 'nmea', label: 'NMEA', ok: true }, { key: 'fix', label: 'FIX', ok: true }]);
});

test('describeModule: states and levels', () => {
    const m = (extra) => describeModule('air', Object.assign({ present: true, missing: false }, extra));
    assert.deepEqual([m({ state: 1 }).state, m({ state: 1 }).level], ['OK', 'ok']);
    assert.deepEqual([m({ state: 2 }).state, m({ state: 2 }).level], ['DEGRADED', CAUTION]);
    assert.deepEqual([m({ state: 3 }).state, m({ state: 3 }).level], ['FAILED', WARNING]);
    assert.deepEqual([m({ state: 0 }).state, m({ state: 0 }).level], ['STARTING', 'unknown']);
    assert.equal(m({ state: 9 }).state, 'UNKNOWN');
});

test('describeModule: missing, never heard and no information', () => {
    const missingInfo = { present: false, missing: true, state: null, version: '1.0.34', uptime: 500, age: 4.2, sensors: null };
    const d = describeModule('air', missingInfo);
    assert.deepEqual([d.state, d.level], ['MISSING', WARNING]);
    assert.equal(d.heard, 'last heard 4.2 s ago');
    assert.equal(d.uptime, 'up --');
    assert.ok(d.sensors.every((s) => s.ok === null));

    const waiting = describeModule('imu', { present: false, missing: false, age: null, version: null, uptime: null, sensors: null });
    assert.deepEqual([waiting.state, waiting.level, waiting.heard, waiting.version], ['WAITING', 'unknown', 'never heard', '--']);

    const none = describeModule('gps', undefined);
    assert.equal(none.state, 'WAITING');
    assert.equal(none.sensors.length, 2);
});

test('describeModule: sensors in order with their state, and a restart note', () => {
    const f = goodFacts().modules.air;
    f.sensors.aoa = false;
    f.restarted_age = 12.4;
    const d = describeModule('air', f);
    assert.deepEqual(d.sensors.map((s) => s.label), ['PITOT', 'AOA', 'STATIC', 'OAT']);
    assert.deepEqual(d.sensors.map((s) => s.ok), [true, false, true, true]);
    assert.equal(d.restarted, 'restarted 12 s ago');
    const imu = describeModule('imu', goodFacts().modules.imu);
    assert.deepEqual(imu.sensors.map((s) => s.label), ['GYRO/ACCEL', 'MAG', 'CAL']);
});

test('GPS receiver status is trusted while the module is present with NMEA, even with no fix', () => {
    let f = goodFacts();
    assert.equal(evaluate(f).valid.gpsStatus, true);

    f = goodFacts();
    f.modules.gps.sensors.fix = false;
    f.valid.gps_position = false;
    const s = evaluate(f);
    assert.equal(s.valid.gpsStatus, true);       // "No Fix" and 0 satellites are real information
    assert.equal(s.valid.gps, false);
});

test('GPS receiver status is not trusted when the module is missing, silent or not heard', () => {
    let f = goodFacts();
    f.modules.gps = { present: false, missing: true, state: null, restarted_age: null, sensors: null };
    assert.equal(evaluate(f).valid.gpsStatus, false);

    f = goodFacts();
    f.modules.gps.sensors.nmea = false;
    assert.equal(evaluate(f).valid.gpsStatus, false);

    f = goodFacts();
    f.modules.gps = { present: false, missing: false, state: null, restarted_age: null, sensors: null };   // start-up grace
    assert.equal(evaluate(f).valid.gpsStatus, false);

    assert.equal(new InstrumentStatus().valid.gpsStatus, false);       // before any data
});

// ---- EFIS row on the module status page

test('describeEfis: all well', () => {
    const d = describeEfis({ version: '1.6.1', state: 1, mask: 15, uptime: 3725, display_clients: 1 });
    assert.deepEqual([d.state, d.level, d.version, d.uptime, d.heard], ['OK', 'ok', 'v1.6.1', 'up 1:02:05', 'this server']);
    assert.deepEqual(d.sensors.map((s) => [s.label, s.ok]),
                     [['AIR', true], ['IMU', true], ['GPS', true], ['DISPLAY', true]]);
});

test('describeEfis: a module missing and no display', () => {
    const d = describeEfis({ version: '1.6.1', state: 2, mask: 0x05, uptime: 10, display_clients: 0 });
    assert.deepEqual([d.state, d.level], ['DEGRADED', CAUTION]);
    assert.deepEqual(d.sensors.map((s) => s.ok), [true, false, true, false]);
    const none = describeEfis({ version: '1.6.1', state: 3, mask: 0x07, uptime: 99, display_clients: 0 });
    assert.deepEqual([none.state, none.level], ['NO DISPLAY', CAUTION]);
});

test('describeEfis: an older server sends nothing', () => {
    for (const efis of [undefined, null]) {
        const d = describeEfis(efis);
        assert.deepEqual([d.state, d.level, d.version, d.uptime, d.heard], ['UNKNOWN', 'unknown', '--', 'up --', 'no data']);
        assert.ok(d.sensors.every((s) => s.ok === null));
    }
});

// ---- a null value is never valid, even while the validity flag still says it is (ISSUES.md #17)

function withValues(facts) {
    // the numbers the server sends alongside the facts
    return Object.assign(facts, {
        airspeed: 112, altitude: 2400, vsi: 0, oat: 22.8, static_pressure: 9420, differential_pressure: 620,
        pitch: 2.0, roll: -3.0, yaw: 90.0, turn_rate: 0.0, accx: 0.1, accy: 0.0, accz: 9.8,
    });
}

test('with real values for everything, nothing changes', () => {
    const s = evaluate(withValues(goodFacts()));
    assert.ok(Object.values(s.valid).every((x) => x === true));
});

test('a null value is invalid at once although its validity flag is still true', () => {
    const cases = [
        ['airspeed', 'airspeed', ['airspeed', 'trueAirspeed']],
        ['altitude', 'altitude', ['altitude']],
        ['vsi', 'vsi', ['vsi']],
        ['oat', 'oat', ['oat', 'trueAirspeed', 'densityAltitude']],
        ['pitch', 'pitch', ['attitude']],
        ['roll', 'roll', ['attitude']],
        ['yaw', 'yaw', ['heading']],
        ['turn_rate', 'turn_rate', ['turnRate']],
        ['accx', 'accx', ['acceleration']],
        ['accy', 'accy', ['acceleration']],
        ['accz', 'accz', ['acceleration']],
        ['static_pressure', 'static_pressure', ['staticPressure', 'trueAirspeed', 'densityAltitude']],
        ['differential_pressure', 'differential_pressure', ['trueAirspeed']],
    ];
    for (const [field, , invalidated] of cases) {
        const facts = withValues(goodFacts());     // every validity flag is true
        facts[field] = null;
        const s = evaluate(facts);
        for (const name of invalidated) assert.equal(s.valid[name], false, field + ' -> ' + name);
        // and nothing else is affected
        const others = Object.keys(s.valid).filter((name) => !invalidated.includes(name));
        for (const name of others) assert.equal(s.valid[name], true, field + ' must not affect ' + name);
    }
});

test('a null heading also hides the heading display', () => {
    const facts = withValues(goodFacts());
    facts.yaw = null;
    const s = evaluate(facts);
    assert.deepEqual(s.heading, { show: false, dim: false, flag: true });
});

test('a value that is missing altogether (an older server) is not treated as null', () => {
    const s = evaluate(goodFacts());               // no values at all in the data
    assert.ok(Object.values(s.valid).every((x) => x === true));
});

// ---- ALIGNING (G5 review feature 11)

// The facts while the IMU starts: heartbeat STARTING, no attitude yet (lost after the server's grace)
function imuStarting(uptime = 2) {
    const f = goodFacts();
    f.modules.imu = { ...f.modules.imu, state: 0, uptime: uptime, sensors: null };
    f.valid.attitude = f.valid.heading = f.valid.turn_rate = f.valid.acceleration = false;
    f.data_lost.attitude = f.data_lost.acceleration = true;
    f.heading_status = { valid: false, suspect: false, gyro_seconds: null };
    return f;
}
function imuRunning(uptime) {
    const f = goodFacts();
    f.modules.imu = { ...f.modules.imu, uptime: uptime };
    return f;
}
function step(status, facts, now) {
    status.messageReceived(now);
    status.update(facts, now);
    return status;
}

test('aligning: an IMU that is starting shows ALIGNING, not NO IMU or a red X', () => {
    const s = step(new InstrumentStatus(), imuStarting(), 1000);
    assert.equal(s.aligning, true);
    assert.ok(!texts(s).includes('NO IMU'));
    assert.equal(s.valid.attitude, false);
    assert.deepEqual(s.heading, { show: false, dim: false, flag: false });
});

test('aligning: lasts 3 s after the attitude arrives, then the attitude shows', () => {
    const s = new InstrumentStatus();
    step(s, imuStarting(2), 1000);
    step(s, imuStarting(3), 2000);
    step(s, imuRunning(4), 3000);           // the attitude arrives
    assert.equal(s.aligning, true);
    assert.equal(s.valid.attitude, false);  // the first frames are not shown
    step(s, imuRunning(6), 5900);
    assert.equal(s.aligning, true);
    step(s, imuRunning(7), 6000);
    assert.equal(s.aligning, false);
    assert.equal(s.valid.attitude, true);
    assert.deepEqual(s.annunciations, []);
});

test('aligning: an IMU already running when the page starts is not aligning', () => {
    const s = step(new InstrumentStatus(), imuRunning(3600), 1000);
    assert.equal(s.aligning, false);
    assert.equal(s.valid.attitude, true);
});

test('aligning: a missing IMU is NO IMU, and a start that never sends attitude gives up after 15 s', () => {
    const missing = goodFacts();
    missing.modules.imu = { present: false, missing: true, state: null, restarted_age: null, sensors: null };
    missing.valid.attitude = false;
    missing.data_lost.attitude = true;
    const m = step(new InstrumentStatus(), missing, 1000);
    assert.equal(m.aligning, false);
    assert.ok(texts(m).includes('NO IMU'));

    const s = new InstrumentStatus();
    step(s, imuStarting(1), 1000);
    step(s, imuStarting(10), 15900);
    assert.equal(s.aligning, true);
    step(s, imuStarting(11), 16000);
    assert.equal(s.aligning, false);
    assert.ok(texts(s).includes('NO IMU'));
    step(s, imuStarting(12), 17000);        // still stuck starting: does not go back to ALIGNING
    assert.equal(s.aligning, false);
});

test('aligning: an IMU restart in flight (STARTING again) aligns again', () => {
    const s = new InstrumentStatus();
    step(s, imuRunning(3600), 1000);
    step(s, imuStarting(1), 2000);
    assert.equal(s.aligning, true);
    assert.ok(!texts(s).includes('NO IMU'));
});
