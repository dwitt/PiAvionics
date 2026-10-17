# Status LED Standard

All sensor modules use the onboard NeoPixel LED (pin 16) for startup diagnostics and runtime health indication. This document defines the universal scheme shared across all modules.

## Hardware

- Adafruit Feather RP2040 onboard NeoPixel
- Pin 16, 1 LED, GRB + 800KHz
- Brightness: 10 (low, non-distracting in cockpit)

## Startup Sequence

| State | LED | Duration |
|-------|-----|----------|
| Power on | Solid **red** | Until first peripheral init |
| Peripheral init attempt | Brief **blue** flash | 150ms |
| Peripheral success | Brief **green** flash | 200ms |
| Peripheral failure | Brief **red** flash | 200ms |
| All passed -- Go | Solid **green** | 2 seconds, then runtime |
| Any failed -- No-Go | POST code (see below) | Repeats forever |

The blue-green/red sequence plays for each peripheral in the standard init order. The number of flashes tells you how far init progressed.

## Standard Init Order

All modules follow this order. Modules skip steps that don't apply to them.

| Step | Category | GPS | AIR | IMU |
|------|----------|-----|-----|-----|
| 1 | NeoPixel -- red | Yes | Yes | Yes |
| 2 | Serial (debug, 10s timeout) | Yes | Yes | Yes |
| 3 | I2C bus setup (Wire1 pins) | -- | Wire1 SDA/SCL | -- |
| 4 | Module sensors (see POST codes) | GPS UART | SSC, pitot, AOA, RTD | ISM330, LIS3MDL, EEPROM |
| 5 | CAN bus | Yes | Yes | Yes |
| 6 | Go / No-Go decision | Yes | Yes | Yes |
| 7 | Sensor configuration | -- | Ranges, types | Ranges, rates, modes |
| 8 | Finalize (I2C clock, CAN filter) | -- | Yes | Yes |

## Universal POST Codes (Red Flashes)

When a peripheral fails to initialize, the LED enters a repeating flash pattern:
- **N red flashes** (200ms on, 200ms off each)
- **1500ms pause**
- Repeat forever

The flash count identifies the failed peripheral. The same count means the same category on every module.

| Flashes | Category | GPS | AIR | IMU |
|---------|----------|-----|-----|-----|
| 1 | Primary I2C sensor | -- | SSC static pressure | ISM330DHCX |
| 2 | Secondary I2C sensor | -- | MS4525DO pitot | LIS3MDL |
| 3 | Tertiary I2C sensor | -- | MS4525DO AOA | -- |
| 4 | *reserved (I2C)* | | | |
| 5 | SPI sensor 1 | -- | MAX31865 RTD | -- |
| 6 | *reserved (SPI)* | | | |
| 7 | UART device | GPS | -- | -- |
| 8 | *reserved (UART)* | | | |
| 9 | EEPROM / Calibration | -- | -- | EEPROM |
| 10 | *reserved* | | | |
| 11 | CAN bus | CAN | CAN | CAN |
| 12 | *reserved (comms)* | | | |

## Config Failure POST Codes (Amber Flashes)

Post-go configuration failures (e.g., sensor range read-back mismatch) use **amber/yellow flashes** with the same timing pattern. Currently only applicable to the IMU module.

| Flashes | Category |
|---------|----------|
| 1 | Sensor range config |
| 2 | Sensor data rate config |
| 3 | Sensor mode config |
| 4 | *reserved* |

## Runtime LED States

During normal operation the LED shows a heartbeat -- a brief flash every ~2 seconds.

| State | LED Color | Meaning |
|-------|-----------|---------|
| Healthy + active | **Green** heartbeat | Loop running, CAN transmitting |
| Sensor fault (recoverable) | **Amber** heartbeat | A sensor read failed but module continues |
| CAN failure | **Red** heartbeat | CAN transmit error |

Since 1.2.0 the heartbeat flashes once a second (not every 4 s) while the colour is amber or red, so a
fault is noticed. Modules map their heartbeat state to the colour: state 1 OK = green, state 2 degraded
(a sensor missing or invalid) = amber (`STATUS_LED_SENSOR_FAULT`), state 3 failed = red
(`STATUS_LED_FAILED`), and a CAN transmit that nobody acknowledges = red (`STATUS_LED_CAN_FAULT`).

### GPS-Specific Runtime States

| State | LED Color |
|-------|-----------|
| Running, no fix | **Blue** heartbeat |
| Running, 2D fix | **Yellow** heartbeat |
| Running, 3D fix | **Green** heartbeat |

## Implementation

The `StatusLED` library lives in `Sensors/Libraries/StatusLED/` with a symlink from `~/Documents/Arduino/libraries/StatusLED/`.

### API

```cpp
#include <StatusLED.h>

StatusLED led;          // uses default pin 16, brightness 10

// Startup phase
led.startup();          // solid red
led.initBegin();        // blue flash -- about to init a peripheral
led.initOK();           // green flash -- peripheral OK
led.initFail();         // red flash -- peripheral failed
led.go();               // solid green for 2s, then returns
led.postCode(n);        // n red flashes, pause, repeat forever (never returns)
led.configFail(n);      // n amber flashes, pause, repeat forever (never returns)

// Runtime phase
led.heartbeat();        // call each loop -- handles timing internally
led.setHealth(state);   // GREEN, AMBER, RED -- controls heartbeat color
```
