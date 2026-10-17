# ModuleInfo

Module identity over USB for all PiAvionics sensor modules (Feather RP2040, arduino-pico).

## What it does

| Feature | How to use it |
|---------|---------------|
| Version in the USB product string | macOS: `ioreg -p IOUSB -l -w0 \| grep kUSBProductString`. Linux/Pi: `ls /dev/serial/by-id` (the name carries the product string). No port needs to be opened. |
| Report on connect | Opening the USB serial port prints the report. |
| Report on request | Send `?` over USB serial at 115200 baud. |
| Never waits | `begin()` does not wait for a host. `service()` is a few flag checks. |

The report looks like this:

```
IMU_AHRS v1.0.3 built Oct 16 2026 17:41:57 uptime 91 s
ISM330DHCX OK
LIS3MDL OK
...
```

## Using it in a sketch

```cpp
#include <ModuleInfo.h>

#define VERSION "1.0.0"
ModuleInfo moduleInfo("AIR_Module", VERSION, __DATE__, __TIME__);

void setup() {
	moduleInfo.begin();                      // first: sets the USB product string
	...
	moduleInfo.log("MS4525DO pitot OK");     // store startup results
	moduleInfo.fatal("Startup failed");      // for a failure that halts the module
	while (1) { moduleInfo.service(); delay(100); }   // halt loops must call service()
}

void loop() {
	moduleInfo.service();                    // first line of loop()
	...
}
```

## CAN heartbeat

Every module sends one heartbeat frame per second (`Protocol/can_messages.yaml`, 0x701 IMU, 0x702 AIR, 0x703 GPS): module number, version, state, a sensors mask and uptime. ModuleInfo builds the frame; the sketch sends it with its own CAN object.

```cpp
moduleInfo.setModuleNumber(MODULE_NUM_AIR);          // in setup(), once
moduleInfo.setHealth(MODULE_STATE_STARTING, 0);       // update as sensors come up or fail
...
static void sendHeartbeat() {
	if (!moduleInfo.heartbeatDue()) return;           // true once a second
	uint8_t frame[8];
	moduleInfo.packHeartbeat(frame);
	can.beginPacket(moduleInfo.heartbeatId());
	can.write(frame, 8);
	can.endPacket();
}
```

- States: `MODULE_STATE_STARTING`, `OK`, `DEGRADED` (running, a sensor missing or invalid), `FAILED` (halted).
- Call `setHealth(MODULE_STATE_FAILED, mask)` before any halt, and call `sendHeartbeat()` from the halt loop (or from the StatusLED service callback, below), so the EFIS can see which sensor failed.
- `StatusLED::setServiceCallback(fn)` (StatusLED 1.1.0) calls `fn` about every millisecond while the LED code waits, including inside `postCode()` and `configFail()`. Use it to keep the heartbeat and the '?' report going during startup and while a failure code blinks. Set it only after the CAN bus is up, or guard the heartbeat with a flag.

## Notes

- `__DATE__` and `__TIME__` are passed in from the sketch so they are the sketch's build time. The Arduino build cache reuses an unchanged sketch, so the time shows when the sketch last changed, not when it was flashed.
- The stored log holds 600 bytes. Lines that do not fit are dropped.
- A failure that halts the module is only visible if the halt loop calls `service()`. `StatusLED::postCode()` and `configFail()` never return and do not call it, so a halting failure there only prints if a host was already connected.
- `Serial.print` returns immediately when no host is connected, so nothing here can stall the module. If a host is connected but not reading, a write can block for up to 1 s (arduino-pico behavior).
- The version in the sketch's `VERSION` define follows the usual rule: increment the patch number on every change.

## Live status lines in the report (1.2.0)

`moduleInfo.setReportHook(fn)` registers `void fn(Print &out)`, called at the end of every report
(on `?` and when a host connects). Use it for live values that the startup log cannot hold, such as the
CAN transmit state and error counters.
