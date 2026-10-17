# PiAvionics CAN messages

> **Generated file. Do not edit.** Edit `Protocol/can_messages.yaml` and run `Protocol/.venv/bin/python Protocol/generate_can_docs.py`.

Version 0.1.1 · reviewed 2026-10-17

Sensor modules broadcast their data on fixed IDs. The Pi EFIS listens, and sends QNH to the AIR module. There are no addressed commands yet (see ISSUES.md issue 9).

## Bus at a glance

| | |
|---|---|
| Bit rate | 250 kbit/s |
| Identifiers | Standard 11-bit |
| Byte order | Little-endian |
| Data length | 8 bytes for every message unless stated (mag messages use 4) |
| Termination | 120 ohm at each end of the bus |
| Hardware | MCP2515 controller, 250 kbit/s (Pi uses can0 via SocketCAN) |

- Lower IDs win bus arbitration. All PiAvionics IDs (0x19 to 0x83) are lower than every MGL device ID (0x200 and up).
- Modules send each message on a timer (period_ms). The firmware adds up to 50 ms of random jitter; it has little effect in practice and is not needed for collision handling, because CAN arbitration never drops a frame.
- The EFIS treats AHRS data (0x48, 0x49) as stale after 0.2 s. No other message has a timeout yet (see ISSUES.md). Module heartbeats (0x701 to 0x703, planned) add a module-level timeout of 3 s.

## Status key

- **✅ Active**: Sent by the module and used by the receiver.
- **🟡 Partial**: Exists, but something is missing or always zero. See the notes.
- **⚪ Planned**: Designed but not implemented in firmware or the EFIS yet.
- **🔘 Reserved**: ID is claimed or named in code, but nothing is sent on it.

## Message index

| ID | Name | Sent by | Received by | Rate | Bytes | Status |
|:--:|------|---------|-------------|------|:-----:|--------|
| `0x019` | [TIME_SYNC](#0x019-time_sync) | GPS module | Pi EFIS | 1 s | 8 | ✅ Active |
| `0x023` | [AHRS_CONTROL](#0x023-ahrs_control) | Pi EFIS | IMU / AHRS module | — | 8 | ⚪ Planned |
| `0x028` | [AIR_DATA](#0x028-air_data) | AIR module | Pi EFIS, CAN display feather | 100 ms | 8 | ✅ Active |
| `0x029` | [AOA](#0x029-aoa) | AIR module | — | 200 ms | 8 | 🟡 Partial |
| `0x02A` | [OAT](#0x02a-oat) | AIR module | Pi EFIS | 200 ms | 8 | ✅ Active |
| `0x02B` | [STATIC_PRESSURE](#0x02b-static_pressure) | AIR module | Pi EFIS | 200 ms | 8 | ✅ Active |
| `0x02E` | [QNH](#0x02e-qnh) | Pi EFIS | AIR module | 1 s | 8 | ✅ Active |
| `0x032` | [EMS_RPM](#0x032-ems_rpm) | EMS module | — | — | 8 | ⚪ Planned |
| `0x048` | [AHRS_ORIENT](#0x048-ahrs_orient) | IMU / AHRS module | Pi EFIS, CAN display feather | 100 ms | 8 | ✅ Active |
| `0x049` | [AHRS_ACCEL](#0x049-ahrs_accel) | IMU / AHRS module | Pi EFIS | 100 ms | 8 | ✅ Active |
| `0x051` | [EMS_OIL](#0x051-ems_oil) | EMS module | — | — | 8 | ⚪ Planned |
| `0x052` | [EMS_EGT_CHT_12](#0x052-ems_egt_cht_12) | EMS module | — | — | 8 | ⚪ Planned |
| `0x053` | [EMS_EGT_CHT_34](#0x053-ems_egt_cht_34) | EMS module | — | — | 8 | ⚪ Planned |
| `0x063` | [GPS_POSITION](#0x063-gps_position) | GPS module | Pi EFIS | 1 s | 8 | ✅ Active |
| `0x064` | [GPS_SPEED_TRACK](#0x064-gps_speed_track) | GPS module | Pi EFIS | 1 s | 8 | ✅ Active |
| `0x065` | [GPS_STATUS](#0x065-gps_status) | GPS module | Pi EFIS | 1 s | 8 | ✅ Active |
| `0x081` | [MAG_X](#0x081-mag_x) | IMU / AHRS module | Pi EFIS | — | 4 | 🔘 Reserved |
| `0x082` | [MAG_Y](#0x082-mag_y) | IMU / AHRS module | Pi EFIS | — | 4 | 🔘 Reserved |
| `0x083` | [MAG_Z](#0x083-mag_z) | IMU / AHRS module | Pi EFIS | — | 4 | 🔘 Reserved |
| `0x701` | [IMU_HEARTBEAT](#0x701-imu_heartbeat) | IMU / AHRS module | Pi EFIS | 1 s | 8 | ⚪ Planned |
| `0x702` | [AIR_HEARTBEAT](#0x702-air_heartbeat) | AIR module | Pi EFIS | 1 s | 8 | ⚪ Planned |
| `0x703` | [GPS_HEARTBEAT](#0x703-gps_heartbeat) | GPS module | Pi EFIS | 1 s | 8 | ⚪ Planned |

## Messages

### 0x019 TIME_SYNC

**UTC date and time**

| | |
|---|---|
| Sent by | GPS module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `year` | uint8 | years since 2000 | 26 means 2026 |
| 1 | `month` | uint8 | month | 1 to 12 |
| 2 | `day` | uint8 | day | 1 to 31 |
| 3 | `hour` | uint8 | h | UTC |
| 4 | `minute` | uint8 | min |  |
| 5 | `second` | uint8 | s |  |
| 6-7 | `reserved` | uint8 |  | Always 0 |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `year` | `month` | `day` | `hour` | `minute` | `second` | `reserved` (1/2) | `reserved` (2/2) |

**Notes**

- Sent only once the GPS has received a valid date.
- The EFIS decodes the bytes as signed (fine for the values here).
- ID 0x19 collides with MGL's "General Command" message from host 1 (see CAN_BUS_MAP.md). It works today only because byte 0 (the year) is not a valid MGL destination (0 to 3).

**Code**

- Sender: GPS_Module.ino (CAN_TIME_SYNC_MSG_ID)
- Receiver: aio_server.py process_time_sync_message

### 0x023 AHRS_CONTROL

**AHRS control command**

| | |
|---|---|
| Sent by | Pi EFIS |
| Received by | IMU / AHRS module |
| Rate | — |
| Data length | 8 bytes |
| Status | ⚪ Planned |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `calibration` | uint8 |  | 1 = save calibration |
| 1-7 | `reserved` | uint8 |  |  |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `calibration` | `reserved` (1/7) | `reserved` (2/7) | `reserved` (3/7) | `reserved` (4/7) | `reserved` (5/7) | `reserved` (6/7) | `reserved` (7/7) |

**Notes**

- IMU_AHRS.ino defines CAN_CALIB_MSG_ID as 0x23, but nothing sends or receives it. See Sensors/IMU_AHRS/IMU_Calibration_Design.md.

### 0x028 AIR_DATA

**Airspeed, altitude and vertical speed**

| | |
|---|---|
| Sent by | AIR module |
| Received by | Pi EFIS, CAN display feather |
| Rate | 100 ms |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `airspeed` | int16 | kt | Indicated airspeed from the pitot sensor |
| 2-5 | `altitude` | int32 | ft | Pressure altitude using the QNH from 0x2E |
| 6-7 | `vertical_speed` | int16 | ft/min | Climb rate from the static pressure, positive = climbing (3 s window, about 3 s lag, about +-40 ft/min at rest) |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `airspeed` (1/2) | `airspeed` (2/2) | `altitude` (1/4) | `altitude` (2/4) | `altitude` (3/4) | `altitude` (4/4) | `vertical_speed` (1/2) | `vertical_speed` (2/2) |

**Notes**

- Altitude is a full 4-byte integer with no padding byte. The older Can Docs.md showed a 3-byte altitude and is wrong.
- Vertical speed is computed by AIR_Module v1.0.21 and later from the low-passed static pressure (standard atmosphere, not QNH): the altitude change over the last 3 s, smoothed. It lags a real change in climb rate by about 3 s.

**Code**

- Sender: AIR_Module.ino (CAN_AIR_MSG_ID)
- Receiver: aio_server.py process_altitude_message, CAN_Feather.ino

### 0x029 AOA

**Angle of attack**

| | |
|---|---|
| Sent by | AIR module |
| Received by | — |
| Rate | 200 ms |
| Data length | 8 bytes |
| Status | 🟡 Partial |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `aoa` | int16 | deg | Scaling not decided. Not calculated yet; always 0 |
| 2-7 | `unused` | uint8 |  | Not defined. Currently carries stale bytes from the previous message |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `aoa` (1/2) | `aoa` (2/2) | `unused` (1/6) | `unused` (2/6) | `unused` (3/6) | `unused` (4/6) | `unused` (5/6) | `unused` (6/6) |

**Notes**

- The AIR module sends this message, but the AOA value is never calculated.
- The EFIS has no handler for 0x29.
- Stale bytes 2-7 come from a shared buffer in AIR_Module.ino.

**Code**

- Sender: AIR_Module.ino (CAN_AOA_MSG_ID)

### 0x02A OAT

**Outside air temperature**

| | |
|---|---|
| Sent by | AIR module |
| Received by | Pi EFIS |
| Rate | 200 ms |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `oat` | int16 | degC (raw × 0.1) | MAX31865 RTD temperature |
| 2 | `humidity` | uint8 | % | Not available; always 0 |
| 3-7 | `unused` | uint8 |  | Always 0 |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `oat` (1/2) | `oat` (2/2) | `humidity` | `unused` (1/5) | `unused` (2/5) | `unused` (3/5) | `unused` (4/5) | `unused` (5/5) |

**Notes**

- Sent only when the RTD (MAX31865) is present and initialised.

**Code**

- Sender: AIR_Module.ino (CAN_TEMP_MSG_ID)
- Receiver: aio_server.py process_oat_message

### 0x02B STATIC_PRESSURE

**Raw static and pitot pressure**

| | |
|---|---|
| Sent by | AIR module |
| Received by | Pi EFIS |
| Rate | 200 ms |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `static_pressure` | int16 | hPa (raw × 0.1) | Filtered static pressure |
| 2 | `sensor_temperature` | int8 | degC | Whole degrees |
| 3-4 | `differential_pressure` | int16 | Pa | Filtered pitot (differential) pressure |
| 5-7 | `padding` | uint8 |  | Always 0 |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `static_pressure` (1/2) | `static_pressure` (2/2) | `sensor_temperature` | `differential_pressure` (1/2) | `differential_pressure` (2/2) | `padding` (1/3) | `padding` (2/3) | `padding` (3/3) |

**Notes**

- Matches the legacy CircuitPython format (struct "<hbhBBB"). The differential pressure (bytes 3-4) was missing from the old Can Docs.md.

**Code**

- Sender: AIR_Module.ino (CAN_RAW_MSG_ID)
- Receiver: aio_server.py process_static_pressure_message

### 0x02E QNH

**Altimeter setting**

| | |
|---|---|
| Sent by | Pi EFIS |
| Received by | AIR module |
| Rate | 1 s (Sent when the setting changes, and at least once a second) |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `qnh_hpa` | int16 | hPa | Informational. The AIR module does not use it |
| 2-3 | `qnh_inhg` | int16 | inHg (raw × 0.0025) | inHg x 400 (2992 x 4 = 11968 is 29.92 inHg) |
| 4-7 | `reserved` | uint8 |  | Always 0 |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `qnh_hpa` (1/2) | `qnh_hpa` (2/2) | `qnh_inhg` (1/2) | `qnh_inhg` (2/2) | `reserved` (1/4) | `reserved` (2/4) | `reserved` (3/4) | `reserved` (4/4) |

**Notes**

- The inHg field is x400, not x100 as the old Can Docs.md said. The AIR module divides it by 4 to get x100, so the extra resolution is not used.
- EFIS accepts QNH between 28.00 and 31.00 inHg (the range is enforced in aio_server.py).

**Code**

- Sender: aio_server.py pack_can_qnh_msg
- Receiver: AIR_Module.ino (CAN_QNH_MSG_ID); the AIR module accepts only this ID

### 0x032 EMS_RPM

**Engine RPM**

| | |
|---|---|
| Sent by | EMS module |
| Received by | — |
| Rate | — |
| Data length | 8 bytes |
| Status | ⚪ Planned |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `rpm` | uint16 | rpm |  |
| 2-7 | `unused` | uint8 |  |  |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `rpm` (1/2) | `rpm` (2/2) | `unused` (1/6) | `unused` (2/6) | `unused` (3/6) | `unused` (4/6) | `unused` (5/6) | `unused` (6/6) |

**Notes**

- From the old Can Docs.md. No C++ firmware or EFIS handler exists yet.

### 0x048 AHRS_ORIENT

**Heading, pitch, roll and turn rate**

| | |
|---|---|
| Sent by | IMU / AHRS module |
| Received by | Pi EFIS, CAN display feather |
| Rate | 100 ms |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `heading` | int16 | deg (raw × 0.1) | Fused yaw, clockwise positive (NED), 0 to 360 |
| 2-3 | `pitch` | int16 | deg (raw × 0.1) | Aircraft (NED) convention |
| 4-5 | `roll` | int16 | deg (raw × 0.1) | Aircraft (NED) convention |
| 6-7 | `turn_rate` | int16 | deg/s (raw × 0.1) | Derived from yaw; 0.5 deg/s deadband |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `heading` (1/2) | `heading` (2/2) | `pitch` (1/2) | `pitch` (2/2) | `roll` (1/2) | `roll` (2/2) | `turn_rate` (1/2) | `turn_rate` (2/2) |

**Notes**

- The IMU converts its native frame (NWU) to aircraft NED before sending. The EFIS only divides by 10.
- The EFIS treats this message as stale after 0.2 s and blanks the values.

**Code**

- Sender: IMU_AHRS.ino (CAN_EULER_MSG_ID)
- Receiver: aio_server.py process_ahrs_orient_message, CAN_Feather.ino

### 0x049 AHRS_ACCEL

**Acceleration**

| | |
|---|---|
| Sent by | IMU / AHRS module |
| Received by | Pi EFIS |
| Rate | 100 ms |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `accel_x` | int16 | m/s2 (raw × 0.01) | Sensor X (forward) |
| 2-3 | `accel_y` | int16 | m/s2 (raw × 0.01) | Sensor Y (left) |
| 4-5 | `accel_z` | int16 | m/s2 (raw × 0.01) | Sensor Z (up) |
| 6-7 | `calibration` | int16 |  | Calibration status; always 0 today |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `accel_x` (1/2) | `accel_x` (2/2) | `accel_y` (1/2) | `accel_y` (2/2) | `accel_z` (1/2) | `accel_z` (2/2) | `calibration` (1/2) | `calibration` (2/2) |

**Notes**

- Calibrated values in the sensor's native frame, not converted to aircraft axes.
- The EFIS treats this message as stale after 0.2 s.

**Code**

- Sender: IMU_AHRS.ino (CAN_ACC_MSG_ID)
- Receiver: aio_server.py process_ahrs_accel_message

### 0x051 EMS_OIL

**Oil pressure and temperature**

| | |
|---|---|
| Sent by | EMS module |
| Received by | — |
| Rate | — |
| Data length | 8 bytes |
| Status | ⚪ Planned |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `oil_pressure_kpa` | int16 | kPa (raw × 0.1) |  |
| 2-3 | `oil_temp_c` | int16 | degC (raw × 0.1) |  |
| 4-5 | `oil_pressure_psi` | int16 | psi (raw × 0.1) |  |
| 6-7 | `oil_temp_f` | int16 | degF (raw × 0.1) |  |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `oil_pressure_kpa` (1/2) | `oil_pressure_kpa` (2/2) | `oil_temp_c` (1/2) | `oil_temp_c` (2/2) | `oil_pressure_psi` (1/2) | `oil_pressure_psi` (2/2) | `oil_temp_f` (1/2) | `oil_temp_f` (2/2) |

**Notes**

- From the old Can Docs.md. Nothing sends or decodes it yet.

### 0x052 EMS_EGT_CHT_12

**EGT and CHT, cylinders 1 and 2**

| | |
|---|---|
| Sent by | EMS module |
| Received by | — |
| Rate | — |
| Data length | 8 bytes |
| Status | ⚪ Planned |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `egt_1` | int16 | degF |  |
| 2-3 | `egt_2` | int16 | degF |  |
| 4-5 | `cht_1` | int16 | degF |  |
| 6-7 | `cht_2` | int16 | degF |  |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `egt_1` (1/2) | `egt_1` (2/2) | `egt_2` (1/2) | `egt_2` (2/2) | `cht_1` (1/2) | `cht_1` (2/2) | `cht_2` (1/2) | `cht_2` (2/2) |

**Notes**

- From the old Can Docs.md, where EGT 2 was labelled "byte 0 / byte 2" (a typo; bytes 2-3 here).

### 0x053 EMS_EGT_CHT_34

**EGT and CHT, cylinders 3 and 4**

| | |
|---|---|
| Sent by | EMS module |
| Received by | — |
| Rate | — |
| Data length | 8 bytes |
| Status | ⚪ Planned |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `egt_3` | int16 | degF |  |
| 2-3 | `egt_4` | int16 | degF |  |
| 4-5 | `cht_3` | int16 | degF |  |
| 6-7 | `cht_4` | int16 | degF |  |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `egt_3` (1/2) | `egt_3` (2/2) | `egt_4` (1/2) | `egt_4` (2/2) | `cht_3` (1/2) | `cht_3` (2/2) | `cht_4` (1/2) | `cht_4` (2/2) |

**Notes**

- From the old Can Docs.md.

### 0x063 GPS_POSITION

**GPS position**

| | |
|---|---|
| Sent by | GPS module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-3 | `latitude` | int32 | deg (raw × 1e-06) | North positive |
| 4-7 | `longitude` | int32 | deg (raw × 1e-06) | East positive |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `latitude` (1/4) | `latitude` (2/4) | `latitude` (3/4) | `latitude` (4/4) | `longitude` (1/4) | `longitude` (2/4) | `longitude` (3/4) | `longitude` (4/4) |

**Notes**

- Sent only while the GPS has a fix. The EFIS keeps the last value when the fix is lost.

**Code**

- Sender: GPS_Module.ino (CAN_GPS1_MSG_ID)
- Receiver: aio_server.py process_gps1_message

### 0x064 GPS_SPEED_TRACK

**GPS ground speed, altitude and track**

| | |
|---|---|
| Sent by | GPS module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `ground_speed` | int16 | kt | Whole knots |
| 2-3 | `altitude` | int16 | ft | GPS altitude |
| 4-5 | `true_track` | int16 | deg | Whole degrees; held at 0 below 2 kt |
| 6-7 | `mag_track` | int16 | deg | True track corrected with WMM2025 declination. The EFIS ignores it |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `ground_speed` (1/2) | `ground_speed` (2/2) | `altitude` (1/2) | `altitude` (2/2) | `true_track` (1/2) | `true_track` (2/2) | `mag_track` (1/2) | `mag_track` (2/2) |

**Notes**

- Sent only while the GPS has a fix.

**Code**

- Sender: GPS_Module.ino (CAN_GPS2_MSG_ID)
- Receiver: aio_server.py process_gps2_message

### 0x065 GPS_STATUS

**GPS fix status**

| | |
|---|---|
| Sent by | GPS module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `fix_quality` | uint8 |  | 0 = no fix, 1 = GPS, 2 = DGPS (WAAS) |
| 1 | `fix_type` | uint8 |  | 1 = no fix, 2 = 2D, 3 = 3D |
| 2 | `satellites` | uint8 | count | Satellites used in the fix |
| 3 | `reserved` | uint8 |  | Always 0 (free for a probe/diagnostic code) |
| 4-5 | `hdop` | int16 | raw × 0.01 | Horizontal dilution of precision |
| 6-7 | `vdop` | int16 | raw × 0.01 | Vertical dilution of precision |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `fix_quality` | `fix_type` | `satellites` | `reserved` | `hdop` (1/2) | `hdop` (2/2) | `vdop` (1/2) | `vdop` (2/2) |

**Notes**

- Sent every second whether or not there is a fix, so the display can show "No Fix".
- Also a good "GPS module is alive" indicator.

**Code**

- Sender: GPS_Module.ino (CAN_GPS3_MSG_ID)
- Receiver: aio_server.py process_gps3_message

### 0x081 MAG_X

**Magnetometer X**

| | |
|---|---|
| Sent by | IMU / AHRS module |
| Received by | Pi EFIS |
| Rate | — |
| Data length | 4 bytes |
| Status | 🔘 Reserved |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-3 | `mag_x` | float32 |  | Units not confirmed (probably uT) |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 |
|------|:---:|:---:|:---:|:---:|
| Field | `mag_x` (1/4) | `mag_x` (2/4) | `mag_x` (3/4) | `mag_x` (4/4) |

**Notes**

- Defined in IMU_AHRS.ino and decoded by the EFIS (for a calibration display), but IMU_AHRS.ino never sends it.

### 0x082 MAG_Y

**Magnetometer Y**

| | |
|---|---|
| Sent by | IMU / AHRS module |
| Received by | Pi EFIS |
| Rate | — |
| Data length | 4 bytes |
| Status | 🔘 Reserved |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-3 | `mag_y` | float32 |  | Units not confirmed (probably uT) |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 |
|------|:---:|:---:|:---:|:---:|
| Field | `mag_y` (1/4) | `mag_y` (2/4) | `mag_y` (3/4) | `mag_y` (4/4) |

**Notes**

- See 0x81.

### 0x083 MAG_Z

**Magnetometer Z**

| | |
|---|---|
| Sent by | IMU / AHRS module |
| Received by | Pi EFIS |
| Rate | — |
| Data length | 4 bytes |
| Status | 🔘 Reserved |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-3 | `mag_z` | float32 |  | Units not confirmed (probably uT) |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 |
|------|:---:|:---:|:---:|:---:|
| Field | `mag_z` (1/4) | `mag_z` (2/4) | `mag_z` (3/4) | `mag_z` (4/4) |

**Notes**

- See 0x81.

### 0x701 IMU_HEARTBEAT

**IMU / AHRS module heartbeat, version and health**

| | |
|---|---|
| Sent by | IMU / AHRS module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | ⚪ Planned |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `module` | uint8 |  | Module number (1 IMU, 2 AIR, 3 GPS). Always equals the ID minus 0x700 |
| 1 | `version_major` | uint8 |  | Firmware version |
| 2 | `version_minor` | uint8 |  | Firmware version |
| 3 | `version_patch` | uint8 |  | Firmware version |
| 4 | `state` | uint8 |  | 0 = starting, 1 = OK, 2 = degraded (a sensor is missing or invalid), 3 = failed (halted, not producing data) |
| 5 | `sensors` | uint8 |  | Bit mask of sensors that are present and giving valid data (1 = good). Bit meanings are per module, see the notes |
| 6-7 | `uptime` | uint16 | s | Seconds since the module started; stops at 65535 (18 h). A drop means the module restarted |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `module` | `version_major` | `version_minor` | `version_patch` | `state` | `sensors` | `uptime` (1/2) | `uptime` (2/2) |

**Notes**

- Heartbeat, same layout for every module. It is sent once as soon as the CAN bus is up (state 0 while starting, or 3 if start-up failed), then every second. A module that halts at start-up because a sensor is missing keeps sending state 3 with the sensors mask showing which one failed, so the EFIS can say what is wrong.
- The EFIS records the time each heartbeat arrived, the version, the state and the sensors mask. A module is shown as missing when no heartbeat has arrived for 3 s (three missed). Which modules the EFIS expects is EFIS configuration.
- Sensors mask for the IMU: bit 0 ISM330DHCX (gyro and accelerometer), bit 1 LIS3MDL (magnetometer), bit 2 calibration loaded. Bit 2 clear gives state 2 (degraded), since the AHRS runs but is uncalibrated.
- The version bytes are the VERSION string of the sketch ("1.0.3" is 1, 0, 3). The same text is in the USB product string and the '?' report (ModuleInfo library).

**Code**

- Sender: not written yet (IMU_AHRS.ino)
- Receiver: not written yet (aio_server.py)

### 0x702 AIR_HEARTBEAT

**AIR module heartbeat, version and health**

| | |
|---|---|
| Sent by | AIR module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | ⚪ Planned |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `module` | uint8 |  | Module number (1 IMU, 2 AIR, 3 GPS). Always equals the ID minus 0x700 |
| 1 | `version_major` | uint8 |  | Firmware version |
| 2 | `version_minor` | uint8 |  | Firmware version |
| 3 | `version_patch` | uint8 |  | Firmware version |
| 4 | `state` | uint8 |  | 0 = starting, 1 = OK, 2 = degraded (a sensor is missing or invalid), 3 = failed (halted, not producing data) |
| 5 | `sensors` | uint8 |  | Bit mask of sensors that are present and giving valid data (1 = good). Bit meanings are per module, see the notes |
| 6-7 | `uptime` | uint16 | s | Seconds since the module started; stops at 65535 (18 h). A drop means the module restarted |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `module` | `version_major` | `version_minor` | `version_patch` | `state` | `sensors` | `uptime` (1/2) | `uptime` (2/2) |

**Notes**

- See 0x701 for the layout and the rules.
- Sensors mask for the AIR module: bit 0 pitot MS4525DO, bit 1 AOA MS4525DO, bit 2 static pressure SSC, bit 3 outside air temperature (MAX31865 RTD). A sensor that stops answering while running clears its bit and the module goes to state 2; the data that depends on it (for example airspeed) is not trusted.

**Code**

- Sender: not written yet (AIR_Module.ino)
- Receiver: not written yet (aio_server.py)

### 0x703 GPS_HEARTBEAT

**GPS module heartbeat, version and health**

| | |
|---|---|
| Sent by | GPS module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | ⚪ Planned |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `module` | uint8 |  | Module number (1 IMU, 2 AIR, 3 GPS). Always equals the ID minus 0x700 |
| 1 | `version_major` | uint8 |  | Firmware version |
| 2 | `version_minor` | uint8 |  | Firmware version |
| 3 | `version_patch` | uint8 |  | Firmware version |
| 4 | `state` | uint8 |  | 0 = starting, 1 = OK, 2 = degraded (a sensor is missing or invalid), 3 = failed (halted, not producing data) |
| 5 | `sensors` | uint8 |  | Bit mask of sensors that are present and giving valid data (1 = good). Bit meanings are per module, see the notes |
| 6-7 | `uptime` | uint16 | s | Seconds since the module started; stops at 65535 (18 h). A drop means the module restarted |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `module` | `version_major` | `version_minor` | `version_patch` | `state` | `sensors` | `uptime` (1/2) | `uptime` (2/2) |

**Notes**

- See 0x701 for the layout and the rules.
- Sensors mask for the GPS module: bit 0 NMEA data arriving from the GPS chip, bit 1 has a position fix. No fix is not a fault, so a missing fix does not change state; it stays 1 (OK) while NMEA is arriving. 0x65 carries the fix details.

**Code**

- Sender: not written yet (GPS_Module.ino)
- Receiver: not written yet (aio_server.py)

## Nodes

| Node | Role | Code |
|------|------|------|
| AIR module | Airspeed, altitude, AOA, static and outside air temperature | `Sensors/AIR_Module/AIR_Module.ino` |
| IMU / AHRS module | Attitude, heading, acceleration | `Sensors/IMU_AHRS/IMU_AHRS.ino` |
| GPS module | Position, speed, track, time | `Sensors/GPS_Module/GPS_Module.ino` |
| Pi EFIS | Display computer (Raspberry Pi). Listens to everything, sends QNH. | `EFIS/aio_server.py` |
| CAN display feather | Listens for airspeed/altitude and attitude for the small LCD display | `CAN_Display/CAN_Feather/CAN_Feather.ino` |
| EMS module | Engine monitor (planned). Only a legacy CircuitPython version exists. | — |

## IDs named in code but unused

| ID | Name | Note |
|:--:|------|------|
| `0x038` | ENCODER | Named in the EFIS code (CAN_MSG_ID.ENCODER) but unused. The rotary encoder is read over I2C, not CAN. |
