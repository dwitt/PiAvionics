/*****************************************************************************
 * SensorHealth - decides whether a sensor is working, from a stream of
 * good / bad samples. Header only, no hardware access.
 *
 * A sensor goes invalid after failCount bad samples in a row and valid again
 * after goodCount good samples in a row, so one dropped read does nothing and
 * a flapping connection does not flap the module state. A sensor that returns
 * the same value over and over (locked up) can be reported with stuck().
 *
 * Used by IMU_AHRS and AIR_Module (see Sensors/FAILURE_DETECTION_PLAN.md).
 *****************************************************************************/

#ifndef SENSOR_HEALTH_H
#define SENSOR_HEALTH_H

#include <Arduino.h>

#define SENSOR_HEALTH_VERSION "1.0.0"

class SensorHealth {
public:
	SensorHealth(uint16_t failCount = 20, uint16_t goodCount = 5, uint16_t stuckCount = 100)
		: _failCount(failCount), _goodCount(goodCount), _stuckCount(stuckCount) {}

	// Count one sample. Returns true if this sample changed the sensor between valid and invalid.
	bool update(bool ok) {
		if (ok) {
			_bad = 0;
			if (_good < 0xFFFF) _good++;
			if (!_valid && _good >= _goodCount) {
				_valid = true;
				return true;
			}
		} else {
			_good = 0;
			if (_bad < 0xFFFF) _bad++;
			if (_valid && _bad >= _failCount) {
				markInvalid();
				return true;
			}
		}
		return false;
	}

	// Declare the sensor invalid now (for example it was not found at start-up).
	void markInvalid() {
		if (_valid) failures++;
		_valid = false;
		_good = 0;
		invalidSince = millis();
		lastRetry = invalidSince;
	}

	// True if the n values are identical to the previous call for stuckCount calls in a row.
	// last[] holds the previous values and must have n elements that stay alive between calls.
	bool stuck(float *last, const float *now, int n) {
		bool same = true;
		for (int i = 0; i < n; i++) {
			if (now[i] != last[i]) same = false;
			last[i] = now[i];
		}
		if (!same) _stuck = 0;
		else if (_stuck < 0xFFFF) _stuck++;
		return _stuck >= _stuckCount;
	}

	// True if a sensor that is invalid should be tried again (every retryMs).
	bool retryDue(uint32_t retryMs) const { return !_valid && millis() - lastRetry >= retryMs; }

	bool valid() const { return _valid; }
	uint32_t outageMs() const { return millis() - invalidSince; }	// how long it has been invalid

	uint32_t failures = 0;			// times the sensor went invalid
	uint32_t attempts = 0;			// re-initialisation attempts (kept by the sketch)
	uint32_t recoveries = 0;		// successful re-initialisations (kept by the sketch)
	uint32_t invalidSince = 0;		// millis() when it went invalid
	uint32_t lastRetry = 0;			// millis() of the last re-initialisation attempt
	const char *lastAttempt = "none";	// how the last attempt ended (kept by the sketch)

private:
	uint16_t _failCount, _goodCount, _stuckCount;
	uint16_t _bad = 0, _good = 0, _stuck = 0;
	bool _valid = true;
};

#endif
