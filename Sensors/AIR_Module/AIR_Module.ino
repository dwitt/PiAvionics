

/*****************************************************************************
 * AIR Module
 *****************************************************************************/

/*****************************************************************************
 * Boards used
 * Adafruit Feather RP2040
 * Adafruit CAN Bus FeatherWing
 * Custom Air FeatherWing
 *****************************************************************************/

#include <Wire.h>               // I2C library
#include "src/Adafruit_MCP2515/src/Adafruit_MCP2515.h"   // for CAN bus FeatherWing
#include "src/StatusLED/StatusLED.h"         // status LED: startup colours and runtime heartbeat
#include "src/SendTimer/SendTimer.h"         // send each CAN message every period +/- 20 ms
#include <Adafruit_MAX31865.h>  // for MAX31865 RTD temperature sensor
#include "MS4525DO.h"           // MS4525DO Sensor Library
#include "SSC.h"                // SSC Sensor Library
#include "KalmanFilter.h"       // Kalman Filter Library
#include "RollingAverageInt.h"  // Rolling Average Library
#include "LowPassFilter.h"      // Low Pass Filter Library
#include "ButterworthLowPass.h" // Butterworth Low Pass Filter Library
#include "RollingAverageFilter.h" // Rolling Average Filter Library
#include "SmoothDeadband.h"      // Smooth Deadband Library
#include "DeadbandFilter.h"      // Deadband Filter Library
#include "src/ModuleInfo/ModuleInfo.h"        // USB product name, startup log and '?' report
#include "src/SensorHealth/SensorHealth.h"      // valid / invalid decision for each sensor

/*****************************************************************************
 * Firmware version - increment the patch number on every change
 *****************************************************************************/
#define VERSION "1.0.37"

// Set to 0 to run without the AOA sensor (Wire1, GPIO8/9). With 0, Wire1 is not
// touched at all and a missing AOA sensor does not stop startup.
#define AOA_ENABLED 1
#define CAN_INT_WORKAROUND 1   // 1 = clear MCP2515 CANINTE at start-up. Not needed with the INT trace cut (tested
                               // 2026-10-17), kept as a safety net for a wing whose INT trace is not cut.

ModuleInfo moduleInfo("AIR_Module", VERSION, __DATE__, __TIME__);

/*****************************************************************************
 * Sensor health objects and limits (used by the RTD reader, the sensor loop and
 * the heartbeat; see the heartbeat section below)
 *****************************************************************************/

#define HB_PITOT   0x01
#define HB_AOA     0x02
#define HB_STATIC  0x04
#define HB_OAT     0x08
#define HB_ALL     (HB_PITOT | HB_AOA | HB_STATIC | HB_OAT)

#define PRESSURE_FAIL_COUNT   20      // bad samples in a row (0.2 s at 100 Hz) before a sensor is invalid
#define PRESSURE_GOOD_COUNT   5       // good samples in a row before it is valid again
#define PRESSURE_STUCK_COUNT  300     // identical samples in a row (3 s) = locked up
#define OAT_FAIL_COUNT        8       // bad RTD samples in a row (2 s at 4 Hz)
#define OAT_GOOD_COUNT        3
#define OAT_MIN_C             -60.0f  // an outside air temperature outside this range is not real
#define OAT_MAX_C             80.0f
#define PITOT_MAX_HPA         70.0f   // +-1 psi sensor: 68.9 hPa is full scale
#define STATIC_MIN_HPA        150.0f  // about 44000 ft
#define STATIC_MAX_HPA        1300.0f

#define FAULT_INJECT 1    // test commands on USB serial, see "Fault injection" below; 0 for flight
#if FAULT_INJECT
static bool injectPitotFail = false, injectAoaFail = false, injectStaticFail = false, injectOatFail = false;
#endif

struct PressureChannel {
  const char *name;
  SensorHealth health;
  const char *reason = "none";      // why the last bad sample was bad
  bool usable = true;               // false: not found at start-up
  float last[1] = {0};              // for the stuck check
  PressureChannel(const char *n) : name(n), health(PRESSURE_FAIL_COUNT, PRESSURE_GOOD_COUNT, PRESSURE_STUCK_COUNT) {}
};

static PressureChannel pitotChan("Pitot"), aoaChan("AOA"), staticChan("Static");
static SensorHealth oatHealth(OAT_FAIL_COUNT, OAT_GOOD_COUNT, 0);
static const char *oatReason = "none";



/*****************************************************************************
 * Defines to help with debugging
 ****************************************************************************/
//#define DEBUG

/*****************************************************************************
 * Create the objects using the libraries
 * Note: When using the Wire1 interface, the SDA and SCL pins must be set
 * to the GPIO8 and GPIO9 pins. But, the CAN Bus FeatherWing uses the GPIO8 
 * for its interrupt line. We are not using the interrupt line so we can cut 
 * the connection on the CAN Bus FeatherWing.
 *****************************************************************************/

#define MS45X5DO_PITOT_I2C_ADDRESS 0x28  // on the Wire interface i2c1
MS4525DO  ms4525do_pitot;

#define MS45X5DO_AOA_I2C_ADDRESS 0x28  // on the Wire1 interface i2c0
MS4525DO  ms4525do_aoa;

#define SSC_I2C_ADDRESS 0x78
SSC ssc;

// Status LED (Feather RP2040 onboard NeoPixel). See Sensors/Libraries/StatusLED/StatusLED.md
#define POST_CAN 11     // red flashes if the CAN bus does not start
StatusLED led;

// Kalman Filter (Q, R, P, X)
KalmanFilter pitotPressureKalman(10, 10, 10, 0);
KalmanFilter staticPressureKalman(1, 850000, 0, 1013.25);

// Rolling Average
RollingAverageInt staticPressureRollingAverage(50);
RollingAverageFilter staticPressureRollingAverageFilter(128, 256);

// Low Pass Filter
LowPassFilter StaticPressureLowPassFilter(0.01);
// ----------------------------------------------------------------------------
// Pitot pressure low-pass filter that reacts to CHANGE, not to the pressure level.
//
// The raw MS4525DO reading is noisy (measured at rest 2026-10-17: std dev about
// 4.7 Pa, in steps of about 6 Pa), so a fixed filter has to be slow (alpha 0.04,
// ~0.25 s). That is also slow to drop to zero when the pressure is removed.
//
// This filter looks at how far the new sample is from the current output (the
// innovation). A sample within the noise band is averaged hard (PITOT_LP_ALPHA_SLOW)
// so the noise is removed. A sample well outside the noise band means the pressure
// really changed, so the filter follows at PITOT_LP_ALPHA_FAST. In between alpha is
// blended linearly. Using the difference, not the pressure level, means the output
// falls as fast as it rises when the pressure stops (a filter whose strength came
// from the pressure itself lingered after a blow). Measured on the module: the
// raw pressure drops from ~100 Pa to rest within 0.2 s of releasing the pitot.
// ----------------------------------------------------------------------------
#define PITOT_NOISE_HPA       0.047f   // raw noise std dev at rest, in hPa (4.7 Pa)
#define PITOT_LP_ALPHA_SLOW   0.02f    // inside the noise band: ~0.5 s at 100 Hz
#define PITOT_LP_ALPHA_FAST   0.2f     // clearly a real change: ~0.05 s
#define PITOT_LP_BAND_LOW     2.0f     // below this many noise std devs: all slow
#define PITOT_LP_BAND_HIGH    5.0f     // above this many noise std devs: all fast

static float pitotFilterValue = 0.0f;
static bool pitotFilterInitialized = false;
static bool pitotFilterRestart = false;    // set when the pitot has just come back
static void pitotFilterReset() { pitotFilterInitialized = false; }

static float pitotPressureAdaptiveFilter(float input) {
  float &value = pitotFilterValue;
  bool &initialized = pitotFilterInitialized;
  if (!initialized) {
    value = input;
    initialized = true;
    return value;
  }
  float change = fabsf(input - value) / PITOT_NOISE_HPA;     // in noise std devs
  float t = (change - PITOT_LP_BAND_LOW) / (PITOT_LP_BAND_HIGH - PITOT_LP_BAND_LOW);
  t = constrain(t, 0.0f, 1.0f);
  float alpha = PITOT_LP_ALPHA_SLOW + (PITOT_LP_ALPHA_FAST - PITOT_LP_ALPHA_SLOW) * t;
  value += alpha * (input - value);
  return value;
}

// Butterworth Low Pass Filter
ButterworthLowPass staticPressureButterworthLowPass;
ButterworthLowPass staticPressureButterworthLowPass2;
ButterworthLowPass pitotPressureButterworthLowPass; 

// Smooth Deadband
SmoothDeadband staticPressureSmoothDeadband(0.11, 0.7);
SmoothDeadband pitotPressureSmoothDeadband(0.1, 0.01);

// Deadband Filter
DeadbandFilter staticPressureDeadbandFilter(0.11, 0.7);
DeadbandFilter pitotPressureDeadbandFilter(0.12, 0.5);


// ----------------------------------------------------------------------------
// The MCP2515 (CAN Bus) uses the SPI interface
// ----------------------------------------------------------------------------

#define CAN_CS_PIN 7
#define CAN_BAUDRATE (250000)

Adafruit_MCP2515 can(CAN_CS_PIN);

// MAX31865 RTD temperature sensor (OAT) on SPI, separate CS pin
#define RTD_CS_PIN 24
#define RTD_NOMINAL 100.0       // PT100 nominal resistance
#define RTD_REF_RESISTOR 430.0  // Adafruit breakout reference resistor
Adafruit_MAX31865 rtd(RTD_CS_PIN);

// How the RTD is read (see the "RTD (OAT) reader" section before setup()).
// The OAT is sampled every RTD_SAMPLE_PERIOD_MS. Each sample is a short state
// machine that never waits: bias on, settle, convert, read, check faults.
#define RTD_SAMPLE_PERIOD_MS 250  // time between the start of two samples
#define RTD_BIAS_SETTLE_MS   11   // wait after bias on (chip needs about 10.5 ms)
#define RTD_CONVERSION_MS    65   // one-shot conversion (52 ms with the 60 Hz setting, 62.5 ms with 50 Hz)
#define RTD_FAULT_CYCLE_MS   2    // automatic fault detection takes about 1 ms

// Smooths the OAT. Samples arrive every 250 ms. The filter time constant is
// -250 ms / ln(1 - alpha): alpha 0.15 = 1.5 s, 0.30 = 0.7 s, 0.50 = 0.36 s.
// Noise on the reading falls by sqrt(alpha / (2 - alpha)): about 0.29 at 0.15,
// 0.42 at 0.30 and 0.58 at 0.50. The raw reading has about 0.14 C of noise.
// Most of the lag in the reading is the probe itself (about 5 s in still air,
// measured), so a lighter filter helps only a little. 0.15 felt too slow, 0.5
// was tried, and 0.30 is the current choice (2026-10-16).
LowPassFilter oatLowPassFilter(0.3);

// CAN Message Constants ------------------------------------------------------
#define CAN_AIR_MSG_ID    0x28
#define CAN_AOA_MSG_ID    0x29
#define CAN_TEMP_MSG_ID   0x2A
#define CAN_RAW_MSG_ID    0x2B
// The QNH block (Protocol/can_messages.yaml): a frame from any ID 0x2C to 0x2F sets the QNH. One hardware filter
// covers it: ID 0x2C, mask 0x7FC. This module never sends in the block.
#define CAN_QNH_FIRST_ID  0x2C
#define CAN_QNH_LAST_ID   0x2F
#define CAN_QNH_FILTER_MASK 0x7FC
#define QNH_LISTEN_MS     2000     // after start-up, wait this long for a QNH frame before using 29.92
#define QNH_FLAG_NOT_SET  0x01     // 0x2B byte 7 bit 0

// Invalid-data markers (Protocol/can_messages.yaml): a field that cannot be trusted is sent as the largest
// positive value of its type, so a receiver shows it as failed and never as a number.
#define CAN_MARKER_INT16  0x7FFF
#define CAN_MARKER_INT32  0x7FFFFFFF

#define CAN_AIR_PERIOD    100 
#define CAN_AOA_PERIOD    200
#define CAN_TEMP_PERIOD   200
#define CAN_RAW_PERIOD    200

// ----------------------------------------------------------------------------
// Create Adafruit Sensor object pointers
// ----------------------------------------------------------------------------

Adafruit_Sensor *pitot_pressure, *pitot_temp, *aoa_pressure, *aoa_temp;
Adafruit_Sensor *static_pressure, *static_temp;

// ----------------------------------------------------------------------------
// Define locally global variables and functions
// ----------------------------------------------------------------------------
static uint32_t timestamp;      // tracking the timing in the main loop

// Timestamps to determine when to send the next CAN message
static SendTimer canAirTimer(CAN_AIR_PERIOD);     // each wait is the period +/- 20 ms, drawn once per send
static SendTimer canAoaTimer(CAN_AOA_PERIOD);
static SendTimer canTempTimer(CAN_TEMP_PERIOD);
static SendTimer canRawTimer(CAN_RAW_PERIOD);

// Data to send via CAN
// Declare static so they are local to this file

static int16_t airSpeed = 0;        // CANbus Data: knots
static int32_t altitude = 0;        // CANbus Data: feet
static int16_t verticalSpeed = 0;   // CANbus Data: feet/minute
static int16_t aoa = 0;             // CANbus Data: degrees
static int32_t staticPressure = 0;  // CANbus Data: hPa * 10
static int16_t staticTemp = 0;      // CANbus Data: degrees C * 10
static int16_t oatTemp = 0;         // CANbus Data: degrees C * 10 (from MAX31865 RTD)
static bool rtdAvailable = false;   // tracks whether RTD init succeeded
static int16_t pitotTemp = 0;       // CANbus Data: degrees C * 10
static int16_t pitotPressure = 0;   // CANbus Data: 

// Sensor data variables

static float sensorStaticPressure = 0;
static float sensorStaticTemp = 0;
static float sensorPitotTemp = 0;
static float sensorPitotPressure = 0;

// Data variables
static float previousStaticPressure = 0;  // hPa (100 Pa [kg/m/s^2])
static float staticPressureFiltered = 0;  // hPa (100 Pa [kg/m/s^2])
static float previousPitotPressure = 0;   // hPa (100 Pa [kg/m/s^2])
static float pitotPressureFiltered = 0;   // hPa (100 Pa [kg/m/s^2])
static float previousAOA = 0;
static float aoaFiltered = 0;            // degrees

static int16_t qnh = 2992;         // inHg * 100
static bool qnhSet = false;        // true once a valid QNH frame has been heard; nothing is stored
static uint32_t qnhListenEnd = 0;  // millis() when the start-up listen window ends (set at the end of setup())

static float staticPressureLowPassFiltered = 0;   // hPa, after the low-pass, before the deadband

// ----------------------------------------------------------------------------
// Vertical speed (ft/min, positive = climbing)
//
// Source: the static pressure AFTER the low-pass but BEFORE the smooth deadband.
// The deadband holds the output and then jumps 1-2 ft, which would show up as
// spikes in ft/min. Measured at rest 2026-10-17: raw static pressure std dev is
// 0.23 hPa (6 ft) in steps of 0.12 hPa (3.3 ft); after the low-pass it is about
// 0.012 hPa (0.3 ft).
//
// Method: every VSI_SAMPLE_MS the pressure altitude of the low-passed pressure is
// stored in a ring buffer holding VSI_WINDOW_SAMPLES samples (3 s). The rate is
// the altitude now minus the altitude one window ago, divided by the window.
// That rate is then smoothed with a low-pass (VSI_ALPHA). Expected at rest: about
// +-15 ft/min. Expected response: ~1.5 s to show a real climb change (the 1 s
// pressure low-pass plus the smoothing). The altitude uses the standard
// atmosphere (1013.25 hPa); the QNH cancels out of a difference.
// ----------------------------------------------------------------------------
#define VSI_SAMPLE_MS        100     // how often an altitude sample is stored
#define VSI_WINDOW_SAMPLES   30      // 30 x 100 ms = 3 s differencing window
#define VSI_ALPHA            0.1f    // smoothing of the rate, per sample (~1 s)
#define VSI_MAX_FPM          6000    // clamp to a sane range before packing into an int16

static float vsiHistory[VSI_WINDOW_SAMPLES];
static uint8_t vsiHistoryIndex = 0;
static uint8_t vsiHistoryCount = 0;      // samples stored so far, up to VSI_WINDOW_SAMPLES
static unsigned long vsiLastSampleMs = 0;
static float vsiSmoothed = 0;            // ft/min

#define SEA_LEVEL_DENSITY_ISA 1.225   // kg/m^3
#define V_M_S_TO_KNOTS 1.9438444924406


/*****************************************************************************
 * RTD (OAT) reader - non-blocking MAX31865
 *
 * Why this exists: the Adafruit library's rtd.temperature() takes about 75 ms
 * because it waits with delay() for the bias to settle (10 ms) and for the
 * conversion to finish (65 ms). Measured on this module: 75.2 ms per call. The
 * sensor loop calls it every pass, so the whole loop (pressures, airspeed,
 * altitude, CAN) ran at about 11 Hz instead of the intended 100 Hz.
 *
 * What this does instead: the same steps the library performs, as a state
 * machine that rtdUpdate() advances a little each time loop() runs. Between
 * steps it returns immediately, so the loop keeps running at full speed.
 *
 *   RTD_WAIT      wait for the next sample time (RTD_SAMPLE_PERIOD_MS)
 *       | switch the bias on and clear old faults
 *   RTD_BIAS      wait RTD_BIAS_SETTLE_MS for the RTD input filter to settle
 *       | start a one-shot conversion
 *   RTD_CONVERT   wait RTD_CONVERSION_MS for the conversion to finish
 *       | read the result, start the chip's automatic fault detection
 *   RTD_FAULTS    wait RTD_FAULT_CYCLE_MS, read the fault status, bias off
 *       | accept the sample if there is no fault
 *   RTD_WAIT
 *
 * The bias is only on for about 80 ms of each 250 ms to limit self-heating of
 * the RTD, as the library does. The library's own register access is private,
 * so the few registers needed are read and written here over SPI. The
 * MAX31865 uses SPI mode 1; CAN (mode 0) is on the same bus, and each
 * transaction sets its own mode, so they do not interfere.
 *
 * The reading is converted to degrees with the library's calculateTemperature()
 * (Callendar-Van Dusen), smoothed with a low-pass filter, and rounded to
 * 0.1 C. A sample with a fault is dropped and the last good value is kept.
 *****************************************************************************/

// Base configuration: 3-wire RTD, 60 Hz filter setting, bias off, no conversion
// running. Every write to the config register starts from this and adds bits.
// The 50/60 Hz setting is the chip's notch filter against mains pickup. The
// aircraft has a battery and an alternator, not mains, so neither setting is
// aimed at anything on board (alternator ripple is at other frequencies and
// changes with engine speed). It only matters on the bench, where the 60 Hz
// setting suits North American mains. The setting also fixes the conversion time (see
// RTD_CONVERSION_MS), and the wait is long enough for either.
#define RTD_CONFIG_BASE  (MAX31865_CONFIG_3WIRE | MAX31865_CONFIG_FILT60HZ)

// Config register bit 2 starts the chip's automatic fault detection cycle
// (open RTD, shorted RTD, bad reference). The chip clears it when done.
#define RTD_CONFIG_FAULT_AUTO  0x04

enum RtdStep { RTD_WAIT, RTD_BIAS, RTD_CONVERT, RTD_FAULTS };

static RtdStep rtdStep = RTD_WAIT;
static unsigned long rtdStepStart = 0;   // millis() when the current step began
static uint16_t rtdRaw = 0;              // raw reading saved between CONVERT and FAULTS
static uint32_t rtdFaultCount = 0;       // samples dropped because of a fault
static bool rtdFaultLogged = false;      // only the first fault is written to the log

// Write one register of the MAX31865. The write flag is bit 7 of the address.
static void rtdWrite8(uint8_t reg, uint8_t value) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));
  digitalWrite(RTD_CS_PIN, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(value);
  digitalWrite(RTD_CS_PIN, HIGH);
  SPI.endTransaction();
}

// Read one register of the MAX31865
static uint8_t rtdRead8(uint8_t reg) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));
  digitalWrite(RTD_CS_PIN, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(RTD_CS_PIN, HIGH);
  SPI.endTransaction();
  return value;
}

// Read the 16-bit RTD result (MSB register, then LSB). Bit 0 is the chip's
// fault flag; bits 15..1 are the 15-bit ADC value.
static uint16_t rtdRead16(uint8_t reg) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));
  digitalWrite(RTD_CS_PIN, LOW);
  SPI.transfer(reg & 0x7F);
  uint16_t value = SPI.transfer(0x00);
  value = (value << 8) | SPI.transfer(0x00);
  digitalWrite(RTD_CS_PIN, HIGH);
  SPI.endTransaction();
  return value;
}

// Advance the RTD state machine. Call on every pass of loop(). Never waits.
static void rtdUpdate() {
  if (!rtdAvailable) return;

  unsigned long now = millis();

  switch (rtdStep) {

    case RTD_WAIT:
      if (now - rtdStepStart >= RTD_SAMPLE_PERIOD_MS) {
        // Bias on, and clear any fault left from before
        rtdWrite8(MAX31865_CONFIG_REG,
                  RTD_CONFIG_BASE | MAX31865_CONFIG_BIAS | MAX31865_CONFIG_FAULTSTAT);
        rtdStep = RTD_BIAS;
        rtdStepStart = now;
      }
      break;

    case RTD_BIAS:
      if (now - rtdStepStart >= RTD_BIAS_SETTLE_MS) {
        // Bias has settled: start one conversion
        rtdWrite8(MAX31865_CONFIG_REG,
                  RTD_CONFIG_BASE | MAX31865_CONFIG_BIAS | MAX31865_CONFIG_1SHOT);
        rtdStep = RTD_CONVERT;
        rtdStepStart = now;
      }
      break;

    case RTD_CONVERT:
      if (now - rtdStepStart >= RTD_CONVERSION_MS) {
        // Conversion is done: take the result, then start the fault detection
        rtdRaw = rtdRead16(MAX31865_RTDMSB_REG);
        rtdWrite8(MAX31865_CONFIG_REG,
                  RTD_CONFIG_BASE | MAX31865_CONFIG_BIAS | RTD_CONFIG_FAULT_AUTO);
        rtdStep = RTD_FAULTS;
        rtdStepStart = now;
      }
      break;

    case RTD_FAULTS:
      if (now - rtdStepStart >= RTD_FAULT_CYCLE_MS) {
        uint8_t fault = rtdRead8(MAX31865_FAULTSTAT_REG);

        // Bias off again until the next sample (limits self-heating)
        rtdWrite8(MAX31865_CONFIG_REG, RTD_CONFIG_BASE);
        rtdStep = RTD_WAIT;
        rtdStepStart = now;

        // Judge the sample. The chip's fault flags catch an open or shorted RTD. A missing
        // MAX31865 board reads back as all zeros or all ones, and a reconnected probe can
        // give a wild first reading, so the temperature also has to be possible.
        uint16_t adc = rtdRaw >> 1;
        float oatC = 0;
        bool ok = (fault == 0 && (rtdRaw & 1) == 0 && adc != 0 && adc != 0x7FFF);
        if (ok) {
          oatC = rtd.calculateTemperature(adc, RTD_NOMINAL, RTD_REF_RESISTOR);
          if (!(oatC >= OAT_MIN_C && oatC <= OAT_MAX_C)) { ok = false; oatReason = "temperature out of range"; }
        } else {
          oatReason = (fault != 0 || (rtdRaw & 1)) ? "MAX31865 fault" : "no reading (board missing?)";
        }
        #if FAULT_INJECT
        if (injectOatFail) { ok = false; oatReason = "injected fault"; }
        #endif

        if (ok && !oatHealth.valid()) oatLowPassFilter.reset();   // start smoothing again from new samples
        oatHealth.update(ok);

        if (ok) {
          // Good sample: smooth it, and keep it as degrees C x 10, rounded (not truncated).
          // Not published until the sensor is valid (a few good samples after a failure).
          float smoothed = oatLowPassFilter.update(oatC);
          if (oatHealth.valid()) oatTemp = (int16_t)lroundf(smoothed * 10.0f);
        } else {
          // Faulty sample: keep the last good value. Only the first fault is
          // logged so a bad connection does not fill the startup log.
          rtdFaultCount++;
          if (!rtdFaultLogged) {
            char msg[48];
            snprintf(msg, sizeof(msg), "RTD fault 0x%02X (first)", fault);
            moduleInfo.log(msg);
            rtdFaultLogged = true;
          }
        }
      }
      break;
  }
}

/*****************************************************************************
 * Switch off the MCP2515 interrupt output
 *
 * The CAN FeatherWing INT output is wired to GPIO8, the same pin as Wire1 SDA
 * (AOA sensor). INT stays asserted (low) across a software reset until the chip
 * is reset, which held SDA low and made the AOA sensor "not found". Clearing the
 * interrupt enable register (CANINTE) stops the chip driving INT at all. The
 * CAN library polls the flag register over SPI and does not use the INT pin,
 * so receiving is not affected. This is the software equivalent of cutting the
 * INT trace on the wing.
 *****************************************************************************/

#define MCP2515_CMD_WRITE 0x02
#define MCP2515_REG_CANINTE 0x2B

static void canDisableInterruptOutput() {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CAN_CS_PIN, LOW);
  SPI.transfer(MCP2515_CMD_WRITE);
  SPI.transfer(MCP2515_REG_CANINTE);
  SPI.transfer(0x00);
  digitalWrite(CAN_CS_PIN, HIGH);
  SPI.endTransaction();
}

/*****************************************************************************
 * Wire1 bus check and recovery
 *
 * After a software reset the AOA sensor can be left in the middle of an I2C
 * transfer holding SDA low, or something else on GPIO8 can hold it low (the CAN
 * FeatherWing interrupt line shares GPIO8 with Wire1 SDA). The line levels are
 * logged, and if SDA is low SCL is clocked up to 9 times to let the sensor
 * finish, followed by a STOP condition.
 *****************************************************************************/

#define WIRE1_SDA_PIN 8
#define WIRE1_SCL_PIN 9

static void wire1CheckAndRecover() {
  char msg[64];

  pinMode(WIRE1_SDA_PIN, INPUT_PULLUP);
  pinMode(WIRE1_SCL_PIN, INPUT_PULLUP);
  delay(2);
  bool sdaHigh = digitalRead(WIRE1_SDA_PIN);
  bool sclHigh = digitalRead(WIRE1_SCL_PIN);
  snprintf(msg, sizeof(msg), "Wire1 before start: SDA=%d SCL=%d", sdaHigh, sclHigh);
  moduleInfo.log(msg);

  if (sdaHigh) return;

  int pulses = 0;
  while (!digitalRead(WIRE1_SDA_PIN) && pulses < 9) {
    pinMode(WIRE1_SCL_PIN, OUTPUT);
    digitalWrite(WIRE1_SCL_PIN, LOW);
    delayMicroseconds(10);
    pinMode(WIRE1_SCL_PIN, INPUT_PULLUP);
    delayMicroseconds(10);
    pulses++;
  }

  // STOP condition: SDA low to high while SCL is high
  pinMode(WIRE1_SDA_PIN, OUTPUT);
  digitalWrite(WIRE1_SDA_PIN, LOW);
  delayMicroseconds(10);
  pinMode(WIRE1_SDA_PIN, INPUT_PULLUP);
  delayMicroseconds(10);

  snprintf(msg, sizeof(msg), "Wire1 SDA was low: %d clock pulses, SDA now %d",
           pulses, digitalRead(WIRE1_SDA_PIN));
  moduleInfo.log(msg);
}

/*****************************************************************************
 * Sensor health and CAN heartbeat (Protocol/can_messages.yaml, 0x702)
 *
 * Every sensor has a SensorHealth object that decides, from the stream of good and
 * bad samples, whether it is working (Sensors/FAILURE_DETECTION_PLAN.md).
 * A pressure sample is bad if the I2C read fails, the sensor status says fault, the
 * count is 0 or full scale (stuck bus), the pressure cannot be real, or the value
 * has not changed in 3 s (locked up). A sensor that was not found at start-up stays
 * invalid (an MS4525DO cannot be zeroed in flight; the SSC could recover).
 *
 * The heartbeat is sent every second: module version, state and a mask of the
 * sensors that are working: bit 0 pitot, bit 1 AOA, bit 2 static pressure, bit 3
 * outside air temperature (RTD). State: STARTING until setup() finishes, OK with
 * all four bits set, DEGRADED when one is missing, FAILED when neither pitot nor
 * static pressure works (or CAN did not start).
 *
 * What the data frames do while a sensor is invalid (until the invalid-data marker
 * is added with the EFIS pass): airspeed 0 and differential pressure 0 when the
 * pitot is invalid; altitude holds its last value and vertical speed is 0 when the
 * static pressure is invalid; the OAT frame (0x2A) is not sent when the RTD is invalid.
 *****************************************************************************/

// Classify one pressure sample. status is the driver's ReadStatus (0 = OK, 1 I2C, 2 status, 3 rail).
static bool pressureSampleOk(PressureChannel &ch, uint8_t status, float hPa, float lo, float hi, bool injected) {
  static const char *why[] = {"ok", "I2C read failed", "sensor status fault", "stuck bus (count 0 or full scale)"};
  const char *r = why[status & 3];
  bool ok = (status == 0);
  if (ok && (hPa < lo || hPa > hi)) { ok = false; r = "out of range"; }
  if (ok && ch.health.stuck(ch.last, &hPa, 1)) { ok = false; r = "value not changing"; }
  if (!ch.usable) { ok = false; r = "not found at start-up"; }
  if (injected) { ok = false; r = "injected fault"; }
  if (!ok) ch.reason = r;
  return ok;
}

static uint8_t hbSensorMask() {
  return (pitotChan.health.valid() ? HB_PITOT : 0) |
         (aoaChan.health.valid()   ? HB_AOA   : 0) |
         (staticChan.health.valid() ? HB_STATIC : 0) |
         (oatHealth.valid()        ? HB_OAT   : 0);
}

static bool canUp = false;            // heartbeat is only sent once the CAN bus is running
static bool setupDone = false;
static bool startupFailed = false;

static void printChannel(Print &out, const char *name, const SensorHealth &h, const char *reason) {
  out.print(name); out.print(h.valid() ? " OK" : " FAILED");
  out.print(" (lost "); out.print(h.failures);
  out.print("x, last problem: "); out.print(reason); out.println(")");
}

// Live sensor and CAN transmit status for the '?' report. A send that nobody acknowledges is
// abandoned after a few ms (patched Adafruit_MCP2515, see Libraries/Adafruit_MCP2515/PATCH_NOTES.md).
static void canReport(Print &out) {
  printChannel(out, "Pitot pressure", pitotChan.health, pitotChan.reason);
  printChannel(out, "AOA pressure", aoaChan.health, aoaChan.reason);
  printChannel(out, "Static pressure", staticChan.health, staticChan.reason);
  printChannel(out, "OAT (RTD)", oatHealth, oatReason);
  out.print("RTD samples dropped for a fault: "); out.println(rtdFaultCount);
  #if FAULT_INJECT
  out.print("Test build: injected faults pitot "); out.print(injectPitotFail);
  out.print(", aoa "); out.print(injectAoaFail);
  out.print(", static "); out.print(injectStaticFail);
  out.print(", oat "); out.println(injectOatFail);
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

static uint8_t airModuleState(uint8_t mask) {
  bool noAirData = !pitotChan.health.valid() && !staticChan.health.valid();
  return startupFailed ? MODULE_STATE_FAILED
       : !setupDone    ? MODULE_STATE_STARTING
       : noAirData     ? MODULE_STATE_FAILED
       : (mask == HB_ALL) ? MODULE_STATE_OK
                          : MODULE_STATE_DEGRADED;
}

// Heartbeat colour: green OK, amber (degraded), red (failed, or nobody acknowledges our CAN frames)
static void updateLedHealth() {
  uint8_t state = airModuleState(hbSensorMask());
  StatusLEDHealth health = STATUS_LED_HEALTHY;
  if (canUp && can.txFault()) health = STATUS_LED_CAN_FAULT;
  else if (state == MODULE_STATE_FAILED) health = STATUS_LED_FAILED;
  else if (state == MODULE_STATE_DEGRADED) health = STATUS_LED_SENSOR_FAULT;
  led.setHealth(health);
}

static void sendHeartbeat() {
  if (!canUp || !moduleInfo.heartbeatDue()) return;

  uint8_t mask = hbSensorMask();
  uint8_t state = airModuleState(mask);
  moduleInfo.setHealth(state, mask);

  uint8_t frame[8];
  moduleInfo.packHeartbeat(frame);
  can.beginPacket(moduleInfo.heartbeatId());
  can.write(frame, 8);
  can.endPacket();
}

// Called from loop() and, through the StatusLED callback, while the LED code waits or blinks
// a failure code, so the heartbeat and the '?' report keep working.
static void airService() {
  moduleInfo.service();
  sendHeartbeat();
}

//----------------------------------------------------------------------------
// Fault injection for tests (USB serial commands, see ModuleInfo setCommandHook).
// Each command is "!" followed by a letter. Capital = fault on, lower case = off:
//   !P/!p pitot   !A/!a AOA   !S/!s static pressure   !O/!o OAT (RTD)
// While a fault is injected the sensor's samples are treated as failed reads.
//----------------------------------------------------------------------------
#if FAULT_INJECT
static bool bangSeen = false;

static void faultCommand(char c) {
  if (c == '!') { bangSeen = true; return; }
  if (!bangSeen) return;
  bangSeen = false;
  switch (c) {
    case 'P': injectPitotFail = true; break;
    case 'p': injectPitotFail = false; break;
    case 'A': injectAoaFail = true; break;
    case 'a': injectAoaFail = false; break;
    case 'S': injectStaticFail = true; break;
    case 's': injectStaticFail = false; break;
    case 'O': injectOatFail = true; break;
    case 'o': injectOatFail = false; break;
  }
}
#endif

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
  moduleInfo.setModuleNumber(MODULE_NUM_AIR);
  moduleInfo.setReportHook(canReport);
  #if FAULT_INJECT
  moduleInfo.setCommandHook(faultCommand);
  #endif
  moduleInfo.setHealth(MODULE_STATE_STARTING, 0);
  led.setServiceCallback(airService);   // '?' report and heartbeat during LED waits

  // --------------------------------------------------------------------------
  // Connect to the MCP2515 CAN Bus controller on the SPI bus - FIRST, before
  // Wire1. can.begin() resets the chip, which releases its INT output (shared
  // with Wire1 SDA on GPIO8); then the interrupt output is switched off.
  // Provide the baud rate for the CAN Bus.
  // --------------------------------------------------------------------------

  bool can_success = can.begin(CAN_BAUDRATE);
  #if CAN_INT_WORKAROUND
  canDisableInterruptOutput();
  #endif
  canUp = can_success;
  sendHeartbeat();    // first heartbeat (state STARTING) as soon as the bus is up
  moduleInfo.log(can_success ? "CAN bus OK" : "CAN bus (MCP2515) NOT FOUND");

  bool ms4525do_pitot_success, ms4525do_aoa_success, ssc_success;

  // --------------------------------------------------------------------------
  // Connect to the MS4525DO pitot static pressure sensor
  // Provide the address and the I2C interface to use.
  // --------------------------------------------------------------------------

  ms4525do_pitot_success = ms4525do_pitot.begin(MS45X5DO_PITOT_I2C_ADDRESS, &Wire);
  moduleInfo.log(ms4525do_pitot_success ? "MS4525DO pitot OK" : "MS4525DO pitot NOT FOUND");
  
  // --------------------------------------------------------------------------
  // Connect to the MS4525DO aoa  pressure sensor
  // Provide the address and the I2C interface to use.
  // The CAN Bus FeatherWing uses the GPIO8 for interrupt line, so the board
  // nees the trace to be cut otherwise it affects the I2C communication.
  // --------------------------------------------------------------------------

  #if AOA_ENABLED
  wire1CheckAndRecover();
  Wire1.setSDA(8);  // GPIO8
  Wire1.setSCL(9);  // GPIO9

  ms4525do_aoa_success = ms4525do_aoa.begin(MS45X5DO_AOA_I2C_ADDRESS, &Wire1);
  moduleInfo.log(ms4525do_aoa_success ? "MS4525DO aoa OK" : "MS4525DO aoa NOT FOUND");
  #else
  ms4525do_aoa_success = true;   // not used, so it must not stop startup
  moduleInfo.log("MS4525DO aoa DISABLED (AOA_ENABLED 0)");
  #endif

  // --------------------------------------------------------------------------
  // Connect to the SSC sensor
  // Provide the address and the I2C interface to use.
  // --------------------------------------------------------------------------

  ssc_success = ssc.begin(SSC_I2C_ADDRESS, &Wire);
  moduleInfo.log(ssc_success ? "SSC static pressure OK" : "SSC static pressure NOT FOUND");

  // --------------------------------------------------------------------------
  // Connect to the MAX31865 RTD temperature sensor (OAT)
  // --------------------------------------------------------------------------

  rtdAvailable = rtd.begin(MAX31865_3WIRE);
  moduleInfo.log(rtdAvailable ? "MAX31865 RTD OK" : "MAX31865 RTD NOT FOUND");
  rtdStepStart = millis() - RTD_SAMPLE_PERIOD_MS;   // take the first sample immediately
  oatHealth.markInvalid();    // valid after the first few good samples

  // --------------------------------------------------------------------------
  // Make a stop or proceed decision based on whether we could connect
  // to all the sensors.
  // --------------------------------------------------------------------------

  // A sensor that is not found no longer stops the module: it is reported invalid in the
  // heartbeat and the others keep working. An MS4525DO that was not found stays off until
  // the next start (it is zeroed at start-up, which cannot be done in flight). Without CAN
  // the module cannot report anything, so it still stops.
  pitotChan.usable = ms4525do_pitot_success;
  aoaChan.usable = ms4525do_aoa_success;
  if (!ms4525do_pitot_success) pitotChan.health.markInvalid();
  if (!ms4525do_aoa_success) aoaChan.health.markInvalid();
  if (!ssc_success) staticChan.health.markInvalid();      // can recover: the SSC needs no zeroing
  if (!rtdAvailable) moduleInfo.log("**WARNING** no OAT");

  if (!can_success) {
    startupFailed = true;
    moduleInfo.fatal("Startup failed");
    led.postCode(POST_CAN); // red flashes forever; the callback below keeps '?' working
  }

  setupDone = true;

  moduleInfo.log(hbSensorMask() == (HB_PITOT | HB_AOA | HB_STATIC) ?
                 "All peripherals connected" : "**WARNING** running with a sensor missing");

  led.go();               // solid green for 2 s, then the runtime heartbeat takes over

  /***************************************************************************
   * Configure the peripherals
   * Set the transfer functions/types and the pressure ranges
   ***************************************************************************/

  ms4525do_pitot.setOutputType(MS4525DO::MS4525DO_Type_A);
  #if AOA_ENABLED
  ms4525do_aoa.setOutputType(MS4525DO::MS4525DO_Type_A);
  #endif
  ssc.setTransferFunction(SSC::SSC_TF_A);

  ms4525do_pitot.setPressureRange(MS4525DO::MS4525DO_001);
  #if AOA_ENABLED
  ms4525do_aoa.setPressureRange(MS4525DO::MS4525DO_001);
  #endif
  ssc.setPressureRange(SSC::SSC_016BA);

  /***************************************************************************
   * Setup the sensors
   ***************************************************************************/

  pitot_pressure = ms4525do_pitot.getPressureSensor();
  pitot_temp = ms4525do_pitot.getTemperatureSensor();
  #if AOA_ENABLED
  aoa_pressure = ms4525do_aoa.getPressureSensor();
  aoa_temp = ms4525do_aoa.getTemperatureSensor();
  #endif
  static_pressure = ssc.getPressureSensor();
  static_temp = ssc.getTemperatureSensor();
  
  //---------------------------------------------------------------------------
  // Print the sensor details if debugging.
  // May block if the serial port is not connected.
  //---------------------------------------------------------------------------

  #if defined(DEBUG)
  pitot_pressure->printSensorDetails();
  pitot_temp->printSensorDetails();
  #if AOA_ENABLED
  aoa_pressure->printSensorDetails();
  aoa_temp->printSensorDetails();
  #endif
  static_pressure->printSensorDetails();
  static_temp->printSensorDetails();
  #endif

  /***************************************************************************
   * Set the I2C speed to 400 kHz
   ***************************************************************************/

  Wire.setClock(400000);
  #if AOA_ENABLED
  Wire1.setClock(400000);
  #endif

  /*************************************************************************** 
   * Configure the CAN bus filters to receive only the QNH block 0x2C to 0x2F
   ***************************************************************************/

  // Maybe consider checking if the filter is set correctly using the return
  // value of the filter function.
  bool filterOk = can.filter(CAN_QNH_FIRST_ID, CAN_QNH_FILTER_MASK);
  moduleInfo.log(filterOk ? "CAN filter OK (QNH block only)" : "**WARNING** CAN filter FAILED (all frames received)");
  // Frames that arrived before the filter was set are still in the chip's two receive buffers, and
  // nothing has read them. A QNH frame arriving before the loop starts reading would find a full
  // buffer and set RX0OVR, which stays set. Read out the old frames, then clear the flag.
  while (can.parsePacket()) {}
  can.clearErrorFlags();

  timestamp = millis();     // initialize the timestamp
  qnhListenEnd = millis() + QNH_LISTEN_MS;   // no altitude is sent until a QNH is heard or this passes

}

void loop() {
  moduleInfo.service();   // report on USB connect or '?'
  sendHeartbeat();        // CAN heartbeat, once a second
  updateLedHealth();
  led.heartbeat();        // brief coloured flash: green OK, amber degraded, red failed
  rtdUpdate();            // advance the non-blocking OAT reader (never waits)

  // --------------------------------------------------------------------------
  // Define variables
  // --------------------------------------------------------------------------
  // Union for CAN data. Allow loading different data typs into various
  // parts of the CAN data message.
  // The union is declared as a static variable so it is not re-initialized
  // every time the loop runs.
  // The variable canData is initialized to an empty struct so that the
  // union is not initialized to random values.


  static union DataUnion 
  {
    int16_t integers[4];
    uint8_t bytes[8];
    float floats[2];
    struct __attribute__((packed)) {
      int16_t integer1;
      int32_t longInteger;
      int16_t integer2;
    } packedData;
  } canData =  {};

  // **************************************************************************
  // Send CAN Data
  // **************************************************************************

  // --------------------------------------------------------------------------
  // Send the Airspeed, Altitude and Vertical Speed data
  // --------------------------------------------------------------------------

  if (canAirTimer.due()) {
    // Pitot lost: no airspeed. Static lost: no altitude and no vertical speed. (A lost sensor used to send
    // airspeed 0 or hold the last altitude.)
    bool pitotOk = pitotChan.health.valid();
    bool staticOk = staticChan.health.valid();
    canData.packedData.integer1 = pitotOk ? airSpeed : (int16_t)CAN_MARKER_INT16;
    // No altitude while listening for a QNH at start-up, so one computed from the unset 29.92 is never sent.
    bool qnhKnown = qnhSet || (int32_t)(millis() - qnhListenEnd) >= 0;
    canData.packedData.longInteger = (staticOk && qnhKnown) ? altitude : (int32_t)CAN_MARKER_INT32;
    canData.packedData.integer2 = staticOk ? verticalSpeed : (int16_t)CAN_MARKER_INT16;

    can.beginPacket(CAN_AIR_MSG_ID);
    can.write(canData.bytes, 8);
    can.endPacket();
    canAirTimer.sent();
  }

  // --------------------------------------------------------------------------
  // Send the AOA data
  // --------------------------------------------------------------------------

  if (canAoaTimer.due()) {
    canData.integers[0] = aoa;

    can.beginPacket(CAN_AOA_MSG_ID);
    can.write(canData.bytes, 8);
    can.endPacket();
    canAoaTimer.sent();
  }

  // --------------------------------------------------------------------------
  // Send the Raw Static Pressure and Temperature data
  // --------------------------------------------------------------------------

  if (canRawTimer.due()) {
    // Pack to match legacy format: <hbhBBB>
    // int16 static_pressure (hPa×10), int8 temperature (°C),
    // int16 differential_pressure (Pa), int16 QNH in use (inHg x 100), flags
    int16_t sp = staticChan.health.valid() ? (int16_t)(staticPressureFiltered * 10)  // hPa → hPa×10
                                           : (int16_t)CAN_MARKER_INT16;
    canData.bytes[0] = sp & 0xFF;
    canData.bytes[1] = (sp >> 8) & 0xFF;
    canData.bytes[2] = (uint8_t)((int8_t)sensorStaticTemp);  // whole °C
    int16_t diffP = pitotChan.health.valid() ? (int16_t)(pitotPressureFiltered * 100)   // hPa → Pa
                                             : (int16_t)CAN_MARKER_INT16;
    canData.bytes[3] = diffP & 0xFF;
    canData.bytes[4] = (diffP >> 8) & 0xFF;
    // QNH in use for the altitude, so a display can flag a baro mismatch. Not valid while still listening.
    bool qnhKnown = qnhSet || (int32_t)(millis() - qnhListenEnd) >= 0;
    int16_t qnhReport = qnhKnown ? qnh : (int16_t)CAN_MARKER_INT16;
    canData.bytes[5] = qnhReport & 0xFF;
    canData.bytes[6] = (qnhReport >> 8) & 0xFF;
    canData.bytes[7] = qnhSet ? 0 : QNH_FLAG_NOT_SET;

    can.beginPacket(CAN_RAW_MSG_ID);
    can.write(canData.bytes, 8);
    can.endPacket();
    canRawTimer.sent();
  }

  // --------------------------------------------------------------------------
  // Send the OAT (Outside Air Temperature) data from the MAX31865 RTD
  // --------------------------------------------------------------------------

  // The frame keeps coming when the RTD is lost, with the marker, so "OAT sensor failed" can be told apart
  // from "AIR module silent". (It used to stop.)
  if (canTempTimer.due()) {
    canData.integers[0] = oatHealth.valid() ? oatTemp : (int16_t)CAN_MARKER_INT16;    // °C × 10 (int16, bytes 0-1)
    canData.bytes[2] = 0;             // Humidity % (not available)
    canData.bytes[3] = 0;
    canData.bytes[4] = 0;
    canData.bytes[5] = 0;
    canData.bytes[6] = 0;
    canData.bytes[7] = 0;

    can.beginPacket(CAN_TEMP_MSG_ID);
    can.write(canData.bytes, 8);
    can.endPacket();
    canTempTimer.sent();
  }

  /***************************************************************************
   * Read the sensor data and update the variables
   ***************************************************************************/

  // --------------------------------------------------------------------------
  // Check if it is time to read the sensors
  // --------------------------------------------------------------------------

  if (millis() - timestamp > 10) {

    // ------------------------------------------------------------------------
    // Read the sensor data 
    // ------------------------------------------------------------------------

    // Pressures are read with the checked drivers: a failed read is a bad sample, not a
    // number. SensorHealth decides when a sensor is lost or back (see the heartbeat section).
    sensors_event_t pitot_temp_event, aoa_temp_event, ssc_temp_event;

    pitot_temp->getEvent(&pitot_temp_event);
    #if AOA_ENABLED
    aoa_temp->getEvent(&aoa_temp_event);
    #endif
    static_temp->getEvent(&ssc_temp_event);

    float pitotHpa = 0, aoaHpa = 0, staticHpa = 0;
    uint8_t pitotStatus = ms4525do_pitot.readPressureChecked(pitotHpa);
    #if AOA_ENABLED
    uint8_t aoaStatus = ms4525do_aoa.readPressureChecked(aoaHpa);
    #else
    uint8_t aoaStatus = 0;     // not used: counts as working
    #endif
    uint8_t staticStatus = ssc.readPressureChecked(staticHpa);

    #if FAULT_INJECT
    bool injP = injectPitotFail, injA = injectAoaFail, injS = injectStaticFail;
    #else
    bool injP = false, injA = false, injS = false;
    #endif
    bool pitotOk = pressureSampleOk(pitotChan, pitotStatus, pitotHpa, -PITOT_MAX_HPA, PITOT_MAX_HPA, injP);
    bool aoaOk = pressureSampleOk(aoaChan, aoaStatus, aoaHpa, -PITOT_MAX_HPA, PITOT_MAX_HPA, injA);
    bool staticOk = pressureSampleOk(staticChan, staticStatus, staticHpa, STATIC_MIN_HPA, STATIC_MAX_HPA, injS);
    (void)aoaOk;

    bool pitotChanged = pitotChan.health.update(pitotOk);
    aoaChan.health.update(aoaOk);
    bool staticChanged = staticChan.health.update(staticOk);

    // A sensor that has just come back restarts its filters from the new readings
    if (pitotChanged && pitotChan.health.valid()) pitotFilterRestart = true;
    if (staticChanged && staticChan.health.valid()) {
      StaticPressureLowPassFilter.reset();
      staticPressureSmoothDeadband.reset(staticHpa);
      vsiHistoryCount = 0;
      vsiHistoryIndex = 0;
      vsiSmoothed = 0;
    }

    // ------------------------------------------------------------------------
    // Update and filter the data
    // ------------------------------------------------------------------------

    if (staticChan.health.valid()) {
      if (staticOk) {     // (a single bad sample is skipped)
        previousStaticPressure = sensorStaticPressure;
        sensorStaticPressure = staticHpa;
        staticPressureFiltered = StaticPressureLowPassFilter.update(sensorStaticPressure);
        staticPressureLowPassFiltered = staticPressureFiltered;   // kept for the vertical speed
        //staticPressureFiltered = staticPressureButterworthLowPass.process(sensorStaticPressure);
        //staticPressureFiltered = staticPressureButterworthLowPass2.process(staticPressureFiltered);
        //staticPressureFiltered = staticPressureKalman.update(sensorStaticPressure);
        //staticPressureFiltered = staticPressureRollingAverage.update(sensorStaticPressure);
        //staticPressureFiltered = staticPressureRollingAverageFilter.update(sensorStaticPressure);
        staticPressureFiltered = staticPressureSmoothDeadband.update(staticPressureFiltered);
      }
      if (ssc_temp_event.temperature != 0) sensorStaticTemp = ssc_temp_event.temperature;
    }

    if (pitotChan.health.valid()) {
      if (pitotOk) {
        if (pitotFilterRestart) { pitotFilterRestart = false; pitotFilterReset(); }
        previousPitotPressure = sensorPitotPressure;
        sensorPitotPressure = pitotHpa;
        //pitotPressureFiltered = pitotPressureKalman.update(sensorPitotPressure);
        pitotPressureFiltered = pitotPressureAdaptiveFilter(sensorPitotPressure);
        pitotPressureFiltered = pitotPressureDeadbandFilter.update(pitotPressureFiltered);
      }
    } else {
      pitotPressureFiltered = 0;
    }

    // The MAX31865 RTD (OAT) is NOT read here. rtd.temperature() blocks for
    // 75 ms, which would hold this whole block to about 11 Hz. It is read
    // without waiting by rtdUpdate(), called at the top of loop().

    // ------------------------------------------------------------------------
    // Calculate the airspeed
    //
    // pitotPressure * 100.0 to convert to Pa (kg/m*s^2)
    // divided by SEA_LEVEL_DENSITY_ISA to convert to m^2/s^2
    // sqrt to get the speed in m/s
    // * V_M_S_TO_KNOTS to convert to knots
    // ------------------------------------------------------------------------

    if (pitotChan.health.valid() && pitotPressureFiltered > 0 ) {
      float speed = sqrt(2 * 
        abs(pitotPressureFiltered * 100.0) / SEA_LEVEL_DENSITY_ISA) * 
        V_M_S_TO_KNOTS;
      airSpeed = static_cast<int16_t>(speed);
    } else {
      airSpeed = 0;
    }

    // ------------------------------------------------------------------------
    // Calculate the altitude
    // ------------------------------------------------------------------------
    
    if (staticChan.health.valid()) {      // otherwise the altitude holds its last value
      float height = 145442.16 * (
        pow(float(qnh) / 2992, 0.190263) - 
        pow(staticPressureFiltered / 1013.25, 0.190263));

      altitude = static_cast<int32_t>(height);
    }

    // ------------------------------------------------------------------------
    // Calculate the vertical speed (see the comment above vsiHistory)
    // ------------------------------------------------------------------------

    if (!staticChan.health.valid()) {
      verticalSpeed = 0;
    } else if (millis() - vsiLastSampleMs >= VSI_SAMPLE_MS) {
      vsiLastSampleMs = millis();

      float pressureAltitude = 145442.16f *
        (1.0f - powf(staticPressureLowPassFiltered / 1013.25f, 0.190263f));

      if (vsiHistoryCount < VSI_WINDOW_SAMPLES) {
        // Not a full window yet (first second after start-up): report 0
        vsiHistory[vsiHistoryIndex] = pressureAltitude;
        vsiHistoryCount++;
      } else {
        // The slot about to be overwritten holds the altitude one window ago
        float windowSeconds = VSI_WINDOW_SAMPLES * VSI_SAMPLE_MS / 1000.0f;
        float rateFpm = (pressureAltitude - vsiHistory[vsiHistoryIndex]) / windowSeconds * 60.0f;
        vsiHistory[vsiHistoryIndex] = pressureAltitude;
        vsiSmoothed += VSI_ALPHA * (rateFpm - vsiSmoothed);
        verticalSpeed = (int16_t)constrain(lroundf(vsiSmoothed), -VSI_MAX_FPM, VSI_MAX_FPM);
      }
      vsiHistoryIndex = (vsiHistoryIndex + 1) % VSI_WINDOW_SAMPLES;
    }

    // ------------------------------------------------------------------------
    // Update the timestamps
    // ------------------------------------------------------------------------

    timestamp = millis();     // keep track of when we last read the sensors

  }

  // **************************************************************************
  // Check for a QNH message on the CAN bus
  // **************************************************************************

  // try to parse packet
  int packetSize = can.parsePacket();

  if (packetSize) {
    // received a packet
    #if defined(DEBUG)
    Serial.print("Received CAN packet with id 0x");
    Serial.print(can.packetId(), HEX);
    Serial.print(" and length ");
    Serial.println(packetSize);
    #endif

    // Check if this is a QNH message (any ID in the block)
    long id = can.packetId();
    if (id >= CAN_QNH_FIRST_ID && id <= CAN_QNH_LAST_ID) {
      // Read the QNH data from the packet
      if (can.available() >= 4) {  // Check we have at least 4 bytes
        uint8_t qnhhPa_low = can.read();
        uint8_t qnhhPa_high = can.read();
        uint8_t qnhx4_low = can.read();
        uint8_t qnhx4_high = can.read();
        int16_t qnhHpa = (qnhhPa_high << 8) | qnhhPa_low;
        int16_t qnhx4 = (qnhx4_high << 8) | qnhx4_low; 
        
        // Convert from QNHx4 format to actual QNH (inHg x 100)
        int16_t new_qnh = qnhx4 / 4;

        // Checks: 28.00 to 31.00 inHg, and the hPa field agrees with the inHg field to within 1 hPa.
        // A frame that fails is ignored.
        float hpaFromInhg = qnhx4 * 0.25f * 0.33863886f;
        bool valid = qnhx4 >= 2800 * 4 && qnhx4 <= 3100 * 4 && fabsf(hpaFromInhg - qnhHpa) <= 1.0f;

        if (valid) {
          if (new_qnh != qnh || !qnhSet) {
            qnh = new_qnh;
            #if defined(DEBUG)
            Serial.print("QNH updated to: ");
            Serial.print(qnh);
            Serial.print(" from 0x");
            Serial.println(id, HEX);
            #endif
          }
          qnhSet = true;
        }
        
        // Read any remaining bytes to clear the packet
        while (can.available()) {
          can.read();
        }
      }
    } else {
      // This is NOT a QNH packet, but we must still read all bytes
      // We should not get any other packets, but just in case.
      while (can.available()) {
        can.read();  // Discard unwanted packet
      }
    }
  }

  #if defined(DEBUG)

  Serial.print("Altitude: ");
  Serial.println(altitude);
  // Serial.print("Static Pressure: ");
  // Serial.println(staticPressure);
  // Serial.print("Static Temp: ");
  // Serial.println(staticTemp);
  // Serial.print("Raw Pressure Count: ");
  // Serial.println(ssc.readRawPressureCount());
  // Serial.print("Pressure value: ");
  // Serial.println(ssc.readPressure());
  // Serial.print("Zero Count Offset: ");
  // Serial.println(ms4525do_pitot._pZeroCountOffset,HEX);

  // Serial.print("Speed: ");
  // Serial.print(airSpeed);
  // Serial.println(" knots");


  // Serial.print("Temperature: ");
  // Serial.print(pitot_temp_event.temperature);
  // Serial.println(" C");

  // Serial.print("Pressure: ");
  // Serial.print(pitot_pressure_event.pressure, 6);
  // Serial.println(" hPa");

  // Serial.print("SSC Pressure: ");
  // Serial.print(ssc_pressure_event.pressure, 6);
  // Serial.println(" hPa");
  // Serial.print("SSC Pressure: ");
  // Serial.print(ssc.readPressure()*.02952998301, 6);
  // Serial.println(" inHg");

  // Serial.print("SSC Temperature: ");
  // Serial.print(ssc_temp_event.temperature);
  // Serial.println(" C");


  // Serial.println();


  #endif
  //delay(10);
}
