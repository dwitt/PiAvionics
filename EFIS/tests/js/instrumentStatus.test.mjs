'use strict';
// Tests for support/instrumentStatus.mjs (no browser needed). From the EFIS directory:
//     node --test tests/js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { InstrumentStatus, WARNING, CAUTION } from '../../support/instrumentStatus.mjs';

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
