

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
#include <Adafruit_MCP2515.h>   // for CAN bus FeatherWing
#include <Adafruit_NeoPixel.h>  // for NeoPixel LED
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
#include <ModuleInfo.h>        // USB product name, startup log and '?' report

/*****************************************************************************
 * Firmware version - increment the patch number on every change
 *****************************************************************************/
#define VERSION "1.0.12"

// Set to 0 to run without the AOA sensor (Wire1, GPIO8/9). With 0, Wire1 is not
// touched at all and a missing AOA sensor does not stop startup.
#define AOA_ENABLED 1
#define CAN_INT_WORKAROUND 1   // 1 = clear MCP2515 CANINTE at start-up. Not needed with the INT trace cut (tested
                               // 2026-10-17), kept as a safety net for a wing whose INT trace is not cut.

ModuleInfo moduleInfo("AIR_Module", VERSION, __DATE__, __TIME__);

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

// NeoPixel LED
#define LED_PIN 16
#define LED_COUNT 1
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Kalman Filter (Q, R, P, X)
KalmanFilter pitotPressureKalman(10, 10, 10, 0);
KalmanFilter staticPressureKalman(1, 850000, 0, 1013.25);

// Rolling Average
RollingAverageInt staticPressureRollingAverage(50);
RollingAverageFilter staticPressureRollingAverageFilter(128, 256);

// Low Pass Filter
LowPassFilter StaticPressureLowPassFilter(0.01);
LowPassFilter PitotPressureLowPassFilter(0.04);

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
#define CAN_QNH_MSG_ID    0x2E

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
static long canAirTimeStamp = 0;
static long canAoaTimeStamp = 0;
static long canTempTimeStamp = 0;
static long canRawTimeStamp = 0;

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
static bool oatValid = false;       // true once a good OAT sample has been taken
                                    // (so 0.0 C is never sent before the first sample)
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
static int16_t previous_qnh = qnh;

static float staticPressureLowPassFiltered = 0;

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

        if (fault == 0 && (rtdRaw & 1) == 0) {
          // Good sample. Drop the fault flag in bit 0, convert to degrees C,
          // smooth it, and keep it as degrees C x 10, rounded (not truncated).
          float oatC = rtd.calculateTemperature(rtdRaw >> 1, RTD_NOMINAL, RTD_REF_RESISTOR);
          oatTemp = (int16_t)lroundf(oatLowPassFilter.update(oatC) * 10.0f);
          oatValid = true;
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
 * Setup Code
 * 
 * Connect to all the Peripherals
 *****************************************************************************/

void setup() {

  pixel.begin();
  pixel.setBrightness(10);
  pixel.setPixelColor(0, pixel.Color(255, 0, 0));
  pixel.show();

  // USB serial never waits for a host. Startup results are stored with
  // moduleInfo.log() and printed from loop() when a host connects, or on '?'.
  moduleInfo.begin();

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

  // --------------------------------------------------------------------------
  // Make a stop or proceed decision based on whether we could connect
  // to all the sensors.
  // --------------------------------------------------------------------------

  if (!ms4525do_pitot_success || !ms4525do_aoa_success ||
     !ssc_success || !can_success || !rtdAvailable ) {
    moduleInfo.fatal("Startup failed");
    while (1) {             // wait for the user to reset the board
      moduleInfo.service(); // but still answer a host that connects later
      delay(100);
    }
  }

  moduleInfo.log("All peripherals connected");

  pixel.setPixelColor(0, pixel.Color(0, 255, 0));
  pixel.show();

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
   * Configure the CAN bus filters to receive only the QNH message
   ***************************************************************************/

  // Maybe consider checking if the filter is set correctly using the return
  // value of the filter function.
  can.filter(CAN_QNH_MSG_ID, 0x7FF);

  timestamp = millis();     // initialize the timestamp

}

void loop() {
  moduleInfo.service();   // report on USB connect or '?'
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

  if (millis() - canAirTimeStamp > CAN_AIR_PERIOD + random(50)) {
    canData.packedData.integer1 = airSpeed;
    canData.packedData.longInteger = altitude;
    canData.packedData.integer2 = verticalSpeed;

    can.beginPacket(CAN_AIR_MSG_ID);
    can.write(canData.bytes, 8);
    can.endPacket();
    canAirTimeStamp = millis(); // update the timestamp
  }

  // --------------------------------------------------------------------------
  // Send the AOA data
  // --------------------------------------------------------------------------

  if (millis() - canAoaTimeStamp > CAN_AOA_PERIOD + random(50)) {
    canData.integers[0] = aoa;

    can.beginPacket(CAN_AOA_MSG_ID);
    can.write(canData.bytes, 8);
    can.endPacket();
    canAoaTimeStamp = millis(); // update the timestamp
  }

  // --------------------------------------------------------------------------
  // Send the Raw Static Pressure and Temperature data
  // --------------------------------------------------------------------------

  if (millis() - canRawTimeStamp > CAN_RAW_PERIOD + random(50)) {
    // Pack to match legacy format: <hbhBBB>
    // int16 static_pressure (hPa×10), int8 temperature (°C),
    // int16 differential_pressure (Pa), 3 bytes padding
    int16_t sp = (int16_t)(staticPressureFiltered * 10);  // hPa → hPa×10
    canData.bytes[0] = sp & 0xFF;
    canData.bytes[1] = (sp >> 8) & 0xFF;
    canData.bytes[2] = (uint8_t)((int8_t)sensorStaticTemp);  // whole °C
    int16_t diffP = (int16_t)(pitotPressureFiltered * 100);   // hPa → Pa
    canData.bytes[3] = diffP & 0xFF;
    canData.bytes[4] = (diffP >> 8) & 0xFF;
    canData.bytes[5] = 0;
    canData.bytes[6] = 0;
    canData.bytes[7] = 0;

    can.beginPacket(CAN_RAW_MSG_ID);
    can.write(canData.bytes, 8);
    can.endPacket();
    canRawTimeStamp = millis(); // update the timestamp
  }

  // --------------------------------------------------------------------------
  // Send the OAT (Outside Air Temperature) data from the MAX31865 RTD
  // --------------------------------------------------------------------------

  if (rtdAvailable && oatValid && millis() - canTempTimeStamp > CAN_TEMP_PERIOD + random(50)) {
    canData.integers[0] = oatTemp;    // °C × 10 (int16, bytes 0-1)
    canData.bytes[2] = 0;             // Humidity % (not available)
    canData.bytes[3] = 0;
    canData.bytes[4] = 0;
    canData.bytes[5] = 0;
    canData.bytes[6] = 0;
    canData.bytes[7] = 0;

    can.beginPacket(CAN_TEMP_MSG_ID);
    can.write(canData.bytes, 8);
    can.endPacket();
    canTempTimeStamp = millis();
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

    sensors_event_t pitot_temp_event, pitot_pressure_event;
    sensors_event_t aoa_temp_event, aoa_pressure_event;
    sensors_event_t ssc_temp_event, ssc_pressure_event;

    pitot_temp->getEvent(&pitot_temp_event); 
    pitot_pressure->getEvent(&pitot_pressure_event);

    #if AOA_ENABLED
    aoa_temp->getEvent(&aoa_temp_event);
    aoa_pressure->getEvent(&aoa_pressure_event);
    #endif

    static_temp->getEvent(&ssc_temp_event);
    static_pressure->getEvent(&ssc_pressure_event);

    // ------------------------------------------------------------------------
    // Update and filter the data
    // ------------------------------------------------------------------------

    previousStaticPressure = sensorStaticPressure;
    sensorStaticPressure = ssc_pressure_event.pressure;
    staticPressureFiltered = StaticPressureLowPassFilter.update(sensorStaticPressure);
    //staticPressureFiltered = staticPressureButterworthLowPass.process(sensorStaticPressure);
    //staticPressureFiltered = staticPressureButterworthLowPass2.process(staticPressureFiltered);
    //staticPressureFiltered = staticPressureKalman.update(sensorStaticPressure);
    //staticPressureFiltered = staticPressureRollingAverage.update(sensorStaticPressure);
    //staticPressureFiltered = staticPressureRollingAverageFilter.update(sensorStaticPressure);
    staticPressureFiltered = staticPressureSmoothDeadband.update(staticPressureFiltered);


    

    previousPitotPressure = sensorPitotPressure;
    sensorPitotPressure = pitot_pressure_event.pressure;
    //pitotPressureFiltered = pitotPressureKalman.update(sensorPitotPressure);
    pitotPressureFiltered = PitotPressureLowPassFilter.update(sensorPitotPressure);
    pitotPressureFiltered = pitotPressureDeadbandFilter.update(pitotPressureFiltered);

    sensorStaticTemp = ssc_temp_event.temperature;

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

    if (pitotPressureFiltered > 0 ) {
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
    
    float height = 145442.16 * (
      pow(float(qnh) / 2992, 0.190263) - 
      pow(staticPressureFiltered / 1013.25, 0.190263));

    altitude = static_cast<int32_t>(height);

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

    // Check if this is a QNH message
    if (can.packetId() == CAN_QNH_MSG_ID) {
      // Read the QNH data from the packet
      if (can.available() >= 4) {  // Check we have at least 4 bytes
        uint8_t qnhhPa_low = can.read();
        uint8_t qnhhPa_high = can.read();
        uint8_t qnhx4_low = can.read();
        uint8_t qnhx4_high = can.read();
        int16_t qnhx4 = (qnhx4_high << 8) | qnhx4_low; 
        
        // Convert from QNHx4 format to actual QNH (inHg)
        int16_t new_qnh = qnhx4 / 4;

        if (new_qnh != previous_qnh) {
          qnh = new_qnh;
          previous_qnh = qnh;
          #if defined(DEBUG)
          Serial.print("QNH updated to: ");
          Serial.println(qnh);
          #endif
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
