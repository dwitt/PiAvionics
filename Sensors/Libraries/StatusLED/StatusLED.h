/*****************************************************************************
 * StatusLED — Standardized NeoPixel status LED for PiAvionics sensor modules
 *
 * Provides startup POST codes, go/no-go indication, and runtime heartbeat.
 * See StatusLED.md for the full specification.
 *****************************************************************************/

#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Version
#define STATUS_LED_VERSION "1.1.0"

// Default hardware config (Feather RP2040 onboard NeoPixel)
#define STATUS_LED_DEFAULT_PIN        16
#define STATUS_LED_DEFAULT_BRIGHTNESS 10

// Timing constants (milliseconds)
#define STATUS_LED_INIT_BLUE_MS       150   // blue flash before init attempt
#define STATUS_LED_INIT_RESULT_MS     200   // green/red flash after init result
#define STATUS_LED_INIT_GAP_MS        100   // gap between init steps
#define STATUS_LED_GO_MS              2000  // solid green on go
#define STATUS_LED_POST_FLASH_ON_MS   200   // POST code flash on
#define STATUS_LED_POST_FLASH_OFF_MS  200   // POST code flash off
#define STATUS_LED_POST_PAUSE_MS      1500  // pause between POST code cycles
#define STATUS_LED_HEARTBEAT_ON_MS    80    // heartbeat flash duration
#define STATUS_LED_HEARTBEAT_PERIOD_MS 4000 // heartbeat interval

// Health states for runtime heartbeat color
enum StatusLEDHealth {
  STATUS_LED_HEALTHY,       // green heartbeat
  STATUS_LED_SENSOR_FAULT,  // amber heartbeat
  STATUS_LED_CAN_FAULT,     // red heartbeat
  STATUS_LED_NO_GPS_FIX,    // blue heartbeat
  STATUS_LED_GPS_2D_FIX,    // yellow heartbeat
  STATUS_LED_GPS_3D_FIX     // green heartbeat (same as healthy)
};

// Optional function called about every millisecond while the LED code waits
// (including inside postCode() and configFail(), which never return). A sketch
// uses it to keep sending its CAN heartbeat and answering '?' while it blinks.
typedef void (*StatusLEDServiceFn)();

class StatusLED {
public:
  StatusLED(uint8_t pin = STATUS_LED_DEFAULT_PIN,
            uint8_t brightness = STATUS_LED_DEFAULT_BRIGHTNESS);

  void begin();
  void setServiceCallback(StatusLEDServiceFn fn);   // pass NULL to remove

  // --- Startup phase ---
  void startup();           // solid red — call at power on
  void initBegin();         // blue flash — about to init a peripheral
  void initOK();            // green flash — peripheral initialized OK
  void initFail();          // red flash — peripheral failed

  void go();                // solid green for 2s — all peripherals OK
  void postCode(uint8_t n); // n red flashes, pause, repeat forever (never returns)
  void configFail(uint8_t n); // n amber flashes, pause, repeat forever (never returns)

  // --- Runtime phase ---
  void heartbeat();         // call every loop — handles timing internally
  void setHealth(StatusLEDHealth state);

private:
  Adafruit_NeoPixel _pixel;
  uint8_t _brightness;
  StatusLEDHealth _health;
  unsigned long _lastHeartbeat;
  StatusLEDServiceFn _service;

  void _wait(unsigned long ms);   // delay() that keeps calling the service callback

  void _setColor(uint8_t r, uint8_t g, uint8_t b);
  void _off();
  void _flash(uint8_t r, uint8_t g, uint8_t b, uint16_t onMs);
  void _flashLoop(uint8_t n, uint8_t r, uint8_t g, uint8_t b);
  uint32_t _healthColor();
};

#endif
