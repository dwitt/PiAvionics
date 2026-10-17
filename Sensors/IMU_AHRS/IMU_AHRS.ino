
/*****************************************************************************
 * IMU Calibration - based on Adafruit AHRS Calibration
 * Works with MotionCal to Calibrate the compass 
 *****************************************************************************/

/*****************************************************************************
 * Boards used
 * Adafruit Feather RP2040
 * Adafruit CAN Bus FeatherWing
 * Adafruit ISM330DHCX + LIS3MDL FeatherWing
 * Adafruit 24LC32 I2C EEPROM
 *****************************************************************************/

#include <ModuleInfo.h>		// USB product name, startup log and '?' report
#include <SensorHealth.h>	// valid / invalid decision for each sensor
#include <Adafruit_ISM330DHCX.h>	// Only for ISM330DHCX
#include <Adafruit_LSM6DS.h>		// I2C Driver base for Adafruit LSM6DSxx sensors

// Remove the following includes once the the code is running.
//#include <Adafruit_LSM6DS3.h>			// Only for LSM6DS3
//#include <Adafruit_LSM6DS33.h>		// Only for LSM6DS33
//#include <Adafruit_LSM6DS3TRC.h>		// Only for LSM6DS3TRC
//#include <Adafruit_LSM6DSL.h>			// Only for LSM6DSL		
//#include <Adafruit_LSM6DSO32.h>		// Only for LSM6DSO32
//#include <Adafruit_LSM6DSOX.h>    	// Only for LSMDSOX

#include <Adafruit_LIS3MDL.h>		// For Magnetometer
#include <Adafruit_MCP2515.h>		// For CAN Bus FeatherWing
#include <Adafruit_EEPROM_I2C.h>	// EEPROM to hold calibration data

/*****************************************************************************
 * Include libraries for other functions
 *****************************************************************************/
//----------------------------------------------------------------------------
// Using a customized copy of the sensor calibration library that has been
// modified to detect an Adafruit Feather RP2040 and use an EEPROM with this 
// board
//----------------------------------------------------------------------------
#include <Sensor_Calibration.h>

//----------------------------------------------------------------------------
// Use the Adafruit AHRS library
// ---------------------------------------------------------------------------
#include <Adafruit_AHRS.h>

//----------------------------------------------------------------------------
// Status LED for startup diagnostics and runtime heartbeat
// See Sensors/Libraries/StatusLED/StatusLED.md for full specification
// ---------------------------------------------------------------------------
#include <StatusLED.h>

/*****************************************************************************
 * Firmware version - increment the patch number on every change
 *****************************************************************************/
#define VERSION "1.0.12"

ModuleInfo moduleInfo("IMU_AHRS", VERSION, __DATE__, __TIME__);

/*****************************************************************************
 * Defines to help with debugging
 *****************************************************************************/
//#define DEBUG					Comment out for production code

/*****************************************************************************
 * Universal POST codes (see Sensors/Libraries/StatusLED/StatusLED.md)
 *****************************************************************************/
#define POST_ISM330DHCX   1    // Primary I2C sensor
#define POST_LIS3MDL      2    // Secondary I2C sensor
#define POST_EEPROM       9    // EEPROM / Calibration
#define POST_CAN          11   // CAN bus

// Config failure POST codes (amber flashes)
#define CONFIG_FAIL_RANGE     1
#define CONFIG_FAIL_DATARATE  2
#define CONFIG_FAIL_MODE      3

/*****************************************************************************
 * Create the objects using the libraries 
 *****************************************************************************/
//----------------------------------------------------------------------------
// The IMU uses the I2C bus
//----------------------------------------------------------------------------

#define ISM330DHCX_I2C_ADDRESS 	0x6A
#define LIS3MDL_I2C_ADDRESS		0x1C

Adafruit_ISM330DHCX ism330dhcx;
Adafruit_LIS3MDL lis3mdl;

//----------------------------------------------------------------------------
// The MCP2515 (CAN Bus) uses SPI
//----------------------------------------------------------------------------

#define CAN_CS_PIN 7
#define CAN_BAUDRATE (250000)

Adafruit_MCP2515 can(CAN_CS_PIN);

//----------------------------------------------------------------------------
// CAN heartbeat (Protocol/can_messages.yaml, 0x701). Sent every second with the
// module version, state and a mask of the sensors that are working.
// Sensors mask: bit 0 ISM330DHCX, bit 1 LIS3MDL, bit 2 calibration loaded.
// All three set = OK; calibration missing = degraded (the AHRS runs uncalibrated).
// While running, bits 0 and 1 follow the health of the sensors (see SensorHealth):
// accel/gyro lost = state 3 (no attitude), magnetometer lost = state 2 (6-axis AHRS).
//----------------------------------------------------------------------------
#define HB_ISM330DHCX   0x01
#define HB_LIS3MDL      0x02
#define HB_CALIBRATION  0x04
#define HB_ALL          (HB_ISM330DHCX | HB_LIS3MDL | HB_CALIBRATION)

static uint8_t hbSensors = 0;
static bool canUp = false;			// heartbeat is only sent once the CAN bus is running

//----------------------------------------------------------------------------
// Sensor health (SensorHealth library). A sensor goes invalid after SENSOR_FAIL_COUNT
// bad samples in a row (0.2 s at 100 Hz) and valid again after SENSOR_GOOD_COUNT good
// ones. While invalid it is re-initialised every SENSOR_RETRY_MS: a chip that lost
// power (wing pulled and replaced) has lost its configuration and answers with zeros.
//----------------------------------------------------------------------------
#define SENSOR_FAIL_COUNT	20
#define SENSOR_GOOD_COUNT	5
#define SENSOR_RETRY_MS		2000
#define SENSOR_STUCK_COUNT	100		// identical samples in a row = chip locked up
#define LONG_OUTAGE_MS		1000	// after this long without data the AHRS starts over

#define FAULT_INJECT 1		// test commands on USB serial, see "Fault injection" below; 0 for flight
#if FAULT_INJECT
static bool injectIsmFail = false, injectMagFail = false;
static char strayChars[8] = "";		// the last characters received that were not a command
static bool bangSeen = false;
#endif

static SensorHealth ismHealth(SENSOR_FAIL_COUNT, SENSOR_GOOD_COUNT, SENSOR_STUCK_COUNT);
static SensorHealth magHealth(SENSOR_FAIL_COUNT, SENSOR_GOOD_COUNT, SENSOR_STUCK_COUNT);
static bool calibrationLoaded = false;
static uint32_t ismOutageMs = 0;	// length of the last accel/gyro outage
static float magSynthErr = 0;		// average difference real - predicted field, uT (shown in '?')

// Publish the sensor health in the heartbeat.
static void publishHealth() {
	uint8_t mask = 0;
	if (ismHealth.valid()) mask |= HB_ISM330DHCX;
	if (magHealth.valid()) mask |= HB_LIS3MDL;
	if (calibrationLoaded) mask |= HB_CALIBRATION;
	uint8_t state = MODULE_STATE_OK;
	if (!ismHealth.valid()) state = MODULE_STATE_FAILED;
	else if (mask != HB_ALL) state = MODULE_STATE_DEGRADED;
	hbSensors = mask;
	moduleInfo.setHealth(state, mask);
}

// Live sensor and CAN transmit status for the '?' report. A send that nobody acknowledges is
// abandoned after a few ms (patched Adafruit_MCP2515, see Libraries/Adafruit_MCP2515/PATCH_NOTES.md).
static void printHealth(Print &out, const char *name, const SensorHealth &h) {
  out.print(name); out.print(h.valid() ? " OK" : " FAILED");
  out.print(" (lost "); out.print(h.failures);
  out.print("x, re-initialised "); out.print(h.recoveries); out.print("x of "); out.print(h.attempts);
  out.print(" tries, last: "); out.print(h.lastAttempt); out.println(")");
}

static void canReport(Print &out) {
  printHealth(out, "ISM330DHCX accel/gyro", ismHealth);
  printHealth(out, "LIS3MDL magnetometer", magHealth);
  out.print("I2C read failures: "); out.println(Adafruit_LSM6DS::readFailures);
  #if FAULT_INJECT
  out.print("Test build: injected ISM fail "); out.print(injectIsmFail); out.print(", mag fail "); out.print(injectMagFail);
  out.print(", stray USB bytes '"); out.print(strayChars); out.println("'");
  #endif
  out.print("Mag prediction error (uT): "); out.println(magSynthErr, 1);
  if (!canUp) { out.println("CAN not running"); return; }
  out.print("CAN tx: ");
  out.print(can.txDown() ? "DOWN (no acknowledge)" : "ok");
  out.print(", timeouts "); out.print(can.txTimeouts());
  out.print(", skipped "); out.print(can.txSuppressed());
  out.print(", TEC "); out.print(can.txErrorCount());
  out.print(", REC "); out.print(can.rxErrorCount());
  out.print(", EFLG 0x"); out.println(can.errorFlags(), HEX);
}

static void sendHeartbeat() {
	if (!canUp || !moduleInfo.heartbeatDue()) return;
	uint8_t frame[8];
	moduleInfo.packHeartbeat(frame);
	can.beginPacket(moduleInfo.heartbeatId());
	can.write(frame, 8);
	can.endPacket();
}

// Called from loop() and, through the StatusLED callback, while the LED code waits
// or blinks a failure code, so the heartbeat and the '?' report keep working.
static void imuService() {
	moduleInfo.service();
	sendHeartbeat();
}

//----------------------------------------------------------------------------
// Status LED (Feather RP2040 onboard NeoPixel)
//----------------------------------------------------------------------------

StatusLED led;

// CAN Message Constants -----------------------------------------------------
#define CAN_EULER_MSG_ID	0x48
#define CAN_ACC_MSG_ID		0x49
#define CAN_CALIB_MSG_ID	0x23
#define CAN_MAGX_MSG_ID		0x81
#define CAN_MAGY_MSG_ID		0x82
#define CAN_MAGZ_MSG_ID		0x83

#define CAN_EULER_PERIOD	100		// milliseconds
#define CAN_ACC_PERIOD		100		// milliseconds
#define CAN_MAG_PERIOD		250		// milliseconds

//----------------------------------------------------------------------------
// The Sensor Calibration EEPROM uses the I2C bus for calibration
// The I2C bus is on the Wire interface of the Feather RP2040
//----------------------------------------------------------------------------

#define EEPROM_I2C_ADDRESS	(0X50)

Sensor_Calibration_EEPROM cal(EEPROM_I2C_ADDRESS, &Wire);	// Wire speed set at end of setup

//----------------------------------------------------------------------------
// Create Adafruit Sensor object pointers
//----------------------------------------------------------------------------
Adafruit_Sensor *accelerometer, *gyroscope;

//----------------------------------------------------------------------------
// Set the frequency to update the SensorFusion filter and to print results
// Create the filter object based on the method to be used for fusing the
// sensor data. Uncomment only one of the filters
//----------------------------------------------------------------------------
#define FILTER_UPDATE_RATE_HZ 100	// 10 millisecond interval
#define PRINT_EVERY_N_UPDATES 10	// 100 milisecond interval

//#define AHRS_DEBUG_OUTPUT			// Enable to print AHRS debug info

Adafruit_NXPSensorFusion filter; 	// slowest
//Adafruit_Madgwick filter;  		// faster than NXP
//Adafruit_Mahony filter;  			// fastest/smalleset

//----------------------------------------------------------------------------
// Define locally global variables and functions
// ---------------------------------------------------------------------------
sensors_event_t mag_event, gyro_event, accel_event;	// sensor event (data) // THESE CAN BE DELETED
uint32_t timestamp;					// tracking the timing in the main loop

// Timestamps to determine when to send the next CAN message

static long canEulerTimeStamp = 0;	
static long canAccTimeStamp = 0;
static long canMagTimeStamp = 0;

// Data to send via CAN
// Declared globally but local to this file 

static int16_t roll = 0;		// degrees * 10
static int16_t pitch = 0;		// degrees * 10
static int16_t yaw = 0;			// degrees * 10
static int16_t turnRateScaled = 0;	// degrees/sec * 10
static int16_t accelX = 0;
static int16_t accelY = 0;
static int16_t accelZ = 0;

static float previousYaw = 0;			// for the turn rate
static unsigned long previousTime = 0;
static bool resyncTurnRate = false;		// the AHRS was restarted: skip the jump in yaw


//----------------------------------------------------------------------------
// Sensor settings
// Note: The accelerometer data range is set to 4G instead of 2G as in
// the Adafruit Calibration routine.
//----------------------------------------------------------------------------
#define ACCEL_RANGE LSM6DS_ACCEL_RANGE_4_G			// +/- 4G
#define GYRO_RANGE LSM6DS_GYRO_RANGE_250_DPS		// 250 degrees/sec
#define MAG_RANGE LIS3MDL_RANGE_4_GAUSS				// 4 Gauss
#define MAG_LSB_PER_GAUSS 6842.0f					// at +/- 4 Gauss

#define ACCEL_DATA_RATE LSM6DS_RATE_104_HZ			// 104 Hz
#define GYRO_DATA_RATE LSM6DS_RATE_104_HZ			// 104 Hz
#define MAG_DATA_RATE LIS3MDL_DATARATE_1000_HZ		// 1000 Hz

#define MAG_POWER_MODE LIS3MDL_MEDIUMMODE			// Medium Power
#define MAG_OPERATION_MODE LIS3MDL_CONTINUOUSMODE	// Continuous

#define LIS3MDL_REG_OUT_X_L_INC	(0x28 | 0x80)		// data registers, address auto-increment

// What failed in the last ismConfigure() / magConfigure(), for the startup log
static const char *configWhy = "";

// Set the sensor ranges and data rates and read every setting back.
// Returns 0 if all is well, otherwise the CONFIG_FAIL_ code (amber flashes).
static uint8_t ismConfigure() {
	ism330dhcx.setAccelRange(ACCEL_RANGE);
	if (ism330dhcx.getAccelRange() != ACCEL_RANGE) { configWhy = "accel range"; return CONFIG_FAIL_RANGE; }
	ism330dhcx.setGyroRange(GYRO_RANGE);
	if (ism330dhcx.getGyroRange() != GYRO_RANGE) { configWhy = "gyro range"; return CONFIG_FAIL_RANGE; }
	ism330dhcx.setAccelDataRate(ACCEL_DATA_RATE);
	if (ism330dhcx.getAccelDataRate() != ACCEL_DATA_RATE) { configWhy = "accel data rate"; return CONFIG_FAIL_DATARATE; }
	ism330dhcx.setGyroDataRate(GYRO_DATA_RATE);
	if (ism330dhcx.getGyroDataRate() != GYRO_DATA_RATE) { configWhy = "gyro data rate"; return CONFIG_FAIL_DATARATE; }
	return 0;
}

static uint8_t magConfigure() {
	lis3mdl.setRange(MAG_RANGE);
	if (lis3mdl.getRange() != MAG_RANGE) { configWhy = "mag range"; return CONFIG_FAIL_RANGE; }
	lis3mdl.setDataRate(MAG_DATA_RATE);
	if (lis3mdl.getDataRate() != MAG_DATA_RATE) { configWhy = "mag data rate"; return CONFIG_FAIL_DATARATE; }
	lis3mdl.setPerformanceMode(MAG_POWER_MODE);
	if (lis3mdl.getPerformanceMode() != MAG_POWER_MODE) { configWhy = "mag power mode"; return CONFIG_FAIL_MODE; }
	lis3mdl.setOperationMode(MAG_OPERATION_MODE);
	if (lis3mdl.getOperationMode() != MAG_OPERATION_MODE) { configWhy = "mag operation mode"; return CONFIG_FAIL_MODE; }
	return 0;
}

// Start the ISM330DHCX from scratch (chip reset, then our settings). Used at start-up and to
// recover a chip that lost power. begin_I2C() replaces the sensor objects, so the pointers
// are fetched again.
static bool ismStart() {
	if (!ism330dhcx.begin_I2C(ISM330DHCX_I2C_ADDRESS, &Wire)) return false;
	accelerometer = ism330dhcx.getAccelerometerSensor();
	gyroscope = ism330dhcx.getGyroSensor();
	return true;
}

static bool magStart() {
	return lis3mdl.begin_I2C(LIS3MDL_I2C_ADDRESS, &Wire);
}

// The LIS3MDL library ignores a failed read and converts whatever is in its buffer, so the
// magnetometer is read here instead. Returns false if the read fails. Output in uT.
static bool magRead(float &x, float &y, float &z, int16_t raw[3]) {
	Wire.beginTransmission(LIS3MDL_I2C_ADDRESS);
	Wire.write(LIS3MDL_REG_OUT_X_L_INC);
	if (Wire.endTransmission(false) != 0) return false;
	if (Wire.requestFrom((uint8_t)LIS3MDL_I2C_ADDRESS, (uint8_t)6) != 6) return false;
	uint8_t b[6];
	for (int i = 0; i < 6; i++) b[i] = Wire.read();
	for (int i = 0; i < 3; i++) raw[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
	x = raw[0] / MAG_LSB_PER_GAUSS * 100.0f;	// gauss to uT
	y = raw[1] / MAG_LSB_PER_GAUSS * 100.0f;
	z = raw[2] / MAG_LSB_PER_GAUSS * 100.0f;
	return true;
}

//----------------------------------------------------------------------------
// Running without the magnetometer (6-axis). The fusion filter needs a magnetic
// field, so while the real one is missing it is given the field the filter itself
// predicts: its own Earth field vector rotated by its current orientation. That
// field never disagrees with the filter, so roll and pitch still follow gravity and
// the heading follows the gyro alone.
// With R the rotation matrix of the filter quaternion, sensor field = R-transposed *
// Earth field (bench test 2026-10-17: the transposed form missed the real field by
// 1 to 4 uT while the module was rotated, the other by 6 to 13 uT). The first version
// used an Earth field learned from the magnetometer instead of the filter's own, and
// the small difference between the two made the heading drift faster and faster.
// magSynthErr in the '?' report shows how well the prediction matches the real field
// while the magnetometer works.
//----------------------------------------------------------------------------
static void quatMatrix(float R[3][3]) {
	float w, x, y, z;
	filter.getQuaternion(&w, &x, &y, &z);
	R[0][0] = 1 - 2 * (y * y + z * z); R[0][1] = 2 * (x * y - z * w);     R[0][2] = 2 * (x * z + y * w);
	R[1][0] = 2 * (x * y + z * w);     R[1][1] = 1 - 2 * (x * x + z * z); R[1][2] = 2 * (y * z - x * w);
	R[2][0] = 2 * (x * z - y * w);     R[2][1] = 2 * (y * z + x * w);     R[2][2] = 1 - 2 * (x * x + y * y);
}

// The field the filter expects in the sensor frame: R-transposed * Earth field
static void magPredicted(float *m) {
	float R[3][3], e[3];
	quatMatrix(R);
	filter.getGeomagneticVector(&e[0], &e[1], &e[2]);
	for (int i = 0; i < 3; i++) m[i] = R[0][i] * e[0] + R[1][i] * e[1] + R[2][i] * e[2];
}

// Magnetometer is working: track how well the prediction matches it.
static void magCheck(const float *m) {
	float pred[3];
	magPredicted(pred);
	float dx = m[0] - pred[0], dy = m[1] - pred[1], dz = m[2] - pred[2];
	magSynthErr += 0.05f * (sqrtf(dx * dx + dy * dy + dz * dz) - magSynthErr);
}

//----------------------------------------------------------------------------
// Fault injection for tests (USB serial commands, see ModuleInfo setCommandHook).
// Each command is "!" followed by a letter (a stray byte on the USB port must not trigger one):
//   !G  accel/gyro reads fail (like an unplugged chip) until !g is sent
//   !M  magnetometer reads fail until !m is sent
//   !z  reset the ISM330DHCX chip (like a power loss: it then returns zeros)
//   !x  reset the LIS3MDL chip (it then stops updating its data)
// While a read failure is injected the sensor is not re-initialised either.
//----------------------------------------------------------------------------
#if FAULT_INJECT

static void faultCommand(char c) {
	if (c == '!') { bangSeen = true; return; }
	if (!bangSeen) {
		size_t n = strlen(strayChars);
		if (n >= sizeof(strayChars) - 1) { memmove(strayChars, strayChars + 1, n - 1); n--; }
		strayChars[n] = c; strayChars[n + 1] = 0;
		return;
	}
	bangSeen = false;
	switch (c) {
		case 'G': injectIsmFail = true; break;
		case 'g': injectIsmFail = false; break;
		case 'M': injectMagFail = true; break;
		case 'm': injectMagFail = false; break;
		case 'z': ism330dhcx.reset(); break;
		case 'x': lis3mdl.reset(); break;
	}
}
#endif

/*****************************************************************************
 * Setup Code 
 *
 * Connect to all of the Peripherals
 *****************************************************************************/

void setup() {

	/*************************************************************************
	 * Step 1: Status LED — solid red (power on)
	 *************************************************************************/

	led.begin();
	led.startup();

	/*************************************************************************
	 * Step 2: USB Serial - never waits for a host. Startup results are stored
	 * with moduleInfo.log() and printed from loop() when a host connects, or when
	 * '?' is received.
	 *************************************************************************/

	moduleInfo.begin();

	/*************************************************************************
	 * Step 3: CAN bus — POST code 11
	 * Started before the sensors so a module that halts on a failed sensor can
	 * still send its heartbeat (state 3) and say which sensor failed.
	 *************************************************************************/

	moduleInfo.setModuleNumber(MODULE_NUM_IMU);
	moduleInfo.setReportHook(canReport);
	#if FAULT_INJECT
	moduleInfo.setCommandHook(faultCommand);
	#endif
	moduleInfo.setHealth(MODULE_STATE_STARTING, 0);
	led.setServiceCallback(imuService);		// '?' report and heartbeat during LED waits

	led.initBegin();
	bool can_success = can.begin(CAN_BAUDRATE);
	if (can_success) {
		led.initOK();
		moduleInfo.log("CAN bus OK");
		canUp = true;
	} else {
		led.initFail();
		moduleInfo.fatal("CAN bus FAILED");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.postCode(POST_CAN);  // never returns
	}


	/*************************************************************************
	 * Step 4: Module sensors — init with LED feedback
	 * Standard order: ISM330DHCX (POST 1), LIS3MDL (POST 2), EEPROM (POST 9)
	 *************************************************************************/

	// -----------------------------------------------------------------------
	// ISM330DHCX accel/gyro (I2C Wire, 0x6A) — POST code 1
	// -----------------------------------------------------------------------

	led.initBegin();
	bool ism330dhcx_success = ismStart();
	if (ism330dhcx_success) {
		led.initOK();
		moduleInfo.log("ISM330DHCX OK");
		hbSensors |= HB_ISM330DHCX;
	} else {
		led.initFail();
		moduleInfo.fatal("ISM330DHCX FAILED");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.postCode(POST_ISM330DHCX);  // never returns
	}

	// -----------------------------------------------------------------------
	// LIS3MDL magnetometer (I2C Wire, 0x1C) — POST code 2
	// Not fatal: without it the AHRS runs on accelerometer and gyro only and the
	// module reports degraded. It is tried again every few seconds while running.
	// -----------------------------------------------------------------------

	led.initBegin();
	bool lis3mdl_success = magStart();
	if (lis3mdl_success) {
		led.initOK();
		moduleInfo.log("LIS3MDL OK");
		hbSensors |= HB_LIS3MDL;
	} else {
		led.initFail();
		moduleInfo.log("**WARNING** LIS3MDL FAILED - running without the magnetometer");
		magHealth.markInvalid();
	}

	// -----------------------------------------------------------------------
	// EEPROM / Calibration (I2C Wire, 0x50) — POST code 9
	// -----------------------------------------------------------------------

	led.initBegin();
	bool eeprom_success = cal.begin();
	if (eeprom_success) {
		led.initOK();
		moduleInfo.log("EEPROM OK");
	} else {
		led.initFail();
		moduleInfo.fatal("EEPROM FAILED");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.postCode(POST_EEPROM);  // never returns
	}

	if (cal.loadCalibration()) {
		moduleInfo.log("Calibration loaded");
		hbSensors |= HB_CALIBRATION;
		calibrationLoaded = true;
	} else {
		moduleInfo.log("**WARNING** No calibration loaded/found");
	}

	cal.printSavedCalibration();

	/*************************************************************************
	 * Step 6: Go — solid green for 2 seconds
	 *************************************************************************/

	moduleInfo.log("All peripherals connected — GO");

	led.go();

	/*************************************************************************
	 * Step 7: Configure peripherals
	 * Sensor ranges, data rates, and modes with read-back verification.
	 * Config failures show amber POST codes.
	 *************************************************************************/

	static char cfgMsg[100];

	uint8_t cfgFail = ismConfigure();
	if (cfgFail) {
		snprintf(cfgMsg, sizeof(cfgMsg), "Config FAIL: %s", configWhy);
		moduleInfo.fatal(cfgMsg);
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.configFail(cfgFail);  // never returns
	}

	if (magHealth.valid()) {
		cfgFail = magConfigure();
		if (cfgFail) {
			snprintf(cfgMsg, sizeof(cfgMsg), "**WARNING** Config FAIL: %s - running without the magnetometer", configWhy);
			moduleInfo.log(cfgMsg);
			magHealth.markInvalid();
		}
	}

	moduleInfo.log("Sensor configuration OK");
	publishHealth();

	/*************************************************************************
	 * Setup Sensors
	 *************************************************************************/

	#if defined(DEBUG)
	accelerometer->printSensorDetails();
	gyroscope->printSensorDetails();
	lis3mdl.printSensorDetails();
	#endif

	/*************************************************************************
	 * Setup the SensorFusion filter
	 *************************************************************************/

	filter.begin(FILTER_UPDATE_RATE_HZ);
	timestamp = millis();

	/*************************************************************************
	 * Step 8: Finalize — set I2C speed
	 *************************************************************************/

	Wire.setClock(400000);		// 400 kHz

}

/*****************************************************************************
 * Main Loop *****************************************************************
 *****************************************************************************/

void loop() {

	// ------------------------------------------------------------------------
	// Host report: printed when a host connects to USB serial, and whenever '?'
	// is received. Never waits - Serial and Serial.available() are flag checks.
	// ------------------------------------------------------------------------
	imuService();

	// ------------------------------------------------------------------------
	// Define variables
	// ------------------------------------------------------------------------
	// Union for CAN data. Allows loading different data types into various
	// parts of the CAN data transmission
	
	static union DataUnion 
	{
		int16_t integers[4];
		uint8_t bytes[8];
		float	floats[2];

	} canData;					

//	unsigned long newTimeStamp, elapsedTime;

	float gx, gy, gz;

	static uint8_t counter = 0;

	// ------------------------------------------------------------------------
	// Runtime heartbeat — brief green flash every ~2 seconds
	// ------------------------------------------------------------------------
	led.heartbeat();

	// ------------------------------------------------------------------------
	// --- Send CAN data
	// ------------------------------------------------------------------------

	// ------------------------------------------------------------------------
	// --- Send yaw, pitch and roll
	// ------------------------------------------------------------------------
	if (millis() > (canEulerTimeStamp + CAN_EULER_PERIOD) + random(50)) {
		canData.integers[0] = yaw;
		canData.integers[1] = pitch;
		canData.integers[2] = roll;
		canData.integers[3] = turnRateScaled;
		can.beginPacket(CAN_EULER_MSG_ID);
		can.write(canData.bytes, 8);
		can.endPacket();
		canEulerTimeStamp = millis();
	}

	// ------------------------------------------------------------------------
	// -- Send accelerations. Fill out the buffer with 0
	// ------------------------------------------------------------------------
	if (millis() > (canAccTimeStamp + CAN_ACC_PERIOD) + random(50)) {
		canData.integers[0] = accelX; 
		canData.integers[1] = accelY;
		canData.integers[2] = accelZ;
		canData.integers[3] = 0;
		can.beginPacket(CAN_ACC_MSG_ID);
		can.write(canData.bytes, 8);
		can.endPacket();
		canAccTimeStamp = millis();
	}

	// ------------------------------------------------------------------------
	// Check if it is time to update the SensorFusion filter.
	// If it is not then start loop again.
	// ------------------------------------------------------------------------
	if ((millis() - timestamp) < (1000 / FILTER_UPDATE_RATE_HZ)) {
		return;
	}

	// ------------------------------------------------------------------------
	// -- Update the time stamps
	// ------------------------------------------------------------------------

	timestamp = millis();					// get the current time

	/**************************************************************************
	 * Read the sensor data, calibrate it, and then fuse it.
	 **************************************************************************/

	// ------------------------------------------------------------------------
	// Read the motion sensors
	// ------------------------------------------------------------------------
	sensors_event_t accel, gyro, mag;	// declare variables to store the data

										// UNITS
	bool ismOk = accelerometer->getEvent(&accel) && gyroscope->getEvent(&gyro);	// m/s^2, rads/s
	#if FAULT_INJECT
	if (injectIsmFail) ismOk = false;
	#endif
	if (ismOk) {
		// A chip that lost power answers with zeros and a locked-up one repeats a sample;
		// a working accelerometer always reads about 1 g and has noise.
		static float lastIsm[6];
		const float nowIsm[6] = {accel.acceleration.x, accel.acceleration.y, accel.acceleration.z,
								 gyro.gyro.x, gyro.gyro.y, gyro.gyro.z};
		bool allZero = nowIsm[0] == 0 && nowIsm[1] == 0 && nowIsm[2] == 0;
		bool stuck = ismHealth.stuck(lastIsm, nowIsm, 6);
		ismOk = !allZero && !stuck;
	}

	memset(&mag, 0, sizeof(mag));
	mag.type = SENSOR_TYPE_MAGNETIC_FIELD;	// tells cal.calibrate() what this is
	int16_t magRaw[3];
	bool magOk = magRead(mag.magnetic.x, mag.magnetic.y, mag.magnetic.z, magRaw);	// uT
	#if FAULT_INJECT
	if (injectMagFail) magOk = false;
	#endif
	if (magOk) {
		static float lastMag[3];
		const float nowMag[3] = {(float)magRaw[0], (float)magRaw[1], (float)magRaw[2]};
		bool allZero = magRaw[0] == 0 && magRaw[1] == 0 && magRaw[2] == 0;
		bool stuck = magHealth.stuck(lastMag, nowMag, 3);
		magOk = !allZero && !stuck;
	}

	// ------------------------------------------------------------------------
	// Sensor health: valid / invalid, heartbeat, and re-initialising a sensor
	// that went away (a wing that was replaced has lost its settings)
	// ------------------------------------------------------------------------
	bool ismChanged = ismHealth.update(ismOk);
	bool magChanged = magHealth.update(magOk);

	if (ismChanged && ismHealth.valid()) {
		ismOutageMs = ismHealth.outageMs();
		if (ismOutageMs > LONG_OUTAGE_MS) {
			filter.begin(FILTER_UPDATE_RATE_HZ);	// the old attitude is not to be trusted
			resyncTurnRate = true;
		}
	}
	if (ismChanged || magChanged) publishHealth();

	bool retryIsm = ismHealth.retryDue(SENSOR_RETRY_MS);
	bool retryMag = magHealth.retryDue(SENSOR_RETRY_MS);
	#if FAULT_INJECT
	retryIsm = retryIsm && !injectIsmFail;
	retryMag = retryMag && !injectMagFail;
	#endif
	if (retryIsm) {
		ismHealth.lastRetry = millis();
		ismHealth.attempts++;
		if (!ismStart()) ismHealth.lastAttempt = "no answer";
		else if (ismConfigure() != 0) ismHealth.lastAttempt = configWhy;
		else { ismHealth.lastAttempt = "ok"; ismHealth.recoveries++; }
	}
	if (retryMag) {
		magHealth.lastRetry = millis();
		magHealth.attempts++;
		if (!magStart()) magHealth.lastAttempt = "no answer";
		else if (magConfigure() != 0) magHealth.lastAttempt = configWhy;
		else { magHealth.lastAttempt = "ok"; magHealth.recoveries++; }
	}

	// Without accelerometer and gyro there is no attitude: keep the last values and do not let
	// the filter integrate stale data. (One bad sample is skipped the same way.)
	if (!ismOk || !ismHealth.valid()) return;

	#if defined(AHRS_DEBUG_OUTPUT)
	if (Serial) Serial.print("I2C took "); Serial.print(millis()-timestamp); Serial.println(" ms");
	#endif

	// ------------------------------------------------------------------------
	// Apply the calibration to each of the sensors
	// ------------------------------------------------------------------------
	static float magField[3];			// the field given to the filter, uT
	static bool haveMag = false;
	if (magOk) {
		cal.calibrate(mag);
		magField[0] = mag.magnetic.x;
		magField[1] = mag.magnetic.y;
		magField[2] = mag.magnetic.z;
		haveMag = true;
	}
	cal.calibrate(accel);
	cal.calibrate(gyro);

	if (magHealth.valid() && haveMag) {
		if (magOk) magCheck(magField);		// otherwise this cycle's sample was bad: use the last one
	} else {
		magPredicted(magField);				// 6-axis: the field the filter expects
	}

	// ------------------------------------------------------------------------
	// NO sensor frame conversion - let filter work in native sensor frame
	// Any necessary corrections will be applied to the OUTPUT Euler angles
	// Sensor frame: X=forward, Y=left, Z=up
	// ------------------------------------------------------------------------
	// accel.acceleration.y = -accel.acceleration.y;
	// accel.acceleration.z = -accel.acceleration.z;

	// gyro.gyro.y = -gyro.gyro.y;
	// gyro.gyro.z = -gyro.gyro.z;

	// mag.magnetic.y = -mag.magnetic.y;
	// mag.magnetic.z = -mag.magnetic.z;

	// ------------------------------------------------------------------------
	// Gyroscope needs to be converted from Rad/s to Degree/s
	// the rest are not unit-important
	// ------------------------------------------------------------------------
	gx = gyro.gyro.x * SENSORS_RADS_TO_DPS;
	gy = gyro.gyro.y * SENSORS_RADS_TO_DPS;
	gz = gyro.gyro.z * SENSORS_RADS_TO_DPS;

	// ------------------------------------------------------------------------
	// Update the SensorFusion filter
	// ------------------------------------------------------------------------
	filter.update(gx, gy, gz, 
				  accel.acceleration.x, accel.acceleration.y, accel.acceleration.z, 
				  magField[0], magField[1], magField[2]);

	#if defined(AHRS_DEBUG_OUTPUT)
	if (Serial) Serial.print("Update took "); Serial.print(millis()-timestamp); Serial.println(" ms");
	#endif

	// ------------------------------------------------------------------------
	// Convert filter output from native sensor frame (NWU) to aircraft
	// standard (NED). The AHRS filter works in the sensor's native frame
	// to avoid sensor fusion conflicts. Corrections are applied here to
	// the OUTPUT Euler angles only.
	// Values are scaled by 10 for integer CAN transmission.
	// ------------------------------------------------------------------------

	roll = round(-filter.getRoll() * 10.0);           // Negate: NWU→NED roll sign
	pitch = round(-filter.getPitch() * 10.0);          // Negate: NWU→NED pitch sign
	yaw = round((360.0 - filter.getYaw()) * 10.0);    // Invert: NWU CCW→NED CW positive

	// ------------------------------------------------------------------------
	// Calculate turn rate as derivative of NED yaw angle.
	// Using the AHRS-filtered yaw avoids raw gyro noise and bias drift,
	// since the filter corrects gyro errors using the magnetometer.
	// ------------------------------------------------------------------------
	float currentYaw = 360.0 - filter.getYaw();       // NED yaw in degrees
	unsigned long currentTime = millis();
	float dt = (currentTime - previousTime) / 1000.0;  // seconds

	if (resyncTurnRate) {			// AHRS restarted: yaw jumped, so no turn rate for this one
		resyncTurnRate = false;
		turnRateScaled = 0;
		previousYaw = currentYaw;
		previousTime = currentTime;
	} else if (dt > 0.05) {	// Calculate over 50ms window to balance noise vs responsiveness
		float turnRate = currentYaw - previousYaw;

		// Handle 360° wraparound
		if (turnRate > 180.0) turnRate -= 360.0;
		if (turnRate < -180.0) turnRate += 360.0;

		turnRate = turnRate / dt;                       // degrees per second

		// Dead band: zero out small values caused by sensor noise
		if (fabs(turnRate) < 0.5) turnRate = 0.0;

		turnRateScaled = round(turnRate * 10.0);
		previousYaw = currentYaw;
		previousTime = currentTime;
	}

	accelX = round(accel.acceleration.x * 100.0);
	accelY = round(accel.acceleration.y * 100.0);
	accelZ = round(accel.acceleration.z * 100.0);

	#if defined(AHRS_DEBUG_OUTPUT)
	if (Serial) Serial.print("Additional Calculations took "); Serial.print(millis()-timestamp); Serial.println(" ms");
	#endif

	// ------------------------------------------------------------------------
	// Only print the calculated output once in a while
	// ------------------------------------------------------------------------
	if (counter++ <= PRINT_EVERY_N_UPDATES) {
		return;
	}

	counter = 0;	// reset the counter for the next print update

	// ------------------------------------------------------------------------
	// Print RAW data if AHRS_DEBUG_OUTPUT defined
	// ------------------------------------------------------------------------
	#if defined(AHRS_DEBUG_OUTPUT)
	if (Serial) Serial.print("Raw: ");
	if (Serial) Serial.print(accel.acceleration.x, 4); Serial.print(", ");
	if (Serial) Serial.print(accel.acceleration.y, 4); Serial.print(", ");
	if (Serial) Serial.print(accel.acceleration.z, 4); Serial.print(", ");
	if (Serial) Serial.print(gx, 4); Serial.print(", ");
	if (Serial) Serial.print(gy, 4); Serial.print(", ");
	if (Serial) Serial.print(gz, 4); Serial.print(", ");
	if (Serial) Serial.print(mag.magnetic.x, 4); Serial.print(", ");
	if (Serial) Serial.print(mag.magnetic.y, 4); Serial.print(", ");
	if (Serial) Serial.print(mag.magnetic.z, 4); Serial.println("");
	#endif

	// ------------------------------------------------------------------------
	// Print the heading, pitch and roll
	// ------------------------------------------------------------------------
	#if defined(DEBUG)
	if (Serial) Serial.print("Orientation: ");
	if (Serial) Serial.print(yaw);
	if (Serial) Serial.print(", ");
	if (Serial) Serial.print(pitch);
	if (Serial) Serial.print(", ");
	if (Serial) Serial.println(roll);
	#endif

	// ------------------------------------------------------------------------
	// Print the gravitational accelerations
	// ------------------------------------------------------------------------
	
	#if defined(DEBUG)
	if (Serial) Serial.print("Acceleration: ");
	if (Serial) Serial.print(accelX);
	if (Serial) Serial.print(", ");
	if (Serial) Serial.print(accelY);
	if (Serial) Serial.print(", ");
	if (Serial) Serial.println(accelZ);
	#endif

	
	// ------------------------------------------------------------------------
	// Print timing if AHRS_DEBUG_OUTOUT is defined
	// ------------------------------------------------------------------------		
	#if defined(AHRS_DEBUG_OUTPUT)
	Serial.print("Took "); Serial.print(millis()-timestamp); Serial.println(" ms");
	#endif  
}
