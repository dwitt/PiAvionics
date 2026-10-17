#include "ModuleInfo.h"
#include <USB.h>	// arduino-pico USB object, used to set the USB product string

ModuleInfo::ModuleInfo(const char *name, const char *version,
                       const char *buildDate, const char *buildTime)
	: _name(name), _version(version), _buildDate(buildDate), _buildTime(buildTime),
	  _logLen(0), _hostWasConnected(false),
	  _moduleNumber(0), _state(MODULE_STATE_STARTING), _sensors(0),
	  _heartbeatSent(false), _lastHeartbeat(0) {
	_product[0] = 0;
	_log[0] = 0;
}

void ModuleInfo::begin(unsigned long baud) {
	snprintf(_product, sizeof(_product), "%s v%s", _name, _version);
	USB.setProduct(_product);	// Must be before Serial.begin() connects USB
	Serial.begin(baud);
}

void ModuleInfo::log(const char *msg) {
	size_t n = strlen(msg);
	if (_logLen + n + 1 >= sizeof(_log)) return;	// full, drop
	memcpy(_log + _logLen, msg, n);
	_logLen += n;
	_log[_logLen++] = '\n';
	_log[_logLen] = 0;
}

void ModuleInfo::fatal(const char *msg) {
	log(msg);
	if (Serial) printReport();
}

void ModuleInfo::service() {
	bool connected = Serial;
	if (connected && !_hostWasConnected) printReport();
	_hostWasConnected = connected;
	while (connected && Serial.available()) {
		if (Serial.read() == '?') printReport();
	}
}

void ModuleInfo::printReport() {
	Serial.print(_name);
	Serial.print(" v");
	Serial.print(_version);
	Serial.print(" built ");
	Serial.print(_buildDate);
	Serial.print(" ");
	Serial.print(_buildTime);
	Serial.print(" uptime ");
	Serial.print(millis() / 1000);
	Serial.println(" s");
	Serial.print(_log);
	if (_reportHook) _reportHook(Serial);
}

// ---------------------------------------------------------------------------
// CAN heartbeat
// ---------------------------------------------------------------------------

void ModuleInfo::setModuleNumber(uint8_t moduleNumber) {
	_moduleNumber = moduleNumber;
}

void ModuleInfo::setHealth(uint8_t state, uint8_t sensors) {
	_state = state;
	_sensors = sensors;
}

bool ModuleInfo::heartbeatDue() {
	unsigned long now = millis();
	if (_heartbeatSent && now - _lastHeartbeat < MODULE_HEARTBEAT_PERIOD_MS) return false;
	_heartbeatSent = true;
	_lastHeartbeat = now;
	return true;
}

void ModuleInfo::packHeartbeat(uint8_t out[8]) {
	// Version "major.minor.patch" -> three bytes (each clamped to 255)
	unsigned int parts[3] = {0, 0, 0};
	int part = 0;
	for (const char *c = _version; *c && part < 3; c++) {
		if (*c >= '0' && *c <= '9') parts[part] = parts[part] * 10 + (*c - '0');
		else if (*c == '.') part++;
	}
	unsigned long uptime = millis() / 1000;
	if (uptime > 0xFFFF) uptime = 0xFFFF;

	out[0] = _moduleNumber;
	out[1] = parts[0] > 255 ? 255 : parts[0];
	out[2] = parts[1] > 255 ? 255 : parts[1];
	out[3] = parts[2] > 255 ? 255 : parts[2];
	out[4] = _state;
	out[5] = _sensors;
	out[6] = uptime & 0xFF;
	out[7] = (uptime >> 8) & 0xFF;
}
