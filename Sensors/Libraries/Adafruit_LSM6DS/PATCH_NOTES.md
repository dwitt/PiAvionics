# Local patch to Adafruit_LSM6DS 4.7.4

**This is a locally modified copy of the Adafruit library. Do not update or reinstall it from
Library Manager without re-applying the patch (see "Re-applying" below).**

Upstream: https://github.com/adafruit/Adafruit_LSM6DS (BSD licence, `license.txt`).
Base version: 4.7.4. Patch dated 2026-04-20. Copied into PiAvionics on 2026-10-17.

## Summary

The stock library ignores whether the I2C read of the sensor data worked. This patch makes a
failed read visible: `_read()` returns `false`, every `getEvent()` passes that back to the
caller, and the previous good values are left untouched. A static counter,
`Adafruit_LSM6DS::readFailures`, counts failed reads.

Nothing changes when reads succeed.

## Why (the problem in the stock library)

`Adafruit_LSM6DS::_read()` calls `data_reg.read(buffer, 14)` and ignores the return value. If the
I2C transaction fails (NACK, timeout, device missing), `buffer` is uninitialised stack memory,
and the driver converts it into accelerometer, gyro and temperature values and returns `true`
from `getEvent()`. A caller cannot tell a good sample from garbage.

## History: where the patch came from

It was written for the **Ride G-Force data logger** (`~/Documents/Arduino/Ride_G-Force_MassStorage`,
RP2040 + ISM330DHCX, 400 kHz I2C, 120 Hz sampling). That logger showed roughly 0.1% bad samples,
even at rest on a bench. The first theory was exactly the bug above (failed reads publishing
stack garbage). The patch was written to test it.

Result: the patch caught only about 1 of 52 observed glitches. The real cause on the logger was
reads that the I2C hardware reports as *successful* but that return corrupted bytes (one or two
axes at a time; suspected electrical coupling from SPI/SD traffic on the other core). That cannot
be detected at the Wire layer, and the logger fixed it with triple-read voting in its sketch.
The patch was kept because it is correct and harmless. (Details are in that project's notes,
`imu_read_validation.md` and `session_2026-04-20.md`.)

Corruption of that kind has not been seen on the PiAvionics IMU module.

## Upstream status (checked 2026-10-17)

Not fixed in upstream: the latest release is still 4.7.4 and master still ignores the read result.
- Issue https://github.com/adafruit/Adafruit_LSM6DS/issues/26 (2021): asks for a success flag.
- PR https://github.com/adafruit/Adafruit_LSM6DS/pull/27 (2021, open, mergeable): the same change as
  this patch (including the DSO32 override), without our `readFailures` counter. On 2025-03-06 the
  maintainer said it looks good but wanted the other drivers checked and testing before merging.
- David added a comment on PR #27 on 2026-10-17 with our test hardware and the finding that no
  other driver overrides `_read()`.
When #27 is merged and released, this local patch can be dropped (the `readFailures` counter
would go with it; nothing in PiAvionics uses it).

## Why PiAvionics keeps it

Failure detection (`Sensors/FAILURE_DETECTION_PLAN.md`, ISSUES.md #13). If the ISM330DHCX is
unplugged, dead or the I2C bus is stuck, the stock library keeps returning "good" garbage. With the
patch, `getEvent()` returns `false`, so the IMU firmware can count consecutive failures, mark the
gyro/accelerometer invalid, and report it in the CAN heartbeat (0x701 sensors mask bit 0).

## What the patch does and does not catch

| Situation | Caught? |
|-----------|---------|
| Chip unplugged, wing removed, SDA/SCL stuck, NACK, bus timeout | Yes: `getEvent()` returns `false`, `readFailures` increments |
| Read "succeeds" but one or two axes are corrupted (the Ride G-Force glitches) | **No** |
| Chip alive but locked up and returning all zeros | **No** (see the plan: a "gx, gy, gz and ax, ay, az all zero" check belongs in the sketch) |

Note: the sketch must check the return value of `getEvent()`; the patch alone only makes the
information available. As of IMU_AHRS 1.0.4 the sketch still ignores it (to be changed in the
failure-detection work).

## What changed

Four files; every changed line is marked `LOCAL PATCH`.

- `Adafruit_LSM6DS.h`
  - `virtual void _read(void)` becomes `virtual bool _read(void)`.
  - Added `static uint32_t readFailures;` (public).
- `Adafruit_LSM6DS.cpp`
  - Defines `uint32_t Adafruit_LSM6DS::readFailures = 0;`.
  - `_read()` returns `bool`. If the 14-byte register read fails it increments `readFailures`,
    returns `false`, and does not touch `rawAcc*`, `rawGyro*`, `rawTemp` or any scaled value.
    Returns `true` at the end.
  - `getEvent(accel, gyro, temp)` returns `false` when `_read()` fails.
  - `Adafruit_LSM6DS_Gyro::getEvent`, `Adafruit_LSM6DS_Accelerometer::getEvent` and
    `Adafruit_LSM6DS_Temp::getEvent` return `false` when `_read()` fails.
- `Adafruit_LSM6DSO32.h` and `.cpp`
  - `_read()` has to be changed to `bool` with the same failure check, because it overrides the
    base class method and the build fails with "conflicting return type" otherwise. We do not use
    the DSO32 chip; Arduino compiles every `.cpp` in the library folder.

`Adafruit_ISM330DHCX` (the class we use) inherits all of this from `Adafruit_LSM6DS`.

## Files in this folder

- `Adafruit_LSM6DS.h.orig`, `Adafruit_LSM6DS.cpp.orig`: the unmodified 4.7.4 originals.
  (There is no `.orig` for the DSO32 files; their change is small and described above.)
- `local_patch.diff`: unified diff (`.orig` to patched) for the two main files, so the change can
  be re-applied to a fresh 4.7.4 (from inside the library folder: `patch -p1 < local_patch.diff`) or by hand.
- `PATCH_NOTES.md`: this file.

## Checking that the patched library is the one being built

Build verbosely and look for the library path:

    arduino-cli compile -v --fqbn rp2040:rp2040:adafruit_feather Sensors/IMU_AHRS/ | grep LSM6DS

It should say version 4.7.4 and name the folder that contains this patch. A quick source check:

    grep -c "LOCAL PATCH" Sensors/Libraries/Adafruit_LSM6DS/Adafruit_LSM6DS.cpp

(should be 7 or more). Any sketch that references `Adafruit_LSM6DS::readFailures` (the Ride
G-Force logger does) will fail to link against an unpatched library, which is an automatic guard.

## Re-applying after a library update or reinstall

1. Install or update the library from Library Manager (check the version; this patch is for 4.7.4).
2. Replace its `Adafruit_LSM6DS.h/.cpp` and `Adafruit_LSM6DSO32.h/.cpp` with the files from this
   folder, or apply `local_patch.diff` to the two main files and make the DSO32 change by hand.
   If the upstream version has changed, review the diff by hand rather than overwriting.
3. Rebuild and check as above. Consider sending the fix upstream; it is a plain bug fix.

## Where the live copy is

This folder is the only copy the PiAvionics sketches use: `IMU_AHRS/src/Adafruit_LSM6DS` is a symlink to it
(see `Sensors/Libraries/README.md`), so it builds the same in the Arduino IDE, arduino-cli and `flash_remote.sh`.
A separate copy in `~/Documents/Arduino/libraries/Adafruit_LSM6DS` (identical when this was written) is used only by
the Ride G-Force logger; it is not linked to this one.
