// ============================================================================
// CAN_Feather -- listens on the piavionics CAN bus and forwards flight
// instrument data to the Display_Feather over I2C for the KS0108 backup
// display. See CAN_Display/README.md for wiring and the CAN message
// reference (verified against the actual sensor firmware, not just
// CAN Reference/Can Docs.md, which has stale entries).
//
// Hardware: Adafruit Feather RP2040 + Adafruit MCP2515 CAN Bus FeatherWing
// (SPI, CS7) -- identical hardware/library to AIR_Module.ino / IMU_AHRS.ino.
// ============================================================================

#include <Wire.h>
#include <Adafruit_MCP2515.h>
#include "DisplayProtocol.h"

#define VERSION "1.0.0"

// ----------------------------------------------------------------------------
// CAN bus (MCP2515 FeatherWing)
// ----------------------------------------------------------------------------
#define CAN_CS_PIN 7
#define CAN_BAUDRATE (250000)
Adafruit_MCP2515 can(CAN_CS_PIN);

#define CAN_AIR_MSG_ID   0x28  // Airspeed, Altitude, VSI
#define CAN_EULER_MSG_ID 0x48  // AHRS heading, pitch, roll, turn rate

// How long a source message's data stays "valid" after last receipt.
#define AIR_STALE_MS  1000
#define AHRS_STALE_MS 1000

static unsigned long lastAirRxMillis = 0;
static unsigned long lastAhrsRxMillis = 0;

// ----------------------------------------------------------------------------
// Outgoing I2C payload to the Display Feather
// ----------------------------------------------------------------------------
static DisplayData displayData = {};
static unsigned long lastDisplayPushMillis = 0;

// ----------------------------------------------------------------------------
// Little-endian helpers -- mirrors the read pattern already used in
// AIR_Module.ino for the QNH message (low byte read first, then high byte).
// ----------------------------------------------------------------------------
static int16_t readInt16LE() {
  uint8_t lo = can.read();
  uint8_t hi = can.read();
  return (int16_t)((hi << 8) | lo);
}

static int32_t readInt32LE() {
  uint8_t b0 = can.read();
  uint8_t b1 = can.read();
  uint8_t b2 = can.read();
  uint8_t b3 = can.read();
  return (int32_t)(((uint32_t)b3 << 24) | ((uint32_t)b2 << 16) |
                    ((uint32_t)b1 << 8) | (uint32_t)b0);
}

// ----------------------------------------------------------------------------
// setup
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  Wire.begin();  // I2C controller (to Display Feather)

  bool can_success = can.begin(CAN_BAUDRATE);
  if (!can_success && Serial) {
    Serial.println("Could not find CAN Bus MCP2515!");
  }

  can.filter(CAN_AIR_MSG_ID, 0x7FF);
  can.filter(CAN_EULER_MSG_ID, 0x7FF);

  if (Serial) {
    Serial.print("CAN_Feather ");
    Serial.println(VERSION);
  }
}

// ----------------------------------------------------------------------------
// loop
// ----------------------------------------------------------------------------
void loop() {
  int packetSize = can.parsePacket();

  if (packetSize) {
    uint32_t id = can.packetId();

    if (id == CAN_AIR_MSG_ID && can.available() >= 8) {
      displayData.airspeed = readInt16LE();
      displayData.altitude = readInt32LE();
      displayData.vsi = readInt16LE();
      lastAirRxMillis = millis();
    } else if (id == CAN_EULER_MSG_ID && can.available() >= 8) {
      displayData.heading = readInt16LE();
      displayData.pitch = readInt16LE();
      displayData.roll = readInt16LE();
      displayData.turnRate = readInt16LE();
      lastAhrsRxMillis = millis();
    }

    // Discard any unread bytes (unfiltered/unexpected packet, or leftover
    // bytes we didn't consume above).
    while (can.available()) {
      can.read();
    }
  }

  unsigned long now = millis();

  uint16_t validMask = 0;
  if (now - lastAirRxMillis < AIR_STALE_MS) validMask |= VALID_AIR;
  if (now - lastAhrsRxMillis < AHRS_STALE_MS) validMask |= VALID_AHRS;
  displayData.validMask = validMask;

  if (now - lastDisplayPushMillis >= DISPLAY_PUSH_PERIOD_MS) {
    displayData.seq++;
    displayData.checksum = displayDataChecksum(&displayData);

    Wire.beginTransmission(DISPLAY_I2C_ADDR);
    Wire.write((const uint8_t *)&displayData, sizeof(displayData));
    Wire.endTransmission();

    lastDisplayPushMillis = now;
  }
}
