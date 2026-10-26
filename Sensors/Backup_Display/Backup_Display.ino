/*
  Backup_Display.ino - Backup display: flight, GPS and system pages
  Hardware: Adafruit Feather RP2040 + Adafruit I2C/SPI LCD backpack (MCP23008)
            + Newhaven NHD-0420DL-FL-GBW 20x4 character LCD
            + Adafruit CAN FeatherWing (MCP2515, CS 7) and the seesaw encoder
  Core:     Earle Philhower "Raspberry Pi Pico/RP2040" (board: Adafruit Feather RP2040)
  Library:  Adafruit LiquidCrystal

  The flight page, fed from the CAN bus (or simulated, see M below):
      IAS 105|HHH|ALT 4520
      VS +500|HHH| HDG 270
      OAT 12C|_o_|   G 1.0       (the o is the slip ball, the _ a line under it)
      PG>1   IAGE   29.92?       (the > is the marker of the selected field, see the encoder)
  HHH is the mini horizon, 3 x 2 character cells (15 x 16 pixels, drawn as 17 x 17 with the gaps between the cells counted).
  The GPS page (fed by the GPS module) has the same layout, with a GPS satellite icon (five custom characters, 3 x 2 cells) where the horizon is:
      GS 105  [icon] ALT 4500
      14:32Z  [icon]  TRK 262
      N43.6532   W079.3832
      PG>2  3D 9sat HD1.1
  Custom characters: slot 6 is a vertical line on the right pixel column of the cell and slot 7 one on the left pixel column.
  They go in columns 7 and 11 of rows 0 to 2, close to the horizon and the slip track on each side. The other 6 are handed out as needed (see slotAcquire): a
  horizon cell that is all sky is the built-in blank, all ground the built-in solid block, and only a cell the line crosses
  gets a custom character (at most 4). The slip ball is 5 x 5 pixels and can straddle two cells, so it takes up to 2
  slots. The cells of the slip track that have no ball show the built-in _, level with the line drawn in the ball's glyphs.

  Encoder (Adafruit seesaw rotary encoder with push button, I2C 0x36), same idea as the EFIS:
      Adjusting (the start state): QNH is selected and a turn changes it, 0.01 inHg per click. The marker next to the
          label is the built-in right arrow.
      Push: toggles between adjusting and selecting.
      Selecting: a turn moves the marker to the next field (clockwise: QNH, then PG, then back to QNH). The marker flashes
          between the arrow and a blank, in the column between the label and the value.
  There is no timeout.

  Pages (PG, bottom left of every page; adjusting it changes the page): 1 Flight, 2 GPS, 3 Engine, 4 Cylinders, 5 System, 6 Versions.
  The fields on each page, in clockwise order from the lower right:
      Flight:  QNH (lower right, only on pages that show altitude), PG
      GPS:     PG
      Engine:  PG
      Cylinders: PG
      System:  PG, BL (backlight on/off, upper left)
      Versions: PG (the module versions from the heartbeats: I IMU, A AIR, G GPS, E EFIS, D this display)
  Brightness and contrast cannot be set from software with this backpack: the backlight is on/off and the contrast is a pot.

  QNH (EFIS/docs/QNH_PLAN.md): the value is shared with the other devices over CAN. This display sends on 0x2D, and adopts a
  valid frame from any ID in 0x2C to 0x2F. A change from the knob is sent at once (at most one frame per 20 ms). A set value
  is sent again after 1000 ms +/- 20 ms without a QNH frame on the bus. For the first 2 s after power-up nothing is sent
  unless the knob is turned, to give the others time to speak. With nothing heard the QNH is 29.92 followed by ? (not set,
  and never sent). A ! after the value means the AIR module (0x2B) reports a different QNH for more than 2 s.

  Watchdog: the chip restarts if either core stops for WATCHDOG_MS. Core 0 feeds the watchdog, but only while core 1 is
  still looping, and an I2C transaction that gets no answer gives up after 25 ms instead of waiting for ever. A restart
  takes about 2 s; the QNH is taken again from the bus.

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
      H        hang core 1 on purpose, to test the watchdog
      U <ms>   horizon update period in ms (default 100, 0 = as fast as the display allows)
      S <hz>   sweep the roll +/-45 degrees and the pitch +/-10 degrees at this many cycles per second (0 = off)
  Each turn or push of the encoder prints the mode, selected field, QNH and page. Once a second the serial port shows how many
  horizon updates were made and the mean time of one, the CAN frames per second and the QNH state.
*/

#include <Wire.h>
#include <Adafruit_LiquidCrystal.h>
#include <Adafruit_seesaw.h>
#include "src/Adafruit_MCP2515/src/Adafruit_MCP2515.h"   // patched copy, see Sensors/Libraries/README.md

#define VERSION "1.7.32"

const uint8_t LCD_COLS = 20;
const uint8_t LCD_ROWS = 4;

// Watchdog and I2C timeout
const uint32_t WATCHDOG_MS = 2000;
const uint32_t I2C_TIMEOUT_MS = 25;
volatile bool core1Ready = false;                    // set at the end of setup1, for the heartbeat
volatile uint32_t core1Beat = 0;                     // millis() at the last pass of loop1

// CAN
const uint8_t CAN_CS_PIN = 7;
const long CAN_BAUDRATE = 250000;
const uint16_t ID_AIR_DATA = 0x28, ID_OAT = 0x2A, ID_AHRS_ORIENT = 0x48, ID_AHRS_ACCEL = 0x49;
const uint16_t ID_TIME_SYNC = 0x66, ID_GPS_POSITION = 0x63, ID_GPS_SPEED_TRACK = 0x64, ID_GPS_STATUS = 0x65;
const uint16_t ID_HEARTBEAT_FIRST = 0x700;           // 0x700 EFIS, 0x701 IMU, 0x702 AIR, 0x703 GPS
// MGL RDAC XF at CAN address 32: ID = 0x200 + message type (Protocol/mgl_can_messages.yaml). It has no heartbeat.
const uint16_t ID_RDAC_TC_1_4 = 0x202, ID_RDAC_TC_5_8 = 0x203, ID_RDAC_ANALOG = 0x205, ID_RDAC_SUPPLY = 0x207, ID_RDAC_RPM_MAP = 0x208;
const uint16_t ID_HEARTBEAT_BACKUP = 0x704;          // this display
const uint32_t HEARTBEAT_PERIOD_MS = 1000;
const int16_t MARKER_INT16 = 0x7FFF;
const int32_t MARKER_INT32 = 0x7FFFFFFF;
const uint32_t MAX_AGE_AIR_MS = 500;                 // "treated as lost after", Protocol/CAN_PROTOCOL.md
const uint32_t MAX_AGE_ATTITUDE_MS = 500;
const uint32_t MAX_AGE_ACCEL_MS = 500;
const uint32_t MAX_AGE_OAT_MS = 1500;
const uint32_t MAX_AGE_GPS_MS = 3500;               // the GPS frames, the status and the time sync come once a second
const uint32_t MAX_AGE_HEARTBEAT_MS = 3000;
const uint32_t MAX_AGE_RDAC_FAST_MS = 1000;          // 0x208 (every 200 ms): older than this and the RDAC is lost, all engine values fail
const uint32_t MAX_AGE_RDAC_MS = 1500;               // the other RDAC frames (every 500 ms); Protocol/engine_conversions.yaml timeouts
const int GPS_TRACK_MIN_SPEED_KT = 2;                // the module holds the track at 0 below this ground speed
const uint32_t STATUS_WARNING_MS = 5000;             // a module that has just failed flashes quickly for this long...
const uint32_t STATUS_WARNING_FLASH_MS = 500;        // (letter, then dash, each for this long)
const uint32_t STATUS_FLASH_MS = 1500;               // ...then slowly
const float STANDARD_GRAVITY = 9.80665f;
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
const Field ENGINE_FIELDS[] = { FIELD_PAGE };
const Field CYLINDERS_FIELDS[] = { FIELD_PAGE };
const Field SYSTEM_FIELDS[] = { FIELD_PAGE, FIELD_BACKLIGHT };
const Field VERSIONS_FIELDS[] = { FIELD_PAGE };
const uint8_t PAGE_FLIGHT = 1, PAGE_GPS = 2, PAGE_ENGINE = 3, PAGE_CYLINDERS = 4, PAGE_SYSTEM = 5, PAGE_VERSIONS = 6;
const char MARKER_CHAR = 0x7E;                       // the built-in right arrow
const uint8_t BAR_LEFT_SLOT = 6, BAR_RIGHT_SLOT = 7; // custom characters: the lines on each side of the horizon (columns 7 and 11, rows 0 to 2)
const uint8_t SLIP_COL = 8, SLIP_ROW = 2;            // the slip track: 3 cells under the horizon
const int SLIP_SIGN = 1;                             // -1 if the ball moves the wrong way (depends on how the IMU is mounted)
const float SLIP_G_PER_STEP = 0.035f;                // lateral acceleration for one pixel step; 6 steps either side of centre
const int SLIP_BALL_PX = 5;                          // the ball is 5 x 5 pixels
const int SLIP_POSITIONS = 13;                       // its left pixel can be at 0 to 12 of the 17 pixels of the track (gaps included)
const float SLIP_FILTER = 0.2f;                      // low pass at each 100 ms redraw (about 0.5 s)
const int QNH_MIN = 2800;                            // inHg x 100
const int QNH_MAX = 3100;
const int PAGE_COUNT = 6;

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
Signal sigTc[8];                                     // RDAC thermocouples 1 to 8, deg C before the cold junction is added (1 to 4 EGT, 5 to 8 CHT)
Signal sigRdacColdJunction, sigRdacSupply;           // deg C; volts x 57.3758 (raw)
Signal sigRdacOilT, sigRdacOilP;                     // raw ADC counts
Signal sigRdacRpm, sigRdacCurrent;                   // 0x208: raw rpm1 (pulses), raw current ADC counts. sigRdacRpm also times the RDAC itself
struct Heartbeat {
  volatile uint8_t state = 0;
  volatile uint8_t version[3] = { 0, 0, 0 };         // major, minor, patch
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
  // engine (RDAC XF), converted as in Protocol/engine_conversions.yaml
  bool hasRpm = false, hasOilP = false, hasOilT = false, hasVolts = false, hasAmps = false;
  bool hasEgt[4] = { false, false, false, false }, hasCht[4] = { false, false, false, false };
  int rpm = 0, oilT = 0, egt[4] = { 0, 0, 0, 0 }, cht[4] = { 0, 0, 0, 0 };
  float oilP = 0, volts = 0, amps = 0;
};

// Core 0 can store a time a moment later than the 'now' core 1 read, so the age can be negative: use a signed difference
bool ageWithin(uint32_t time, uint32_t maxAgeMs, uint32_t now) {
  return time != 0 && (int32_t)(now - time) <= (int32_t)maxAgeMs;
}

bool fresh(const Signal &sig, uint32_t maxAgeMs, uint32_t now) {
  return sig.valid && ageWithin(sig.time, maxAgeMs, now);
}

// --- RDAC XF conversions: keep in step with Protocol/engine_conversions.yaml (reference: Protocol/engine_convert.py) ---------
const float RDAC_ADC_FULL_SCALE = 3908;              // an open input reads this
const float OILP_PULLUP_OHM = 1000, OILT_PULLUP_OHM = 1500;
const float RDAC_SUPPLY_COUNTS_PER_VOLT = 57.3758f;
const float RPM_PULSES_PER_REV = 0;                  // 0 = the RPM source is not chosen yet, so RPM shows as dashes
const int AMPS_ZERO_RAW = -1;                        // -1 = the current sensor is not calibrated yet, so amps shows as dashes
const float AMPS_PER_COUNT = 0;

// Sender resistance from the raw divider reading, -1 when the input is open or shorted
float senderOhms(int raw, float pullupOhm) {
  if (raw <= 0 || raw >= RDAC_ADC_FULL_SCALE - 20) return -1;
  return pullupOhm * raw / (RDAC_ADC_FULL_SCALE - raw);
}

// VDO 5 bar (80 psi) sender: R = a p^2 + b p + c. Returns NAN when open, shorted or beyond the sender's range
float oilPressureBar(float ohms) {
  if (ohms < 0) return NAN;
  const float a = -0.3682f, b = 36.465f, c = 10.648f;
  if (ohms <= c) return 0;
  const float disc = b * b - 4 * a * (c - ohms);
  if (disc < 0) return NAN;
  const float p = (-b + sqrtf(disc)) / (2 * a);
  return (p > 5.3f) ? NAN : p;
}

// VDO 150 degC (300 F) sender: Steinhart-Hart through 322.8 ohm at 50 C, 36.5 at 120 and 18.6 at 150. NAN outside 40 to 160 C
float oilTempC(float ohms) {
  if (ohms < 0) return NAN;
  const double ln = log((double)ohms);
  const double t = 1.0 / (0.0015525513345798962 + 0.00028091203795547765 * ln - 4.1934592032947954e-07 * ln * ln * ln) - 273.15;
  return (t < 40 || t > 160) ? NAN : (float)t;
}

void engineViewFromCan(View &v, uint32_t now) {
  if (!ageWithin(sigRdacRpm.time, MAX_AGE_RDAC_FAST_MS, now)) return;        // the RDAC is lost: everything stays missing
  const bool tcOk = fresh(sigRdacColdJunction, MAX_AGE_RDAC_MS, now);
  for (int i = 0; i < 4; i++) {
    v.hasEgt[i] = tcOk && fresh(sigTc[i], MAX_AGE_RDAC_MS, now);
    v.egt[i] = sigTc[i].value + sigRdacColdJunction.value;
    v.hasCht[i] = tcOk && fresh(sigTc[4 + i], MAX_AGE_RDAC_MS, now);
    v.cht[i] = sigTc[4 + i].value + sigRdacColdJunction.value;
  }
  const float tempC = oilTempC(senderOhms(sigRdacOilT.value, OILT_PULLUP_OHM));
  v.hasOilT = fresh(sigRdacOilT, MAX_AGE_RDAC_MS, now) && !isnan(tempC);
  v.oilT = lroundf(tempC);
  const float bar = oilPressureBar(senderOhms(sigRdacOilP.value, OILP_PULLUP_OHM));
  v.hasOilP = fresh(sigRdacOilP, MAX_AGE_RDAC_MS, now) && !isnan(bar);
  v.oilP = bar;
  v.hasVolts = fresh(sigRdacSupply, MAX_AGE_RDAC_MS, now);
  v.volts = sigRdacSupply.value / RDAC_SUPPLY_COUNTS_PER_VOLT;
  v.hasRpm = RPM_PULSES_PER_REV > 0;
  v.rpm = lroundf(sigRdacRpm.value / (RPM_PULSES_PER_REV > 0 ? RPM_PULSES_PER_REV : 1));
  v.hasAmps = AMPS_ZERO_RAW >= 0 && fresh(sigRdacCurrent, MAX_AGE_RDAC_FAST_MS, now);
  v.amps = (sigRdacCurrent.value - AMPS_ZERO_RAW) * AMPS_PER_COUNT;
}

void viewFromCan(View &v, uint32_t now) {
  v.hasAirspeed = fresh(sigAirspeed, MAX_AGE_AIR_MS, now);
  v.airspeed = sigAirspeed.value;
  v.hasAltitude = fresh(sigAltitude, MAX_AGE_AIR_MS, now);
  v.altitude = sigAltitude.value;
  v.hasVerticalSpeed = fresh(sigVerticalSpeed, MAX_AGE_AIR_MS, now);
  v.verticalSpeed = sigVerticalSpeed.value;       // as the AIR module sends it: it holds the value steady itself
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
  engineViewFromCan(v, now);
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
  v.hasRpm = v.hasOilP = v.hasOilT = v.hasVolts = v.hasAmps = true;
  v.rpm = 2450 + (int)(50 * sin(t * 0.5f));
  v.oilP = 3.4f + 0.2f * sin(t * 0.3f);
  v.oilT = 95;
  v.volts = 13.9f;
  v.amps = -3.5f;
  for (int i = 0; i < 4; i++) {
    v.hasEgt[i] = v.hasCht[i] = true;
    v.egt[i] = 690 + 10 * i + (int)(8 * sin(t * 0.4f + i));
    v.cht[i] = 170 + 4 * i + (int)(3 * sin(t * 0.3f + i));
  }
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

// --- Custom character slots ---------------------------------------------------------------------------------------------
// The display has 8 custom characters. Slots 6 and 7 are the lines on each side of the horizon. The other 6 are handed out as needed to the horizon cells (owners 0 to
// 5) and to the cells of the slip track (owners 6 to 8). The CGRAM is written only when a glyph changes.
const int CHAR_BLANK = 0x20, CHAR_SOLID = 0xFF;      // the built-in blank and solid block
const int NO_OWNER = -1, RESERVED = -2;
const int OWNER_SLIP = 6;                            // the slip track cells are owners 6, 7 and 8
const int OWNER_GPS_ICON = 9;                       // the GPS icon on the GPS page is owners 9 to 13
int slotOwner[8] = { NO_OWNER, NO_OWNER, NO_OWNER, NO_OWNER, NO_OWNER, NO_OWNER, NO_OWNER, NO_OWNER };
uint8_t slotData[8][8];

void slotsInit() {
  memset(slotData, 0xFF, sizeof(slotData));          // no real glyph has a row of 0xFF, so the first write always happens
  slotOwner[BAR_LEFT_SLOT] = RESERVED;               // the two lines are loaded once and never handed out
  slotOwner[BAR_RIGHT_SLOT] = RESERVED;
}

int slotAcquire(int owner) {
  for (int slot = 0; slot < 8; slot++) if (slotOwner[slot] == owner) return slot;
  for (int slot = 0; slot < 8; slot++) {
    if (slotOwner[slot] == NO_OWNER) { slotOwner[slot] = owner; return slot; }
  }
  return -1;                                         // none free
}

void slotRelease(int owner) {
  for (int slot = 0; slot < 8; slot++) if (slotOwner[slot] == owner) slotOwner[slot] = NO_OWNER;
}

void slotWrite(int slot, const uint8_t *glyph) {
  if (memcmp(slotData[slot], glyph, 8) == 0) return;
  lcd.createChar(slot, (uint8_t *)glyph);
  memcpy(slotData[slot], glyph, 8);
}

// Put a character in a screen position if it is not already there
void putChar(uint8_t col, uint8_t row, int code, int *shownCode) {
  if (*shownCode == code) return;
  lcd.setCursor(col, row);
  lcd.write((uint8_t)code);
  *shownCode = code;
}

// --- The mini horizon -------------------------------------------------------------------------------------------------------
const int HZ_CELLS = HZ_CELLS_WIDE * HZ_CELLS_HIGH;
int hzShown[HZ_CELLS] = { -1, -1, -1, -1, -1, -1 };  // the character on the screen for each cell; -1 = unknown

// Put the horizon on the screen again at the next update (after the screen was cleared)
void horizonDraw() {
  for (int i = 0; i < HZ_CELLS; i++) hzShown[i] = -1;
}

// Draw the horizon. Ground is filled, sky is blank. A cell that is all sky or all ground uses the built-in blank or solid
// block; only a cell the line crosses takes a custom character.
// pitchDeg: positive = nose up (the horizon moves down). rollDeg: positive turns the horizon clockwise (the right end drops), the same as the EFIS page does with the bus value.
void horizonUpdate(float pitchDeg, float rollDeg) {
  rollDeg = constrain(rollDeg, -HZ_MAX_ROLL_DEG, HZ_MAX_ROLL_DEG);
  const float slope = -tanf(rollDeg * (float)M_PI / 180.0f);   // screen y points down: a clockwise turn lowers the right end
  const float cx = (HZ_WIDTH_PX - 1) / 2.0f;
  const float cy = (HZ_HEIGHT_PX - 1) / 2.0f + pitchDeg * HZ_PX_PER_DEG;

  uint8_t glyph[HZ_CELLS][8];
  int ground[HZ_CELLS];                              // ground pixels in the cell, of 40
  for (int cell = 0; cell < HZ_CELLS; cell++) {
    const int cellX = (cell % HZ_CELLS_WIDE) * (5 + HZ_GAP_PX);
    const int cellY = (cell / HZ_CELLS_WIDE) * (8 + HZ_GAP_PX);
    ground[cell] = 0;
    for (uint8_t row = 0; row < 8; row++) {
      uint8_t bits = 0;
      for (uint8_t col = 0; col < 5; col++) {
        const float horizonY = cy - slope * ((cellX + col) - cx);
        if ((cellY + row) > horizonY) { bits |= 1 << (4 - col); ground[cell]++; }
      }
      glyph[cell][row] = bits;
    }
  }

  for (int cell = 0; cell < HZ_CELLS; cell++) if (ground[cell] == 0 || ground[cell] == 40) slotRelease(cell);   // free slots first
  for (int cell = 0; cell < HZ_CELLS; cell++) {
    int code;
    if (ground[cell] == 0) {
      code = CHAR_BLANK;
    } else if (ground[cell] == 40) {
      code = CHAR_SOLID;
    } else {
      const int slot = slotAcquire(cell);
      if (slot >= 0) { slotWrite(slot, glyph[cell]); code = slot; }
      else code = ground[cell] > 20 ? CHAR_SOLID : CHAR_BLANK;     // no slot free: the nearer of the two
    }
    putChar(HZ_COL + cell % HZ_CELLS_WIDE, HZ_ROW + cell / HZ_CELLS_WIDE, code, &hzShown[cell]);
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
    else if (cmd == 'H') { Serial.println("Hanging core 1"); while (1) {} }
  }
}

// --- The slip ball ---------------------------------------------------------------------------------------------------------
// The track is the 3 cells under the horizon, 17 pixels wide with the gaps between the cells counted (the gaps are never
// drawn). The ball is 5 x 5 pixels with its left pixel at 'position' (0 to 12), so it can straddle two cells. A line runs under
// it on row 6, level with the built-in _ that the cells without the ball show.
int slipShown[3] = { -1, -1, -1 };                   // the character on the screen for each cell of the track; -1 = unknown

void slipForget() { for (int i = 0; i < 3; i++) slipShown[i] = -1; }

void slipDraw(int position) {
  const uint8_t ballRows[5] = { 0b01110, 0b11111, 0b11111, 0b11111, 0b01110 };
  uint8_t glyph[3][8] = {};
  bool hasBall[3] = { false, false, false };
  for (int i = 0; i < SLIP_BALL_PX; i++) {                         // each row of the ball
    for (int j = 0; j < SLIP_BALL_PX; j++) {                       // each pixel of the row
      const int x = position + j;
      const int cell = x / (5 + HZ_GAP_PX), column = x % (5 + HZ_GAP_PX);
      if (column > 4 || !(ballRows[i] & (1 << (4 - j)))) continue;   // the gap, or a pixel the ball does not have
      glyph[cell][i] |= 1 << (4 - column);
      hasBall[cell] = true;
    }
  }
  for (int cell = 0; cell < 3; cell++) if (!hasBall[cell]) slotRelease(OWNER_SLIP + cell);
  for (int cell = 0; cell < 3; cell++) {
    int code = '_';
    if (hasBall[cell]) {
      glyph[cell][6] = 0b11111;                                    // the line
      const int slot = slotAcquire(OWNER_SLIP + cell);
      if (slot >= 0) { slotWrite(slot, glyph[cell]); code = slot; }
    }
    putChar(SLIP_COL + cell, SLIP_ROW, code, &slipShown[cell]);
  }
}

// The lines on each side of the horizon: the right pixel column of the cell for the left one, the left pixel column for the right
// one, so that both are one pixel (the gap between characters) from the horizon
void markerCreate() {
  uint8_t left[8] = { 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001 };
  uint8_t right[8] = { 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000 };
  lcd.createChar(BAR_LEFT_SLOT, left);
  lcd.createChar(BAR_RIGHT_SLOT, right);
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
  if (forPage == PAGE_CYLINDERS) { count = sizeof(CYLINDERS_FIELDS) / sizeof(CYLINDERS_FIELDS[0]); return CYLINDERS_FIELDS; }
  if (forPage == PAGE_ENGINE) { count = sizeof(ENGINE_FIELDS) / sizeof(ENGINE_FIELDS[0]); return ENGINE_FIELDS; }
  if (forPage == PAGE_VERSIONS) { count = sizeof(VERSIONS_FIELDS) / sizeof(VERSIONS_FIELDS[0]); return VERSIONS_FIELDS; }
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
uint16_t uint16At(const uint8_t *b, int i) { return (uint16_t)(b[i] | (b[i + 1] << 8)); }

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
  } else if (id == ID_RDAC_TC_1_4 || id == ID_RDAC_TC_5_8) {
    const int first = (id == ID_RDAC_TC_1_4) ? 0 : 4;
    for (int i = 0; i < 4; i++) storeSignal(sigTc[first + i], int16At(b, 2 * i), true, now);
  } else if (id == ID_RDAC_ANALOG) {
    storeSignal(sigRdacOilT, uint16At(b, 0), true, now);
    storeSignal(sigRdacOilP, uint16At(b, 2), true, now);
  } else if (id == ID_RDAC_SUPPLY) {
    storeSignal(sigRdacColdJunction, int16At(b, 0), true, now);
    storeSignal(sigRdacSupply, uint16At(b, 2), true, now);
  } else if (id == ID_RDAC_RPM_MAP) {
    storeSignal(sigRdacRpm, uint16At(b, 0), true, now);
    storeSignal(sigRdacCurrent, uint16At(b, 6), true, now);
  } else if (id >= ID_HEARTBEAT_FIRST && id < ID_HEARTBEAT_FIRST + 4) {
    Heartbeat &hb = heartbeats[id - ID_HEARTBEAT_FIRST];
    hb.state = b[4];
    hb.version[0] = b[1];
    hb.version[1] = b[2];
    hb.version[2] = b[3];
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

// The heartbeat (0x704, layout of Protocol/can_messages.yaml 0x701): module 4, version, state, sensors, uptime.
// State 0 until core 1 is up, then 1, or 2 if the encoder was not found. Sensors: bit 0 LCD, bit 1 encoder.
void heartbeatService(uint32_t now) {
  static uint32_t lastSend = 0;
  static bool sent = false;
  if (!canUp || (sent && now - lastSend < HEARTBEAT_PERIOD_MS)) return;
  unsigned major = 0, minor = 0, patch = 0;
  sscanf(VERSION, "%u.%u.%u", &major, &minor, &patch);
  const uint32_t uptime = min(now / 1000, (uint32_t)65535);
  const uint8_t state = !core1Ready ? 0 : (encoderFound ? 1 : 2);
  const uint8_t sensors = core1Ready ? (1 | (encoderFound ? 2 : 0)) : 0;
  const uint8_t frame[8] = { 4, (uint8_t)major, (uint8_t)minor, (uint8_t)patch, state, sensors,
                             (uint8_t)(uptime & 0xFF), (uint8_t)(uptime >> 8) };
  can.beginPacket(ID_HEARTBEAT_BACKUP);
  can.write(frame, 8);
  can.endPacket();
  lastSend = now;
  sent = true;
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}   // wait briefly for USB serial
  Serial.print("Backup_Display ");
  Serial.println(VERSION);
  if (rp2040.getResetReason() == RP2040::WDT_RESET) Serial.println("Restarted by the watchdog");

  canUp = can.begin(CAN_BAUDRATE);
  Serial.println(canUp ? "CAN bus OK" : "CAN bus (MCP2515) NOT FOUND, simulating");
  simulate = !canUp;
}

void loop() {
  static bool watching = false;
  const uint32_t now = millis();
  if (!watching) {                             // start once both cores are running, so setup1 is not timed
    core1Beat = now;
    rp2040.wdt_begin(WATCHDOG_MS);
    watching = true;
  }
  if (canUp) canPoll();
  qnhService(now);
  heartbeatService(now);
  if (now - core1Beat < WATCHDOG_MS / 2) rp2040.wdt_reset();   // not fed when core 1 has stopped
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
  Wire.setTimeout(I2C_TIMEOUT_MS, true);
  if (!lcd.begin(LCD_COLS, LCD_ROWS)) {
    Serial.println("LCD backpack not found - check wiring/address");
    while (1) delay(100);
  }
  Wire.setClock(I2C_CLOCK_HZ);
  lcd.setBacklight(HIGH);
  lcd.clear();
  slotsInit();
  markerCreate();
  encoderFound = encoderBegin();
  core1Ready = true;
}

// The marker for a field: blank when it is not selected, the arrow when adjusting, flashing between the arrow and a
// blank when selecting
char markerChar(Field f, uint32_t now) {
  if (f != selectedField) return ' ';
  if (adjusting) return MARKER_CHAR;
  return ((now / MARKER_FLASH_MS) % 2 == 0) ? MARKER_CHAR : ' ';
}

void putMarker(char *buf, Field f, uint32_t now) { buf[MARKER_POS[f].col] = markerChar(f, now); }

volatile uint32_t canRate = 0;                       // CAN frames in the last second, for the System page

void drawFlightText(const View &v, uint32_t now) {
  char buf[LCD_COLS + 1], a[8], b[8];
  // Rows 0 and 1 leave columns 8 to 10 for the horizon and row 2 leaves them for the slip ball
  if (v.hasAirspeed) snprintf(a, sizeof(a), "%3d", v.airspeed); else snprintf(a, sizeof(a), "---");
  if (v.hasAltitude) snprintf(b, sizeof(b), "%5d", v.altitude); else snprintf(b, sizeof(b), "-----");
  snprintf(buf, sizeof(buf), "IAS %-3s%c", a, (char)BAR_LEFT_SLOT);
  printAt(0, 0, buf, 8);
  snprintf(buf, sizeof(buf), "%cALT%s", (char)BAR_RIGHT_SLOT, b);
  printAt(11, 0, buf, 9);

  if (v.hasVerticalSpeed) snprintf(a, sizeof(a), "%+5d", v.verticalSpeed); else snprintf(a, sizeof(a), "-----");
  if (v.hasHeading) snprintf(b, sizeof(b), "%03d", v.heading); else snprintf(b, sizeof(b), "---");
  snprintf(buf, sizeof(buf), "VS%s%c", a, (char)BAR_LEFT_SLOT);
  printAt(0, 1, buf, 8);
  snprintf(buf, sizeof(buf), "%c HDG %s", (char)BAR_RIGHT_SLOT, b);
  printAt(11, 1, buf, 9);

  if (v.hasOat) snprintf(a, sizeof(a), "%2d", v.oat); else snprintf(a, sizeof(a), "--");
  if (v.hasG) snprintf(b, sizeof(b), "%4.1f", v.g); else snprintf(b, sizeof(b), " ---");
  snprintf(buf, sizeof(buf), "OAT %sC%c", a, (char)BAR_LEFT_SLOT);
  printAt(0, 2, buf, 8);
  snprintf(buf, sizeof(buf), "%c   G%s", (char)BAR_RIGHT_SLOT, b);
  printAt(11, 2, buf, 9);

  // Slip ball: lateral acceleration, low passed, 0.05 g to a pixel step
  static float slip = 0;
  if (v.hasAccelY) {
    slip += (v.accelYg - slip) * SLIP_FILTER;
    slipDraw(constrain(SLIP_POSITIONS / 2 + lroundf(SLIP_SIGN * slip / SLIP_G_PER_STEP), 0, SLIP_POSITIONS - 1));
  } else {
    slip = 0;
    printAt(SLIP_COL, SLIP_ROW, "---", 3);
    slipForget();                              // draw the ball again when the data returns
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

// A GPS satellite icon in columns 8 to 10 of rows 0 and 1, five custom characters:
//   row 0: TL TM blank     row 1: BL BM BR
void drawGpsIcon() {
  static const uint8_t glyph[5][8] = {
    { 0x18, 0x1C, 0x0E, 0x07, 0x03, 0x00, 0x00, 0x00 },   // TL
    { 0x0E, 0x10, 0x13, 0x11, 0x0E, 0x00, 0x14, 0x0E },   // TM: a G in the top five rows above the satellite body
    { 0x00, 0x0A, 0x0A, 0x09, 0x04, 0x02, 0x01, 0x00 },   // BL
    { 0x0E, 0x15, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00 },   // BM
    { 0x00, 0x00, 0x00, 0x18, 0x1C, 0x0E, 0x07, 0x03 },   // BR
  };
  int code[5];
  for (int i = 0; i < 5; i++) {
    const int slot = slotAcquire(OWNER_GPS_ICON + i);
    if (slot >= 0) slotWrite(slot, glyph[i]);
    code[i] = slot >= 0 ? slot : ' ';                     // blank if no slot is free
  }
  lcd.setCursor(8, 0);
  lcd.write((uint8_t)code[0]);
  lcd.write((uint8_t)code[1]);
  lcd.write((uint8_t)' ');
  lcd.setCursor(8, 1);
  lcd.write((uint8_t)code[2]);
  lcd.write((uint8_t)code[3]);
  lcd.write((uint8_t)code[4]);
}

void drawGpsText(const View &v, uint32_t now) {
  char buf[LCD_COLS + 1], a[12], b[12], c[12];
  // Same grid as the Flight page: GPS takes the place of the horizon in columns 8 to 11
  if (v.hasGpsSpeed) snprintf(a, sizeof(a), "%3d", v.gpsSpeed); else snprintf(a, sizeof(a), "---");
  if (v.hasGpsAlt) snprintf(b, sizeof(b), "%5d", v.gpsAlt); else snprintf(b, sizeof(b), "-----");
  snprintf(buf, sizeof(buf), "GS %3s", a);
  printAt(0, 0, buf, 8);
  snprintf(buf, sizeof(buf), " ALT%s", b);
  printAt(11, 0, buf, 9);

  if (v.hasTime) snprintf(a, sizeof(a), "%02d:%02dZ", v.hour, v.minute); else snprintf(a, sizeof(a), "--:--Z");
  if (v.hasGpsTrack) snprintf(b, sizeof(b), "%03d", v.gpsTrack); else snprintf(b, sizeof(b), "---");
  snprintf(buf, sizeof(buf), "%-6s", a);
  printAt(0, 1, buf, 8);
  drawGpsIcon();
  snprintf(buf, sizeof(buf), "  TRK %s", b);
  printAt(11, 1, buf, 9);

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

// A number in a field of the given width, or dashes when it is missing
void numberOrDashes(char *out, size_t size, bool has, const char *format, float value, uint8_t width) {
  char num[12];
  if (has) snprintf(num, sizeof(num), format, value);
  else memset(num, '-', width), num[width] = 0;
  snprintf(out, size, "%*s", width, num);
}

// Highest of the four values that are present: its cylinder number (1 to 4) or 0 when none is
int highestOf(const bool has[4], const int value[4]) {
  int best = 0;
  for (int i = 0; i < 4; i++) if (has[i] && (best == 0 || value[i] > value[best - 1])) best = i + 1;
  return best;
}

// The engine values arrive in bar and degrees C; the page shows psi and degrees F
float barToPsi(float bar) { return bar * 14.5038f; }
float celsiusToFahrenheit(float c) { return c * 1.8f + 32.0f; }

void drawEngineText(const View &v, uint32_t now) {
  char buf[LCD_COLS + 1], rpm[8], oilP[8], oilT[8], hot[8], volts[8], amps[8];
  numberOrDashes(rpm, sizeof(rpm), v.hasRpm, "%.0f", v.rpm, 4);
  numberOrDashes(oilP, sizeof(oilP), v.hasOilP, "%.0f", barToPsi(v.oilP), 3);
  numberOrDashes(oilT, sizeof(oilT), v.hasOilT, "%.0f", celsiusToFahrenheit(v.oilT), 3);
  snprintf(buf, sizeof(buf), "OilP %s psi%8s", oilP, "EMS");
  printAt(0, 0, buf, 20);

  const int e = highestOf(v.hasEgt, v.egt);
  numberOrDashes(hot, sizeof(hot), e > 0, "%.0f", e > 0 ? celsiusToFahrenheit(v.egt[e - 1]) : 0, 4);
  snprintf(buf, sizeof(buf), "OilT %s F EGT%c %s", oilT, e > 0 ? '0' + e : '-', hot);
  printAt(0, 1, buf, 20);

  const int c = highestOf(v.hasCht, v.cht);
  numberOrDashes(hot, sizeof(hot), c > 0, "%.0f", c > 0 ? celsiusToFahrenheit(v.cht[c - 1]) : 0, 4);
  snprintf(buf, sizeof(buf), "RPM %s   CHT%c %s", rpm, c > 0 ? '0' + c : '-', hot);
  printAt(0, 2, buf, 20);

  numberOrDashes(volts, sizeof(volts), v.hasVolts, "%.1f", v.volts, 4);
  numberOrDashes(amps, sizeof(amps), v.hasAmps, "%+.1f", v.amps, 5);
  snprintf(buf, sizeof(buf), "PG %d    %sV %sA", page, volts, amps);
  putMarker(buf, FIELD_PAGE, now);
  printAt(0, 3, buf, 20);
}

// Four values in columns, right justified in 4 characters, or dashes for a missing one
void cylinderRow(char *buf, size_t size, char label, const bool has[4], const int value[4]) {
  char col[4][8];
  for (int i = 0; i < 4; i++) numberOrDashes(col[i], sizeof(col[i]), has[i], "%.0f", celsiusToFahrenheit(value[i]), 4);
  snprintf(buf, size, "%c%s %s %s %s", label, col[0], col[1], col[2], col[3]);
}

void drawCylindersText(const View &v, uint32_t now) {
  char buf[LCD_COLS + 1];
  printAt(0, 0, "    1    2    3    4", 20);
  cylinderRow(buf, sizeof(buf), 'E', v.hasEgt, v.egt);
  printAt(0, 1, buf, 20);
  cylinderRow(buf, sizeof(buf), 'C', v.hasCht, v.cht);
  printAt(0, 2, buf, 20);
  snprintf(buf, sizeof(buf), "PG %d", page);
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

// "1.0.18", or "--" when the module has sent no heartbeat for 3 s
void moduleVersionText(char *out, size_t size, uint8_t node, uint32_t now) {
  const Heartbeat &hb = heartbeats[node];
  if (!ageWithin(hb.time, MAX_AGE_HEARTBEAT_MS, now)) snprintf(out, size, "--");
  else snprintf(out, size, "%u.%u.%u", hb.version[0], hb.version[1], hb.version[2]);
}

void drawVersionsText(uint32_t now) {
  char buf[LCD_COLS + 1];
  char imu[12], air[12], gps[12], efis[12];
  moduleVersionText(imu, sizeof(imu), 1, now);
  moduleVersionText(air, sizeof(air), 2, now);
  moduleVersionText(gps, sizeof(gps), 3, now);
  moduleVersionText(efis, sizeof(efis), 0, now);
  snprintf(buf, sizeof(buf), "I %-8sA %s", imu, air);
  printAt(0, 0, buf, 20);
  snprintf(buf, sizeof(buf), "G %-8sE %s", gps, efis);
  printAt(0, 1, buf, 20);
  printAt(0, 2, "D " VERSION, 20);
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
  core1Beat = now;
  serialCommands();
  encoderPoll();

  View v;
  if (simulate) viewFromSimulation(v, now);
  else viewFromCan(v, now);

  if (page != drawnPage) {                     // a new page: start from a clean screen
    lcd.clear();
    drawnPage = page;
    horizonState = HZ_UNKNOWN;
    slipForget();
    for (int i = 0; i < 5; i++) slotRelease(OWNER_GPS_ICON + i);
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
  if (page == PAGE_VERSIONS) drawVersionsText(now);
  else if (page == PAGE_CYLINDERS) drawCylindersText(v, now);
  else if (page == PAGE_ENGINE) drawEngineText(v, now);
  else if (page == PAGE_SYSTEM) drawSystemText(now);
  else if (page == PAGE_GPS) drawGpsText(v, now);
  else drawFlightText(v, now);
}
