# CAN bus map: PiAvionics and MGL on one bus

> **Generated file. Do not edit.** Edit `Protocol/can_messages.yaml` and `Protocol/mgl_can_messages.yaml` and run `Protocol/.venv/bin/python Protocol/generate_can_docs.py`.

Both systems run at 250 kbit/s with standard 11-bit IDs, so they can share a bus electrically. This page shows where the IDs land and whether anything collides.

## Where PiAvionics IDs fall in MGL's scheme

MGL splits an ID into a device address (upper 7 bits) and a message type (lower 4 bits). Addresses 1 to 15 are MGL host addresses: only address 1 is in use today, and the rest are reserved for future hosts.

| PiAvionics ID | Name | MGL address | MGL type | What MGL says about it |
|:-------------:|------|:-----------:|:--------:|------------------------|
| `0x019` | TIME_SYNC | 1 | 9 | ⛔ **Collision**: host message GENERAL_COMMAND |
| `0x023` | AHRS_CONTROL | 2 | 3 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x028` | AIR_DATA | 2 | 8 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x029` | AOA | 2 | 9 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x02A` | OAT | 2 | 10 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x02B` | STATIC_PRESSURE | 2 | 11 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x02C` | QNH_SPARE_2C | 2 | 12 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x02D` | QNH_BACKUP | 2 | 13 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x02E` | QNH | 2 | 14 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x02F` | QNH_SPARE_2F | 2 | 15 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x032` | EMS_RPM | 3 | 2 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x048` | AHRS_ORIENT | 4 | 8 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x049` | AHRS_ACCEL | 4 | 9 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x051` | EMS_OIL | 5 | 1 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x052` | EMS_EGT_CHT_12 | 5 | 2 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x053` | EMS_EGT_CHT_34 | 5 | 3 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x063` | GPS_POSITION | 6 | 3 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x064` | GPS_SPEED_TRACK | 6 | 4 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x065` | GPS_STATUS | 6 | 5 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x081` | MAG_X | 8 | 1 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x082` | MAG_Y | 8 | 2 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x083` | MAG_Z | 8 | 3 | ⚠️ Reserved: Host / EFIS address space, no current use |
| `0x700` | EFIS_HEARTBEAT | 112 | 0 | ✅ Not used by MGL |
| `0x701` | IMU_HEARTBEAT | 112 | 1 | ✅ Not used by MGL |
| `0x702` | AIR_HEARTBEAT | 112 | 2 | ✅ Not used by MGL |
| `0x703` | GPS_HEARTBEAT | 112 | 3 | ✅ Not used by MGL |

## Findings

- ⛔ `0x019` TIME_SYNC uses the ID MGL reserves for host message GENERAL_COMMAND. See the note on `0x019` in the PiAvionics document.
- ⚠️ 21 PiAvionics messages (`0x023`, `0x028`, `0x029`, `0x02A`, `0x02B`, `0x02C`, `0x02D`, `0x02E`, `0x02F`, `0x032`, `0x048`, `0x049`, `0x051`, `0x052`, `0x053`, `0x063`, `0x064`, `0x065`, `0x081`, `0x082`, `0x083`) sit in MGL's reserved host address space (addresses 2 to 15). No current MGL device uses them, but MGL may in future.
- ✅ No PiAvionics message overlaps an MGL **device** block (RDAC, compass, AHRS and the rest).
- ℹ️ Lower IDs win arbitration, so every PiAvionics ID (0x19 to 0x83) outranks all MGL device data (0x100 and up). That is harmless at this bus load.

## Address space

| CAN IDs | Addresses | Owner |
|:-------:|:---------:|-------|
| `0x000`–`0x00F` | 0 | Reserved by MGL |
| `0x010`–`0x0FF` | 1–15 | Host / EFIS |
| `0x100`–`0x1FF` | 16–31 | CAN servos |
| `0x200`–`0x23F` | 32–35 | RDAC XF engine unit |
| `0x240`–`0x27F` | 36–39 | SP-6 compass |
| `0x280`–`0x2BF` | 40–43 | SP-7 / SP-9 AHRS |
| `0x2C0`–`0x2DF` | 44–45 | Transponder |
| `0x2E0`–`0x2FF` | 46–47 | Reserved by MGL |
| `0x300`–`0x33F` | 48–51 | SP-10 flap/trim/motor controller |
| `0x340`–`0x3BF` | 52–59 | ECB electronic breakers |
| `0x3C0`–`0x3FF` | 60–63 | Touch pad |
| `0x400`–`0x43F` | 64–67 | iEFIS extender |
| `0x440`–`0x45F` | 68–69 | External MGL autopilot |
| `0x460`–`0x7FF` | 70–127 | **Free.** Not used by MGL |

PiAvionics IDs currently in use: `0x019`, `0x023`, `0x028`, `0x029`, `0x02A`, `0x02B`, `0x02C`, `0x02D`, `0x02E`, `0x02F`, `0x032`, `0x048`, `0x049`, `0x051`, `0x052`, `0x053`, `0x063`, `0x064`, `0x065`, `0x081`, `0x082`, `0x083`, `0x700`, `0x701`, `0x702`, `0x703`.

## Estimated bus load

Worst case (every frame at maximum bit stuffing) at 250 kbit/s. Only messages that are sent today (Active or Partial) are counted for PiAvionics. The MGL rows are one unit of each device.

| Source | Frames/s | Bus load |
|--------|---------:|---------:|
| PiAvionics: AIR module | 26.0 | 1.4 % |
| PiAvionics: IMU / AHRS module | 21.0 | 1.1 % |
| PiAvionics: GPS module | 5.0 | 0.3 % |
| PiAvionics: Pi EFIS | 2.0 | 0.1 % |
| PiAvionics: Backup display | 1.0 | 0.1 % |
| **PiAvionics total** | **55.0** | **3.0 %** |
| MGL: RDAC XF engine unit | 17.2 | 0.9 % |
| MGL: SP-6 compass | 20.0 | 1.1 % |
| MGL: SP-7 / SP-9 AHRS | 60.0 | 3.2 % |
| MGL: Transponder | 0.5 | 0.0 % |
| MGL: SP-10 flap/trim/motor controller | 3.0 | 0.1 % |
| MGL: ECB electronic breakers | 0.5 | 0.0 % |
| MGL: iEFIS extender | 24.0 | 1.2 % |
| **All of the above on one bus** | **180.2** | **9.6 %** |

A healthy CAN bus stays below about 30 to 40 % load.
