/*
  Backup_Display.ino - Backup display, flight page
  Hardware: Adafruit Feather RP2040 + Adafruit I2C/SPI LCD backpack (MCP23008)
            + Newhaven NHD-0420DL-FL-GBW 20x4 character LCD
            + Adafruit CAN FeatherWing (MCP2515, CS 7) and the seesaw encoder
  Core:     Earle Philhower "Raspberry Pi Pico/RP2040" (board: Adafruit Feather RP2040)
  Library:  Adafruit LiquidCrystal

  The flight page, fed from the CAN bus (or simulated, see M below):
      IAS 105   ALT  4520
      HHH VS +500 HDG 270
      HHH OAT 12C  G 1.0
      PG 1  IAGE QNH 29.92
  HHH is the mini horizon, 3 x 2 character cells (15 x 16 pixels) drawn with 6 of the 8 custom characters.
  Slot 6 is the "adjusting" marker (a small solid triangle) and slot 7 is the same triangle reversed.

  Encoder (Adafruit seesaw rotary encoder with push button, I2C 0x36), same idea as the EFIS:
      Adjusting (the start state): QNH is selected and a turn changes it, 0.01 inHg per click. The marker next to the
          label is a small solid triangle.
      Push: toggles between adjusting and selecting.
      Selecting: a turn moves the marker to the next field (clockwise: QNH, then PG, then back to QNH). The marker flashes
          between the triangle and its reverse, in the column between the label and the value.
  There is no timeout.

  Two cores: core 0 only receives CAN (setup/loop), core 1 does everything on I2C, the LCD and the encoder
  (setup1/loop1). The LCD writes are slow (tens of ms) and would let the MCP2515's two receive buffers overflow.

  A value that is missing or stale shows as dashes. Valid ages follow Protocol/CAN_PROTOCOL.md.
  Status letters I A G E (IMU, AIR, GPS, EFIS): the letter when the module's heartbeat says OK, a lower case letter when it
  says degraded or starting. When no heartbeat has come for 3 s, or it says failed, the letter flashes between itself and a
  dash for as long as the fault lasts. The letter still says which module.

  USB serial commands (115200), the P R S A U commands act on the simulation:
      M <0|1>  0 = real CAN data, 1 = simulated data (the default is simulated if the CAN chip is not found)
      P <deg>  hold the pitch (positive = nose up)      R <deg>  hold the roll (positive = right wing down)
      C <khz>  I2C clock in kHz (default 400)
      A        back to the moving simulation
      U <ms>   horizon update period in ms (default 100, 0 = as fast as the display allows)
      S <hz>   sweep the roll +/-45 degrees and the pitch +/-10 degrees at this many cycles per second (0 = off)
  Once a second the serial port shows how many horizon updates were made and the mean time of one.
*/

#include <Wire.h>
#include <Adafruit_LiquidCrystal.h>
#include <Adafruit_seesaw.h>
#include "src/Adafruit_MCP2515/src/Adafruit_MCP2515.h"   // patched copy, see Sensors/Libraries/README.md

#define VERSION "1.2.7"

const uint8_t LCD_COLS = 20;
const uint8_t LCD_ROWS = 4;

// CAN
const uint8_t CAN_CS_PIN = 7;
const long CAN_BAUDRATE = 250000;
const uint16_t ID_AIR_DATA = 0x28, ID_OAT = 0x2A, ID_AHRS_ORIENT = 0x48, ID_AHRS_ACCEL = 0x49;
const uint16_t ID_HEARTBEAT_FIRST = 0x700;           // 0x700 EFIS, 0x701 IMU, 0x702 AIR, 0x703 GPS
const int16_t MARKER_INT16 = 0x7FFF;
const int32_t MARKER_INT32 = 0x7FFFFFFF;
const uint32_t MAX_AGE_AIR_MS = 500;                 // "treated as lost after", Protocol/CAN_PROTOCOL.md
const uint32_t MAX_AGE_ATTITUDE_MS = 500;
const uint32_t MAX_AGE_ACCEL_MS = 500;
const uint32_t MAX_AGE_OAT_MS = 1500;
const uint32_t MAX_AGE_HEARTBEAT_MS = 3000;
const uint32_t STATUS_FLASH_MS = 1000;               // a failed module shows its letter, then a dash, each for this long
const float STANDARD_GRAVITY = 9.80665f;
const int VS_STEP_FT_MIN = 50;                       // the vertical speed is shown to the nearest 50 ft/min...
const int VS_STICK_FT_MIN = 40;                      // ...and stays where it is until the raw value is this far from it
                                                     // (the raw value drifts by about +/-16 at rest and a plain
                                                     // rounding flickers whenever it crosses a boundary)
const uint32_t I2C_CLOCK_HZ = 400000;   // 100 kHz made one horizon update take 150 ms, 400 kHz takes 35 ms

// encoder
const uint8_t ENCODER_I2C_ADDRESS = 0x36;
const uint8_t SEESAW_SWITCH_PIN = 24;
const int ENCODER_SIGN = -1;          // a clockwise turn gives a negative count on this encoder
const unsigned long ENCODER_POLL_MS = 10;
const unsigned long MARKER_FLASH_MS = 500;

// fields, in clockwise order around the display
enum Field : uint8_t { FIELD_QNH, FIELD_PAGE, FIELD_COUNT };
const uint8_t MARKER_COL[FIELD_COUNT] = { 14, 2 };   // column between the label and the value, on row 3
const uint8_t MARKER_ADJUSTING = 6;                  // custom character slots
const uint8_t MARKER_REVERSED = 7;
const int QNH_MIN = 2800;                            // inHg x 100
const int QNH_MAX = 3100;
const int PAGE_COUNT = 1;

// mini horizon
const uint8_t HZ_COL = 0;            // top left cell
const uint8_t HZ_ROW = 1;
const uint8_t HZ_CELLS_WIDE = 3;
const uint8_t HZ_CELLS_HIGH = 2;
const int HZ_WIDTH_PX = HZ_CELLS_WIDE * 5;
const int HZ_HEIGHT_PX = HZ_CELLS_HIGH * 8;
const float HZ_PX_PER_DEG = 0.5f;    // pitch scale: the full height shows about +/- 16 degrees
const float HZ_MAX_ROLL_DEG = 60.0f;

Adafruit_LiquidCrystal lcd(0);       // I2C address 0x20 + 0 (A0-A2 not jumpered)
Adafruit_MCP2515 can(CAN_CS_PIN);
Adafruit_seesaw encoder;
bool encoderFound = false;

Field selectedField = FIELD_QNH;
bool adjusting = true;
int qnh = 2992;                      // inHg x 100
int page = 1;

// --- Data shared between the cores ------------------------------------------------------------------------------------
// Core 0 writes, core 1 reads. Each signal is a value, whether the sender marked it valid, and the time it arrived.
struct Signal {
  volatile int32_t value = 0;
  volatile bool valid = false;
  volatile uint32_t time = 0;       // millis() when it last arrived; 0 = never
};
Signal sigAirspeed, sigAltitude, sigVerticalSpeed;   // kt, ft, ft/min
Signal sigHeading, sigPitch, sigRoll;                // deg x 10
Signal sigOat;                                       // deg C x 10
Signal sigAccelZ;                                    // m/s2 x 100
struct Heartbeat {
  volatile uint8_t state = 0;
  volatile uint32_t time = 0;
};
Heartbeat heartbeats[4];                             // indexed by the node number: 0 EFIS, 1 IMU, 2 AIR, 3 GPS
volatile uint32_t canFrames = 0;                     // frames received, for the once-a-second report
bool canUp = false;

// What the page shows: a value and whether it may be shown
struct View {
  bool hasAirspeed = false, hasAltitude = false, hasVerticalSpeed = false, hasHeading = false;
  bool hasAttitude = false, hasOat = false, hasG = false;
  int airspeed = 0, altitude = 0, verticalSpeed = 0, heading = 0, oat = 0;
  float pitch = 0, roll = 0, g = 1;
};

// Core 0 can store a time a moment later than the 'now' core 1 read, so the age can be negative: use a signed difference
bool ageWithin(uint32_t time, uint32_t maxAgeMs, uint32_t now) {
  return time != 0 && (int32_t)(now - time) <= (int32_t)maxAgeMs;
}

bool fresh(const Signal &sig, uint32_t maxAgeMs, uint32_t now) {
  return sig.valid && ageWithin(sig.time, maxAgeMs, now);
}

void viewFromCan(View &v, uint32_t now) {
  v.hasAirspeed = fresh(sigAirspeed, MAX_AGE_AIR_MS, now);
  v.airspeed = sigAirspeed.value;
  v.hasAltitude = fresh(sigAltitude, MAX_AGE_AIR_MS, now);
  v.altitude = sigAltitude.value;
  v.hasVerticalSpeed = fresh(sigVerticalSpeed, MAX_AGE_AIR_MS, now);
  static int shownVs = 0;
  const int rawVs = sigVerticalSpeed.value;
  if (abs(rawVs - shownVs) >= VS_STICK_FT_MIN)
    shownVs = lroundf(rawVs / (float)VS_STEP_FT_MIN) * VS_STEP_FT_MIN;
  v.verticalSpeed = shownVs;
  v.hasHeading = fresh(sigHeading, MAX_AGE_ATTITUDE_MS, now);
  v.heading = ((sigHeading.value + 5) / 10) % 360;
  v.hasAttitude = fresh(sigPitch, MAX_AGE_ATTITUDE_MS, now) && fresh(sigRoll, MAX_AGE_ATTITUDE_MS, now);
  v.pitch = sigPitch.value / 10.0f;
  v.roll = sigRoll.value / 10.0f;
  v.hasOat = fresh(sigOat, MAX_AGE_OAT_MS, now);
  v.oat = lroundf(sigOat.value / 10.0f);
  v.hasG = fresh(sigAccelZ, MAX_AGE_ACCEL_MS, now);
  v.g = sigAccelZ.value / 100.0f / STANDARD_GRAVITY;
}

// --- Simulation (M 1) -------------------------------------------------------------------------------------------------
bool simulate = false;
bool holdPitch = false, holdRoll = false;
float heldPitch = 0, heldRoll = 0;
unsigned long horizonPeriodMs = 100;
float sweepHz = 0;

void viewFromSimulation(View &v, uint32_t now) {
  const float t = now / 1000.0f;
  v.hasAirspeed = v.hasAltitude = v.hasVerticalSpeed = v.hasHeading = v.hasAttitude = v.hasOat = v.hasG = true;
  v.airspeed = 105 + (int)(8 * sin(t * 0.5f));
  v.altitude = 4520 + (int)(150 * sin(t * 0.1f));
  v.verticalSpeed = (int)(500 * cos(t * 0.1f));
  v.heading = ((int)(t * 6) + 270) % 360;
  v.oat = 12;
  v.g = 1.0f + 0.1f * sin(t * 0.7f);
  v.pitch = holdPitch ? heldPitch : 8.0f * sin(t * 0.4f);
  v.roll = holdRoll ? heldRoll : 35.0f * sin(t * 0.25f);
  if (sweepHz > 0) {
    const float phase = 2.0f * (float)M_PI * sweepHz * t;
    v.roll = 45.0f * sin(phase);
    v.pitch = 10.0f * sin(phase * 0.5f);
  }
}

// Write a fixed-width string at col,row (pads/truncates, avoids clear() flicker)
void printAt(uint8_t col, uint8_t row, const char *s, uint8_t width) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "%-*.*s", width, width, s);
  lcd.setCursor(col, row);
  lcd.print(buf);
}

// Draw the horizon into the six custom characters. Ground is filled, sky is blank.
// pitchDeg: positive = nose up (the horizon moves down). rollDeg: positive = right wing down (the right end rises).
void horizonUpdate(float pitchDeg, float rollDeg) {
  static uint8_t shown[HZ_CELLS_WIDE * HZ_CELLS_HIGH][8];
  static bool first = true;

  rollDeg = constrain(rollDeg, -HZ_MAX_ROLL_DEG, HZ_MAX_ROLL_DEG);
  const float slope = tanf(rollDeg * (float)M_PI / 180.0f);
  const float cx = (HZ_WIDTH_PX - 1) / 2.0f;
  const float cy = (HZ_HEIGHT_PX - 1) / 2.0f + pitchDeg * HZ_PX_PER_DEG;

  for (uint8_t cell = 0; cell < HZ_CELLS_WIDE * HZ_CELLS_HIGH; cell++) {
    const int cellX = (cell % HZ_CELLS_WIDE) * 5;
    const int cellY = (cell / HZ_CELLS_WIDE) * 8;
    uint8_t glyph[8];
    for (uint8_t row = 0; row < 8; row++) {
      uint8_t bits = 0;
      for (uint8_t col = 0; col < 5; col++) {
        const float horizonY = cy - slope * ((cellX + col) - cx);
        if ((cellY + row) > horizonY) bits |= 1 << (4 - col);
      }
      glyph[row] = bits;
    }
    if (first || memcmp(glyph, shown[cell], 8) != 0) {
      lcd.createChar(cell, glyph);
      memcpy(shown[cell], glyph, 8);
    }
  }
  first = false;
}

// Put the six horizon characters on the screen (once, after clear)
void horizonDraw() {
  for (uint8_t r = 0; r < HZ_CELLS_HIGH; r++) {
    lcd.setCursor(HZ_COL, HZ_ROW + r);
    for (uint8_t c = 0; c < HZ_CELLS_WIDE; c++) lcd.write((uint8_t)(r * HZ_CELLS_WIDE + c));
  }
}

void serialCommands() {
  while (Serial.available()) {
    char cmd = toupper(Serial.read());
    if (cmd == 'P') { heldPitch = Serial.parseFloat(); holdPitch = true; }
    else if (cmd == 'R') { heldRoll = Serial.parseFloat(); holdRoll = true; }
    else if (cmd == 'U') { horizonPeriodMs = max(0L, Serial.parseInt()); }
    else if (cmd == 'S') { sweepHz = max(0.0f, Serial.parseFloat()); holdPitch = holdRoll = false; }
    else if (cmd == 'C') { Wire.setClock(1000L * Serial.parseInt()); }
    else if (cmd == 'M') { simulate = Serial.parseInt() != 0; }
    else if (cmd == 'A') { holdPitch = holdRoll = false; sweepHz = 0; horizonPeriodMs = 100; }
  }
}

// Small solid right-pointing triangle, and its reverse (the cell filled with the triangle cut out, bottom row blank)
void markerCreate() {
  uint8_t triangle[8] = { 0b00000, 0b01000, 0b01100, 0b01110, 0b01100, 0b01000, 0b00000, 0b00000 };
  uint8_t reversed[8];
  for (uint8_t i = 0; i < 8; i++) reversed[i] = ~triangle[i] & 0x1F;
  reversed[7] = 0;                    // the bottom row stays blank
  lcd.createChar(MARKER_ADJUSTING, triangle);
  lcd.createChar(MARKER_REVERSED, reversed);
}

// Find the encoder on either I2C bus (the STEMMA QT connector is Wire1 on the Feather RP2040)
bool encoderBegin() {
  TwoWire *buses[] = { &Wire, &Wire1 };
  const char *names[] = { "Wire", "Wire1" };
  for (uint8_t i = 0; i < 2; i++) {
    if (i == 1) { Wire1.begin(); Wire1.setClock(I2C_CLOCK_HZ); }
    encoder = Adafruit_seesaw(buses[i]);
    if (encoder.begin(ENCODER_I2C_ADDRESS)) {
      encoder.pinMode(SEESAW_SWITCH_PIN, INPUT_PULLUP);
      encoder.getEncoderDelta();     // discard turns made before now
      Serial.printf("Encoder found on %s, seesaw version %lu\n", names[i], encoder.getVersion() >> 16);
      return true;
    }
  }
  Serial.println("Encoder not found on Wire or Wire1");
  return false;
}

// Apply a turn of 'clicks' (positive = clockwise) to the selected field, or move the selection
void encoderTurned(int clicks) {
  if (adjusting) {
    if (selectedField == FIELD_QNH) qnh = constrain(qnh + clicks, QNH_MIN, QNH_MAX);
    else if (selectedField == FIELD_PAGE) page = constrain(page + clicks, 1, PAGE_COUNT);
  } else {
    int next = ((int)selectedField + clicks) % FIELD_COUNT;
    if (next < 0) next += FIELD_COUNT;
    selectedField = (Field)next;
  }
  Serial.printf("%s field=%d qnh=%d.%02d page=%d\n", adjusting ? "adjust" : "select", selectedField, qnh / 100, qnh % 100, page);
}

void encoderPoll() {
  static unsigned long lastPoll = 0;
  static bool wasPressed = false;
  const unsigned long now = millis();
  if (!encoderFound || now - lastPoll < ENCODER_POLL_MS) return;
  lastPoll = now;

  const int32_t delta = encoder.getEncoderDelta();
  if (delta != 0) encoderTurned(ENCODER_SIGN * delta);

  const bool pressed = !encoder.digitalRead(SEESAW_SWITCH_PIN);   // active low
  if (wasPressed && !pressed) {              // toggled on release, as on the EFIS
    adjusting = !adjusting;
    Serial.println(adjusting ? "adjusting" : "selecting");
  }
  wasPressed = pressed;
}

// --- Core 0: CAN receive ----------------------------------------------------------------------------------------------

void storeSignal(Signal &sig, int32_t value, bool valid, uint32_t now) {
  sig.value = value;
  sig.valid = valid;
  sig.time = now;
}

int16_t int16At(const uint8_t *b, int i) { return (int16_t)(b[i] | (b[i + 1] << 8)); }

void canHandleFrame(uint16_t id, const uint8_t *b, uint32_t now) {
  if (id == ID_AIR_DATA) {
    const int16_t airspeed = int16At(b, 0);
    const int32_t altitude = (int32_t)((uint32_t)b[2] | ((uint32_t)b[3] << 8) | ((uint32_t)b[4] << 16) | ((uint32_t)b[5] << 24));
    const int16_t verticalSpeed = int16At(b, 6);
    storeSignal(sigAirspeed, airspeed, airspeed != MARKER_INT16, now);
    storeSignal(sigAltitude, altitude, altitude != MARKER_INT32, now);
    storeSignal(sigVerticalSpeed, verticalSpeed, verticalSpeed != MARKER_INT16, now);
  } else if (id == ID_OAT) {
    const int16_t oat = int16At(b, 0);
    storeSignal(sigOat, oat, oat != MARKER_INT16, now);
  } else if (id == ID_AHRS_ORIENT) {
    const int16_t heading = int16At(b, 0), pitch = int16At(b, 2), roll = int16At(b, 4);
    storeSignal(sigHeading, heading, heading != MARKER_INT16, now);
    storeSignal(sigPitch, pitch, pitch != MARKER_INT16, now);
    storeSignal(sigRoll, roll, roll != MARKER_INT16, now);
  } else if (id == ID_AHRS_ACCEL) {
    const int16_t accelZ = int16At(b, 4);
    storeSignal(sigAccelZ, accelZ, accelZ != MARKER_INT16, now);
  } else if (id >= ID_HEARTBEAT_FIRST && id < ID_HEARTBEAT_FIRST + 4) {
    Heartbeat &hb = heartbeats[id - ID_HEARTBEAT_FIRST];
    hb.state = b[4];
    hb.time = now;
  }
}

void canPoll() {
  while (can.parsePacket()) {
    uint8_t b[8] = {};
    for (int i = 0; i < 8 && can.available(); i++) b[i] = can.read();
    while (can.available()) can.read();
    if (can.packetRtr()) continue;
    canHandleFrame((uint16_t)can.packetId(), b, millis());
    canFrames = canFrames + 1;
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}   // wait briefly for USB serial
  Serial.print("Backup_Display ");
  Serial.println(VERSION);

  canUp = can.begin(CAN_BAUDRATE);
  Serial.println(canUp ? "CAN bus OK" : "CAN bus (MCP2515) NOT FOUND, simulating");
  simulate = !canUp;
}

void loop() {
  if (canUp) canPoll();
}

// --- Core 1: LCD, encoder, serial commands ----------------------------------------------------------------------------

// The four status letters, one for each module, in the order I A G E
const char STATUS_LETTERS[4] = { 'I', 'A', 'G', 'E' };
const uint8_t STATUS_NODE[4] = { 1, 2, 3, 0 };
const uint8_t STATUS_COL = 6;

// Build the four status characters. A failed module alternates its letter and a dash.
void statusText(char *out, uint32_t now) {
  for (uint8_t i = 0; i < 4; i++) {
    const Heartbeat &hb = heartbeats[STATUS_NODE[i]];
    const bool failed = !ageWithin(hb.time, MAX_AGE_HEARTBEAT_MS, now) || hb.state == 3;
    if (simulate) {
      out[i] = STATUS_LETTERS[i];
    } else if (failed) {
      out[i] = ((now / STATUS_FLASH_MS) % 2 == 0) ? STATUS_LETTERS[i] : '-';
    } else {
      out[i] = (hb.state == 1) ? STATUS_LETTERS[i] : tolower(STATUS_LETTERS[i]);
    }
  }
  out[4] = 0;
}

void setup1() {
  Wire.begin();
  if (!lcd.begin(LCD_COLS, LCD_ROWS)) {
    Serial.println("LCD backpack not found - check wiring/address");
    while (1) delay(100);
  }
  Wire.setClock(I2C_CLOCK_HZ);
  lcd.setBacklight(HIGH);
  lcd.clear();
  markerCreate();
  horizonUpdate(0, 0);
  horizonDraw();
  encoderFound = encoderBegin();
}

void loop1() {
  static unsigned long lastText = 0, lastHorizon = 0, lastReport = 0;
  static unsigned long updates = 0, updateMicros = 0, lastFrames = 0;
  static bool horizonShown = true;
  const unsigned long now = millis();
  serialCommands();
  encoderPoll();

  View v;
  if (simulate) viewFromSimulation(v, now);
  else viewFromCan(v, now);

  if (now - lastHorizon >= horizonPeriodMs) {
    lastHorizon = now;
    if (v.hasAttitude) {
      if (!horizonShown) { horizonDraw(); horizonShown = true; }
      const unsigned long start = micros();
      horizonUpdate(v.pitch, v.roll);
      updateMicros += micros() - start;
      updates++;
    } else if (horizonShown) {
      printAt(HZ_COL, HZ_ROW, "ATT", HZ_CELLS_WIDE);
      printAt(HZ_COL, HZ_ROW + 1, "---", HZ_CELLS_WIDE);
      horizonShown = false;
    }
  }

  if (now - lastReport >= 1000) {
    const uint32_t frames = canFrames;
    Serial.printf("%lu horizon updates/s, %lu us each, CAN %lu frames/s\n", updates, updates ? updateMicros / updates : 0,
                  (unsigned long)(frames - lastFrames));
    lastFrames = frames;
    updates = updateMicros = 0;
    lastReport = now;
  }

  if (now - lastText < 100) return;            // the text rows refresh at 10 Hz
  lastText = now;

  char buf[LCD_COLS + 1], a[8], b[8];
  if (v.hasAirspeed) snprintf(a, sizeof(a), "%3d", v.airspeed); else snprintf(a, sizeof(a), "---");
  if (v.hasAltitude) snprintf(b, sizeof(b), "%5d", v.altitude); else snprintf(b, sizeof(b), "-----");
  snprintf(buf, sizeof(buf), "IAS %-3s   ALT %5s", a, b);
  printAt(0, 0, buf, 20);

  if (v.hasVerticalSpeed) snprintf(a, sizeof(a), "%+5d", v.verticalSpeed); else snprintf(a, sizeof(a), "-----");
  if (v.hasHeading) snprintf(b, sizeof(b), "%03d", v.heading); else snprintf(b, sizeof(b), "---");
  snprintf(buf, sizeof(buf), " VS %5s HDG %s", a, b);
  printAt(3, 1, buf, 17);

  if (v.hasOat) snprintf(a, sizeof(a), "%2d", v.oat); else snprintf(a, sizeof(a), "--");
  if (v.hasG) snprintf(b, sizeof(b), "%.1f", v.g); else snprintf(b, sizeof(b), "---");
  snprintf(buf, sizeof(buf), " OAT %sC  G %s", a, b);
  printAt(3, 2, buf, 17);

  // Row 3: PG, the module status letters and QNH. PG and QNH each have a marker column between the label and the value.
  char markers[FIELD_COUNT];
  for (uint8_t f = 0; f < FIELD_COUNT; f++) {
    if (f != selectedField) markers[f] = ' ';
    else if (adjusting) markers[f] = (char)MARKER_ADJUSTING;
    else markers[f] = ((now / MARKER_FLASH_MS) % 2 == 0) ? (char)MARKER_ADJUSTING : (char)MARKER_REVERSED;
  }
  char status[5];
  statusText(status, now);
  snprintf(buf, sizeof(buf), "PG %d  %s QNH %d.%02d", page, status, qnh / 100, qnh % 100);
  buf[MARKER_COL[FIELD_PAGE]] = markers[FIELD_PAGE];
  buf[MARKER_COL[FIELD_QNH]] = markers[FIELD_QNH];
  printAt(0, 3, buf, 20);
}
