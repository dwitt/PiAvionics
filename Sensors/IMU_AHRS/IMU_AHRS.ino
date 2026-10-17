
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
#define VERSION "1.0.5"

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
//----------------------------------------------------------------------------
#define HB_ISM330DHCX   0x01
#define HB_LIS3MDL      0x02
#define HB_CALIBRATION  0x04
#define HB_ALL          (HB_ISM330DHCX | HB_LIS3MDL | HB_CALIBRATION)

static uint8_t hbSensors = 0;
static bool canUp = false;			// heartbeat is only sent once the CAN bus is running

// Live CAN transmit status for the '?' report. A send that nobody acknowledges is abandoned
// after a few ms (patched Adafruit_MCP2515, see Libraries/Adafruit_MCP2515/PATCH_NOTES.md).
static void canReport(Print &out) {
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
Adafruit_Sensor *accelerometer, *gyroscope, *magnetometer;

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
	bool ism330dhcx_success = ism330dhcx.begin_I2C(ISM330DHCX_I2C_ADDRESS, &Wire);
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
	// -----------------------------------------------------------------------

	led.initBegin();
	bool lis3mdl_success = lis3mdl.begin_I2C(LIS3MDL_I2C_ADDRESS, &Wire);
	if (lis3mdl_success) {
		led.initOK();
		moduleInfo.log("LIS3MDL OK");
		hbSensors |= HB_LIS3MDL;
	} else {
		led.initFail();
		moduleInfo.fatal("LIS3MDL FAILED");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.postCode(POST_LIS3MDL);  // never returns
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

	// Note: The accelerometer data range is set to 4G instead of 2G as in
	// the Adafruit Calibration routine.

	#define ACCEL_RANGE LSM6DS_ACCEL_RANGE_4_G			// +/- 4G
	#define GYRO_RANGE LSM6DS_GYRO_RANGE_250_DPS		// 250 degrees/sec
	#define MAG_RANGE LIS3MDL_RANGE_4_GAUSS				// 4 Gauss

	#define ACCEL_DATA_RATE LSM6DS_RATE_104_HZ			// 104 Hz
	#define GYRO_DATA_RATE LSM6DS_RATE_104_HZ			// 104 Hz
	#define MAG_DATA_RATE LIS3MDL_DATARATE_1000_HZ		// 1000 Hz

	#define MAG_POWER_MODE LIS3MDL_MEDIUMMODE			// Medium Power
	#define MAG_OPERATION_MODE LIS3MDL_CONTINUOUSMODE	// Continuous

	// --- Sensor ranges (config fail code 1) ---

	ism330dhcx.setAccelRange(ACCEL_RANGE);
	if (ism330dhcx.getAccelRange() != ACCEL_RANGE) {
		moduleInfo.fatal("Config FAIL: accel range");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.configFail(CONFIG_FAIL_RANGE);  // never returns
	}

	ism330dhcx.setGyroRange(GYRO_RANGE);
	if (ism330dhcx.getGyroRange() != GYRO_RANGE) {
		moduleInfo.fatal("Config FAIL: gyro range");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.configFail(CONFIG_FAIL_RANGE);  // never returns
	}

	lis3mdl.setRange(MAG_RANGE);
	if (lis3mdl.getRange() != MAG_RANGE) {
		moduleInfo.fatal("Config FAIL: mag range");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.configFail(CONFIG_FAIL_RANGE);  // never returns
	}

	// --- Sensor data rates (config fail code 2) ---

	ism330dhcx.setAccelDataRate(ACCEL_DATA_RATE);
	if (ism330dhcx.getAccelDataRate() != ACCEL_DATA_RATE) {
		moduleInfo.fatal("Config FAIL: accel data rate");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.configFail(CONFIG_FAIL_DATARATE);  // never returns
	}

	ism330dhcx.setGyroDataRate(GYRO_DATA_RATE);
	if (ism330dhcx.getGyroDataRate() != GYRO_DATA_RATE) {
		moduleInfo.fatal("Config FAIL: gyro data rate");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.configFail(CONFIG_FAIL_DATARATE);  // never returns
	}

	lis3mdl.setDataRate(MAG_DATA_RATE);
	if (lis3mdl.getDataRate() != MAG_DATA_RATE) {
		moduleInfo.fatal("Config FAIL: mag data rate");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.configFail(CONFIG_FAIL_DATARATE);  // never returns
	}

	// --- Sensor modes (config fail code 3) ---

	lis3mdl.setPerformanceMode(MAG_POWER_MODE);
	if (lis3mdl.getPerformanceMode() != MAG_POWER_MODE) {
		moduleInfo.fatal("Config FAIL: mag power mode");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.configFail(CONFIG_FAIL_MODE);  // never returns
	}

	lis3mdl.setOperationMode(MAG_OPERATION_MODE);
	if (lis3mdl.getOperationMode() != MAG_OPERATION_MODE) {
		moduleInfo.fatal("Config FAIL: mag operation mode");
		moduleInfo.setHealth(MODULE_STATE_FAILED, hbSensors);
		led.configFail(CONFIG_FAIL_MODE);  // never returns
	}

	moduleInfo.log("Sensor configuration OK");
	moduleInfo.setHealth((hbSensors & HB_ALL) == HB_ALL ? MODULE_STATE_OK : MODULE_STATE_DEGRADED, hbSensors);

	/*************************************************************************
	 * Setup Sensors
	 *************************************************************************/

	accelerometer = ism330dhcx.getAccelerometerSensor();
	gyroscope = ism330dhcx.getGyroSensor();
	magnetometer = &lis3mdl;

	#if defined(DEBUG)
	accelerometer->printSensorDetails();
	gyroscope->printSensorDetails();
	magnetometer->printSensorDetails();
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
	accelerometer->getEvent(&accel);	// m/s^2
	gyroscope->getEvent(&gyro);			// rads/s
	magnetometer->getEvent(&mag);		// uT
	
	#if defined(AHRS_DEBUG_OUTPUT)
	if (Serial) Serial.print("I2C took "); Serial.print(millis()-timestamp); Serial.println(" ms");
	#endif

	// ------------------------------------------------------------------------
	// Apply the calibration to each of the sensors
	// ------------------------------------------------------------------------
	cal.calibrate(mag);
	cal.calibrate(accel);
	cal.calibrate(gyro);

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
				  mag.magnetic.x, mag.magnetic.y, mag.magnetic.z);

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
	static float previousYaw = 0;
	static unsigned long previousTime = 0;

	float currentYaw = 360.0 - filter.getYaw();       // NED yaw in degrees
	unsigned long currentTime = millis();
	float dt = (currentTime - previousTime) / 1000.0;  // seconds

	if (dt > 0.05) {	// Calculate over 50ms window to balance noise vs responsiveness
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
