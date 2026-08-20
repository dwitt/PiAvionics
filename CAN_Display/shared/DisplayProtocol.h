#pragma once

#include <stdint.h>
#include <stddef.h>

// I2C protocol shared between CAN_Feather (controller) and Display_Feather
// (peripheral). This file is symlinked into both sketch folders so they
// can't drift out of sync -- edit only the copy in CAN_Display/shared/.

#define PROTOCOL_VERSION "1.0.0"

// 7-bit I2C address of the Display Feather (peripheral)
#define DISPLAY_I2C_ADDR 0x42

// How often CAN_Feather pushes a fresh DisplayData struct to the Display Feather.
#define DISPLAY_PUSH_PERIOD_MS 200

// validMask bits: set when the corresponding CAN message has been received
// within its staleness window (see CAN_Feather's *_STALE_MS constants).
// Clear means Display_Feather should render that field as "---".
// Bits 2-15 reserved for a future phase (GPS, RDAC/EMS).
#define VALID_AIR  (1 << 0)  // CAN 0x28: airspeed, altitude, VSI
#define VALID_AHRS (1 << 1)  // CAN 0x48: heading, pitch, roll, turn rate

typedef struct __attribute__((packed)) {
  uint8_t  seq;         // increments every send; Display_Feather uses this to
                         // detect a stalled I2C link (separately from validMask,
                         // which tracks staleness of the underlying CAN data)
  uint16_t validMask;   // VALID_* bits
  int16_t  airspeed;    // knots
  int32_t  altitude;    // ft
  int16_t  vsi;         // ft/min
  int16_t  heading;     // deg x10
  int16_t  pitch;       // deg x10
  int16_t  roll;        // deg x10
  int16_t  turnRate;    // deg/s x10
  uint8_t  checksum;    // XOR of every preceding byte in this struct
} DisplayData;

// XOR checksum over every byte of the struct except the checksum field itself.
static inline uint8_t displayDataChecksum(const DisplayData *d) {
  const uint8_t *bytes = (const uint8_t *)d;
  uint8_t sum = 0;
  for (size_t i = 0; i < sizeof(DisplayData) - 1; i++) {
    sum ^= bytes[i];
  }
  return sum;
}
