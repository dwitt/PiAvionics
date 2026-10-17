/*****************************************************************************
 * ModuleInfo - module identity over USB for PiAvionics sensor modules
 *
 * - The USB product string is "<name> v<version>", so the host can read the
 *   version without opening the port (ioreg on macOS, lsusb or
 *   /dev/serial/by-id on Linux).
 * - Startup results are stored with log() and printed when a host connects
 *   to USB serial, and whenever '?' is received.
 * - Builds the CAN heartbeat frame (module, version, state, sensors, uptime).
 *   The sketch sends it with its own CAN object.
 * - Nothing here waits for a host. See ModuleInfo.md.
 *****************************************************************************/

#ifndef MODULE_INFO_H
#define MODULE_INFO_H

#include <Arduino.h>

#define MODULE_INFO_VERSION "1.1.0"

// Heartbeat (Protocol/can_messages.yaml 0x701 to 0x703): one CAN frame per module,
// ID = MODULE_HEARTBEAT_ID_BASE + module number, sent every MODULE_HEARTBEAT_PERIOD_MS.
#define MODULE_HEARTBEAT_ID_BASE   0x700
#define MODULE_HEARTBEAT_PERIOD_MS 1000

enum ModuleNumber : uint8_t {
	MODULE_NUM_EFIS = 0,		// reserved
	MODULE_NUM_IMU  = 1,
	MODULE_NUM_AIR  = 2,
	MODULE_NUM_GPS  = 3,
	MODULE_NUM_EMS  = 4			// reserved
};

enum ModuleState : uint8_t {
	MODULE_STATE_STARTING = 0,
	MODULE_STATE_OK       = 1,
	MODULE_STATE_DEGRADED = 2,	// running, but a sensor is missing or invalid
	MODULE_STATE_FAILED   = 3	// halted, not producing data
};

#define MODULE_INFO_LOG_SIZE 600	// bytes of stored startup log
#define MODULE_INFO_NAME_SIZE 48	// USB product string buffer

class ModuleInfo {
public:
	// Name and version are kept by pointer, so pass string literals or other
	// strings that stay valid. Pass __DATE__ and __TIME__ from the sketch so
	// they are the sketch's build time, not the library's.
	ModuleInfo(const char *name, const char *version,
	           const char *buildDate, const char *buildTime);

	// Sets the USB product string and starts USB serial. Call first in setup().
	void begin(unsigned long baud = 115200);

	// Store a line of startup status (kept even if no host is connected)
	void log(const char *msg);

	// Store a line for a failure that halts the module, and print the report
	// immediately if a host is already connected
	void fatal(const char *msg);

	// Call regularly (every loop() pass, and inside any halt loop).
	// Prints the report when a host connects or '?' is received.
	void service();

	// Print version, build time, uptime and the stored log
	void printReport();

	// --- CAN heartbeat ---
	// Say which module this is. Call once in setup().
	void setModuleNumber(uint8_t moduleNumber);

	// Current health. state is a ModuleState; sensors is a bit mask of the
	// sensors that are present and valid (bit meanings are per module, see
	// Protocol/can_messages.yaml).
	void setHealth(uint8_t state, uint8_t sensors);
	uint8_t state() const { return _state; }
	uint8_t sensors() const { return _sensors; }

	// True once per MODULE_HEARTBEAT_PERIOD_MS (and immediately on the first call)
	bool heartbeatDue();

	// CAN ID to send the heartbeat on
	uint16_t heartbeatId() const { return MODULE_HEARTBEAT_ID_BASE + _moduleNumber; }

	// Fill 8 bytes: module, version major/minor/patch, state, sensors, uptime (s, u16, little-endian)
	void packHeartbeat(uint8_t out[8]);

private:
	const char *_name;
	const char *_version;
	const char *_buildDate;
	const char *_buildTime;
	char _product[MODULE_INFO_NAME_SIZE];	// must stay valid: USB keeps the pointer
	char _log[MODULE_INFO_LOG_SIZE];
	size_t _logLen;
	bool _hostWasConnected;
	uint8_t _moduleNumber;
	uint8_t _state;
	uint8_t _sensors;
	bool _heartbeatSent;
	unsigned long _lastHeartbeat;
};

#endif
