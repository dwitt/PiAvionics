# MGL Avionics CAN messages

> **Generated file. Do not edit.** Edit `Protocol/mgl_can_messages.yaml` and run `Protocol/.venv/bin/python Protocol/generate_can_docs.py`.

Transcribed from MGL CAN Protocol.pdf, Release 10 (2018-08-16). Check MGL's document before relying on a detail.

MGL devices each own a block of 16 message IDs, chosen by a 7-bit device address. Devices never send to each other; only the host (address 1) sends commands.

## How IDs are built

```
CAN ID = (device address << 4) | message type
```

The device address is the upper 7 bits and the message type is the lower 4 bits, so every device owns a block of 16 IDs. Host (EFIS) commands use host address 1 (IDs 0x10 to 0x1F). Each device sends on its own block.

- Host addresses 1 to 15 are reserved for EFIS hosts. Only host 1 is used today; MGL says the others will be used as more message types are needed.
- Servo messages (addresses 16 to 31) and the SP-10 / ECB details that are not in this document are not transcribed here.

## Device addresses

| Device | Addresses | CAN ID range | Selection |
|--------|:---------:|:------------:|-----------|
| Host / EFIS | 1–15 | `0x010`–`0x0FF` | Fixed (only host 1 in use) |
| CAN servos | 16–31 | `0x100`–`0x1FF` | Set by an identification message (not detailed in this document) |
| RDAC XF engine unit | 32–35 | `0x200`–`0x23F` | 2 DIP switches |
| SP-6 compass | 36–39 | `0x240`–`0x27F` | Software command (host message 5) |
| SP-7 / SP-9 AHRS | 40–43 | `0x280`–`0x2BF` | Software command (host message 5) |
| Transponder | 44–45 | `0x2C0`–`0x2DF` | Software command |
| Reserved by MGL | 46–47 | `0x2E0`–`0x2FF` | - |
| SP-10 flap/trim/motor controller | 48–51 | `0x300`–`0x33F` | DIP switch (function assigned by DIP array) |
| ECB electronic breakers | 52–59 | `0x340`–`0x3BF` | 3 DIP switches |
| Touch pad | 60–63 | `0x3C0`–`0x3FF` | Software command |
| iEFIS extender | 64–67 | `0x400`–`0x43F` | Software command (extender message type 6) |
| External MGL autopilot | 68–69 | `0x440`–`0x45F` | - |

## Message index

| ID | Name | Direction | Rate | Bytes |
|:--:|------|-----------|------|:-----:|
| `0x012` | HOST_ATTITUDE_SPEED | Host → SP-6 compass | 500 ms | 8 |
| `0x013` | SP7_COMMAND | Host → SP-7 / SP-9 AHRS | — | 1-7 |
| `0x014` | SP6_COMMAND | Host → SP-6 compass | — | 2 |
| `0x015` | SET_DEVICE_ID | Host → SP-6 compass, SP-7 / SP-9 AHRS | — | 4 |
| `0x015` | SET_SP6_ADDRESS | Host → SP-6 compass | — | 4 |
| `0x015` | TRANSPONDER_ID_ALT | Host → Transponder | 1 s | 8 |
| `0x016` | TRANSPONDER_STATE | Host → Transponder | 1 s | 8 |
| `0x017` | TRANSPONDER_SQUITTER | Host → Transponder | 1 s | 8 |
| `0x018` | SP10_COMMAND | Host → SP-10 flap/trim/motor controller | — | 1-8 |
| `0x019` | GENERAL_COMMAND | Host → RDAC XF engine unit, ECB electronic breakers | — | 1-8 |
| `0x01A` | EXTENDER_COMMAND | Host → iEFIS extender | — | 2-8 |
| `0x201` | RDAC_FUEL_FLOW | RDAC XF engine unit → Host | 4 s | 8 |
| `0x202` | RDAC_TC_1_4 | RDAC XF engine unit → Host | 500 ms | 8 |
| `0x203` | RDAC_TC_5_8 | RDAC XF engine unit → Host | 500 ms | 8 |
| `0x204` | RDAC_TC_9_12 | RDAC XF engine unit → Host | 500 ms | 8 |
| `0x205` | RDAC_ANALOG_1 | RDAC XF engine unit → Host | 500 ms | 8 |
| `0x206` | RDAC_ANALOG_2 | RDAC XF engine unit → Host | 500 ms | 8 |
| `0x207` | RDAC_SUPPLY | RDAC XF engine unit → Host | 500 ms | 4 |
| `0x208` | RDAC_RPM_MAP | RDAC XF engine unit → Host | 200 ms | 8 |
| `0x209` | RDAC_CALIBRATION | RDAC XF engine unit → Host | — | 8 |
| `0x241` | SP6_HEADING | SP-6 compass → Host | 50 ms | 8 |
| `0x242` | SP6_DEV_1 | SP-6 compass → Host | — | 8 |
| `0x242` | SP6_CAL | SP-6 compass → Host | — | 4 |
| `0x243` | SP6_DEV_2 | SP-6 compass → Host | — | 8 |
| `0x244` | SP6_DEV_3 | SP-6 compass → Host | — | 2 |
| `0x281` | SP7_ACCEL | SP-7 / SP-9 AHRS → Host | 50 ms | 8 |
| `0x282` | SP7_GYRO | SP-7 / SP-9 AHRS → Host | 50 ms | 8 |
| `0x283` | SP7_EULER | SP-7 / SP-9 AHRS → Host | 50 ms | 8 |
| `0x2C5` | TRANSPONDER_REPLY_ID | Transponder → Host | 4 s | 8 |
| `0x2C6` | TRANSPONDER_REPLY_STATE | Transponder → Host | 4 s | 8 |
| `0x300` | SP10_FLAP | SP-10 flap/trim/motor controller → Host | 1 s | 4 |
| `0x301` | SP10_PITCH_TRIM | SP-10 flap/trim/motor controller → Host | 1 s | 4 |
| `0x302` | SP10_ROLL_TRIM | SP-10 flap/trim/motor controller → Host | 1 s | 4 |
| `0x308` | SP10_SETUP | SP-10 flap/trim/motor controller → Host | — | 3-7 |
| `0x30E` | SP10_DIAG_A | SP-10 flap/trim/motor controller → Host | — | 8 |
| `0x30F` | SP10_DIAG_B | SP-10 flap/trim/motor controller → Host | — | 8 |
| `0x340` | ECB_STATE | ECB electronic breakers → Host | 4 s | 5 |
| `0x341` | ECB_CURRENT | ECB electronic breakers → Host | 4 s | 8 |
| `0x342` | ECB_TRIP_1_4 | ECB electronic breakers → Host | — | 8 |
| `0x343` | ECB_TRIP_5_8 | ECB electronic breakers → Host | — | 8 |
| `0x344` | ECB_TRIP_TIME | ECB electronic breakers → Host | — | 8 |
| `0x345` | ECB_WIGWAG | ECB electronic breakers → Host | — | 4 |
| `0x3C0` | TOUCHPAD_GESTURE | Touch pad → Host | — | 1 |
| `0x3C0` | TOUCHPAD_XY | Touch pad → Host | — | 4 |
| `0x400` | EXT_ANALOG_1_4 | iEFIS extender → Host | 125 ms | 8 |
| `0x401` | EXT_ANALOG_5_8 | iEFIS extender → Host | 125 ms | 8 |
| `0x402` | EXT_STATUS | iEFIS extender → Host | 125 ms | 6 |
| `0x403` | EXT_RS232_RX1 | iEFIS extender → Host | — | 1-8 |

## Messages

### 0x012 HOST_ATTITUDE_SPEED

**Aircraft speed and attitude**

| | |
|---|---|
| CAN ID | `0x012` |
| Direction | Host → SP-6 compass |
| Rate | 500 ms (At least once per 500 ms) |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `bank` | int16 | deg (raw × 0.1) | -180.0 to 180.0; 0x7FFF if unknown |
| 2-3 | `pitch` | int16 | deg (raw × 0.1) | -90.0 to 90.0; 0x7FFF if unknown |
| 4-5 | `yaw` | int16 | deg (raw × 0.1) | 0 to 359.9; 0x7FFF if unknown |
| 6-7 | `speed` | uint16 | mph | Ground speed (TAS if no GPS), 0 to 1000 |

### 0x013 SP7_COMMAND

**Command to SP-7 / SP-9 AHRS**

| | |
|---|---|
| CAN ID | `0x013` |
| Direction | Host → SP-7 / SP-9 AHRS |
| Rate | — |
| Data length | 1-7 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `command` | uint8 |  | 0 = set gyro horizon from the accelerometers |
| 1-6 | `data` | uint8 |  | Optional |

### 0x014 SP6_COMMAND

**Command to SP-6 compass**

| | |
|---|---|
| CAN ID | `0x014` |
| Direction | Host → SP-6 compass |
| Rate | — |
| Data length | 2 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `command` | uint8 |  | See commands below |
| 1 | `command_repeat` | uint8 |  | Must repeat byte 0. The SP-6 rejects a command that is not exactly 2 bytes |

**Notes**

- Pre-self-calibrating firmware: 0 set North, 1 set South, 2 set East, 3 set West, 4 clear alignment, 5 start deviation calibration, 6 end and keep result, 7 cancel, 8 restore factory settings, 9 operation mode (only 2 and 4 supported: ACEL 3D and EFIS 3D).
- Self-calibrating firmware: 12 clear and start a new calibration, 13 end and store result (restores the last stored calibration if none is active), 14 set pitch and bank to zero (level), 15 set bank to 90 degrees, 16 set pitch to 90 degrees.

### 0x015 SET_DEVICE_ID

**Set SP-6/7/9 device number**

| | |
|---|---|
| CAN ID | `0x015` |
| Direction | Host → SP-6 compass, SP-7 / SP-9 AHRS |
| Rate | — |
| Data length | 4 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `key_1` | uint8 |  | 0xAA |
| 1 | `key_2` | uint8 |  | 0x55 |
| 2 | `number` | uint8 |  | Requested device address 32 to 43 (as documented) |
| 3 | `check` | uint8 |  | Byte 2 XOR 0xFF |

**Notes**

- Send with one device on the bus. A device transmits one data group only.

### 0x015 SET_SP6_ADDRESS

**Set SP-6 compass bus address**

| | |
|---|---|
| CAN ID | `0x015` |
| Direction | Host → SP-6 compass |
| Rate | — |
| Data length | 4 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `key_1` | uint8 |  | 0xAA |
| 1 | `key_2` | uint8 |  | 0x55 |
| 2 | `address` | uint8 |  | Requested CAN address 36 to 39 |
| 3 | `check` | uint8 |  | Byte 2 XOR 0xFF |

**Notes**

- Send with only one SP-6 connected to the bus.

### 0x015 TRANSPONDER_ID_ALT

**EFIS to transponder: aircraft ID and baro altitude**

| | |
|---|---|
| CAN ID | `0x015` |
| Direction | Host → Transponder |
| Rate | 1 s |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-5 | `aircraft_id` | 6-bit x8 |  | Eight characters, 6 bits each: A-Z = 0x01-0x1A, space = 0x20, 0-9 = 0x30-0x39 |
| 6-7 | `altitude` | int16 | ft (raw × 10) | Pressure altitude in 10 ft steps; -101 means invalid |

**Notes**

- Must be sent once a second. The transponder goes to Off/Standby if nothing arrives for 4 seconds.

### 0x016 TRANSPONDER_STATE

**EFIS to transponder: squawk and state**

| | |
|---|---|
| CAN ID | `0x016` |
| Direction | Host → Transponder |
| Rate | 1 s |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `squawk` | octal x4 |  | 3 bits per digit; octal 1234 is sent as 0x9C 0x02 |
| 2 | `category` | uint8 |  | Bits 0-2 L/W code, bits 3-7 aircraft category (DO-260B) |
| 3 | `state` | uint8 |  | Bits 0-2: 0 off, 1 standby, 2 ground, 3 on, 4 alt. Bit 3: IDENT on 0 to 1. Bit 4: 1 = in flight |
| 4-6 | `icao_address` | uint24 |  | 24-bit ICAO address, LSB first |
| 7 | `speed_class` | uint8 |  | 0 to 6 speed class; bit 7 = ADS-B In capable |

**Notes**

- Must be sent once a second.

### 0x017 TRANSPONDER_SQUITTER

**EFIS to transponder: extended squitter (3 messages)**

| | |
|---|---|
| CAN ID | `0x017` |
| Direction | Host → Transponder |
| Rate | 1 s |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `message_type` | uint8 |  | 0, 1 or 2 selects the layout below |
| 1-7 | `data` | varies |  | Type 0: GPS fix (0-3), ground track (deg x 10), ground speed (kt x 10), height of geoid (ft/5 + 1000). Type 1: satellites tracked, altitude HMSL (ft/5 + 1000), latitude. Type 2: UTC h:m:s, longitude |

**Notes**

- MGL position format: 180000 = 1 degree; negative is S or W.
- Mode-S transponders only.

### 0x018 SP10_COMMAND

**Command to SP-10 motor/flap/trim controller**

| | |
|---|---|
| CAN ID | `0x018` |
| Direction | Host → SP-10 flap/trim/motor controller |
| Rate | — |
| Data length | 1-8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `command` | uint8 |  | 0 set position (flap), 1 set position (pitch trim), 2 set position (bank trim), 4/5/6 pulse flap/pitch/bank motor, 8 setup, 9 go to stored position, 10 request diagnostics |
| 1 | `command_repeat` | uint8 |  | Repeat of byte 0 |
| 2-7 | `data` | varies |  | Position 0-4095 (2 bytes LSB first), direction, timeout (100 ms steps), power; or setup item and value |

**Notes**

- Any setup message makes the SP-10 send its setup data.

### 0x019 GENERAL_COMMAND

**General command (RDAC and ECB)**

| | |
|---|---|
| CAN ID | `0x019` |
| Direction | Host → RDAC XF engine unit, ECB electronic breakers |
| Rate | — |
| Data length | 1-8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `destination` | uint8 |  | 0 to 3 = RDAC 1 to 4; 8 to 15 = ECB 1 to 8 |
| 1 | `command` | uint8 |  | RDAC: 0x81 request calibration, 0x82-0x86 set ambient/TC/analog/MAP/volt calibration, 0xA0 store in flash. ECB: 0 trip current, 1 trip speed, 2 WIGWAG state, 3 WIGWAG time, 4 breaker on/off, 5 request setup |
| 2-7 | `data` | uint8 |  | Depends on the command |

**Notes**

- PiAvionics 0x19 (GPS time) uses this same ID. Byte 0 of the time message is the year, never 0 to 3 or 8 to 15, so the RDAC and ECB ignore it today.

### 0x01A EXTENDER_COMMAND

**iEFIS extender command (extender 1; types 10 to 13 address extenders 1 to 4)**

| | |
|---|---|
| CAN ID | `0x01A` |
| Direction | Host → iEFIS extender |
| Rate | — |
| Data length | 2-8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `type` | uint8 |  | 0 set outputs, 1-5 set up RS232 port, 6 set address, 7-11 send data to RS232 port |
| 1-7 | `data` | varies |  | Depends on the type |

**Notes**

- Normally unused in an MGL EFIS (the extender uses a serial link). Message type 0 switches the extender into CAN-only mode.

### 0x201 RDAC_FUEL_FLOW

**RDAC XF fuel flow pulses**

| | |
|---|---|
| CAN ID | `0x201` (first unit; other units add 0x10 each, up to `0x231`) |
| Direction | RDAC XF engine unit → Host |
| Rate | 4 s |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `flow1_count` | uint16 |  | Pulses in the last 4 s |
| 2-3 | `flow1_ratio` | uint16 |  | 1000 = 100.0 %; 65536 = no pulse |
| 4-5 | `flow2_count` | uint16 |  |  |
| 6-7 | `flow2_ratio` | uint16 |  |  |

### 0x202 RDAC_TC_1_4

**RDAC XF thermocouples 1 to 4**

| | |
|---|---|
| CAN ID | `0x202` (first unit; other units add 0x10 each, up to `0x232`) |
| Direction | RDAC XF engine unit → Host |
| Rate | 500 ms |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `tc1` | int16 | degC |  |
| 2-3 | `tc2` | int16 | degC |  |
| 4-5 | `tc3` | int16 | degC |  |
| 6-7 | `tc4` | int16 | degC |  |

**Notes**

- Uncompensated K-type reading (24.9 mV = 600 C). Add the cold junction temperature from message 7.

### 0x203 RDAC_TC_5_8

**RDAC XF thermocouples 5 to 8**

| | |
|---|---|
| CAN ID | `0x203` (first unit; other units add 0x10 each, up to `0x233`) |
| Direction | RDAC XF engine unit → Host |
| Rate | 500 ms |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `tc5` | int16 | degC |  |
| 2-3 | `tc6` | int16 | degC |  |
| 4-5 | `tc7` | int16 | degC |  |
| 6-7 | `tc8` | int16 | degC |  |

### 0x204 RDAC_TC_9_12

**RDAC XF thermocouples 9 to 12**

| | |
|---|---|
| CAN ID | `0x204` (first unit; other units add 0x10 each, up to `0x234`) |
| Direction | RDAC XF engine unit → Host |
| Rate | 500 ms |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `tc9` | int16 | degC |  |
| 2-3 | `tc10` | int16 | degC |  |
| 4-5 | `tc11` | int16 | degC |  |
| 6-7 | `tc12` | int16 | degC |  |

### 0x205 RDAC_ANALOG_1

**RDAC XF analog inputs: oil and aux**

| | |
|---|---|
| CAN ID | `0x205` (first unit; other units add 0x10 each, up to `0x235`) |
| Direction | RDAC XF engine unit → Host |
| Rate | 500 ms |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `oil_temp` | uint16 |  | Raw ADC 0-4095 |
| 2-3 | `oil_pressure` | uint16 |  | Raw ADC |
| 4-5 | `aux1` | uint16 |  | Raw ADC |
| 6-7 | `aux2` | uint16 |  | Raw ADC |

### 0x206 RDAC_ANALOG_2

**RDAC XF analog inputs: fuel and coolant**

| | |
|---|---|
| CAN ID | `0x206` (first unit; other units add 0x10 each, up to `0x236`) |
| Direction | RDAC XF engine unit → Host |
| Rate | 500 ms |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `fuel_pressure` | uint16 |  | Raw ADC |
| 2-3 | `coolant` | uint16 |  | Raw ADC |
| 4-5 | `fuel_level_1` | uint16 |  | Raw ADC |
| 6-7 | `fuel_level_2` | uint16 |  | Raw ADC |

### 0x207 RDAC_SUPPLY

**RDAC XF temperature and supply voltage**

| | |
|---|---|
| CAN ID | `0x207` (first unit; other units add 0x10 each, up to `0x237`) |
| Direction | RDAC XF engine unit → Host |
| Rate | 500 ms |
| Data length | 4 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `cold_junction_temp` | int16 | degC |  |
| 2-3 | `supply_voltage` | uint16 |  | Volts = raw / 5.73758, in 0.1 V steps |

**Notes**

- Messages 2 to 7 must all be sent. Send them together every 500 ms.

### 0x208 RDAC_RPM_MAP

**RDAC XF RPM, MAP and current**

| | |
|---|---|
| CAN ID | `0x208` (first unit; other units add 0x10 each, up to `0x238`) |
| Direction | RDAC XF engine unit → Host |
| Rate | 200 ms |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `rpm1` | uint16 | rpm | 1 pulse per revolution; 50000 or more means RPM x 10 |
| 2-3 | `rpm2` | uint16 | rpm |  |
| 4-5 | `map` | uint16 |  | Raw ADC 0-4095 |
| 6-7 | `current` | uint16 |  | Raw ADC 0-4095 |

### 0x209 RDAC_CALIBRATION

**RDAC XF calibration reply**

| | |
|---|---|
| CAN ID | `0x209` (first unit; other units add 0x10 each, up to `0x239`) |
| Direction | RDAC XF engine unit → Host |
| Rate | — |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `id` | uint8 |  | 0: V1 ambient, V2 TC, V3 analog. 1: V1 MAP, V2 volt |
| 1 | `spare` | uint8 |  |  |
| 2-3 | `v1` | uint16 |  |  |
| 4-5 | `v2` | uint16 |  |  |
| 6-7 | `v3` | uint16 |  |  |

**Notes**

- Reply to a 'request calibration data' command. RDAC firmware from 29 Oct 2013.

### 0x241 SP6_HEADING

**SP-6 heading and raw magnetometer**

| | |
|---|---|
| CAN ID | `0x241` (first unit; other units add 0x10 each, up to `0x271`) |
| Direction | SP-6 compass → Host |
| Rate | 50 ms (20 per second) |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `heading` | uint16 | deg (raw × 0.01) | Magnetic heading, 0 to 359.99 |
| 2-6 | `raw_mag_xyz` | 3 x uint12 packed |  | X, Y, Z raw readings, 2048 = zero field, little-endian packed (2-3.5, 3.5-5, 5-6.5) |
| 7 | `slip` | int8 |  | Ball position |

### 0x242 SP6_DEV_1

**SP-6 deviation calibration packet 1 (older firmware)**

| | |
|---|---|
| CAN ID | `0x242` (first unit; other units add 0x10 each, up to `0x272`) |
| Direction | SP-6 compass → Host |
| Rate | — |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `max_ew` | uint16 |  |  |
| 2-3 | `min_ew` | uint16 |  |  |
| 4-5 | `max_ns` | uint16 |  |  |
| 6-7 | `min_ns` | uint16 |  |  |

**Notes**

- Sent only during an active deviation calibration.

### 0x242 SP6_CAL

**SP-6 calibration progress (self-calibrating firmware)**

| | |
|---|---|
| CAN ID | `0x242` (first unit; other units add 0x10 each, up to `0x272`) |
| Direction | SP-6 compass → Host |
| Rate | — |
| Data length | 4 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `samples` | uint16 |  | Unique samples in the calibration buffer |
| 2-3 | `ellipsoid_fit` | uint16 | % | Typical result 96 to 98 % |

**Notes**

- Sent while calibration is active, in addition to message 1.

### 0x243 SP6_DEV_2

**SP-6 deviation calibration packet 2 (older firmware)**

| | |
|---|---|
| CAN ID | `0x243` (first unit; other units add 0x10 each, up to `0x273`) |
| Direction | SP-6 compass → Host |
| Rate | — |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `ew_current` | uint16 |  |  |
| 2-3 | `ns_current` | uint16 |  |  |
| 4-5 | `max_z` | uint16 |  |  |
| 6-7 | `min_z` | uint16 |  |  |

### 0x244 SP6_DEV_3

**SP-6 deviation calibration packet 3 (older firmware)**

| | |
|---|---|
| CAN ID | `0x244` (first unit; other units add 0x10 each, up to `0x274`) |
| Direction | SP-6 compass → Host |
| Rate | — |
| Data length | 2 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `z_current` | uint16 |  |  |

### 0x281 SP7_ACCEL

**SP-7 accelerometer data**

| | |
|---|---|
| CAN ID | `0x281` (first unit; other units add 0x10 each, up to `0x2B1`) |
| Direction | SP-7 / SP-9 AHRS → Host |
| Rate | 50 ms (20 per second) |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `accel_x` | int16 | g (raw × 0.001) |  |
| 2-3 | `accel_y` | int16 | g (raw × 0.001) |  |
| 4-5 | `accel_z` | int16 | g (raw × 0.001) |  |
| 6-7 | `total_g` | int16 | g (raw × 0.001) |  |

### 0x282 SP7_GYRO

**SP-7 gyro rates**

| | |
|---|---|
| CAN ID | `0x282` (first unit; other units add 0x10 each, up to `0x2B2`) |
| Direction | SP-7 / SP-9 AHRS → Host |
| Rate | 50 ms (20 per second) |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `turn_rate` | int16 | deg/min (raw × 0.1) | -60 to 60 |
| 2-3 | `bank_rate` | int16 |  | 360 deg/s = 16384 |
| 4-5 | `pitch_rate` | int16 |  | 360 deg/s = 16384 |
| 6-7 | `yaw_rate` | int16 |  | 360 deg/s = 16384 |

### 0x283 SP7_EULER

**SP-7 Euler attitude**

| | |
|---|---|
| CAN ID | `0x283` (first unit; other units add 0x10 each, up to `0x2B3`) |
| Direction | SP-7 / SP-9 AHRS → Host |
| Rate | 50 ms (20 per second) |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `roll` | int16 | deg (raw × 0.01) | -179.99 to 180.00 |
| 2-3 | `pitch` | int16 | deg (raw × 0.01) | -90.00 to 90.00 |
| 4-5 | `yaw` | int16 | deg (raw × 0.01) | -179.99 to 180.00 |
| 6 | `slip` | int8 |  | -50 to 50 |
| 7 | `flags` | uint8 |  | Bit 0: 1 = gyro mode. Bit 1: over-range. Bits 5-7: 001 = SP-7 |

### 0x2C5 TRANSPONDER_REPLY_ID

**Transponder reply: aircraft ID and altitude**

| | |
|---|---|
| CAN ID | `0x2C5` (first unit; other units add 0x10 each, up to `0x2D5`) |
| Direction | Transponder → Host |
| Rate | 4 s (Replies to the EFIS message; every 4 s if the EFIS message is missing) |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `copy_of_host_5` | same as host message 5 |  | Same layout as TRANSPONDER_ID_ALT |

### 0x2C6 TRANSPONDER_REPLY_STATE

**Transponder reply: squawk and state**

| | |
|---|---|
| CAN ID | `0x2C6` (first unit; other units add 0x10 each, up to `0x2D6`) |
| Direction | Transponder → Host |
| Rate | 4 s |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `copy_of_host_6` | same as host message 6 |  | Same layout as TRANSPONDER_STATE. State bits 3-7 differ: 3 reply seen, 4 failure, 5 no data from transponder, 6 IDENT active, 7 squitter sent in last 0.5 s |

### 0x300 SP10_FLAP

**SP-10 flap status**

| | |
|---|---|
| CAN ID | `0x300` (first unit; other units add 0x10 each, up to `0x330`) |
| Direction | SP-10 flap/trim/motor controller → Host |
| Rate | 1 s (At most 1 s apart; on change, no faster than 0.2 s) |
| Data length | 4 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `function` | uint8 |  | 0 = flaps |
| 1 | `flags` | uint8 |  | Bit 0: motor on. Bits 1-3: last commanded position + 1 |
| 2-3 | `position` | uint16 |  | 0-4095 |

### 0x301 SP10_PITCH_TRIM

**SP-10 pitch trim status**

| | |
|---|---|
| CAN ID | `0x301` (first unit; other units add 0x10 each, up to `0x331`) |
| Direction | SP-10 flap/trim/motor controller → Host |
| Rate | 1 s |
| Data length | 4 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `function` | uint8 |  | 1 = pitch trim |
| 1 | `flags` | uint8 |  |  |
| 2-3 | `position` | uint16 |  | 0-4095 |

### 0x302 SP10_ROLL_TRIM

**SP-10 roll trim status**

| | |
|---|---|
| CAN ID | `0x302` (first unit; other units add 0x10 each, up to `0x332`) |
| Direction | SP-10 flap/trim/motor controller → Host |
| Rate | 1 s |
| Data length | 4 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `function` | uint8 |  | 2 = roll trim |
| 1 | `flags` | uint8 |  |  |
| 2-3 | `position` | uint16 |  | 0-4095 |

### 0x308 SP10_SETUP

**SP-10 setup 1 to 7**

| | |
|---|---|
| CAN ID | `0x308` (first unit; other units add 0x10 each, up to `0x338`) |
| Direction | SP-10 flap/trim/motor controller → Host |
| Rate | — |
| Data length | 3-7 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `setup_number` | uint8 |  | 0 to 6 selects which setup group follows |
| 1-6 | `settings` | varies |  | Flap, pitch and roll positions (0-4095) or motor timeout, direction and force |

**Notes**

- Sent when changed, or on request. The host should request the setup rather than keep its own copy.

### 0x30E SP10_DIAG_A

**SP-10 raw ADC diagnostics (ADC 0-3)**

| | |
|---|---|
| CAN ID | `0x30E` (first unit; other units add 0x10 each, up to `0x33E`) |
| Direction | SP-10 flap/trim/motor controller → Host |
| Rate | — |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `adc_0_3` | 4 x uint16 |  | Raw ADC 0 to 3 |

### 0x30F SP10_DIAG_B

**SP-10 raw ADC diagnostics (ADC 4-7)**

| | |
|---|---|
| CAN ID | `0x30F` (first unit; other units add 0x10 each, up to `0x33F`) |
| Direction | SP-10 flap/trim/motor controller → Host |
| Rate | — |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `adc_4_7` | 4 x uint16 |  | Raw ADC 4 to 7 |

### 0x340 ECB_STATE

**ECB breaker states and voltage**

| | |
|---|---|
| CAN ID | `0x340` (first unit; other units add 0x10 each, up to `0x3B0`) |
| Direction | ECB electronic breakers → Host |
| Rate | 4 s (Every 4 s or when a breaker changes) |
| Data length | 5 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `breaker_on` | uint8 |  | One bit per breaker, LSB = breaker 1 |
| 1 | `breaker_tripped` | uint8 |  | 1 = tripped |
| 2 | `doubling` | uint8 |  | Bits 0-3 paired breakers, 4-5 triples, 6-7 quads |
| 3-4 | `input_voltage` | uint16 | V (raw × 0.1) |  |

### 0x341 ECB_CURRENT

**ECB breaker currents**

| | |
|---|---|
| CAN ID | `0x341` (first unit; other units add 0x10 each, up to `0x3B1`) |
| Direction | ECB electronic breakers → Host |
| Rate | 4 s (0.5 to 4 s depending on changes) |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `current_1_8` | 8 x uint8 | A (raw × 0.1) | Breakers 1 to 8. Add the currents of doubled breakers |

### 0x342 ECB_TRIP_1_4

**ECB trip current, breakers 1 to 4**

| | |
|---|---|
| CAN ID | `0x342` (first unit; other units add 0x10 each, up to `0x3B2`) |
| Direction | ECB electronic breakers → Host |
| Rate | — |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `trip_1_4` | 4 x uint16 | A (raw × 0.1) |  |

**Notes**

- Sent in reply to a request for settings.

### 0x343 ECB_TRIP_5_8

**ECB trip current, breakers 5 to 8**

| | |
|---|---|
| CAN ID | `0x343` (first unit; other units add 0x10 each, up to `0x3B3`) |
| Direction | ECB electronic breakers → Host |
| Rate | — |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `trip_5_8` | 4 x uint16 | A (raw × 0.1) |  |

### 0x344 ECB_TRIP_TIME

**ECB trip response times**

| | |
|---|---|
| CAN ID | `0x344` (first unit; other units add 0x10 each, up to `0x3B4`) |
| Direction | ECB electronic breakers → Host |
| Rate | — |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `trip_time_1_8` | 8 x uint8 | s (raw × 0.1) |  |

### 0x345 ECB_WIGWAG

**ECB DIP switches and WIGWAG**

| | |
|---|---|
| CAN ID | `0x345` (first unit; other units add 0x10 each, up to `0x3B5`) |
| Direction | ECB electronic breakers → Host |
| Rate | — |
| Data length | 4 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `dip_switch` | uint8 |  |  |
| 1 | `wigwag_even` | uint8 |  |  |
| 2 | `wigwag_odd` | uint8 |  |  |
| 3 | `wigwag_time` | uint8 | s (raw × 0.1) |  |

### 0x3C0 TOUCHPAD_GESTURE

**Touch pad gesture**

| | |
|---|---|
| CAN ID | `0x3C0` (first unit; other units add 0x10 each, up to `0x3F0`) |
| Direction | Touch pad → Host |
| Rate | — |
| Data length | 1 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0 | `gesture` | uint8 |  | 0x10 tap, 0x20 double tap, 0x31/41/51/61 swipe up/right/down/left, 0x11 tap and hold, 0x32/42/52/62 swipe and hold |

### 0x3C0 TOUCHPAD_XY

**Touch pad position**

| | |
|---|---|
| CAN ID | `0x3C0` (first unit; other units add 0x10 each, up to `0x3F0`) |
| Direction | Touch pad → Host |
| Rate | — |
| Data length | 4 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `x` | uint16 |  |  |
| 2-3 | `y` | uint16 |  |  |

**Notes**

- Axis direction and orientation depend on mounting; the EFIS has a calibration for it.

### 0x400 EXT_ANALOG_1_4

**Extender analog inputs 1 to 4**

| | |
|---|---|
| CAN ID | `0x400` (first unit; other units add 0x10 each, up to `0x430`) |
| Direction | iEFIS extender → Host |
| Rate | 125 ms (125 ms if changed, otherwise every 1 s) |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `analog_1_4` | 4 x uint16 |  | 0-4095 |

### 0x401 EXT_ANALOG_5_8

**Extender analog inputs 5 to 8**

| | |
|---|---|
| CAN ID | `0x401` (first unit; other units add 0x10 each, up to `0x431`) |
| Direction | iEFIS extender → Host |
| Rate | 125 ms |
| Data length | 8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `analog_5_8` | 4 x uint16 |  | 0-4095 |

### 0x402 EXT_STATUS

**Extender status**

| | |
|---|---|
| CAN ID | `0x402` (first unit; other units add 0x10 each, up to `0x432`) |
| Direction | iEFIS extender → Host |
| Rate | 125 ms |
| Data length | 6 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-1 | `pressure` | uint16 |  | Raw ADC |
| 2 | `supply_voltage` | uint8 | V (raw × 0.1) | 5.0 to 30.5 V (byte is 0 to 255, offset 5.0 V) |
| 3 | `ports_ready` | uint8 |  | Bits 0-4: RS232 ports 1-5 initialised |
| 4 | `outputs` | uint8 |  | Bits 0-4: outputs 1-5 (1 = on) |
| 5 | `temperature` | uint8 | degC | Degrees C + 128 |

### 0x403 EXT_RS232_RX1

**Extender RS232 received data, ports 1 to 5 (types 3 to 7)**

| | |
|---|---|
| CAN ID | `0x403` (first unit; other units add 0x10 each, up to `0x433`) |
| Direction | iEFIS extender → Host |
| Rate | — |
| Data length | 1-8 bytes |

| Bytes | Field | Type | Unit | Description |
|:-----:|-------|------|------|-------------|
| 0-7 | `data` | bytes |  | 1 to 8 received bytes; type 3 is port 1 and type 7 is port 5 |
