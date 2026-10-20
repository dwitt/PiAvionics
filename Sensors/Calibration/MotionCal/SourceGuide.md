ocuments# MotionCal Source Guide

This document describes every source file in the MotionCal project — a desktop GUI tool for calibrating magnetometer, accelerometer, and gyroscope sensors. Originally by Paul Stoffregen (PJRC), this is the IanBUK fork with macOS/Apple Silicon fixes and UI enhancements.

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                        gui.cpp / gui.h                           │
│            wxWidgets GUI — panels, grids, buttons, menus         │
│            14ms timer drives the main loop                       │
└─────┬──────────┬──────────────┬──────────────────────────────────┘
      │          │              │
      ▼          ▼              ▼
 portlist.cpp  serialdata.c   visualize.c
 (enumerate    (open/read     (OpenGL 3D
  serial        serial port)   sphere view)
  ports)        │
               ▼
       serialdata.parsing.c ──► rawdata.c ──► magcal.c
       (parse Raw:/Cal1:/       (buffer &     (Freescale/NXP
        Cal2: ASCII lines)       oversample)   mag calibration)
               │                    │              │
               ▼                    ▼              ▼
       serialdata.messaging.c  fusion.c /      matrix.c
       (callback dispatch to    mahony.c       (linear algebra:
        GUI for all message     (AHRS sensor    eigenvalue,
        types)                   fusion)        inverse, etc.)
                                    │
                                    ▼
                                quality.c
                                (4 calibration
                                 quality metrics)
```

**Data flow:** Serial bytes arrive → `serialdata.c` assembles lines → `serialdata.parsing.c` parses `Raw:` CSV into 9 int16 values → `rawdata.c` buffers magnetometer samples and runs calibration → `magcal.c` computes hard-iron/soft-iron corrections → `visualize.c` renders the 3D sphere → `quality.c` computes gap/variance/wobble/fit metrics → GUI displays everything.

---

## Header Files

### `imuread.h` (258 lines)
**The central header** — included by every `.c` file.

Defines all shared data structures:
- `Point_t` — 3D float vector (x, y, z)
- `YawPitchRoll` — orientation angles
- `ImuData` — accel + gyro + mag composite (for callbacks)
- `SoftIronCalibrationData` / `OffsetsCalibrationData` — calibration packets from device
- `Quaternion_t` — orientation quaternion (q0-q3)
- `MagCalibration_t` — the big magnetometer calibration state: hard-iron offsets `V[3]`, inverse soft-iron matrix `invW[3][3]`, field strength `B`, fit error, trial values, plus the circular buffer of up to 650 raw magnetometer readings (`BpFast[3][MAGBUFFSIZE]`)
- `AccelSensor_t`, `MagSensor_t`, `GyroSensor_t` — sensor data at different sample rates
- `LineEnding` enum — LF, CR, CRLF (IanBUK addition)

Also declares all extern functions and callback typedefs that wire the modules together.

Key constants:
- `MAGBUFFSIZE = 650` — magnetometer sample buffer size
- `BUFFER_SIZE = 512` — serial read buffer
- `SENSORFS = 100` — assumed sensor sample rate (Hz)
- `OVERSAMPLE_RATIO = 4` — fusion runs at SENSORFS/4 = 25 Hz
- `G_PER_COUNT`, `UT_PER_COUNT`, `DEG_PER_SEC_PER_COUNT` — unit conversion factors

### `gui.h` (263 lines)
**wxWidgets GUI declarations.**

Defines three classes:
- **`MyCanvas`** — `wxGLCanvas` subclass for the 3D magnetometer sphere visualization. Handles paint, resize, mouse events.
- **`MyFrame`** — main application window (`wxFrame`). Contains all UI widgets (port list, baud rate, line ending dropdowns; pause/clear/send-calibration buttons; raw data grid, orientation grid; message log; calibration results panel). Has ~40 methods for building UI panels, handling events, and processing data callbacks.
- **`MyApp`** — `wxApp` subclass, creates the `MyFrame` at startup.

UI control IDs (10000-11000 range) for event routing. Grid constants for the 3x3 raw data display (accel/gyro/mag × X/Y/Z) and 1x3 orientation display (yaw/pitch/roll).

---

## GUI Layer

### `gui.cpp` (1264 lines)
**The main application file** — builds the entire wxWidgets UI and orchestrates everything.

**Startup flow:**
1. `MyApp::OnInit()` creates a `MyFrame` at 1120×760
2. `MyFrame` constructor calls `BuildMenu()`, `BuildBufferDisplayCallBack()`, then constructs the two-panel layout (left=processing, right=calibration)
3. Initializes OpenGL canvas, resets raw data, starts a **14ms repeating timer** (`wxTIMER_CONTINUOUS`)

**Layout:**
- **Left panel ("Processing"):** Connection settings (port/baud/line-ending dropdowns), Actions (pause/clear/send-cal buttons + status icon), Received Data (raw 3×3 grid + orientation 1×3 grid), Messages log
- **Right panel ("Calibration"):** Magnetic offset (3 values), Magnetic mapping (3×3 soft-iron matrix), Field strength, Accel offsets, Gyro offsets, calibration advice text

**Timer callback** (`OnTimer`, every 14ms): If not paused and port is open, calls `read_serial_data()`. Detects if port closed unexpectedly. Shows green check when calibration confirmed.

**Callback system** (`BuildBufferDisplayCallBack`): Registers 6 static→instance callback bridges so the C serial code can push parsed data into the C++ GUI:
- `StaticUpdateImuData` → `UpdateImuData` (updates raw data grid, calls `ProcessImuDataFromCallback`)
- `StaticUpdateOrientationData` → `UpdateOrientationData` (updates orientation grid)
- `StaticUnknownMessageReceived` → logs to messages panel
- `StaticCalibrationResponseMessageReceived` → logs calibration response
- `StaticSoftIronCalibrationDataReceived` / `StaticOffsetCalibrationDataReceived` → logs changed calibration data

**`ProcessImuDataFromCallback`**: The heart of the display update — refreshes the OpenGL canvas, reads the 4 quality metrics, logs changes to the message panel, enables/disables the Send Calibration button based on thresholds (gaps<15, variance<4.5, wobble<4, fit<5), and updates all calibration value labels.

**`OnSendCal`**: Logs the calibration matrix, calls `send_calibration()` (in `rawdata.c`) which sends a 68-byte binary packet over serial, then shows the results in a popup.

**Port management**: `ResetConnectionParameters()` closes the current port and opens the new one with the selected baud rate and line ending. Port list is de-duplicated (macOS IOKit sometimes returns duplicates).

---

## Serial Communication Layer

### `serialdata.c` (537 lines)
**Serial port open/read/write/close** — platform-specific implementations for Linux/macOS (POSIX termios) and Windows (Win32 COM API).

**macOS/Linux implementation:**
- `open_port(name, baud, lineEnding)`: Opens the serial device with `O_RDWR | O_NONBLOCK`, calls `cfmakeraw()` and `cfsetspeed()` with the selected baud rate. Sets the global `_lineEndingMode`. Returns error codes (-4 to -2) or 1 on success.
- `read_serial_data()`: Reads from the serial port into a 512-byte buffer. Assembles characters into complete lines using the configured line ending (LF, CR, or CRLF). When a complete line is found, calls `newdata()` which dispatches to both `packet_parse()` (binary) and `ascii_parse()` (text), plus `sendDataCallback()` (for the IanBUK callback system). Handles `EAGAIN` gracefully (10ms sleep). Closes port after 6 consecutive zero-byte reads.
- `write_serial_data()`: Blocking write with `select()` timeout for flow control.
- `close_port()`: Closes the file descriptor.
- `open_port_by_name(name)`: Simpler version that defaults to 115200 baud (used by the CLI `imuread` tool).

**Windows implementation** (lines 281-535): Uses `CreateFile`, `ReadFile`/`WriteFile` with overlapped I/O, `COMMCONFIG` for serial settings. Same logical flow but with Win32 APIs.

### `serialdata.messaging.c` (258 lines)
**Callback dispatch layer** — added by IanBUK to decouple serial parsing from GUI updates.

Maintains 7 function pointers (all initially NULL):
- `_imuDataCallback` — fired for `Raw:` messages
- `_orientationDataCallback` — fired for `Ori:` messages
- `_unknownMessageCallback` — fired for unrecognized messages
- `_calibrationResponseMessage` — fired for `CalR:` messages
- `_offsetsCallback` / `_softIronCallback` — fired for `Cal1:` / `Cal2:` messages
- `_displayBufferCallback` — general buffer display

Each has a setter (`setXxxCallback`) and a null-safe fire function (`fireXxxCallback`).

**`sendDataCallback(data, len)`** is the main entry point — called from `serialdata.c` for every complete line. It:
1. Checks the message prefix (`Raw`, `Ori`, `Cal1`, `Cal2`, `CalR`)
2. Parses CSV values using `strtok`
3. Fires the appropriate typed callback with a structured data object

For `Raw:` messages: parses 9 floats into an `ImuData` struct (accel x/y/z, gyro x/y/z, mag x/y/z).
For `Ori:` messages: parses 3 floats into a `YawPitchRoll` struct.
For `Cal1:`: parses 9 offset floats + 1 calMag into `OffsetsCalibrationData`.
For `Cal2:`: parses up to 10 soft-iron floats into `SoftIronCalibrationData`.

### `serialdata.parsing.c` (373 lines)
**Binary and ASCII protocol parsers** — the original PJRC parsing code.

**Binary protocol** (`packet_parse` → `packet_encoded` → `packet`):
- Uses `0x7E` as packet delimiter (SLIP-like framing)
- `0x7D` escape sequences for byte-stuffing
- `packet()` dispatches: type 1 (34 bytes) = primary IMU data with quaternion orientation; type 6 (14 bytes) = magnetometer calibration data points
- `packet_primary_data()`: Extracts quaternion (bytes 24-31, int16 scaled by 30000) into `current_orientation`
- `packet_magnetic_cal()`: Stores mag calibration data — id=1: hard-iron offsets, id=2: diagonal soft-iron, id=3: off-diagonal soft-iron, id≥10: raw magnetometer buffer entries

**ASCII protocol** (`ascii_parse`):
- State machine parsing `Raw:`, `Cal1:`, `Cal2:` messages character by character
- `Raw:` expects 9 comma-separated int16 values (accel, gyro, mag), terminated by CR (0x0D). Calls `raw_data()` when complete.
- `Cal1:` expects 10 floats (integer.decimal format). Calls `cal1_data()` for calibration confirmation.
- `Cal2:` expects 9 floats. Calls `cal2_data()` for calibration confirmation.

### `portlist.cpp` (294 lines)
**Serial port enumeration** — platform-specific discovery of available serial ports.

**macOS** (lines 109-237): Uses IOKit framework. Queries `IOServiceMatching(kIOSerialBSDServiceValue)` twice — once for modem-type devices and once for RS-232 devices. Extracts the `kIOCalloutDeviceKey` path (e.g., `/dev/cu.usbmodem...`) from each matching service. Uses the deprecated `IOMasterPort()` (generates a compile warning on macOS 12+).

**Linux** (lines 47-204): Scans `/dev/` for files starting with `tty` followed by known serial driver prefixes (a comprehensive list of 50+ Linux serial driver names). For each candidate, verifies it's a character device, opens it, checks `tcgetattr()` and `ioctl(TIOCMGET)` to confirm it's real hardware. Note: this raises DTR briefly on every port during scanning — an unfortunate Linux limitation.

**Windows** (lines 238-282): Uses `QueryDosDeviceA()` to enumerate COM ports. Falls back to brute-force probing COM1-COM32 with `GetDefaultCommConfig()` if that fails.

---

## Calibration Engine

### `magcal.c` (620 lines)
**Magnetometer calibration algorithms** — originally from Freescale Semiconductor (now NXP). The source file header explicitly warns: *"The mathematics behind this file is extremely complex, and it will be very easy (almost inevitable) that you screw it up."*

**`MagCal_Run()`**: Called periodically (every 20th invocation, to save CPU). Counts valid samples in the 650-entry buffer. Selects one of three solvers based on sample count:
- **4-element** (`fUpdateCalibration4INV`, ≥40 samples): Simple sphere fit using 4×4 matrix inversion. Computes hard-iron offsets only (soft-iron matrix = identity). Minimum fit error floor: 12%.
- **7-element** (`fUpdateCalibration7EIG`, ≥100 samples): Eigen-decomposition of a 7×7 matrix. Computes hard-iron offsets + diagonal soft-iron correction. Minimum fit error floor: 7.5%.
- **10-element** (`fUpdateCalibration10EIG`, ≥150 samples): Full eigen-decomposition of a 10×10 matrix. Computes hard-iron offsets + full 3×3 soft-iron matrix (6 independent values from symmetric matrix). No artificial floor.

Each solver:
1. Builds a measurement matrix from the buffered magnetometer readings (with DC offset removal and scaling)
2. Solves for the ellipsoid parameters that best fit the data
3. Extracts hard-iron offsets (center of ellipsoid) and soft-iron matrix (shape correction)
4. Validates the trial geomagnetic field strength is in range (22-67 µT, covering Earth's full range)
5. Accepts the new calibration if it improves on the previous fit error (with aging: old fits get +2%/cycle to allow updates)

### `matrix.c` (447 lines)
**Linear algebra utilities** — also from Freescale.

Functions:
- `f3x3matrixAeqI` — set 3×3 to identity
- `fmatrixAeqI` — set NxN to identity
- `f3x3matrixAeqScalar` — fill 3×3 with a constant
- `f3x3matrixAeqAxScalar` — scale all elements
- `f3x3matrixAeqMinusA` — negate all elements
- `f3x3matrixAeqInvSymB` — direct inverse of symmetric 3×3 (using cofactor/determinant)
- `f3x3matrixDetA` — 3×3 determinant
- **`eigencompute`** — Jacobi eigenvalue algorithm for real symmetric matrices up to 10×10. Iteratively applies rotation matrices to zero off-diagonal elements. Converges in ~6 iterations typically (max 15). Returns eigenvalues and normalized eigenvectors. This is the core math behind the 7- and 10-element calibration solvers.
- **`fmatrixAeqInvA`** — Gauss-Jordan elimination for general NxN matrix inversion with full pivoting. Used by the 4-element calibration solver.
- `fmatrixAeqRenormRotA` — re-orthonormalize a 3×3 rotation matrix using Gram-Schmidt

### `rawdata.c` (358 lines)
**Raw sensor data management** — buffers incoming measurements and orchestrates calibration + fusion.

**`raw_data_reset()`**: Clears the 650-entry magnetometer buffer, resets fusion, initializes magcal defaults (field strength guess = 50 µT, soft-iron = identity, fit error = 100%).

**`add_magcal_data(data)`**: Adds a new magnetometer reading to the buffer. If the buffer is full, calls `choose_discard_magcal()` to intelligently pick which old point to replace:
- If angular coverage is good (gaps < 25%): periodically find and discard the point with the worst field magnitude error (farthest from expected B)
- Otherwise: find the two closest points in the buffer and randomly discard one (preserves spatial diversity)

**`raw_data(data)`**: Called for each `Raw:` message. Adds mag data to buffer, runs `MagCal_Run()`. If calibration changes significantly (hard-iron shift > 0.8 µT), resets the fusion filter. Accumulates oversampled accel/gyro/mag readings (4:1 ratio), then calls `fusion_update()` and `fusion_read()` to update the orientation quaternion.

**`send_calibration()`**: Builds the 68-byte calibration packet:
- Bytes 0-1: Header `0x75, 0x54` (117, 84)
- Bytes 2-13: Accelerometer offsets (3 floats, always 0.0)
- Bytes 14-25: Gyroscope offsets (3 floats, always 0.0)
- Bytes 26-37: Hard-iron offsets `magcal.V[0..2]` (3 floats)
- Bytes 38-41: Field strength `magcal.B` (1 float)
- Bytes 42-65: Soft-iron matrix — 6 floats: `invW[0][0], invW[1][1], invW[2][2], invW[0][1], invW[0][2], invW[1][2]` (compressed symmetric 3×3)
- Bytes 66-67: CRC16 checksum (polynomial 0xA001, init 0xFFFF)

Also stores sent values in `cal_data_sent[]` and sets `cal_confirm_needed=3` (bitmask: awaiting both Cal1 and Cal2 echo-back confirmation).

**`cal1_data()` / `cal2_data()`**: Verify that calibration values echoed back from the device match what was sent (within tolerance). When both confirm, calls `calibration_confirmed()` (sets the green check icon in the GUI).

**`apply_calibration(rawx, rawy, rawz, *out)`**: Applies the current calibration to a raw magnetometer reading: subtracts hard-iron offsets `V[]`, then multiplies by the inverse soft-iron matrix `invW[][]`.

---

## Sensor Fusion

### `fusion.c` (≈1200 lines, only NXP path shown)
**NXP 9-DOF Kalman filter** — conditionally compiled with `#ifdef USE_NXP_FUSION` (currently **disabled** — `USE_MAHONY_FUSION` is active instead).

This is the full Freescale/NXP sensor fusion algorithm — a 9-DOF gravity+compass Kalman filter that estimates orientation by fusing accelerometer (gravity reference), magnetometer (compass reference), and gyroscope (rate integration). Maintains a full state vector with orientation quaternion, gyro bias estimates, linear acceleration estimates, and magnetic disturbance estimates.

Includes its own math utilities: `fqAeq1` (identity quaternion), `feCompassNED` (eCompass from accel+mag), `fNEDAnglesDegFromRotationMatrix`, quaternion↔rotation matrix conversions, fast trig approximations (`fasin_deg`, `facos_deg`, `fatan_deg`, `fatan2_deg` using 15° polynomial segments).

Not used in the current build — included as a reference/alternative to the simpler Mahony filter.

### `mahony.c` (331 lines)
**Mahony AHRS filter** — the **active** fusion algorithm (`#ifdef USE_MAHONY_FUSION`).

Implementation of Madgwick's version of Mahony's complementary filter (from x-io.co.uk). Much simpler than the NXP Kalman filter — uses proportional+integral error correction between measured and estimated gravity/magnetic field directions.

**Key functions:**
- `fusion_init()` → `mahony_init()`: Resets quaternion to [1,0,0,0], clears integral error terms, sets `reset_next_update` flag for aggressive first correction
- `fusion_update()`: Converts gyro to rad/s, runs `mahony_update()` at the oversample rate (4× per fusion cycle)
- `fusion_read()`: Returns the current quaternion
- `mahony_update(gx,gy,gz, ax,ay,az, mx,my,mz)`: The 9-DOF AHRS update:
  1. Normalize accel and mag vectors
  2. Compute estimated gravity and magnetic field directions from current quaternion
  3. Cross-product error between estimated and measured directions
  4. Apply PI correction to gyro rates (proportional gain Kp=0.02, integral gain Ki=0)
  5. Integrate quaternion using corrected gyro rates
  6. Normalize quaternion
- `mahony_updateIMU()`: 6-DOF fallback (accel+gyro only) when magnetometer data is invalid
- `invSqrt()`: The famous Quake III fast inverse square root hack (magic number `0x5f375a86`), with 3 Newton-Raphson refinement steps for accuracy

**Tuning:** `twoKp = 0.04` (proportional), `twoKi = 0.0` (integral disabled). Sample rate assumed 100 Hz.

---

## Visualization

### `visualize.c` (252 lines)
**OpenGL 3D rendering** of the magnetometer calibration sphere.

**`visualize_init()`**: Sets up OpenGL state — white background, depth testing, lighting (one directional light), material properties. Creates two display lists: a high-res sphere (16×14 segments) for front-facing points and a low-res sphere (12×10) for back-facing points. Uses GLU quadrics.

**`display_callback()`**: Called every frame (triggered by `m_canvas->Refresh()` in the GUI timer). For each valid entry in the 650-slot mag buffer:
1. Applies current calibration (`apply_calibration`) to get corrected XYZ
2. Updates quality metrics (`quality_update`)
3. Rotates point by current orientation quaternion (converted to 3×3 rotation matrix)
4. Draws a small sphere at that position (red if calibrating, green if ready to save)

Also draws a yellow/blue triangle representing the sensor's current physical orientation (using `SensorOrientation` yaw/pitch/roll from `Ori:` messages — an IanBUK addition).

**`resize_callback()`**: Sets up the projection matrix with a perspective frustum (aspect-ratio corrected).

The `invert_q0/q1/q2/q3` and `invert_x/y/z` flags (togglable via keyboard in the CLI `imuread` tool) help debug axis alignment between the sensor and the display.

---

## Quality Metrics

### `quality.c` (244 lines)
**Four calibration quality metrics** displayed in the GUI and used to decide when calibration is good enough to send.

The sphere surface is divided into **100 regions of equal area**: 1 arctic cap + 15 northern temperate + 34 northern tropic + 34 southern tropic + 15 southern temperate + 1 antarctic cap. Each calibrated magnetometer point is assigned to a region based on its latitude/longitude on the unit sphere.

**`quality_surface_gap_error()`** (Gaps):
Counts how many of the 100 regions have insufficient data. Empty region = +1.0, single point = +0.2, two points = +0.01. Range: 0 (perfect coverage) to 100 (no data). GUI enables "Send Calibration" when gaps < 15.

**`quality_magnitude_variance_error()`** (Variance):
Standard deviation of all calibrated point magnitudes divided by mean, as a percentage. Measures how consistently all points are the same distance from the origin. Low values mean the hard-iron correction has centered the sphere well. GUI threshold: < 4.5%.

**`quality_wobble_error()`** (Wobble):
For each occupied sphere region, computes the average point position. Compares this average against the ideal position on a perfect sphere of mean radius. The RMS offset of all regions from their ideal positions, as a percentage of radius. Detects systematic distortion that variance alone might miss. GUI threshold: < 4.0%.

**`quality_spherical_fit_error()`** (Fit Error):
Directly returns `magcal.FitError` from the calibration engine. This is the residual error from the least-squares ellipsoid fit, as a percentage. GUI threshold: < 5.0%.

---

## Utility Files

### `logging.c` (92 lines)
**File-based logging** to `log.txt` in the application directory.

- `logMessage(message)`: Appends a line to log.txt (opens, writes, closes each call — simple but not high-performance)
- `debugPrint(name, data, len, showHex)`: Dumps raw byte data as hex+ASCII (e.g., `4F[O] 72[r] 69[i]`) or plain characters
- `logTerminalSettings(termsettings)`: Logs termios struct members (c_iflag, c_oflag, c_cflag, c_lflag) for debugging serial port configuration
- `print_data(name, data, len)`: Similar to `debugPrint` but always hex format

### `imuread.c` (144 lines)
**Standalone CLI tool** — a Linux-only GLUT-based alternative to the wxWidgets GUI. Compiled as the `imuread` target in the Makefile. **Not used on macOS** (the `#else` branch just has an empty `main()`).

When built for Linux: opens the serial port directly (hardcoded port name), uses GLUT for windowed OpenGL rendering, and provides keyboard shortcuts (0-3 toggle quaternion inversions, x/y/z toggle axis inversions) for debugging orientation alignment. Pressing any other key sends calibration if fit error < 9%.

### `png2c.pl` (96 lines)
**Perl build tool** — converts PNG image files into C source code for embedding in the binary.

Run by the Makefile rule `images.cpp: $(IMGS) png2c.pl`. Takes PNG filenames as arguments, reads each file as raw bytes, and generates:
- A `static unsigned char png_xxx[]` array for each image
- Lookup tables by name and index
- A `MyBitmap(name)` function that decodes PNGs at runtime using `wxMemoryInputStream` + `wxImage`

The three embedded images: `checkgreen.png` (calibration confirmed), `checkempty.png` (calibration ready), `checkemptygray.png` (not ready).

---

## Build Files

### `Makefile` (117 lines)
Multi-platform build configuration with 4 targets:
- **`MACOSX_CLANG`** (active): Uses `/usr/bin/clang++`, `-std=c++11`, `wx-config` from PATH. Builds `MotionCal` binary then bundles as `MotionCal.app`.
- `MACOSX`: Older GCC 4.2 path (obsolete)
- `LINUX`: GCC with hardcoded wxWidgets path, also builds `imuread` CLI tool
- `WINDOWS`: MinGW cross-compilation with `windres` for icon embedding

Object files: `logging.o visualize.o serialdata.o serialdata.messaging.o serialdata.parsing.o rawdata.o magcal.o matrix.o fusion.o quality.o mahony.o` plus `gui.o portlist.o images.o`.

The `.app` bundle is created by copying the binary, `Info.plist` (with version substitution), and `icon.icns` into the standard macOS bundle structure.

### `Info.plist` (29 lines)
macOS app bundle metadata. Sets bundle identifier, version placeholder (replaced by `sed` during build), icon file reference.

### `.gitignore` (20 lines)
Excludes build artifacts (`*.o`, `images.cpp`, `MotionCal`, `MotionCal.app/`, `MotionCal.dmg`), pre-built directories, logs, and IDE files.

### `.vscode/` directory
VS Code settings for the project (editor configuration).

---

## Non-Source Files

| File | Description |
|------|-------------|
| `NewUI.jpg` | Screenshot of the IanBUK UI redesign |
| `checkgreen.png` | Green check icon — calibration confirmed |
| `checkempty.png` | Empty check icon — calibration ready to send |
| `checkemptygray.png` | Gray check icon — calibration not ready |
| `icon.icns` | macOS app icon |
| `icon.ico` | Windows app icon |
| `resource.rc` | Windows resource file (references `icon.ico`) |
| `cp_windows.sh` | Script to copy Windows build to distribution |
| `README.md` | IanBUK's documentation of changes and build instructions |
| `PJRC_Reference.md` | Reference notes on the original PJRC tool |
