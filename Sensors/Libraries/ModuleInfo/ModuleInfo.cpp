#include "ModuleInfo.h"
#include <USB.h>	// arduino-pico USB object, used to set the USB product string

ModuleInfo::ModuleInfo(const char *name, const char *version,
                       const char *buildDate, const char *buildTime)
	: _name(name), _version(version), _buildDate(buildDate), _buildTime(buildTime),
	  _logLen(0), _hostWasConnected(false) {
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
}
