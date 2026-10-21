# PiAvionics CAN messages

> **Generated file. Do not edit.** Edit `Protocol/can_messages.yaml` and run `Protocol/.venv/bin/python Protocol/generate_can_docs.py`.

Version 0.3.0 · reviewed 2026-10-20

Sensor modules broadcast their data on fixed IDs. The Pi EFIS listens, shares the QNH with the AIR module and the other displays, and sends its own heartbeat (0x700). There are no addressed commands yet (see ISSUES.md issue 9).

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
- Modules send each message on a timer (period_ms). Each interval varies by a flat +/- 20 ms about the period, drawn once per send (SendTimer library; AIR_Module 1.0.37, IMU_AHRS 1.0.17, GPS_Module 1.0.10, measured on the bus 2026-10-20: means within 1 ms of the period, minimum period - 20 ms; about 1.5% of frames are later than period + 25 ms because the module's loop was busy). Before that the random number was drawn again on every pass of loop(), so the jitter was only 1 to 3 ms and the mean 2 to 3 ms over the period. The jitter is not needed for collision handling, because CAN arbitration never drops a frame.
- Invalid-data markers (protocol 0.2.0). A field that cannot be trusted is sent as the largest positive value of its type: 0x7FFF for a 16-bit field, 0x7FFFFFFF for a 32-bit field. No real value can reach it (see the Invalid marker column). A receiver must show such a field as failed and never as a number. The EFIS server understands the markers (1.6.0 onward); the module firmware sends them from AIR_Module 1.0.35 and IMU_AHRS 1.0.16 (2026-10-18); the GPS module sends none (its frames stop instead). A module's heartbeat (0x701 to 0x703) also says which sensor is lost.
- Timeouts (decided 2026-10-17, not yet implemented in the EFIS). The EFIS treats data as lost after: 0.5 s without 0x28, 0x48 or 0x49; 1.5 s without 0x2A or 0x2B; 3.5 s without 0x63, 0x64 or 0x65; 3 s without a module heartbeat (the module is shown as missing). Today the EFIS times out only 0x48 and 0x49, after 0.2 s.
- Three things make a value invalid on the EFIS: the marker, a heartbeat that says the sensor is lost, or the message not arriving within its timeout. All three look the same on the display.

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
| `0x02C` | [QNH_SPARE_2C](#0x02c-qnh_spare_2c) | Future panel | AIR module, Pi EFIS, Backup display | — | 8 | 🔘 Reserved |
| `0x02D` | [QNH_BACKUP](#0x02d-qnh_backup) | Backup display | AIR module, Pi EFIS | 1 s | 8 | ⚪ Planned |
| `0x02E` | [QNH](#0x02e-qnh) | Pi EFIS | AIR module, Backup display | 1 s | 8 | ✅ Active |
| `0x02F` | [QNH_SPARE_2F](#0x02f-qnh_spare_2f) | Future panel | AIR module, Pi EFIS, Backup display | — | 8 | 🔘 Reserved |
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
| `0x700` | [EFIS_HEARTBEAT](#0x700-efis_heartbeat) | Pi EFIS | CAN display feather | 1 s | 8 | ✅ Active |
| `0x701` | [IMU_HEARTBEAT](#0x701-imu_heartbeat) | IMU / AHRS module | Pi EFIS | 1 s | 8 | 🟡 Partial |
| `0x702` | [AIR_HEARTBEAT](#0x702-air_heartbeat) | AIR module | Pi EFIS | 1 s | 8 | 🟡 Partial |
| `0x703` | [GPS_HEARTBEAT](#0x703-gps_heartbeat) | GPS module | Pi EFIS | 1 s | 8 | 🟡 Partial |

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
| Treated as lost after | 0.5 s without a message |

| Bytes | Field | Type | Unit | Description | Invalid marker |
|:-----:|-------|------|------|-------------|-------------|
| 0-1 | `airspeed` | int16 | kt | Indicated airspeed from the pitot sensor | `0x7FFF` |
| 2-5 | `altitude` | int32 | ft | Altitude using the QNH block 0x2C to 0x2F. Invalid while the AIR module is in its 2 s start-up listen window | `0x7FFFFFFF` |
| 6-7 | `vertical_speed` | int16 | ft/min | Climb rate from the static pressure, positive = climbing (3 s window, about 3 s lag, about +-40 ft/min at rest) | `0x7FFF` |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `airspeed` (1/2) | `airspeed` (2/2) | `altitude` (1/4) | `altitude` (2/4) | `altitude` (3/4) | `altitude` (4/4) | `vertical_speed` (1/2) | `vertical_speed` (2/2) |

**Notes**

- Invalid markers (AIR_Module 1.0.35 onward, 2026-10-18): airspeed is 0x7FFF when the pitot sensor is lost; altitude (0x7FFFFFFF) and vertical speed (0x7FFF) when the static pressure sensor is lost. Before 1.0.35 a pitot loss sent airspeed 0, and a static loss held the last altitude (0 if the sensor was never found) and sent vertical speed 0. The AIR heartbeat (0x702) mask bits 0 and 2 say which sensor is lost.
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
| Treated as lost after | 1.5 s without a message |

| Bytes | Field | Type | Unit | Description | Invalid marker |
|:-----:|-------|------|------|-------------|-------------|
| 0-1 | `oat` | int16 | degC (raw × 0.1) | MAX31865 RTD temperature | `0x7FFF` |
| 2 | `humidity` | uint8 | % | Not available; always 0 | — |
| 3-7 | `unused` | uint8 |  | Always 0 | — |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `oat` (1/2) | `oat` (2/2) | `humidity` | `unused` (1/5) | `unused` (2/5) | `unused` (3/5) | `unused` (4/5) | `unused` (5/5) |

**Notes**

- When the RTD (MAX31865) is lost (open or shorted probe, MAX31865 board missing, or a temperature outside -60 to 80 C) the AIR module keeps sending this message with the invalid marker 0x7FFF in the temperature (AIR_Module 1.0.35 onward, decided 2026-10-17), so the EFIS can tell a failed sensor from a silent module. Before 1.0.35 it stopped sending. Heartbeat 0x702 bit 3 says the same.

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
| Treated as lost after | 1.5 s without a message |

| Bytes | Field | Type | Unit | Description | Invalid marker |
|:-----:|-------|------|------|-------------|-------------|
| 0-1 | `static_pressure` | int16 | hPa (raw × 0.1) | Filtered static pressure | `0x7FFF` |
| 2 | `sensor_temperature` | int8 | degC | Whole degrees | — |
| 3-4 | `differential_pressure` | int16 | Pa | Filtered pitot (differential) pressure | `0x7FFF` |
| 5-6 | `qnh_in_use` | int16 | inHg (raw × 0.01) | The QNH the AIR module is using for the altitude (inHg x 100, 2992 is 29.92) | `0x7FFF` |
| 7 | `flags` | uint8 |  | Bit 0 = QNH not set (the AIR module has heard no QNH frame and is using 29.92). Other bits 0 | — |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `static_pressure` (1/2) | `static_pressure` (2/2) | `sensor_temperature` | `differential_pressure` (1/2) | `differential_pressure` (2/2) | `qnh_in_use` (1/2) | `qnh_in_use` (2/2) | `flags` |

**Notes**

- Bytes 5-7 report the QNH in use (2026-10-20, see EFIS/docs/QNH_PLAN.md). They were padding (always 0), so a receiver that unpacks them as padding is not affected. A receiver compares bytes 5-6 with its own QNH to detect a baro mismatch.
- Matches the legacy CircuitPython format (struct "<hbhBBB"). The differential pressure (bytes 3-4) was missing from the old Can Docs.md.
- Invalid markers (AIR_Module 1.0.35 onward, 2026-10-18): static pressure is 0x7FFF when the static sensor is lost, differential pressure when the pitot is lost. Before 1.0.35 the differential pressure was 0 when the pitot was lost and the static pressure held its last value. The 1.5 s timeout was not in the 2026-10-17 decision list; it was set equal to the OAT message because the EFIS uses both for density altitude.

**Code**

- Sender: AIR_Module.ino (CAN_RAW_MSG_ID)
- Receiver: aio_server.py process_static_pressure_message

### 0x02C QNH_SPARE_2C

**Altimeter setting (spare sender ID for a future panel)**

| | |
|---|---|
| Sent by | Future panel |
| Received by | AIR module, Pi EFIS, Backup display |
| Rate | Same rules as 0x2E. Nothing sends it yet |
| Data length | 8 bytes |
| Status | 🔘 Reserved |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `qnh_hpa` | int16 | hPa | Informational |
| 2-3 | `qnh_inhg` | int16 | inHg (raw × 0.0025) | inHg x 400 |
| 4-7 | `reserved` | uint8 |  | Always 0 |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `qnh_hpa` (1/2) | `qnh_hpa` (2/2) | `qnh_inhg` (1/2) | `qnh_inhg` (2/2) | `reserved` (1/4) | `reserved` (2/4) | `reserved` (3/4) | `reserved` (4/4) |

**Notes**

- Part of the QNH block 0x2C to 0x2F. See 0x2E for the rules.

### 0x02D QNH_BACKUP

**Altimeter setting from the backup display**

| | |
|---|---|
| Sent by | Backup display |
| Received by | AIR module, Pi EFIS |
| Rate | 1 s (Sent when a person changes the setting, and after 1000 ms +/- 20 ms of silence on the QNH block while the display holds a set value) |
| Data length | 8 bytes |
| Status | ⚪ Planned |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `qnh_hpa` | int16 | hPa | Informational |
| 2-3 | `qnh_inhg` | int16 | inHg (raw × 0.0025) | inHg x 400 |
| 4-7 | `reserved` | uint8 |  | Always 0 |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `qnh_hpa` (1/2) | `qnh_hpa` (2/2) | `qnh_inhg` (1/2) | `qnh_inhg` (2/2) | `reserved` (1/4) | `reserved` (2/4) | `reserved` (3/4) | `reserved` (4/4) |

**Notes**

- Part of the QNH block 0x2C to 0x2F. See 0x2E for the rules.

**Code**

- Sender: backup display (not written yet)
- Receiver: AIR_Module.ino, aio_server.py

### 0x02E QNH

**Altimeter setting from the Pi EFIS**

| | |
|---|---|
| Sent by | Pi EFIS |
| Received by | AIR module, Backup display |
| Rate | 1 s (Sent when a person changes the setting, and after 1000 ms +/- 20 ms of silence on the QNH block while the EFIS holds a set value) |
| Data length | 8 bytes |
| Status | ✅ Active |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `qnh_hpa` | int16 | hPa | Informational. Receivers check it agrees with the inHg field |
| 2-3 | `qnh_inhg` | int16 | inHg (raw × 0.0025) | inHg x 400 (2992 x 4 = 11968 is 29.92 inHg) |
| 4-7 | `reserved` | uint8 |  | Always 0 |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `qnh_hpa` (1/2) | `qnh_hpa` (2/2) | `qnh_inhg` (1/2) | `qnh_inhg` (2/2) | `reserved` (1/4) | `reserved` (2/4) | `reserved` (3/4) | `reserved` (4/4) |

**Notes**

- QNH block 0x2C to 0x2F (decided 2026-10-20, see EFIS/docs/QNH_PLAN.md). Any device with a QNH control may set the QNH and every device holds the value. One device sends on one ID only (0x2E EFIS, 0x2D backup display, 0x2C and 0x2F spare); two devices never send the same ID. The payload is the same on all four IDs. Receivers use one hardware filter: ID 0x2C, mask 0x7FC.
- Rules. (1) Change: a person changes the QNH on a device, which sends the new value at once on its own ID, every change, at most one frame per 20 ms. (2) Hear: a device adopts a valid frame from any ID in the block; the last frame heard wins and CAN arbitration is the only arbiter. (3) Speak on silence: a device that holds a set value and has heard no QNH frame for 1000 ms +/- 20 ms (drawn once per silent period) sends its value. (4) Never invent: a device sends only a value a person entered or one it adopted; the 29.92 a device starts with ("not set") is never sent. (5) Silence is not a fault. (6) The AIR module reports the QNH it is using in 0x2B bytes 5-7, and a display flags a mismatch if its set value differs from that for more than 2 s.
- Receiver checks: inHg x 400 must be 11200 to 12400 (28.00 to 31.00 inHg), and the hPa field must agree with the inHg field to within 1 hPa. A frame that fails is ignored.
- Power-up: a device listens for 2 s and adopts any valid frame. If it hears none it uses 29.92 flagged "not set". Nothing is stored in non-volatile memory.
- The inHg field is x400, not x100 as the old Can Docs.md said. The AIR module divides it by 4 to get x100.

**Code**

- Sender: aio_server.py (qnh_state.py)
- Receiver: AIR_Module.ino (CAN_QNH_FIRST_ID to CAN_QNH_LAST_ID)

### 0x02F QNH_SPARE_2F

**Altimeter setting (spare sender ID for a future panel)**

| | |
|---|---|
| Sent by | Future panel |
| Received by | AIR module, Pi EFIS, Backup display |
| Rate | Same rules as 0x2E. Nothing sends it yet |
| Data length | 8 bytes |
| Status | 🔘 Reserved |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `qnh_hpa` | int16 | hPa | Informational |
| 2-3 | `qnh_inhg` | int16 | inHg (raw × 0.0025) | inHg x 400 |
| 4-7 | `reserved` | uint8 |  | Always 0 |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `qnh_hpa` (1/2) | `qnh_hpa` (2/2) | `qnh_inhg` (1/2) | `qnh_inhg` (2/2) | `reserved` (1/4) | `reserved` (2/4) | `reserved` (3/4) | `reserved` (4/4) |

**Notes**

- Part of the QNH block 0x2C to 0x2F. See 0x2E for the rules.

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
| Treated as lost after | 0.5 s without a message |

| Bytes | Field | Type | Unit | Description | Invalid marker |
|:-----:|-------|------|------|-------------|-------------|
| 0-1 | `heading` | int16 | deg (raw × 0.1) | Fused yaw, clockwise positive (NED), 0 to 360 | `0x7FFF` |
| 2-3 | `pitch` | int16 | deg (raw × 0.1) | Aircraft (NED) convention | `0x7FFF` |
| 4-5 | `roll` | int16 | deg (raw × 0.1) | Aircraft (NED) convention | `0x7FFF` |
| 6-7 | `turn_rate` | int16 | deg/s (raw × 0.1) | Derived from yaw; 0.5 deg/s deadband | `0x7FFF` |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `heading` (1/2) | `heading` (2/2) | `pitch` (1/2) | `pitch` (2/2) | `roll` (1/2) | `roll` (2/2) | `turn_rate` (1/2) | `turn_rate` (2/2) |

**Notes**

- Invalid markers (IMU_AHRS 1.0.16 onward, 2026-10-18): all four fields (heading, pitch, roll, turn rate) are 0x7FFF when the accelerometer/gyro (ISM330DHCX) is lost. Before 1.0.16 the last attitude was held and kept being sent. When only the magnetometer is lost the IMU keeps running as a 6-axis AHRS: pitch and roll stay correct and the heading continues from the gyro alone, drifting about 0.1 deg/s. That heading is NOT marked invalid; the EFIS learns of it from heartbeat 0x701 (sensors bit 1 clear) and shows it under a red X for 5 minutes (decision 1 in EFIS/docs/MODULE_STATUS_PLAN.md).
- The IMU converts its native frame (NWU) to aircraft NED before sending. The EFIS only divides by 10.
- Today the EFIS treats this message as stale after 0.2 s and blanks the values; the new timeout is 0.5 s.

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
| Treated as lost after | 0.5 s without a message |

| Bytes | Field | Type | Unit | Description | Invalid marker |
|:-----:|-------|------|------|-------------|-------------|
| 0-1 | `accel_x` | int16 | m/s2 (raw × 0.01) | Sensor X (forward) | `0x7FFF` |
| 2-3 | `accel_y` | int16 | m/s2 (raw × 0.01) | Sensor Y (left) | `0x7FFF` |
| 4-5 | `accel_z` | int16 | m/s2 (raw × 0.01) | Sensor Z (up) | `0x7FFF` |
| 6-7 | `calibration` | int16 |  | Calibration status; always 0 today | — |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `accel_x` (1/2) | `accel_x` (2/2) | `accel_y` (1/2) | `accel_y` (2/2) | `accel_z` (1/2) | `accel_z` (2/2) | `calibration` (1/2) | `calibration` (2/2) |

**Notes**

- Invalid markers (IMU_AHRS 1.0.16 onward, 2026-10-18): the three accelerations are 0x7FFF when the accelerometer is lost. Before 1.0.16 the last values were held.
- Calibrated values in the sensor's native frame, not converted to aircraft axes.
- Today the EFIS treats this message as stale after 0.2 s; the new timeout is 0.5 s.

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
| Treated as lost after | 3.5 s without a message |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-3 | `latitude` | int32 | deg (raw × 1e-06) | North positive |
| 4-7 | `longitude` | int32 | deg (raw × 1e-06) | East positive |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `latitude` (1/4) | `latitude` (2/4) | `latitude` (3/4) | `latitude` (4/4) | `longitude` (1/4) | `longitude` (2/4) | `longitude` (3/4) | `longitude` (4/4) |

**Notes**

- Sent only while the GPS has a fix. Decided 2026-10-17: no invalid markers on the GPS messages; when the GPS has no data or no fix the frames simply stop. Today the EFIS keeps the last position forever; it will time the data out after 3.5 s. 0x65 and the GPS heartbeat (0x703) say why.

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
| Treated as lost after | 3.5 s without a message |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `ground_speed` | int16 | kt | Whole knots |
| 2-3 | `altitude` | int16 | ft | GPS altitude |
| 4-5 | `true_track` | int16 | deg | Whole degrees; held at 0 below 2 kt |
| 6-7 | `mag_track` | int16 | deg | True track corrected with WMM2025 declination, held at 0 below 2 kt. The EFIS uses this one for the HSI track diamond (the compass rose is magnetic) |

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
| Treated as lost after | 3.5 s without a message |

| Bytes | Field | Type | Unit | Description | Invalid marker |
|:-----:|-------|------|------|-------------|-------------|
| 0 | `fix_quality` | uint8 |  | 0 = no fix, 1 = GPS, 2 = DGPS (WAAS) | — |
| 1 | `fix_type` | uint8 |  | 1 = no fix, 2 = 2D, 3 = 3D | — |
| 2 | `satellites` | uint8 | count | Satellites used in the fix | — |
| 3 | `declination` | int8 | deg | Magnetic declination at the GPS position from the module's WMM2025 calculation, whole degrees, East positive (magnetic = true - declination). 0x7F until the module has calculated it (first fix with a date). Held at the last value when the fix is lost | `0x07F` |
| 4-5 | `hdop` | int16 | raw × 0.01 | Horizontal dilution of precision | — |
| 6-7 | `vdop` | int16 | raw × 0.01 | Vertical dilution of precision | — |

**Byte layout**

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Field | `fix_quality` | `fix_type` | `satellites` | `declination` | `hdop` (1/2) | `hdop` (2/2) | `vdop` (1/2) | `vdop` (2/2) |

**Notes**

- Sent every second whether or not there is a fix, so the display can show "No Fix".
- Also a good "GPS module is alive" indicator.
- The declination (byte 3, added in GPS_Module 1.0.9; it was a reserved byte that always read 0) lets the EFIS and the backup display convert true courses to magnetic. The EFIS hides the flight-plan course until it is known.

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

### 0x700 EFIS_HEARTBEAT

**Pi EFIS heartbeat and view of the modules**

| | |
|---|---|
| Sent by | Pi EFIS |
| Received by | CAN display feather |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | ✅ Active |
| Treated as lost after | 3 s without a message |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `module` | uint8 |  | Node number (0 EFIS, 1 IMU, 2 AIR, 3 GPS). Always equals the ID minus 0x700 |
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

- Planned (decided 2026-10-17, EFIS/docs/MODULE_STATUS_PLAN.md). Same layout as the module heartbeats (0x701 and the rules there), module number 0. Sent by a task in aio_server.py every second, so it stops if the EFIS software stops. The backup display (Sensors/Backup_Display) uses it to tell "the Pi is alive but a module is missing" from "the Pi is dead".
- Version bytes: the EFIS software version (to be defined). Uptime: seconds since the EFIS server started.
- Sensors mask for the EFIS: bit 0 the AIR module is seen healthy (heartbeat state 1 and arriving), bit 1 the IMU module, bit 2 the GPS module, bit 3 a display (browser) client is connected. Bits 4 to 7 are 0.
- State: 1 all three modules healthy, 2 a module is degraded, failed or missing, 3 the EFIS cannot produce a display (for example no display client for a while after start-up, to be defined with the implementation).

**Code**

- Sender: aio_server.py send_efis_heartbeat (efis_heartbeat.py), EFIS server 1.6.1 onward
- Receiver: not written yet (CAN_Feather.ino)

### 0x701 IMU_HEARTBEAT

**IMU / AHRS module heartbeat, version and health**

| | |
|---|---|
| Sent by | IMU / AHRS module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | 🟡 Partial |
| Treated as lost after | 3 s without a message |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `module` | uint8 |  | Node number (0 EFIS, 1 IMU, 2 AIR, 3 GPS). Always equals the ID minus 0x700 |
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

- Heartbeat, same layout for every module. It is sent once as soon as the CAN bus is up (state 0 while starting, or 3 if start-up failed), then every second. Firmware: IMU_AHRS 1.0.15, AIR_Module 1.0.34, GPS_Module 1.0.8 (ModuleInfo library 1.3.0). The EFIS does not read it yet (planned: EFIS/docs/MODULE_STATUS_PLAN.md). The sensors mask follows the live health of each sensor, not just what was found at start-up. A sensor is invalid after a short run of bad samples (IMU and AIR pressure: 20 samples, 0.2 s; AIR temperature: 2 s) and valid again after a few good ones. The status LED shows the same: green state 1, amber state 2, red state 3 (also red when CAN frames are not acknowledged).
- The EFIS records the time each heartbeat arrived, the version, the state and the sensors mask. A module is shown as missing when no heartbeat has arrived for 3 s (three missed). Which modules the EFIS expects is EFIS configuration.
- Sensors mask for the IMU: bit 0 ISM330DHCX (gyro and accelerometer), bit 1 LIS3MDL (magnetometer), bit 2 calibration loaded. State 2 (degraded) when the magnetometer is lost (the AHRS keeps running on gyro and accelerometer only) or calibration is missing. State 3 (failed) when the accelerometer/gyro is lost: there is no attitude. A missing magnetometer at start-up is no longer fatal; a missing ISM330DHCX still halts start-up with state 3.
- The version bytes are the VERSION string of the sketch ("1.0.3" is 1, 0, 3). The same text is in the USB product string and the '?' report (ModuleInfo library).

**Code**

- Sender: IMU_AHRS.ino (sendHeartbeat), ModuleInfo library
- Receiver: not written yet (aio_server.py)

### 0x702 AIR_HEARTBEAT

**AIR module heartbeat, version and health**

| | |
|---|---|
| Sent by | AIR module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | 🟡 Partial |
| Treated as lost after | 3 s without a message |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `module` | uint8 |  | Node number (0 EFIS, 1 IMU, 2 AIR, 3 GPS). Always equals the ID minus 0x700 |
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
- Sensors mask for the AIR module: bit 0 pitot MS4525DO, bit 1 AOA MS4525DO, bit 2 static pressure SSC, bit 3 outside air temperature (MAX31865 RTD). All four follow the live health of the sensor. A sensor that is not found at start-up no longer stops the module: its bit is clear and the others keep working. An MS4525DO (pitot, AOA) that was missing at start-up stays invalid until the next restart, because it is zeroed at start-up. State 2 when any bit is clear; state 3 only when neither the pitot nor the static pressure works (or CAN did not start).

**Code**

- Sender: AIR_Module.ino (sendHeartbeat), ModuleInfo library
- Receiver: not written yet (aio_server.py)

### 0x703 GPS_HEARTBEAT

**GPS module heartbeat, version and health**

| | |
|---|---|
| Sent by | GPS module |
| Received by | Pi EFIS |
| Rate | 1 s |
| Data length | 8 bytes |
| Status | 🟡 Partial |
| Treated as lost after | 3 s without a message |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `module` | uint8 |  | Node number (0 EFIS, 1 IMU, 2 AIR, 3 GPS). Always equals the ID minus 0x700 |
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
- Sensors mask for the GPS module: bit 0 NMEA data arriving from the GPS chip, bit 1 has a position fix. No fix is not a fault, so a missing fix does not change state; it stays 1 (OK) while NMEA is arriving. 0x65 carries the fix details. State 2 (degraded) when no valid NMEA has arrived for 5 s; the module then re-probes the GPS chip every 5 s. LED: blue no fix, yellow 2D, green 3D, amber silent.

**Code**

- Sender: GPS_Module.ino (sendHeartbeat), ModuleInfo library
- Receiver: not written yet (aio_server.py)

## Nodes

| Node | Role | Code |
|------|------|------|
| AIR module | Airspeed, altitude, AOA, static and outside air temperature | `Sensors/AIR_Module/AIR_Module.ino` |
| IMU / AHRS module | Attitude, heading, acceleration | `Sensors/IMU_AHRS/IMU_AHRS.ino` |
| GPS module | Position, speed, track, time | `Sensors/GPS_Module/GPS_Module.ino` |
| Pi EFIS | Display computer (Raspberry Pi). Listens to everything, shares the QNH. | `EFIS/aio_server.py` |
| Backup display | Feather RP2040 with a 20x4 LCD and rotary encoder; shows the flight data and sets the QNH | — |
| Future panel | Spare sender for the QNH IDs 0x2C and 0x2F | — |
| CAN display feather | Listens for airspeed/altitude and attitude for the small LCD display | `Legacy/CAN_Display_v1/CAN_Feather/CAN_Feather.ino` |
| EMS module | Engine monitor (planned). Only a legacy CircuitPython version exists. | — |

## IDs named in code but unused

| ID | Name | Note |
|:--:|------|------|
| `0x038` | ENCODER | Named in the EFIS code (CAN_MSG_ID.ENCODER) but unused. The rotary encoder is read over I2C, not CAN. |
