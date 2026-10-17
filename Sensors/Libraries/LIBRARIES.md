# Third-party libraries used by the sensor sketches

Installed from the Arduino Library Manager into `~/Documents/Arduino/libraries` (versions as built on 2026-10-17, board
package `rp2040:rp2040` 6.3.0). Reinstall these on a new machine. Our own and patched libraries are in this folder
(`README.md`), except `DW_Sensor_Calibration` (`Sensors/Calibration/`).

| Library | Version | IMU | AIR | GPS |
|---|---|---|---|---|
| Adafruit BusIO | 1.17.4 | x | x | x |
| Adafruit Unified Sensor | 1.1.15 | x | x | |
| Adafruit CAN (needed by the MCP2515 library) | 0.2.3 | x | x | x |
| Adafruit NeoPixel (used by StatusLED) | 1.15.5 | x | x | x |
| Adafruit LIS3MDL | 1.2.5 | x | | |
| Adafruit FRAM I2C (provides `Adafruit_EEPROM_I2C.h`) | 2.0.3 | x | | |
| Adafruit AHRS | 2.4.0 | x | | |
| Adafruit MAX31865 library | 1.6.2 | | x | |
| Adafruit GPS Library | 1.9.0 | | | x |
| Wire, SPI (part of the rp2040 board package) | 1.0 | x | x | x |

The GPS sketch also uses `XYZgeomag.hpp` (WMM2025), which sits in the sketch folder.

To refresh this table: `arduino-cli compile -v --fqbn rp2040:rp2040:adafruit_feather Sensors/<Sketch> | grep "^Using library"`.
