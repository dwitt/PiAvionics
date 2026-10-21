/*
  Backup_Display.ino - Backup display, flight page
  Hardware: Adafruit Feather RP2040 + Adafruit I2C/SPI LCD backpack (MCP23008)
            + Newhaven NHD-0420DL-FL-GBW 20x4 character LCD
            + Adafruit CAN FeatherWing (MCP2515, CS 7) and the seesaw encoder
  Core:     Earle Philhower "Raspberry Pi Pico/RP2040" (board: Adafruit Feather RP2040)
  Library:  Adafruit LiquidCrystal

  The flight page, fed from the CAN bus (or simulated, see M below):
      IAS 105 HHH ALT 4520
      VS +500 HHH  HDG 270
      OAT 12C[ o ]   G 1.0       (the o is the slip ball)
      PG 1   IAGE   29.92?
  HHH is the mini horizon, 3 x 2 character cells (15 x 16 pixels, drawn as 17 x 17 with the gaps between the cells counted) drawn with 6 of the 8 custom characters.
  Slot 6 is the "adjusting" marker (a small solid triangle) and slot 7 is the slip ball: the ball cell is drawn as a custom
  character with the ball at the right pixel offset, the other two cells of the track are blank.

  Encoder (Adafruit seesaw rotary encoder with push button, I2C 0x36), same idea as the EFIS:
      Adjusting (the start state): QNH is selected and a turn changes it, 0.01 inHg per click. The marker next to the
          label is a small solid triangle.
      Push: toggles between adjusting and selecting.
      Selecting: a turn moves the marker to the next field (clockwise: QNH, then PG, then back to QNH). The marker flashes
          between the triangle and its reverse, in the column between the label and the value.
  There is no timeout.

  Pages (PG, bottom left of every page; adjusting it changes the page): 1 Flight, 2 GPS, 3 System. An Engine page is to come.
  The fields on each page, in clockwise order from the lower right:
      Flight:  QNH (lower right, only on pages that show altitude), PG
      GPS:     PG
      System:  PG, BL (backlight on/off, upper left)
  Brightness and contrast cannot be set from software with this backpack: the backlight is on/off and the contrast is a pot.

  QNH (EFIS/docs/QNH_PLAN.md): the value is shared with the other devices over CAN. This display sends on 0x2D, and adopts a
  valid frame from any ID in 0x2C to 0x2F. A change from the knob is sent at once (at most one frame per 20 ms). A set value
  is sent again after 1000 ms +/- 20 ms without a QNH frame on the bus. For the first 2 s after power-up nothing is sent
  unless the knob is turned, to give the others time to speak. With nothing heard the QNH is 29.92 followed by ? (not set,
  and never sent). A ! after the value means the AIR module (0x2B) reports a different QNH for more than 2 s.

  Two cores: core 0 only receives CAN (setup/loop), core 1 does everything on I2C, the LCD and the encoder
  (setup1/loop1). The LCD writes are slow (tens of ms) and would let the MCP2515's two receive buffers overflow.

  A value that is missing or stale shows as dashes. Valid ages follow Protocol/CAN_PROTOCOL.md.
  Status letters I A G E (IMU, AIR, GPS, EFIS): the letter when the module's heartbeat says OK, a lower case letter when it
  says degraded or starting. When no heartbeat has come for 3 s, or it says failed, the lower case letter flashes against a
  dash: quickly for the first 5 s as the warning, then slowly for as long as the fault lasts. The letter still says which
  module.

  USB serial commands (115200), the P R S A U commands act on the simulation:
      B <0|1>  backlight off / on
      M <0|1>  0 = real CAN data, 1 = simulated data (the default is simulated if the CAN chip is not found)
      P <deg>  hold the pitch (positive = nose up)      R <deg>  hold the roll (positive turns the horizon clockwise, as on the EFIS)
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

#define VERSION "1.6.5"

const uint8_t LCD_COLS = 20;
const uint8_t LCD_ROWS = 4;

// CAN
const uint8_t CAN_CS_PIN = 7;
const long CAN_BAUDRATE = 250000;
const uint16_t ID_AIR_DATA = 0x28, ID_OAT = 0x2A, ID_AHRS_ORIENT = 0x48, ID_AHRS_ACCEL = 0x49;
const uint16_t ID_TIME_SYNC = 0x19, ID_GPS_POSITION = 0x63, ID_GPS_SPEED_TRACK = 0x64, ID_GPS_STATUS = 0x65;
const uint16_t ID_HEARTBEAT_FIRST = 0x700;           // 0x700 EFIS, 0x701 IMU, 0x702 AIR, 0x703 GPS
const int16_t MARKER_INT16 = 0x7FFF;
const int32_t MARKER_INT32 = 0x7FFFFFFF;
const uint32_t MAX_AGE_AIR_MS = 500;                 // "treated as lost after", Protocol/CAN_PROTOCOL.md
const uint32_t MAX_AGE_ATTITUDE_MS = 500;
const uint32_t MAX_AGE_ACCEL_MS = 500;
const uint32_t MAX_AGE_OAT_MS = 1500;
const uint32_t MAX_AGE_GPS_MS = 3500;               // the GPS frames, the status and the time sync come once a second
const uint32_t MAX_AGE_HEARTBEAT_MS = 3000;
const int GPS_TRACK_MIN_SPEED_KT = 2;                // the module holds the track at 0 below this ground speed
const uint32_t STATUS_WARNING_MS = 5000;             // a module that has just failed flashes quickly for this long...
const uint32_t STATUS_WARNING_FLASH_MS = 500;        // (letter, then dash, each for this long)
const uint32_t STATUS_FLASH_MS = 1500;               // ...then slowly
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
enum Field : uint8_t { FIELD_QNH, FIELD_PAGE, FIELD_BACKLIGHT, FIELD_COUNT };
struct Position { uint8_t col, row; };
const Position MARKER_POS[FIELD_COUNT] = { { 13, 3 }, { 2, 3 }, { 2, 0 } };   // the column between the label and the value
const Field FLIGHT_FIELDS[] = { FIELD_QNH, FIELD_PAGE };           // clockwise from the lower right
const Field GPS_FIELDS[] = { FIELD_PAGE };
const Field SYSTEM_FIELDS[] = { FIELD_PAGE, FIELD_BACKLIGHT };
const uint8_t PAGE_FLIGHT = 1, PAGE_GPS = 2, PAGE_SYSTEM = 3;
const uint8_t MARKER_ADJUSTING = 6;                  // custom character slots
const uint8_t SLIP_GLYPH = 7;                        // custom character slot of the slip ball
const uint8_t SLIP_COL = 8, SLIP_ROW = 2;            // the slip track: 3 cells under the horizon
const int SLIP_SIGN = 1;                             // -1 if the ball moves the wrong way (depends on how the IMU is mounted)
const float SLIP_G_PER_STEP = 0.03f;                 // lateral acceleration for one pixel step; 7 steps either side of centre
const int SLIP_POSITIONS = 15;                       // 5 in each of the 3 cells (see slipDraw)
const float SLIP_FILTER = 0.2f;                      // low pass at each 100 ms redraw (about 0.5 s)
const int QNH_MIN = 2800;                            // inHg x 100
const int QNH_MAX = 3100;
const int PAGE_COUNT = 3;

// mini horizon
const uint8_t HZ_COL = 8;            // top left cell: in the middle of the screen, rows 0 and 1
const uint8_t HZ_ROW = 0;
const uint8_t HZ_CELLS_WIDE = 3;
const uint8_t HZ_CELLS_HIGH = 2;
const int HZ_GAP_PX = 1;             // the blank space between characters, counted as part of the picture but never drawn
const int HZ_WIDTH_PX = HZ_CELLS_WIDE * 5 + (HZ_CELLS_WIDE - 1) * HZ_GAP_PX;
const int HZ_HEIGHT_PX = HZ_CELLS_HIGH * 8 + (HZ_CELLS_HIGH - 1) * HZ_GAP_PX;
const float HZ_PX_PER_DEG = 0.5f;    // pitch scale: the full height shows about +/- 16 degrees
const float HZ_MAX_ROLL_DEG = 60.0f;

Adafruit_LiquidCrystal lcd(0);       // I2C address 0x20 + 0 (A0-A2 not jumpered)
Adafruit_MCP2515 can(CAN_CS_PIN);
Adafruit_seesaw encoder;
bool encoderFound = false;

Field selectedField = FIELD_QNH;
bool adjusting = true;
int page = PAGE_FLIGHT;
bool backlightOn = true;

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
Signal sigAccelY, sigAccelZ;                         // m/s2 x 100
Signal sigGpsLat, sigGpsLon;                         // deg x 1e6
Signal sigGpsSpeed, sigGpsAlt, sigGpsTrack;          // kt, ft, deg (magnetic)
Signal sigGpsHour, sigGpsMinute;                     // UTC
Signal sigGpsFixType, sigGpsSats, sigGpsHdop;        // fix type 1 none / 2 2D / 3 3D, satellites, HDOP x 100
struct Heartbeat {
  volatile uint8_t state = 0;
  volatile uint32_t time = 0;
};
Heartbeat heartbeats[4];                             // indexed by the node number: 0 EFIS, 1 IMU, 2 AIR, 3 GPS
volatile uint32_t canFrames = 0;                     // frames received, for the once-a-second report
bool canUp = false;
volatile int qnhValue = 2992;                        // inHg x 100; core 0 writes, core 1 reads (see QNH below)
volatile bool qnhIsSet = false;                      // false: 29.92 "not set", never sent
volatile bool qnhMismatch = false;

// What the page shows: a value and whether it may be shown
struct View {
  bool hasAirspeed = false, hasAltitude = false, hasVerticalSpeed = false, hasHeading = false;
  bool hasAttitude = false, hasOat = false, hasG = false, hasAccelY = false;
  int airspeed = 0, altitude = 0, verticalSpeed = 0, heading = 0, oat = 0;
  float pitch = 0, roll = 0, g = 1, accelYg = 0;
  bool hasPosition = false, hasGpsSpeed = false, hasGpsTrack = false, hasGpsAlt = false, hasTime = false, hasFixStatus = false;
  double lat = 0, lon = 0;
  int gpsSpeed = 0, gpsTrack = 0, gpsAlt = 0, hour = 0, minute = 0, fixType = 1, sats = 0, hdop100 = 0;
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
  v.hasAccelY = fresh(sigAccelY, MAX_AGE_ACCEL_MS, now);
  v.accelYg = sigAccelY.value / 100.0f / STANDARD_GRAVITY;

  v.hasPosition = fresh(sigGpsLat, MAX_AGE_GPS_MS, now) && fresh(sigGpsLon, MAX_AGE_GPS_MS, now);
  v.lat = sigGpsLat.value / 1e6;
  v.lon = sigGpsLon.value / 1e6;
  v.hasGpsSpeed = fresh(sigGpsSpeed, MAX_AGE_GPS_MS, now);
  v.gpsSpeed = sigGpsSpeed.value;
  v.hasGpsTrack = fresh(sigGpsTrack, MAX_AGE_GPS_MS, now) && v.hasGpsSpeed && v.gpsSpeed >= GPS_TRACK_MIN_SPEED_KT;
  v.gpsTrack = sigGpsTrack.value;
  v.hasGpsAlt = fresh(sigGpsAlt, MAX_AGE_GPS_MS, now);
  v.gpsAlt = sigGpsAlt.value;
  v.hasTime = fresh(sigGpsHour, MAX_AGE_GPS_MS, now) && fresh(sigGpsMinute, MAX_AGE_GPS_MS, now);
  v.hour = sigGpsHour.value;
  v.minute = sigGpsMinute.value;
  v.hasFixStatus = fresh(sigGpsFixType, MAX_AGE_GPS_MS, now);
  v.fixType = sigGpsFixType.value;
  v.sats = sigGpsSats.value;
  v.hdop100 = sigGpsHdop.value;
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
  v.hasPosition = v.hasGpsSpeed = v.hasGpsTrack = v.hasGpsAlt = v.hasTime = v.hasFixStatus = true;
  v.lat = 43.6532;
  v.lon = -79.3832;
  v.gpsSpeed = v.airspeed;
  v.gpsTrack = v.heading;
  v.gpsAlt = v.altitude - 20;
  v.hour = 14;
  v.minute = 32;
  v.fixType = 3;
  v.sats = 9;
  v.hdop100 = 110;
  v.airspeed = 105 + (int)(8 * sin(t * 0.5f));
  v.altitude = 4520 + (int)(150 * sin(t * 0.1f));
  v.verticalSpeed = (int)(500 * cos(t * 0.1f));
  v.heading = ((int)(t * 6) + 270) % 360;
  v.oat = 12;
  v.g = 1.0f + 0.1f * sin(t * 0.7f);
  v.hasAccelY = true;
  v.accelYg = 0.12f * sin(t * 0.6f);
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

// Draw the horizon into the six custom characters. Ground is filled, sky is blank, with a one pixel box round the edge.
// pitchDeg: positive = nose up (the horizon moves down). rollDeg: positive turns the horizon clockwise (the right end drops), the same as the EFIS page does with the bus value.
void horizonUpdate(float pitchDeg, float rollDeg) {
  static uint8_t shown[HZ_CELLS_WIDE * HZ_CELLS_HIGH][8];
  static bool first = true;

  rollDeg = constrain(rollDeg, -HZ_MAX_ROLL_DEG, HZ_MAX_ROLL_DEG);
  const float slope = -tanf(rollDeg * (float)M_PI / 180.0f);   // screen y points down: a clockwise turn lowers the right end
  const float cx = (HZ_WIDTH_PX - 1) / 2.0f;
  const float cy = (HZ_HEIGHT_PX - 1) / 2.0f + pitchDeg * HZ_PX_PER_DEG;

  for (uint8_t cell = 0; cell < HZ_CELLS_WIDE * HZ_CELLS_HIGH; cell++) {
    const int cellX = (cell % HZ_CELLS_WIDE) * (5 + HZ_GAP_PX);
    const int cellY = (cell / HZ_CELLS_WIDE) * (8 + HZ_GAP_PX);
    uint8_t glyph[8];
    for (uint8_t row = 0; row < 8; row++) {
      uint8_t bits = 0;
      for (uint8_t col = 0; col < 5; col++) {
        const float horizonY = cy - slope * ((cellX + col) - cx);
        if ((cellY + row) > horizonY) bits |= 1 << (4 - col);
      }
      // one pixel box round the edge of the six cells
      const uint8_t cellCol = cell % HZ_CELLS_WIDE, cellRow = cell / HZ_CELLS_WIDE;
      if (cellCol == 0) bits |= 0b10000;
      if (cellCol == HZ_CELLS_WIDE - 1) bits |= 0b00001;
      if ((cellRow == 0 && row == 0) || (cellRow == HZ_CELLS_HIGH - 1 && row == 7)) bits = 0b11111;
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
    else if (cmd == 'B') { backlightOn = Serial.parseInt() != 0; lcd.setBacklight(backlightOn ? HIGH : LOW); }
    else if (cmd == 'M') { simulate = Serial.parseInt() != 0; }
    else if (cmd == 'A') { holdPitch = holdRoll = false; sweepHz = 0; horizonPeriodMs = 100; }
  }
}

// Small solid right-pointing triangle for the "adjusting" marker
void markerCreate() {
  uint8_t triangle[8] = { 0b00000, 0b01000, 0b01100, 0b01110, 0b01100, 0b01000, 0b00000, 0b00000 };
  lcd.createChar(MARKER_ADJUSTING, triangle);
}

// The slip ball: position 0 to 14 across the three cells (7 is the middle). The ball is 3 pixels wide and is drawn in one
// character cell. Next to a gap between characters it may have a third of it in the gap, where it is not drawn: in each cell
// the ball's left pixel is at -1, 0, 1, 2 or 3 and the part outside the cell is clipped. Going on to the next cell the ball
// comes back with a third missing on the opposite side.
int slipShown = -1;                                  // the position on the screen; -1 = not drawn (draw it again)

void slipDraw(int position) {
  static char cells[4] = "   ";
  if (position == slipShown) return;
  slipShown = position;
  const uint8_t ballRows[5] = { 0b010, 0b111, 0b111, 0b111, 0b010 };
  const uint8_t cell = position / 5;
  const int left = position % 5 - 1;                              // the ball's left pixel in the cell: -1 to 3
  uint8_t glyph[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  for (uint8_t i = 0; i < 5; i++) {                               // a filled ball, 3 wide and 5 high
    for (uint8_t j = 0; j < 3; j++) {
      const int column = left + j;
      if ((ballRows[i] & (0b100 >> j)) && column >= 0 && column <= 4) glyph[2 + i] |= 1 << (4 - column);
    }
  }
  lcd.createChar(SLIP_GLYPH, glyph);
  for (uint8_t i = 0; i < 3; i++) cells[i] = (i == cell) ? (char)SLIP_GLYPH : ' ';
  printAt(SLIP_COL, SLIP_ROW, cells, 3);
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

const Field *pageFields(int forPage, int &count) {
  if (forPage == PAGE_SYSTEM) { count = sizeof(SYSTEM_FIELDS) / sizeof(SYSTEM_FIELDS[0]); return SYSTEM_FIELDS; }
  if (forPage == PAGE_GPS) { count = sizeof(GPS_FIELDS) / sizeof(GPS_FIELDS[0]); return GPS_FIELDS; }
  count = sizeof(FLIGHT_FIELDS) / sizeof(FLIGHT_FIELDS[0]);
  return FLIGHT_FIELDS;
}

// Apply a turn of 'clicks' (positive = clockwise) to the selected field, or move the selection
void encoderTurned(int clicks) {
  if (adjusting) {
    if (selectedField == FIELD_QNH) {
      rp2040.fifo.push_nb((uint32_t)clicks);   // core 0 owns the QNH
    } else if (selectedField == FIELD_PAGE) {
      page = ((page - 1 + clicks) % PAGE_COUNT + PAGE_COUNT) % PAGE_COUNT + 1;
    } else if (selectedField == FIELD_BACKLIGHT) {
      if (clicks % 2 != 0) backlightOn = !backlightOn;   // each click in either direction changes the state
      lcd.setBacklight(backlightOn ? HIGH : LOW);
    }
  } else {
    int count;
    const Field *fields = pageFields(page, count);
    int index = 0;
    for (int i = 0; i < count; i++) if (fields[i] == selectedField) index = i;
    index = ((index + clicks) % count + count) % count;
    selectedField = fields[index];
  }
  Serial.printf("%s field=%d qnh=%d.%02d page=%d backlight=%d\n", adjusting ? "adjust" : "select", selectedField, qnhValue / 100,
                qnhValue % 100, page, backlightOn);
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

// --- QNH, owned by core 0 ---------------------------------------------------------------------------------------------
const uint16_t ID_STATIC_PRESSURE = 0x2B;            // AIR report of the QNH it uses, bytes 5-6 (inHg x 100) and 7 (flags)
const uint16_t ID_QNH_FIRST = 0x2C, ID_QNH_LAST = 0x2F, ID_QNH_SEND = 0x2D;
const uint32_t QNH_LISTEN_MS = 2000;
const uint32_t QNH_SILENCE_MS = 1000;
const uint32_t QNH_SILENCE_JITTER_MS = 20;           // plus or minus, drawn once for each silent period
const uint32_t QNH_MIN_SEND_GAP_MS = 20;             // never more than 50 frames a second
const uint32_t QNH_MISMATCH_AFTER_MS = 2000;
const uint32_t QNH_AIR_REPORT_MAX_AGE_MS = 1500;
const uint8_t AIR_FLAG_QNH_NOT_SET = 0x01;
const float INHG_X100_TO_HPA = 1.0f / 2.95299875f;

static bool qnhPending = false;                      // a changed value waiting to go out
static bool qnhEverSent = false;
static uint32_t qnhLastSend = 0, qnhLastActivity = 0, qnhSilenceDelay = QNH_SILENCE_MS;   // activity = QNH frame heard or sent
static int airQnh = -1;                              // -1 = the AIR module sent the invalid marker
static bool airNotSet = false;
static uint32_t airReportTime = 0, mismatchSince = 0;

uint32_t qnhDrawSilenceDelay() {
  return QNH_SILENCE_MS - QNH_SILENCE_JITTER_MS + rp2040.hwrand32() % (2 * QNH_SILENCE_JITTER_MS + 1);
}

// The receiver checks of the plan: 28.00 to 31.00 inHg, and the hPa field agrees with the inHg field to within 1 hPa
bool qnhFrameValid(int hpa, int x4) {
  if (x4 < QNH_MIN * 4 || x4 > QNH_MAX * 4) return false;
  return fabsf(x4 * 0.25f * INHG_X100_TO_HPA - hpa) <= 1.0f;
}

void qnhHeard(const uint8_t *b, uint32_t now) {
  const int hpa = int16At(b, 0), x4 = int16At(b, 2);
  if (!qnhFrameValid(hpa, x4)) return;
  qnhValue = x4 / 4;
  qnhIsSet = true;
  qnhPending = false;                                // whatever was waiting is older than this frame
  qnhLastActivity = now;
  qnhSilenceDelay = qnhDrawSilenceDelay();
}

void qnhAirReport(const uint8_t *b, uint32_t now) {
  const int16_t inUse = int16At(b, 5);
  airQnh = (inUse == MARKER_INT16) ? -1 : inUse;
  airNotSet = (b[7] & AIR_FLAG_QNH_NOT_SET) != 0;
  airReportTime = now;
}

void qnhSend(uint32_t now) {
  if (canUp) {
    const int16_t hpa = lroundf(qnhValue * INHG_X100_TO_HPA);
    const int16_t x4 = qnhValue * 4;
    const uint8_t frame[8] = { (uint8_t)(hpa & 0xFF), (uint8_t)(hpa >> 8), (uint8_t)(x4 & 0xFF), (uint8_t)(x4 >> 8), 0, 0, 0, 0 };
    can.beginPacket(ID_QNH_SEND);
    can.write(frame, 8);
    can.endPacket();
  }
  qnhPending = false;
  qnhEverSent = true;
  qnhLastSend = now;
  qnhLastActivity = now;
  qnhSilenceDelay = qnhDrawSilenceDelay();
}

// Call every pass of loop() on core 0
void qnhService(uint32_t now) {
  // turns of the knob from core 1, in clicks
  while (rp2040.fifo.available()) {
    const int clicks = (int32_t)rp2040.fifo.pop();
    const int next = constrain(qnhValue + clicks, QNH_MIN, QNH_MAX);
    if (next != qnhValue) {
      qnhValue = next;
      qnhIsSet = true;
      qnhPending = true;
    }
  }

  const bool guardOk = !qnhEverSent || now - qnhLastSend >= QNH_MIN_SEND_GAP_MS;
  const bool silent = qnhIsSet && now >= QNH_LISTEN_MS && now - qnhLastActivity >= qnhSilenceDelay;
  if (guardOk && (qnhPending || silent)) qnhSend(now);

  // mismatch with the AIR module's report
  const bool airKnown = airReportTime != 0 && (int32_t)(now - airReportTime) <= (int32_t)QNH_AIR_REPORT_MAX_AGE_MS
                        && (airQnh >= 0 || airNotSet);
  const bool differs = qnhIsSet && airKnown && (airNotSet || airQnh != qnhValue);
  if (!differs) {
    mismatchSince = 0;
    qnhMismatch = false;
  } else {
    if (mismatchSince == 0) mismatchSince = now;
    qnhMismatch = now - mismatchSince > QNH_MISMATCH_AFTER_MS;
  }
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
    const int16_t accelY = int16At(b, 2), accelZ = int16At(b, 4);
    storeSignal(sigAccelY, accelY, accelY != MARKER_INT16, now);
    storeSignal(sigAccelZ, accelZ, accelZ != MARKER_INT16, now);
  } else if (id == ID_GPS_POSITION) {
    storeSignal(sigGpsLat, (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24)), true, now);
    storeSignal(sigGpsLon, (int32_t)((uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24)), true, now);
  } else if (id == ID_GPS_SPEED_TRACK) {
    storeSignal(sigGpsSpeed, int16At(b, 0), true, now);
    storeSignal(sigGpsAlt, int16At(b, 2), true, now);
    storeSignal(sigGpsTrack, int16At(b, 6), true, now);          // the magnetic track
  } else if (id == ID_GPS_STATUS) {
    storeSignal(sigGpsFixType, b[1], true, now);
    storeSignal(sigGpsSats, b[2], true, now);
    storeSignal(sigGpsHdop, int16At(b, 4), true, now);
  } else if (id == ID_TIME_SYNC) {
    storeSignal(sigGpsHour, b[3], true, now);
    storeSignal(sigGpsMinute, b[4], true, now);
  } else if (id == ID_STATIC_PRESSURE) {
    qnhAirReport(b, now);
  } else if (id >= ID_QNH_FIRST && id <= ID_QNH_LAST) {
    qnhHeard(b, now);
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
  qnhService(millis());
}

// --- Core 1: LCD, encoder, serial commands ----------------------------------------------------------------------------

// The four status letters, one for each module, in the order I A G E
const char STATUS_LETTERS[4] = { 'I', 'A', 'G', 'E' };
const uint8_t STATUS_NODE[4] = { 1, 2, 3, 0 };
const uint8_t STATUS_COL = 6;

// Build the four status characters. A failed module alternates its letter and a dash, quickly at first.
void statusText(char *out, uint32_t now) {
  static bool wasFailed[4] = { false, false, false, false };
  static uint32_t failedSince[4] = { 0, 0, 0, 0 };
  for (uint8_t i = 0; i < 4; i++) {
    const Heartbeat &hb = heartbeats[STATUS_NODE[i]];
    const bool failed = !ageWithin(hb.time, MAX_AGE_HEARTBEAT_MS, now) || hb.state == 3;
    if (failed && !wasFailed[i]) failedSince[i] = now;
    wasFailed[i] = failed;
    if (simulate) {
      out[i] = STATUS_LETTERS[i];
    } else if (failed) {
      const uint32_t half = (now - failedSince[i] < STATUS_WARNING_MS) ? STATUS_WARNING_FLASH_MS : STATUS_FLASH_MS;
      out[i] = ((now / half) % 2 == 0) ? tolower(STATUS_LETTERS[i]) : '-';
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

// The marker for a field: blank when it is not selected, the triangle when adjusting, flashing between the triangle and its
// reverse when selecting
char markerChar(Field f, uint32_t now) {
  if (f != selectedField) return ' ';
  if (adjusting) return (char)MARKER_ADJUSTING;
  return ((now / MARKER_FLASH_MS) % 2 == 0) ? (char)MARKER_ADJUSTING : ' ';   // on trial: flash against a blank (the reverse in slot 7 is not used)
}

void putMarker(char *buf, Field f, uint32_t now) { buf[MARKER_POS[f].col] = markerChar(f, now); }

volatile uint32_t canRate = 0;                       // CAN frames in the last second, for the System page

void drawFlightText(const View &v, uint32_t now) {
  char buf[LCD_COLS + 1], a[8], b[8];
  // Rows 0 and 1 leave columns 8 to 10 for the horizon and row 2 leaves them for the slip ball
  if (v.hasAirspeed) snprintf(a, sizeof(a), "%3d", v.airspeed); else snprintf(a, sizeof(a), "---");
  if (v.hasAltitude) snprintf(b, sizeof(b), "%5d", v.altitude); else snprintf(b, sizeof(b), "-----");
  snprintf(buf, sizeof(buf), "IAS %-3s", a);
  printAt(0, 0, buf, 8);
  snprintf(buf, sizeof(buf), " ALT%s", b);
  printAt(11, 0, buf, 9);

  if (v.hasVerticalSpeed) snprintf(a, sizeof(a), "%+5d", v.verticalSpeed); else snprintf(a, sizeof(a), "-----");
  if (v.hasHeading) snprintf(b, sizeof(b), "%03d", v.heading); else snprintf(b, sizeof(b), "---");
  snprintf(buf, sizeof(buf), "VS%s", a);
  printAt(0, 1, buf, 8);
  snprintf(buf, sizeof(buf), " HDG %s", b);
  printAt(12, 1, buf, 8);

  if (v.hasOat) snprintf(a, sizeof(a), "%2d", v.oat); else snprintf(a, sizeof(a), "--");
  if (v.hasG) snprintf(b, sizeof(b), "%4.1f", v.g); else snprintf(b, sizeof(b), " ---");
  snprintf(buf, sizeof(buf), "OAT %sC[", a);                  // [ and ] are the ends of the slip track
  printAt(0, 2, buf, 8);
  snprintf(buf, sizeof(buf), "]   G%s", b);
  printAt(11, 2, buf, 9);

  // Slip ball: lateral acceleration, low passed, 0.05 g to a pixel step
  static float slip = 0;
  if (v.hasAccelY) {
    slip += (v.accelYg - slip) * SLIP_FILTER;
    slipDraw(constrain(SLIP_POSITIONS / 2 + lroundf(SLIP_SIGN * slip / SLIP_G_PER_STEP), 0, SLIP_POSITIONS - 1));
  } else {
    slip = 0;
    printAt(SLIP_COL, SLIP_ROW, "---", 3);
    slipShown = -1;                            // draw the ball again when the data returns
  }

  // Row 3: PG, the module status letters and the QNH (no label). The marker column is just before each value.
  char status[5];
  statusText(status, now);
  const int qnhNow = qnhValue;
  const char qnhFlag = !qnhIsSet ? '?' : (qnhMismatch ? '!' : ' ');
  snprintf(buf, sizeof(buf), "PG %d   %c%c%c%c   %d.%02d%c", page, status[0], status[1], status[2], status[3], qnhNow / 100,
           qnhNow % 100, qnhFlag);
  putMarker(buf, FIELD_PAGE, now);
  putMarker(buf, FIELD_QNH, now);
  printAt(0, 3, buf, 20);
}

void drawGpsText(const View &v, uint32_t now) {
  char buf[LCD_COLS + 1], a[12], b[12], c[12];
  // Same grid as the Flight page: GPS and DATA take the place of the horizon in columns 8 to 11
  if (v.hasGpsSpeed) snprintf(a, sizeof(a), "%3d", v.gpsSpeed); else snprintf(a, sizeof(a), "---");
  if (v.hasGpsAlt) snprintf(b, sizeof(b), "%5d", v.gpsAlt); else snprintf(b, sizeof(b), "-----");
  snprintf(buf, sizeof(buf), "GS %3s  GPS ALT%s", a, b);
  printAt(0, 0, buf, 20);

  if (v.hasTime) snprintf(a, sizeof(a), "%02d:%02dZ", v.hour, v.minute); else snprintf(a, sizeof(a), "--:--Z");
  if (v.hasGpsTrack) snprintf(b, sizeof(b), "%03d", v.gpsTrack); else snprintf(b, sizeof(b), "---");
  snprintf(buf, sizeof(buf), "%-6s  DATA TRK %s", a, b);
  printAt(0, 1, buf, 20);

  if (v.hasPosition) {
    snprintf(a, sizeof(a), "%c%07.4f", v.lat >= 0 ? 'N' : 'S', fabs(v.lat));
    snprintf(b, sizeof(b), "%c%08.4f", v.lon >= 0 ? 'E' : 'W', fabs(v.lon));
  } else {
    snprintf(a, sizeof(a), "--.----");
    snprintf(b, sizeof(b), "---.----");
  }
  snprintf(buf, sizeof(buf), "%-8s   %9s", a, b);
  printAt(0, 2, buf, 20);

  if (!v.hasFixStatus) {
    snprintf(buf, sizeof(buf), "PG %d  NOFIX", page);
  } else if (v.fixType < 2) {
    snprintf(buf, sizeof(buf), "PG %d  NOFIX %dsat", page, v.sats);
  } else {
    snprintf(buf, sizeof(buf), "PG %d  %dD %dsat HD%d.%d", page, v.fixType, v.sats, v.hdop100 / 100, (v.hdop100 % 100) / 10);
  }
  putMarker(buf, FIELD_PAGE, now);
  printAt(0, 3, buf, 20);
}

// "OK", "DG" degraded, "ST" starting, "FL" failed, "--" no heartbeat for 3 s
const char *moduleStateText(uint8_t node, uint32_t now) {
  const Heartbeat &hb = heartbeats[node];
  if (!ageWithin(hb.time, MAX_AGE_HEARTBEAT_MS, now)) return "--";
  switch (hb.state) {
    case 0: return "ST";
    case 1: return "OK";
    case 2: return "DG";
    default: return "FL";
  }
}

void drawSystemText(uint32_t now) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "BL %-3s%14s", backlightOn ? "ON" : "OFF", "v" VERSION);
  putMarker(buf, FIELD_BACKLIGHT, now);
  printAt(0, 0, buf, 20);

  snprintf(buf, sizeof(buf), "I:%s A:%s G:%s E:%s", moduleStateText(1, now), moduleStateText(2, now), moduleStateText(3, now),
           moduleStateText(0, now));
  printAt(0, 1, buf, 20);

  const unsigned long s = now / 1000;
  snprintf(buf, sizeof(buf), "CAN%3lu/s UP %lu:%02lu:%02lu", (unsigned long)canRate, s / 3600, (s / 60) % 60, s % 60);
  printAt(0, 2, buf, 20);

  snprintf(buf, sizeof(buf), "PG %d", page);
  putMarker(buf, FIELD_PAGE, now);
  printAt(0, 3, buf, 20);
}

void loop1() {
  enum HorizonState : uint8_t { HZ_UNKNOWN, HZ_SHOWN, HZ_NO_DATA };
  static unsigned long lastText = 0, lastHorizon = 0, lastReport = 0;
  static unsigned long updates = 0, updateMicros = 0, lastFrames = 0;
  static HorizonState horizonState = HZ_UNKNOWN;
  static int drawnPage = 0;
  const unsigned long now = millis();
  serialCommands();
  encoderPoll();

  View v;
  if (simulate) viewFromSimulation(v, now);
  else viewFromCan(v, now);

  if (page != drawnPage) {                     // a new page: start from a clean screen
    lcd.clear();
    drawnPage = page;
    horizonState = HZ_UNKNOWN;
    slipShown = -1;
  }

  if (page == PAGE_FLIGHT && now - lastHorizon >= horizonPeriodMs) {
    lastHorizon = now;
    if (v.hasAttitude) {
      if (horizonState != HZ_SHOWN) { horizonDraw(); horizonState = HZ_SHOWN; }
      const unsigned long start = micros();
      horizonUpdate(v.pitch, v.roll);
      updateMicros += micros() - start;
      updates++;
    } else if (horizonState != HZ_NO_DATA) {
      printAt(HZ_COL, HZ_ROW, "ATT", HZ_CELLS_WIDE);
      printAt(HZ_COL, HZ_ROW + 1, "---", HZ_CELLS_WIDE);
      horizonState = HZ_NO_DATA;
    }
  }

  if (now - lastReport >= 1000) {
    const uint32_t frames = canFrames;
    canRate = frames - lastFrames;
    Serial.printf("%lu horizon updates/s, %lu us each, CAN %lu frames/s, QNH %d set=%d mismatch=%d\n", updates,
                  updates ? updateMicros / updates : 0, (unsigned long)canRate, (int)qnhValue, (int)qnhIsSet, (int)qnhMismatch);
    lastFrames = frames;
    updates = updateMicros = 0;
    lastReport = now;
  }

  if (now - lastText < 100) return;            // the text rows refresh at 10 Hz
  lastText = now;
  if (page == PAGE_SYSTEM) drawSystemText(now);
  else if (page == PAGE_GPS) drawGpsText(v, now);
  else drawFlightText(v, now);
}
