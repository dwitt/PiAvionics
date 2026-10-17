/*****************************************************************************
 * StatusLED — Standardized NeoPixel status LED for PiAvionics sensor modules
 *
 * See StatusLED.md for the full specification.
 *****************************************************************************/

#include "StatusLED.h"

StatusLED::StatusLED(uint8_t pin, uint8_t brightness)
  : _pixel(1, pin, NEO_GRB + NEO_KHZ800),
    _brightness(brightness),
    _health(STATUS_LED_HEALTHY),
    _lastHeartbeat(0),
    _service(nullptr)
{
}

void StatusLED::setServiceCallback(StatusLEDServiceFn fn) {
  _service = fn;
}

void StatusLED::_wait(unsigned long ms) {
  unsigned long start = millis();
  do {
    if (_service) _service();
    delay(1);
  } while (millis() - start < ms);
}

void StatusLED::begin() {
  _pixel.begin();
  _pixel.setBrightness(_brightness);
  _off();
}

// ---------------------------------------------------------------------------
// Startup phase
// ---------------------------------------------------------------------------

void StatusLED::startup() {
  _setColor(255, 0, 0);  // solid red
}

void StatusLED::initBegin() {
  _flash(0, 0, 255, STATUS_LED_INIT_BLUE_MS);  // blue
  _wait(STATUS_LED_INIT_GAP_MS);
}

void StatusLED::initOK() {
  _flash(0, 255, 0, STATUS_LED_INIT_RESULT_MS);  // green
  _wait(STATUS_LED_INIT_GAP_MS);
}

void StatusLED::initFail() {
  _flash(255, 0, 0, STATUS_LED_INIT_RESULT_MS);  // red
  _wait(STATUS_LED_INIT_GAP_MS);
}

void StatusLED::go() {
  _setColor(0, 255, 0);  // solid green
  _wait(STATUS_LED_GO_MS);
  _off();
}

void StatusLED::postCode(uint8_t n) {
  // Red flashes — never returns
  while (true) {
    _flashLoop(n, 255, 0, 0);
  }
}

void StatusLED::configFail(uint8_t n) {
  // Amber flashes — never returns
  while (true) {
    _flashLoop(n, 255, 140, 0);
  }
}

// ---------------------------------------------------------------------------
// Runtime phase
// ---------------------------------------------------------------------------

void StatusLED::setHealth(StatusLEDHealth state) {
  _health = state;
}

void StatusLED::heartbeat() {
  unsigned long now = millis();
  bool fault = (_health == STATUS_LED_SENSOR_FAULT || _health == STATUS_LED_CAN_FAULT ||
                _health == STATUS_LED_FAILED);
  if (now - _lastHeartbeat >= (fault ? STATUS_LED_FAULT_PERIOD_MS : STATUS_LED_HEARTBEAT_PERIOD_MS)) {
    _lastHeartbeat = now;
    uint32_t color = _healthColor();
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    _flash(r, g, b, STATUS_LED_HEARTBEAT_ON_MS);
  }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void StatusLED::_setColor(uint8_t r, uint8_t g, uint8_t b) {
  _pixel.setPixelColor(0, _pixel.Color(r, g, b));
  _pixel.show();
}

void StatusLED::_off() {
  _pixel.setPixelColor(0, 0);
  _pixel.show();
}

void StatusLED::_flash(uint8_t r, uint8_t g, uint8_t b, uint16_t onMs) {
  _setColor(r, g, b);
  _wait(onMs);
  _off();
}

void StatusLED::_flashLoop(uint8_t n, uint8_t r, uint8_t g, uint8_t b) {
  for (uint8_t i = 0; i < n; i++) {
    _setColor(r, g, b);
    _wait(STATUS_LED_POST_FLASH_ON_MS);
    _off();
    _wait(STATUS_LED_POST_FLASH_OFF_MS);
  }
  _wait(STATUS_LED_POST_PAUSE_MS);
}

uint32_t StatusLED::_healthColor() {
  switch (_health) {
    case STATUS_LED_HEALTHY:      return 0x00FF00;  // green
    case STATUS_LED_SENSOR_FAULT: return 0xFF8C00;  // amber
    case STATUS_LED_CAN_FAULT:    return 0xFF0000;  // red
    case STATUS_LED_FAILED:       return 0xFF0000;  // red
    case STATUS_LED_NO_GPS_FIX:   return 0x0000FF;  // blue
    case STATUS_LED_GPS_2D_FIX:   return 0xFFFF00;  // yellow
    case STATUS_LED_GPS_3D_FIX:   return 0x00FF00;  // green
    default:                      return 0x00FF00;  // green
  }
}
