// ============================================================================
// Display_Feather -- drives a KS0108 128x64 GLCD as a backup flight
// instrument readout. Receives a DisplayData struct over I2C (as peripheral)
// from CAN_Feather, which listens on the piavionics CAN bus. See
// CAN_Display/README.md for wiring.
//
// Hardware: Adafruit Feather RP2040 + KS0108 128x64 GLCD (20-pin, 2x KS0108 +
// KS0107 + charge-pump), through two 74HCT541 3.3V->5V level shifters (data
// bus + control lines). R/W is tied to GND on the 5V side (write-only mode
// -- u8g2's KS0108 driver has no read path regardless of wiring). See
// README for the full level-shifter wiring and why.
// ============================================================================

#include <U8g2lib.h>
#include <Wire.h>
#include "DisplayProtocol.h"

#define VERSION "1.0.0"

// ----------------------------------------------------------------------------
// GLCD wiring -- see README.md for the full GLCD-pin -> Feather-pin table.
// R/W is tied to GND in hardware (not driven by a GPIO): u8g2's KS0108
// driver is write-only and never needs it.
// ----------------------------------------------------------------------------
#define GLCD_D0 D5
#define GLCD_D1 D6
#define GLCD_D2 D9
#define GLCD_D3 D10
#define GLCD_D4 D11
#define GLCD_D5 D12
#define GLCD_D6 D24
#define GLCD_D7 D25
#define GLCD_RS A0   // register select (DI)
#define GLCD_E  A2   // enable strobe
#define GLCD_CS1 A3
#define GLCD_CS2 D0
#define GLCD_RST D1

U8G2_KS0108_128X64_F u8g2(
  U8G2_R0,
  GLCD_D0, GLCD_D1, GLCD_D2, GLCD_D3, GLCD_D4, GLCD_D5, GLCD_D6, GLCD_D7,
  /* enable=*/ GLCD_E, /* dc=*/ GLCD_RS,
  /* cs0=*/ U8X8_PIN_NONE, /* cs1=*/ GLCD_CS1, /* cs2=*/ GLCD_CS2,
  /* reset=*/ GLCD_RST);

// ----------------------------------------------------------------------------
// I2C link from CAN_Feather (peripheral)
// ----------------------------------------------------------------------------
static volatile uint8_t rxBuffer[sizeof(DisplayData)];
static volatile uint8_t rxLen = 0;
static volatile bool rxPending = false;

static DisplayData data = {};
static uint8_t lastSeq = 0;
static unsigned long lastGoodRxMillis = 0;

// Link is considered down if no valid packet has arrived recently, even if
// individual CAN messages inside the last-known payload are still "valid"
// per validMask (that mask only reflects CAN staleness as of the last push).
#define LINK_STALE_MS 1000

void onI2CReceive(int numBytes) {
  uint8_t i = 0;
  while (Wire.available() && i < sizeof(DisplayData)) {
    rxBuffer[i++] = Wire.read();
  }
  while (Wire.available()) {
    Wire.read();  // discard anything unexpected
  }
  rxLen = i;
  rxPending = true;
}

// ----------------------------------------------------------------------------
// setup
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);

  Wire.begin(DISPLAY_I2C_ADDR);
  Wire.onReceive(onI2CReceive);

  if (Serial) {
    Serial.print("Display_Feather ");
    Serial.println(VERSION);
  }
}

// ----------------------------------------------------------------------------
// Formatting helpers -- fixed-point x10 CAN fields to one decimal place.
// ----------------------------------------------------------------------------
static void formatX10(char *out, size_t outSize, int16_t x10, const char *unit) {
  int whole = x10 / 10;
  int frac = abs(x10 % 10);
  snprintf(out, outSize, "%d.%d%s", whole, frac, unit);
}

// ----------------------------------------------------------------------------
// loop
// ----------------------------------------------------------------------------
void loop() {
  if (rxPending) {
    noInterrupts();
    uint8_t len = rxLen;
    DisplayData incoming;
    memcpy(&incoming, (const void *)rxBuffer, sizeof(DisplayData));
    rxPending = false;
    interrupts();

    if (len == sizeof(DisplayData) &&
        displayDataChecksum(&incoming) == incoming.checksum) {
      data = incoming;
      lastGoodRxMillis = millis();
    }
  }

  bool linkUp = (millis() - lastGoodRxMillis) < LINK_STALE_MS;

  char iasStr[12], altStr[12], vsiStr[12], hdgStr[12];
  char pitchStr[12], rollStr[12], turnStr[12];

  if (linkUp && (data.validMask & VALID_AIR)) {
    snprintf(iasStr, sizeof(iasStr), "%d kt", data.airspeed);
    snprintf(altStr, sizeof(altStr), "%ld ft", (long)data.altitude);
    snprintf(vsiStr, sizeof(vsiStr), "%d fpm", data.vsi);
  } else {
    strcpy(iasStr, "--- kt");
    strcpy(altStr, "--- ft");
    strcpy(vsiStr, "--- fpm");
  }

  if (linkUp && (data.validMask & VALID_AHRS)) {
    snprintf(hdgStr, sizeof(hdgStr), "%d", data.heading / 10);
    formatX10(pitchStr, sizeof(pitchStr), data.pitch, "");
    formatX10(rollStr, sizeof(rollStr), data.roll, "");
    formatX10(turnStr, sizeof(turnStr), data.turnRate, "/s");
  } else {
    strcpy(hdgStr, "---");
    strcpy(pitchStr, "---");
    strcpy(rollStr, "---");
    strcpy(turnStr, "---");
  }

  // Layout is a first-pass starting point (2 columns x 4 rows, 6x10 font) --
  // exact column widths will want tuning once this is on the real GLCD, since
  // glyph metrics can shift things a few pixels from what's planned here.
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tf);

    u8g2.drawStr(0, 10, "IAS");
    u8g2.drawStr(28, 10, iasStr);
    u8g2.drawStr(66, 10, "PIT");
    u8g2.drawStr(88, 10, pitchStr);

    u8g2.drawStr(0, 26, "ALT");
    u8g2.drawStr(28, 26, altStr);
    u8g2.drawStr(66, 26, "ROL");
    u8g2.drawStr(88, 26, rollStr);

    u8g2.drawStr(0, 42, "VSI");
    u8g2.drawStr(28, 42, vsiStr);
    u8g2.drawStr(66, 42, "HDG");
    u8g2.drawStr(88, 42, hdgStr);

    u8g2.drawStr(0, 58, "TRN");
    u8g2.drawStr(28, 58, turnStr);
    u8g2.drawStr(66, 58, linkUp ? "LINK OK" : "NO LINK");
  } while (u8g2.nextPage());
}
