# IMU In-Flight Calibration Design

Design notes for replacing the standalone MotionCal desktop application with an integrated calibration system that runs through the existing EFIS infrastructure.

## Goal

Allow magnetometer (and accel/gyro) calibration from the cockpit without connecting a laptop. The pilot opens a calibration page on the EFIS display, rotates the sensor board, watches a 3D sphere fill in on screen, and applies the calibration when quality metrics are satisfied.

## Current Calibration Workflow

1. Flash `IMU_Calibration.ino` to the Feather RP2040 (different firmware)
2. Connect USB serial to a Mac running MotionCal
3. Rotate the board through all orientations
4. MotionCal computes calibration and sends a 68-byte packet over serial
5. `IMU_Calibration.ino` receives the packet and writes to EEPROM
6. Flash `IMU_AHRS.ino` back onto the board (production firmware)
7. `IMU_AHRS.ino` loads calibration from EEPROM at startup

Problems: requires a laptop, USB cable, two firmware reflashes, and the standalone MotionCal app.

## Proposed Workflow

1. Pilot triggers "Calibrate IMU" from the EFIS display (soft button or menu)
2. EFIS server sends a CAN command to the IMU: **enter calibration mode**
3. IMU stops sending fused Euler/accel data, starts streaming raw sensor data over CAN
4. Server forwards raw data to the browser over WebSocket
5. Browser (or server) runs the calibration math and renders a 3D sphere visualization
6. Pilot rotates the sensor board, watches the sphere fill in and quality metrics improve
7. When quality is good, pilot presses "Apply Calibration"
8. Calibration data is sent back: browser -> WebSocket -> server -> CAN -> IMU
9. IMU writes calibration to EEPROM, reloads it, reinitializes fusion filter
10. IMU automatically exits calibration mode and resumes normal operation

## IMU Firmware Changes

### Calibration Mode State Machine

The IMU needs a mode flag that switches between normal and calibration operation.

```
NORMAL_MODE <---> CALIBRATION_MODE
```

- **Normal mode** (default): reads sensors, applies calibration, runs fusion, sends Euler/accel over CAN
- **Calibration mode**: reads sensors, sends raw scaled values over CAN, listens for calibration data, does not run fusion

### New CAN Messages

#### Command Message (server -> IMU)

A new CAN message ID for calibration commands sent to the IMU.

| Byte | Field | Values |
|------|-------|--------|
| 0 | Command | 1 = Enter calibration mode, 2 = Exit calibration mode |
| 1-7 | Reserved | 0 |

The IMU would need to remove or expand its current CAN filter (currently transmit-only except for QNH on AIR module) to receive this command.

#### Raw Sensor Data (IMU -> server)

9 values need to be sent: accel XYZ, gyro XYZ, mag XYZ. At 8 bytes per CAN frame, options include:

**Option A: Two CAN frames per sample**
- Frame 1: accel X (int16), accel Y (int16), accel Z (int16), gyro X (int16) = 8 bytes
- Frame 2: gyro Y (int16), gyro Z (int16), mag X (int16), mag Y (int16) = 8 bytes
- Frame 3: mag Z (int16), padding = 8 bytes
- Total: 3 frames per sample

**Option B: Reuse existing CAN messages**
- The IMU already sends Euler (0x48) and Accel (0x49) messages
- In calibration mode, repurpose these IDs to carry raw data instead
- Add a new ID for magnetometer raw data
- Simpler — no new frame sequencing protocol needed

**Scaling (matching MotionCal format):**
- Accelerometer: `int16_t(accel_m_s2 * 8192.0 / 9.8)`
- Gyroscope: `int16_t(gyro_rad_s * SENSORS_RADS_TO_DPS * 16.0)`
- Magnetometer: `int16_t(mag_uT * 10.0)`

#### Calibration Data (server -> IMU)

The calibration packet is 68 bytes:
- Bytes 0-1: Header (0x75, 0x54)
- Bytes 2-25: Accel offsets (3 floats) + Gyro offsets (3 floats)
- Bytes 26-37: Mag hard-iron offsets (3 floats)
- Bytes 38-41: Field strength (1 float)
- Bytes 42-65: Soft-iron matrix (6 floats, compressed symmetric 3x3)
- Bytes 66-67: CRC16 (polynomial 0xA001, init 0xFFFF)

This needs to be split across multiple CAN frames. Options:

**Option A: Sequential frames with index**
- Use a dedicated CAN message ID for calibration data
- Byte 0 = frame index (0-8), Bytes 1-7 = payload
- 9 frames x 7 bytes = 63 payload bytes, plus a 10th frame for the remaining 5 bytes
- IMU reassembles into a 68-byte buffer, validates CRC, writes to EEPROM

**Option B: Bulk transfer**
- Send all 68 bytes as a burst of 9 frames (8 bytes each = 72 bytes, last frame padded)
- IMU buffers them in order, validates CRC after receiving all frames
- Simpler but needs ordering guarantee (CAN provides this on a single bus)

### EEPROM Write Path

The EEPROM write logic already exists in `Sensor_Calibration_EEPROM.cpp` (`saveCalibration()`). The `IMU_Calibration.ino` sketch has `receiveCalibration()` which parses the 68-byte serial packet. This logic would move into `IMU_AHRS.ino` but triggered by CAN instead of serial.

## Calibration Engine

### Where to run the math

**Option A: In the browser (JavaScript/WebAssembly)**
- Port `magcal.c`, `matrix.c`, `quality.c` to JavaScript or compile to WASM
- 3D visualization with WebGL/Three.js — potentially nicer than MotionCal's OpenGL
- All computation on the client, server just relays data
- More complex to implement but keeps server simple

**Option B: On the Pi server (Python)**
- Port the calibration algorithms to Python with NumPy
- Server computes calibration, sends quality metrics + sphere data to browser for display
- Browser only handles visualization and user interaction
- Simpler data path, Pi has plenty of CPU

**Option C: Compile MotionCal's C code to WASM**
- Use Emscripten to compile `magcal.c`, `matrix.c`, `quality.c` directly to WebAssembly
- Zero porting effort for the math — guaranteed identical behavior to MotionCal
- JavaScript wrapper for the API, WebGL for visualization
- Best of both worlds: proven math, browser UI

### Key Algorithms to Port

| File | Function | Purpose |
|------|----------|---------|
| `magcal.c` | `MagCal_Run()` | Orchestrates the 3 calibration solvers |
| `magcal.c` | `fUpdateCalibration4INV()` | 4-element sphere fit (>= 40 samples) |
| `magcal.c` | `fUpdateCalibration7EIG()` | 7-element with diagonal soft-iron (>= 100 samples) |
| `magcal.c` | `fUpdateCalibration10EIG()` | 10-element full soft-iron (>= 150 samples) |
| `matrix.c` | `eigencompute()` | Jacobi eigenvalue decomposition (up to 10x10) |
| `matrix.c` | `fmatrixAeqInvA()` | Gauss-Jordan matrix inversion |
| `quality.c` | `quality_surface_gap_error()` | Sphere coverage quality |
| `quality.c` | `quality_magnitude_variance_error()` | Magnitude consistency |
| `quality.c` | `quality_wobble_error()` | Sphere shape quality |
| `rawdata.c` | `apply_calibration()` | Apply current cal for visualization |
| `rawdata.c` | `choose_discard_magcal()` | Smart buffer management |
| `rawdata.c` | `send_calibration()` | Build the 68-byte packet |

## EFIS Display

### PixiJS Full-Screen Takeover

The calibration UI uses PixiJS — the same renderer already used for the EFIS display. No new graphics dependencies needed. Since calibration only happens on the ground, the normal EFIS display is simply swapped out entirely during calibration and restored when done.

**Approach:**
- When calibration mode is triggered, the EFIS display removes its normal stage children (attitude indicator, HSI, ribbons, etc.) and replaces them with the calibration screen — same canvas, same PixiJS application
- When calibration completes or is cancelled, the normal EFIS display children are restored
- This avoids managing two separate renderers or canvases

### Sphere Visualization in 2D

PixiJS is a 2D renderer, but true 3D is not needed. MotionCal's sphere is essentially a rotated scatter plot. Options for rendering in PixiJS:

- **Orthographic projection**: project calibrated mag points from 3D onto a 2D circle using the current orientation quaternion (same math as MotionCal's `visualize.c` but outputting 2D screen coordinates instead of OpenGL calls)
- **Depth cues**: front-facing points rendered larger and brighter, rear points smaller and dimmer — gives a convincing 3D appearance without a 3D engine
- **Color coding**: red dots while calibrating, green dots when quality thresholds are met (matching MotionCal behavior)
- The sphere outline itself is just a PixiJS circle graphic for reference

### Calibration Screen Layout

```
┌─────────────────────────────────────────────┐
│              IMU Calibration                 │
├─────────────────────────────────────────────┤
│                                             │
│          ┌───────────────────┐              │
│          │                   │              │
│          │   Magnetometer    │              │
│          │   Sphere View     │              │
│          │   (2D projected)  │              │
│          │                   │              │
│          └───────────────────┘              │
│                                             │
│  Gaps: 45.2%   Variance: 8.1%              │
│  Wobble: 6.3%  Fit Error: 4.2%             │
│                                             │
│  Hard Iron:  12.34  -5.67  8.90            │
│  Field:      48.2 uT                       │
│  Samples:    287 / 650                      │
│                                             │
│  ┌──────────────┐  ┌──────────────┐        │
│  │    Apply      │  │    Cancel    │        │
│  └──────────────┘  └──────────────┘        │
└─────────────────────────────────────────────┘
```

- Quality metrics update in real time as points are collected
- "Apply" button is disabled (greyed out) until all 4 quality thresholds are met (gaps < 15, variance < 4.5, wobble < 4, fit error < 5)
- "Cancel" exits calibration mode without saving, restores normal EFIS display
- Sample counter shows buffer fill progress

### Integration

- Triggered from the EFIS menu system (soft button or menu overlay option)
- Ground-only operation — calibration should not be started in flight
- Server enters calibration relay mode (sends CAN enter-calibration command, forwards raw CAN data to browser over WebSocket, stops normal nav data)
- Browser enters calibration UI mode (swaps out normal EFIS stage, shows calibration screen)
- On "Apply": browser sends computed calibration data via WebSocket, server relays over CAN to IMU, IMU writes to EEPROM and exits calibration mode
- On "Cancel": server sends CAN exit-calibration command, both server and browser return to normal mode

## Decisions Made

- Calibration is ground-only — no need for split screen or in-flight access
- PixiJS for the entire calibration UI — no WebGL/Three.js dependency needed
- Full-screen takeover of the EFIS display during calibration

## Open Questions

- Where to run the calibration math: browser (JavaScript/WASM) or server (Python)?
- Should the fusion filter (Mahony) also run during calibration for orientation visualization, or is a static sphere view sufficient?
- What CAN message IDs to assign for calibration commands and data?
- Do we need to calibrate accel and gyro biases too, or just magnetometer?
- Should we keep the standalone MotionCal + IMU_Calibration.ino path as a fallback?
