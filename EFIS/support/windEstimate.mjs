'use strict';

export const VERSION = '1.0.0';

/*****************************************************************************
 * WindEstimate — the wind worked out on the page from what it already has:
 *     wind = ground vector (GPS ground speed along the magnetic track)
 *          - air vector (true airspeed along the magnetic heading)
 * Directions are magnetic, as the HSI's rose. The wind vector is averaged
 * (about 10 s) so a gust or a noisy sample does not jump the readout.
 *
 * It gives no wind (null) when it cannot be trusted:
 *   - ground speed or true airspeed below MIN_SPEED_KT (on the ground, taxiing);
 *   - the heading not valid or gyro only (no magnetometer), no valid TAS or GPS;
 *   - until WARM_UP_S seconds of good samples have been averaged.
 * While turning faster than HOLD_TURN_DEG_S the last wind is held, not updated:
 * the heading, track and airspeed do not change together in a turn.
 * Expect some error: IAS is taken as CAS, and 5° of heading error at 100 kt
 * gives about 9 kt of false crosswind.
 ****************************************************************************/
export const MIN_SPEED_KT = 30;
export const HOLD_TURN_DEG_S = 2;
export const AVERAGE_S = 10;
export const WARM_UP_S = 5;

const RAD = Math.PI / 180;

export class WindEstimate {

    constructor() {
        this.reset();
    }

    reset() {
        this._north = 0;        // averaged wind vector, the way the wind blows (kt)
        this._east = 0;
        this._seconds = 0;      // seconds of good samples averaged since the last reset
    }

    /**
     * Add a sample; returns the wind or null.
     * @param {object} s {groundSpeed, track, tas, heading, inputsValid, turnRate (deg/s, null if unknown), dt (s)}
     * @returns {{from: number, speed: number, headwind: number, crosswind: number}|null}
     *   from: the magnetic direction the wind blows from (0-359); speed in kt;
     *   headwind: + head, - tail; crosswind: + from the right, - from the left (relative to the heading)
     */
    update(s) {
        const usable = s.inputsValid &&
                       [s.groundSpeed, s.track, s.tas, s.heading].every(v => typeof v === 'number' && isFinite(v)) &&
                       s.groundSpeed >= MIN_SPEED_KT && s.tas >= MIN_SPEED_KT;
        if (!usable) {
            this.reset();
            return null;
        }
        const turning = typeof s.turnRate === 'number' && Math.abs(s.turnRate) > HOLD_TURN_DEG_S;
        if (!turning) {
            const north = s.groundSpeed * Math.cos(s.track * RAD) - s.tas * Math.cos(s.heading * RAD);
            const east = s.groundSpeed * Math.sin(s.track * RAD) - s.tas * Math.sin(s.heading * RAD);
            const dt = Math.min(Math.max(s.dt, 0), 1);
            if (this._seconds === 0) {
                this._north = north;
                this._east = east;
            } else {
                const k = 1 - Math.exp(-dt / AVERAGE_S);
                this._north += (north - this._north) * k;
                this._east += (east - this._east) * k;
            }
            this._seconds += dt;
        }
        if (this._seconds < WARM_UP_S) {
            return null;
        }
        const speed = Math.hypot(this._north, this._east);
        // the wind blows towards atan2(east, north); it comes from the opposite direction
        const from = (Math.atan2(this._east, this._north) / RAD + 180 + 360) % 360;
        const relative = (from - s.heading) * RAD;
        return {
            from: from,
            speed: speed,
            headwind: speed * Math.cos(relative),
            crosswind: speed * Math.sin(relative),
        };
    }
}
