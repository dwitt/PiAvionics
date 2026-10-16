/*****************************************************************************
 * ModuleInfo - module identity over USB for PiAvionics sensor modules
 *
 * - The USB product string is "<name> v<version>", so the host can read the
 *   version without opening the port (ioreg on macOS, lsusb or
 *   /dev/serial/by-id on Linux).
 * - Startup results are stored with log() and printed when a host connects
 *   to USB serial, and whenever '?' is received.
 * - Nothing here waits for a host. See ModuleInfo.md.
 *****************************************************************************/

#ifndef MODULE_INFO_H
#define MODULE_INFO_H

#include <Arduino.h>

#define MODULE_INFO_VERSION "1.0.0"

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

private:
	const char *_name;
	const char *_version;
	const char *_buildDate;
	const char *_buildTime;
	char _product[MODULE_INFO_NAME_SIZE];	// must stay valid: USB keeps the pointer
	char _log[MODULE_INFO_LOG_SIZE];
	size_t _logLen;
	bool _hostWasConnected;
};

#endif
