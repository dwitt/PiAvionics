

/*****************************************************************************
 * GPS Module
 *****************************************************************************/

/*****************************************************************************
 * Boards used
 * Adafruit Feather RP2040
 * Adafruit CAN Bus FeatherWing
 * Adafruit Ultimate GPS Breakout (66 channel, 10 Hz)
 *****************************************************************************/

#include <Adafruit_GPS.h>        // Adafruit GPS NMEA parsing library
#include "src/Adafruit_MCP2515/src/Adafruit_MCP2515.h"    // for CAN Bus FeatherWing
#include "src/StatusLED/StatusLED.h"          // status LED: startup colours and runtime heartbeat
#include "src/SendTimer/SendTimer.h"         // send each CAN message every period +/- 20 ms
#include "XYZgeomag.hpp"         // WMM2025 magnetic declination (nhz2/XYZgeomag)
#include "src/ModuleInfo/ModuleInfo.h"        // USB product name, startup log and '?' report

#define VERSION "1.0.12"

ModuleInfo moduleInfo("GPS_Module", VERSION, __DATE__, __TIME__);

/*****************************************************************************
 * Defines to help with debugging
 ****************************************************************************/
// #define DEBUG
// #define DEBUG_NO_CAN    // Skip CAN bus, output messages to Serial instead

// ----------------------------------------------------------------------------
// GPS uses Serial1 (hardware UART on Feather RP2040)
// TX = GPIO0, RX = GPIO1
// ----------------------------------------------------------------------------

#define GPS_BAUD_INITIAL  9600
#define GPS_BAUD_FAST     115200

Adafruit_GPS gps(&Serial1);

// ----------------------------------------------------------------------------
// Status LED (Feather RP2040 onboard NeoPixel). See Sensors/Libraries/StatusLED/StatusLED.md
// Runtime heartbeat colour: amber = GPS silent (degraded), red = nobody acknowledges our CAN
// frames, blue = no fix, yellow = 2D fix, green = 3D fix.
// ----------------------------------------------------------------------------

#define POST_CAN 11     // red flashes if the CAN bus does not start
StatusLED led;

// ----------------------------------------------------------------------------
// The MCP2515 (CAN Bus) uses the SPI interface
// ----------------------------------------------------------------------------

#define CAN_CS_PIN 7
#define CAN_BAUDRATE (250000)

Adafruit_MCP2515 can(CAN_CS_PIN);

// CAN Message Constants ------------------------------------------------------
#define CAN_GPS1_MSG_ID       0x63
#define CAN_GPS2_MSG_ID       0x64
#define CAN_GPS3_MSG_ID       0x65
#define CAN_TIME_SYNC_MSG_ID  0x66

#define CAN_GPS1_PERIOD       1000    // milliseconds
#define CAN_GPS2_PERIOD       1000    // milliseconds
#define CAN_GPS3_PERIOD       1000    // milliseconds
#define CAN_TIME_SYNC_PERIOD  1000    // milliseconds

// ----------------------------------------------------------------------------
// Conversion Constants
// ----------------------------------------------------------------------------

#define M_TO_FT 3.28084f
#define TRACK_SPEED_THRESHOLD 2  // knots - hold track to 0 below this speed

// ----------------------------------------------------------------------------
// Define locally global variables and functions
// ----------------------------------------------------------------------------

// Timestamps to determine when to send the next CAN message
static SendTimer canGps1Timer(CAN_GPS1_PERIOD);   // each wait is the period +/- 20 ms, drawn once per send
static SendTimer canGps2Timer(CAN_GPS2_PERIOD);
static SendTimer canGps3Timer(CAN_GPS3_PERIOD);
static SendTimer canTimeSyncTimer(CAN_TIME_SYNC_PERIOD);

// Data to send via CAN
// Declare static so they are local to this file

static int32_t latitude = 0;         // CANbus Data: degrees * 1,000,000
static int32_t longitude = 0;        // CANbus Data: degrees * 1,000,000
static int16_t groundSpeed = 0;      // CANbus Data: knots
static int16_t gpsAltitude = 0;      // CANbus Data: feet
static int16_t trueTrack = 0;        // CANbus Data: degrees
static int16_t trackMag = 0;         // CANbus Data: degrees
static float   magDeclination = 0;  // Magnetic declination from WMM2025 (East +, West -)
static bool    magDeclinationValid = false;   // true once it has been calculated for a real position and date

// Time data
static uint8_t gpsYear = 0;
static uint8_t gpsMonth = 0;
static uint8_t gpsDay = 0;
static uint8_t gpsHour = 0;
static uint8_t gpsMinute = 0;
static uint8_t gpsSecond = 0;

static bool gpsHasDate = false;

static unsigned long lastWmmCompute = 0;
#define WMM_COMPUTE_INTERVAL 60000  // Recompute magnetic declination every 60 seconds

// ----------------------------------------------------------------------------
// Convert GPS date/time to a decimal year for WMM calculations.
// Approximate: each month = 1/12 year, each day = 1/365 year.
// ----------------------------------------------------------------------------
float gpsDecimalYear(uint8_t year2d, uint8_t month, uint8_t day) {
  float yr = 2000.0f + year2d;
  yr += (month - 1) / 12.0f;
  yr += (day - 1) / 365.0f;
  return yr;
}

// ----------------------------------------------------------------------------
// Compute magnetic declination using the WMM2025 model.
// Uses GPS latitude, longitude (degrees), altitude (metres), and date.
// Returns declination in degrees (East positive, West negative).
// ----------------------------------------------------------------------------
float computeMagDeclination(float lat, float lon, float altMetres,
                            uint8_t year2d, uint8_t month, uint8_t day) {
  float dyear = gpsDecimalYear(year2d, month, day);
  geomag::Vector pos = geomag::geodetic2ecef(lat, lon, altMetres);
  geomag::Vector mag = geomag::GeoMag(dyear, pos, geomag::WMM2025);
  geomag::Elements elem = geomag::magField2Elements(mag, lat, lon);
  return elem.declination;
}

// ----------------------------------------------------------------------------
// The magnetic declination as sent in byte 3 of the GPS status message (0x65):
// whole degrees, East positive, 0x7F until it has been calculated.
// ----------------------------------------------------------------------------
uint8_t declinationByte() {
  if (!magDeclinationValid) return 0x7F;
  return (uint8_t)(int8_t)lroundf(magDeclination);
}

// ----------------------------------------------------------------------------
// Helper to print a CAN message to Serial (used when DEBUG_NO_CAN is defined)
// ----------------------------------------------------------------------------
#if defined(DEBUG_NO_CAN)
void serialPrintCANMsg(uint16_t id, uint8_t *data, uint8_t len) {
  Serial.print("CAN 0x");
  Serial.print(id, HEX);
  Serial.print(" [");
  Serial.print(len);
  Serial.print("] ");
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    if (i < len - 1) Serial.print(" ");
  }
  Serial.println();
}
#endif


// ----------------------------------------------------------------------------
// GPS start-up helpers
//
// The GPS powers up at 9600 baud, but may already be at 115200 (after an
// RP2040-only reset the GPS keeps its settings). If the RP2040 and GPS
// disagree on baud rate we read noise, never see a valid sentence, and report
// "no fix" forever. So we probe for valid NMEA instead of assuming a baud rate.
// ----------------------------------------------------------------------------

#define GPS_PROBE_MS        1500   // listen this long at each baud rate
#define GPS_INIT_ATTEMPTS   3      // passes at boot
#define GPS_SILENCE_MS      5000   // no valid NMEA for this long -> re-init

static unsigned long lastValidNmea = 0;

// ----------------------------------------------------------------------------
// CAN heartbeat (Protocol/can_messages.yaml, 0x703). Sent every second with the
// module version, state and a sensors mask: bit 0 = valid NMEA arriving from the
// GPS chip, bit 1 = the GPS has a position fix. No fix is not a fault: the state
// is OK while NMEA is arriving and DEGRADED when the GPS has been silent.
//
// lastRealNmea is the time of the last valid sentence. Unlike lastValidNmea it is
// not reset by the watchdog after a retry, so it says what the GPS really did.
// ----------------------------------------------------------------------------
#define HB_NMEA  0x01
#define HB_FIX   0x02

static unsigned long lastRealNmea = 0;
static bool realNmeaSeen = false;
static uint32_t gpsReprobes = 0;      // times the silence watchdog started the baud probe again

// Watchdog: started at the end of a successful setup() (a failed start-up stays halted on purpose, showing its POST code
// and sending its heartbeat). gpsService() feeds it, and loop() and every wait in the GPS probe call that, so a GPS that
// stays silent keeps being probed without a restart. The chip restarts only if nothing runs for WATCHDOG_MS.
#define WATCHDOG_MS   2000

#define FAULT_INJECT 1    // test commands on USB serial ("Fault injection" below); 0 for flight
#if FAULT_INJECT
static bool injectSilent = false;     // everything from the GPS is thrown away
#endif
static bool canUp = false;        // heartbeat is only sent once the CAN bus is running

// Live CAN transmit status for the '?' report. A send that nobody acknowledges is abandoned
// after a few ms (patched Adafruit_MCP2515, see Libraries/Adafruit_MCP2515/PATCH_NOTES.md).
static void canReport(Print &out) {
  bool nmeaOk = realNmeaSeen && (millis() - lastRealNmea < GPS_SILENCE_MS);
  out.print("GPS: NMEA "); out.print(nmeaOk ? "OK" : "SILENT");
  out.print(", last valid sentence "); out.print(realNmeaSeen ? (millis() - lastRealNmea) / 1000 : 0);
  out.print(" s ago, fix "); out.print(gps.fix ? "yes" : "no");
  out.print(", satellites "); out.print((int)gps.satellites);
  out.print(", re-probes "); out.println(gpsReprobes);
  #if FAULT_INJECT
  out.print("Test build: GPS silenced "); out.println(injectSilent);
  #endif
  if (!canUp) { out.println("CAN not running"); return; }
  out.print("CAN tx: ");
  out.print(can.txDown() ? "DOWN (no acknowledge)" : "ok");
  out.print(", timeouts "); out.print(can.txTimeouts());
  out.print(", skipped "); out.print(can.txSuppressed());
  out.print(", TEC "); out.print(can.txErrorCount());
  out.print(", REC "); out.print(can.rxErrorCount());
  out.print(", EFLG 0x"); out.println(can.errorFlags(), HEX);
}
static bool setupDone = false;    // state is STARTING until setup() has finished

static void noteValidNmea() {
  lastRealNmea = millis();
  realNmeaSeen = true;
}

static void sendHeartbeat() {
  if (!canUp || !moduleInfo.heartbeatDue()) return;

  bool nmeaOk = realNmeaSeen && (millis() - lastRealNmea < GPS_SILENCE_MS);
  bool hasFix = nmeaOk && gps.fixquality > 0;
  uint8_t mask = (nmeaOk ? HB_NMEA : 0) | (hasFix ? HB_FIX : 0);
  uint8_t state = !setupDone ? MODULE_STATE_STARTING
                : nmeaOk     ? MODULE_STATE_OK
                             : MODULE_STATE_DEGRADED;
  moduleInfo.setHealth(state, mask);

  uint8_t frame[8];
  moduleInfo.packHeartbeat(frame);
  can.beginPacket(moduleInfo.heartbeatId());
  can.write(frame, 8);
  can.endPacket();
}

// Report on USB connect or '?', and send the heartbeat. Also called while the GPS
// is being probed (a few seconds), so the heartbeat does not stop during a retry.
static void gpsService() {
  rp2040.wdt_reset();
  moduleInfo.service();
  sendHeartbeat();
}

//----------------------------------------------------------------------------
// Fault injection for tests (USB serial commands, see ModuleInfo setCommandHook).
//   !N  the GPS goes silent: everything it sends is thrown away, until !n is sent
//   !h  hang the main loop (the watchdog should restart the module)
// The GPS wing is hard to unplug, so this is how the "no data from the GPS" path is tested.
//----------------------------------------------------------------------------
#if FAULT_INJECT
static bool bangSeen = false;

static void faultCommand(char c) {
  if (c == '!') { bangSeen = true; return; }
  if (!bangSeen) return;
  bangSeen = false;
  if (c == 'N') injectSilent = true;
  if (c == 'n') injectSilent = false;
  if (c == 'h') while (true) {}
}
#endif

// Feed everything the GPS has sent to the NMEA parser
static void gpsDrain() {
  while (Serial1.available()) {
    #if FAULT_INJECT
    if (injectSilent) { Serial1.read(); continue; }
    #endif
    gps.read();
  }
}

// True if the sentence starts with '$' and its *XX checksum is correct
static bool gpsNmeaValid(const char *nmea) {
  if (!nmea || nmea[0] != '$') return false;
  uint8_t sum = 0;
  const char *c = nmea + 1;
  while (*c && *c != '*' && *c != '\r' && *c != '\n') sum ^= (uint8_t)*c++;
  if (*c != '*') return false;
  char *end;
  char hex[3] = { c[1], c[2], 0 };
  long given = strtol(hex, &end, 16);
  return (end == hex + 2) && (given == sum);
}

// Read the GPS for up to timeoutMs; true as soon as a valid sentence arrives
static bool gpsWaitForNMEA(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    gpsService();
    gpsDrain();
    if (gps.newNMEAreceived()) {
      const char *nmea = gps.lastNMEA();
      bool ok = gpsNmeaValid(nmea);
      if (ok) gps.parse((char *)nmea);
      if (ok) { noteValidNmea(); return true; }
    }
  }
  return false;
}

static void gpsOpenSerial(uint32_t baud) {
  Serial1.end();
  Serial1.begin(baud);
  gps.begin(baud);
  while (Serial1.available()) Serial1.read();   // discard leftovers
}

// Find the GPS's current baud rate, move it to 115200, and configure output.
// Returns true once valid NMEA has been seen at 115200.
static bool gpsInit(int attempts) {
  for (int a = 0; a < attempts; a++) {
    bool ready = false;

    // Already at 115200? (RP2040-only reset, or baud remembered by the GPS)
    gpsOpenSerial(GPS_BAUD_FAST);
    if (gpsWaitForNMEA(GPS_PROBE_MS)) {
      ready = true;
    } else {
      // Power-up default: 9600. Confirm it is talking, then switch it.
      gpsOpenSerial(GPS_BAUD_INITIAL);
      if (gpsWaitForNMEA(GPS_PROBE_MS)) {
        gps.sendCommand(PMTK_SET_BAUD_115200);
        delay(500);
        gpsOpenSerial(GPS_BAUD_FAST);
        ready = gpsWaitForNMEA(GPS_PROBE_MS);
      }
    }

    if (!ready) continue;

    // Output only RMC, GGA and GSA, at 1 Hz, with SBAS/WAAS enabled.
    // PMTK314 fields: GLL,RMC,VTG,GGA,GSA,GSV,GRS,GST,...,ZDA,...
    gps.sendCommand("$PMTK314,0,1,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29");
    delay(100);
    gps.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
    delay(100);
    gps.sendCommand(PMTK_ENABLE_SBAS);
    delay(100);
    gps.sendCommand(PMTK_ENABLE_WAAS);
    delay(100);
    return true;
  }
  return false;
}


/*****************************************************************************
 * Setup Code
 *
 * Connect to all the Peripherals
 *****************************************************************************/

void setup() {

  led.begin();
  led.startup();      // solid red until setup() has finished

  // USB serial never waits for a host. Startup results are stored with
  // moduleInfo.log() and printed from loop() when a host connects, or on '?'.
  moduleInfo.begin();
  if (rp2040.getResetReason() == RP2040::WDT_RESET) moduleInfo.log("Restarted by the watchdog");

  bool gps_success, can_success;

  moduleInfo.setModuleNumber(MODULE_NUM_GPS);
  moduleInfo.setReportHook(canReport);
  #if FAULT_INJECT
  moduleInfo.setCommandHook(faultCommand);
  #endif
  moduleInfo.setHealth(MODULE_STATE_STARTING, 0);
  led.setServiceCallback(gpsService);   // '?' report and heartbeat during LED waits

  // --------------------------------------------------------------------------
  // Connect to the CAN Bus - before the GPS, so the heartbeat can run while the GPS is probed
  // --------------------------------------------------------------------------

  #if defined(DEBUG_NO_CAN)
  can_success = true;  // Skip CAN init for serial-only debugging
  if (Serial) {
    Serial.println("DEBUG_NO_CAN: CAN bus skipped, output to Serial.");
  }
  #else
  can_success = can.begin(CAN_BAUDRATE);
  #endif

  moduleInfo.log(can_success ? "CAN bus OK" : "CAN bus init FAILED");
  #if !defined(DEBUG_NO_CAN)
  canUp = can_success;      // with DEBUG_NO_CAN there is no CAN to send the heartbeat on
  #endif


  // --------------------------------------------------------------------------
  // Connect to the GPS module
  // Probe for the GPS baud rate, switch it to 115200, then configure it
  // --------------------------------------------------------------------------

  gps_success = gpsInit(GPS_INIT_ATTEMPTS);
  lastValidNmea = millis();
  moduleInfo.log(gps_success ? "GPS OK (NMEA at 115200)" : "GPS not responding (loop() keeps retrying)");

  // --------------------------------------------------------------------------
  // Go / No-Go decision
  // --------------------------------------------------------------------------

  if (can_success) {
    // The runtime heartbeat colour shows whether the GPS is talking and has a fix
    led.go();
    setupDone = true;
    rp2040.wdt_begin(WATCHDOG_MS);
  } else {
    moduleInfo.fatal("Startup failed");
    led.postCode(POST_CAN);   // red flashes forever; the callback keeps '?' working
  }
}


/*****************************************************************************
 * Main Loop
 *
 * Tasks:
 * 1. Read and parse GPS NMEA data
 * 2. Update GPS data variables
 * 3. Transmit CAN data for GPS position, speed/alt, and time sync
 *****************************************************************************/

void loop() {

  gpsService();   // report on USB connect or '?', and the CAN heartbeat

  // LED heartbeat colour. A GPS with no valid NMEA is degraded (amber); with NMEA the colour
  // shows the fix (blue none, yellow 2D, green 3D); nobody acknowledging our CAN frames is red.
  {
    bool nmeaOk = realNmeaSeen && (millis() - lastRealNmea < GPS_SILENCE_MS);
    StatusLEDHealth health;
    if (canUp && can.txFault()) health = STATUS_LED_CAN_FAULT;
    else if (!nmeaOk) health = STATUS_LED_SENSOR_FAULT;
    else if (!gps.fix) health = STATUS_LED_NO_GPS_FIX;
    else if (gps.fixquality_3d >= 3) health = STATUS_LED_GPS_3D_FIX;
    else health = STATUS_LED_GPS_2D_FIX;
    led.setHealth(health);
    led.heartbeat();
  }

  // --------------------------------------------------------------------------
  // Define variables
  // --------------------------------------------------------------------------
  // Unions for CAN data. Allow loading different data types into various
  // parts of the CAN data message.
  // The unions are declared as static variables so they are not re-initialized
  // every time the loop runs.

  // GPS1 message: 2 x int32_t (latitude, longitude)
  static union {
    int32_t longs[2];
    uint8_t bytes[8];
  } canGps1Data = {};

  // GPS2 message: 4 x int16_t (speed, altitude, track, mag track)
  static union {
    int16_t integers[4];
    uint8_t bytes[8];
  } canGps2Data = {};

  // GPS3 message: GPS status (fixquality, fix3d, sats, reserved, HDOP, VDOP)
  static uint8_t canGps3Data[8] = {};

  // Time Sync message: 8 x uint8_t
  static uint8_t canTimeSyncData[8] = {};

  // --------------------------------------------------------------------------
  // Read and parse GPS data
  // --------------------------------------------------------------------------
  // Drain the serial buffer by reading all available characters each loop.
  // gps.read() processes one character at a time; without this loop the
  // buffer overflows when the rest of the loop is slow (e.g. WMM math).

  gpsDrain();

  if (gps.newNMEAreceived()) {
    #if defined(DEBUG)
    static unsigned long lastNmeaDebug = 0;
    bool printNmea = (millis() - lastNmeaDebug > 5000);
    if (printNmea && Serial) {
      Serial.print("NMEA: ");
      Serial.print(gps.lastNMEA());
    }
    #endif
    if (gpsNmeaValid(gps.lastNMEA())) {
      lastValidNmea = millis();
      noteValidNmea();
    }
    gps.parse(gps.lastNMEA());
    #if defined(DEBUG)
    if (printNmea && Serial) {
      Serial.print("  fix="); Serial.print(gps.fix);
      Serial.print(" qual="); Serial.print(gps.fixquality);
      Serial.print(" sat="); Serial.print((int)gps.satellites);
      Serial.print(" date="); Serial.print(gps.month);
      Serial.print("/"); Serial.print(gps.day);
      Serial.print("/"); Serial.print(gps.year);
      Serial.print(" time="); Serial.print(gps.hour);
      Serial.print(":"); Serial.print(gps.minute);
      Serial.print(":"); Serial.println(gps.seconds);
      lastNmeaDebug = millis();
    }
    #endif
  }

  // --------------------------------------------------------------------------
  // GPS watchdog: if no valid NMEA for GPS_SILENCE_MS, drop the stale fix and
  // re-run the baud probe / configuration. The 5 s spacing between retries
  // comes from resetting lastValidNmea after each attempt.
  // --------------------------------------------------------------------------

  if (millis() - lastValidNmea > GPS_SILENCE_MS) {
    gps.fix = false;
    gps.fixquality = 0;
    gps.fixquality_3d = 0;
    gps.satellites = 0;
    gps.HDOP = 0;
    gps.VDOP = 0;

    gpsReprobes++;
    gpsInit(1);
    lastValidNmea = millis();
  }

  // --------------------------------------------------------------------------
  // Update GPS data variables when we have a fix
  // --------------------------------------------------------------------------

  if (gps.fix) {

    // Get latitude and longitude in decimal degrees
    // Multiply by 1,000,000 to capture 6 decimal places
    // 4-byte int32_t holds 9 significant digits without loss
    // 6 decimal places = accuracy to 0.01" (seconds of arc)

    latitude = (int32_t)(gps.latitudeDegrees * 1000000.0f);
    longitude = (int32_t)(gps.longitudeDegrees * 1000000.0f);

    if (gps.speed >= 0) {
      groundSpeed = (int16_t)gps.speed;           // knots
    }

    if (gps.altitude > -9999) {
      gpsAltitude = (int16_t)(gps.altitude * M_TO_FT);  // metres -> feet
    }

    // Update time data
    gpsHour = gps.hour;
    gpsMinute = gps.minute;
    gpsSecond = gps.seconds;

    uint16_t fullYear = gps.year;  // Adafruit GPS library returns 2-digit year
    if (fullYear > 255) {
      fullYear = fullYear - 2000;
    }
    gpsYear = (uint8_t)fullYear;
    gpsMonth = gps.month;
    gpsDay = gps.day;

    if (gpsMonth > 0 && gpsDay > 0) {
      gpsHasDate = true;
    }

    // Compute magnetic declination from WMM2025 (throttled — declination
    // changes negligibly over minutes, but the computation is expensive on
    // the M0+ with no hardware FPU and was starving the serial buffer)
    // The first calculation is made as soon as there is a fix and a date, then every minute.
    if (gpsHasDate && (!magDeclinationValid || millis() - lastWmmCompute > WMM_COMPUTE_INTERVAL)) {
      magDeclination = computeMagDeclination(
        gps.latitudeDegrees, gps.longitudeDegrees, gps.altitude,
        gpsYear, gpsMonth, gpsDay);
      magDeclinationValid = true;
      lastWmmCompute = millis();
    }

    if (gps.angle >= 0 && gps.speed >= TRACK_SPEED_THRESHOLD) {
      trueTrack = (int16_t)gps.angle;             // degrees true

      // Calculate magnetic track from true track and WMM declination
      // Magnetic heading = True heading - Declination (East +, West -)
      float magTrack = gps.angle - magDeclination;
      if (magTrack < 0) magTrack += 360.0f;
      if (magTrack >= 360.0f) magTrack -= 360.0f;
      trackMag = (int16_t)magTrack;
    } else if (gps.speed < TRACK_SPEED_THRESHOLD) {
      trueTrack = 0;                              // no meaningful track when stationary
      trackMag = 0;
    }
  }

  // **************************************************************************
  // Send CAN Data
  // Only send GPS data when we have a valid fix (matches Python behaviour)
  // **************************************************************************

  if (gps.fix) {

    // --------------------------------------------------------------------------
    // Send the GPS Coordinates (latitude, longitude)
    // --------------------------------------------------------------------------

    if (canGps1Timer.due()) {
      canGps1Data.longs[0] = latitude;
      canGps1Data.longs[1] = longitude;

      #if defined(DEBUG_NO_CAN)
      serialPrintCANMsg(CAN_GPS1_MSG_ID, canGps1Data.bytes, 8);
      #else
      can.beginPacket(CAN_GPS1_MSG_ID);
      can.write(canGps1Data.bytes, 8);
      can.endPacket();
      #endif
      canGps1Timer.sent();
    }

    // --------------------------------------------------------------------------
    // Send the GPS Speed, Altitude, and True Track
    // --------------------------------------------------------------------------

    if (canGps2Timer.due()) {
      canGps2Data.integers[0] = groundSpeed;
      canGps2Data.integers[1] = gpsAltitude;
      canGps2Data.integers[2] = trueTrack;
      canGps2Data.integers[3] = trackMag;

      #if defined(DEBUG_NO_CAN)
      serialPrintCANMsg(CAN_GPS2_MSG_ID, canGps2Data.bytes, 8);
      #else
      can.beginPacket(CAN_GPS2_MSG_ID);
      can.write(canGps2Data.bytes, 8);
      can.endPacket();
      #endif
      canGps2Timer.sent();

      #if defined(DEBUG)
      if (Serial) {
        Serial.print("lat = "); Serial.print(latitude);
        Serial.print(", lon = "); Serial.print(longitude);
        Serial.print(", spd = "); Serial.print(groundSpeed);
        Serial.print(", alt = "); Serial.print(gpsAltitude);
        Serial.print(", trk = "); Serial.print(trueTrack);
        Serial.print(", mag = "); Serial.print(trackMag);
        Serial.print(", dec = "); Serial.print(magDeclination);
        Serial.print(", hasDate = "); Serial.println(gpsHasDate);
      }
      #endif
    }

    // --------------------------------------------------------------------------
    // Send the Time Sync data
    // --------------------------------------------------------------------------

    if (canTimeSyncTimer.due()) {

      if (gpsHasDate) {

        #if defined(DEBUG)
        if (Serial) {
          Serial.print(gpsYear); Serial.print("/");
          Serial.print(gpsMonth); Serial.print("/");
          Serial.print(gpsDay); Serial.print(" ");
          Serial.print(gpsHour); Serial.print(":");
          Serial.print(gpsMinute); Serial.print(":");
          Serial.println(gpsSecond);
        }
        #endif
        canTimeSyncData[0] = gpsYear;
        canTimeSyncData[1] = gpsMonth;
        canTimeSyncData[2] = gpsDay;
        canTimeSyncData[3] = gpsHour;
        canTimeSyncData[4] = gpsMinute;
        canTimeSyncData[5] = gpsSecond;
        canTimeSyncData[6] = 0;
        canTimeSyncData[7] = 0;

        #if defined(DEBUG_NO_CAN)
        serialPrintCANMsg(CAN_TIME_SYNC_MSG_ID, canTimeSyncData, 8);
        #else
        can.beginPacket(CAN_TIME_SYNC_MSG_ID);
        can.write(canTimeSyncData, 8);
        can.endPacket();
        #endif
        canTimeSyncTimer.sent();
      }
    }

  } // end if (gps.fix)

  // --------------------------------------------------------------------------
  // Send GPS Status (sent regardless of fix, so display shows "No Fix" status)
  // --------------------------------------------------------------------------

  if (canGps3Timer.due()) {
    canGps3Data[0] = gps.fixquality;      // 0=No fix, 1=GPS, 2=DGPS (WAAS)
    canGps3Data[1] = gps.fixquality_3d;   // 1=No fix, 2=2D, 3=3D
    canGps3Data[2] = gps.satellites;       // Number of satellites in use
    canGps3Data[3] = declinationByte();    // Magnetic declination, whole degrees, East +; 0x7F = not calculated yet

    // HDOP and VDOP as int16 × 100 (e.g. 1.2 → 120)
    int16_t hdop = (int16_t)(gps.HDOP * 100);
    int16_t vdop = (int16_t)(gps.VDOP * 100);
    canGps3Data[4] = hdop & 0xFF;
    canGps3Data[5] = (hdop >> 8) & 0xFF;
    canGps3Data[6] = vdop & 0xFF;
    canGps3Data[7] = (vdop >> 8) & 0xFF;

    #if defined(DEBUG_NO_CAN)
    serialPrintCANMsg(CAN_GPS3_MSG_ID, canGps3Data, 8);
    #else
    can.beginPacket(CAN_GPS3_MSG_ID);
    can.write(canGps3Data, 8);
    can.endPacket();
    #endif
    canGps3Timer.sent();

    #if defined(DEBUG)
    if (Serial) {
      Serial.print("fix = "); Serial.print(gps.fixquality);
      Serial.print(", 3d = "); Serial.print(gps.fixquality_3d);
      Serial.print(", sat = "); Serial.print(gps.satellites);
      Serial.print(", hdop = "); Serial.print(gps.HDOP);
      Serial.print(", vdop = "); Serial.println(gps.VDOP);
    }
    #endif
  }
}
