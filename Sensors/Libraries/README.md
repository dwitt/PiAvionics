# Shared libraries for the sensor modules

These libraries are ours (or locally patched copies of Adafruit libraries) and are shared by the IMU, AIR and
GPS sketches. They live here, once.

| Folder | What | Used by |
|---|---|---|
| `ModuleInfo` | USB product name, startup log, `?` report, CAN heartbeat packing, test-command hook | IMU, AIR, GPS |
| `StatusLED` | NeoPixel startup colours, POST codes, runtime heartbeat colours | IMU, AIR, GPS |
| `SensorHealth` | valid / invalid decision for a sensor from good/bad samples | IMU, AIR |
| `Adafruit_MCP2515` | **patched** CAN library (bounded send, error counters) | IMU, AIR, GPS |
| `Adafruit_LSM6DS` | **patched** accel/gyro library (a failed read returns false) | IMU |

The patched ones have a `PATCH_NOTES.md` and a `local_patch.diff`.

## How the sketches find them

Each sketch folder has a `src/` folder with **relative symlinks** to the libraries it uses, for example
`IMU_AHRS/src/ModuleInfo -> ../../Libraries/ModuleInfo`, and includes them with quotes:

    #include "src/ModuleInfo/ModuleInfo.h"
    #include "src/Adafruit_MCP2515/src/Adafruit_MCP2515.h"     // the patched library has its own src/ folder
    #include "src/Adafruit_LSM6DS/Adafruit_ISM330DHCX.h"

The Arduino IDE and `arduino-cli` compile everything under a sketch's `src/` folder together with the sketch
(tested 2026-10-17 with arduino-cli; IDE 2.x uses the same engine). So no setup is needed in
`~/Documents/Arduino/libraries`, and Library Manager updates can never touch or delete these copies (an update of a
library that is a symlink into the repo deletes the folder it points to; this layout avoids that).
`flash_remote.sh` still passes `--libraries Sensors/Libraries`; with the quoted includes it makes no difference.

Add a new sketch's links with: `mkdir src; ln -s ../../Libraries/<Name> src/<Name>` (run inside the sketch folder).

## What still comes from the Arduino folder

Third-party libraries (unchanged, from Library Manager) and one of our own: `DW_Sensor_Calibration`
(`Sensors/Calibration/DW_Sensor_Calibration`, symlinked in `~/Documents/Arduino/libraries`, used by the IMU as
`<Sensor_Calibration.h>`). `StatusLED` is also still symlinked there from earlier; nothing needs that link any more.
The versions in use are in `LIBRARIES.md`.

## Notes

- Never leave a symlink inside a library that points back at the library (a stray `StatusLED/StatusLED` link once did,
  and made the sketch scan fail with "directories symlink loop detected").
- The Ride G-Force logger (a separate project) builds from `~/Documents/Arduino/libraries`, which has its own copy
  of the patched `Adafruit_LSM6DS`. That copy is not linked to this one.
