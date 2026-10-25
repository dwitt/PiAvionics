"""Module heartbeat tracking and data validity for the EFIS server.

The server tracks the facts; the web page decides what to show and how (wording, colours,
flashing, how long a caution stays up, when a gyro-only heading is blanked). This module never
produces display text.

Pure Python (standard library only) so it can be tested on any computer.
See EFIS/docs/MODULE_STATUS_PLAN.md and Protocol/can_messages.yaml (protocol 0.2.0).

How it is used (aio_server.py):
    tracker = StatusTracker(time.monotonic())
    tracker.on_heartbeat(can_id, data_bytes, now)          # for CAN IDs 0x701 to 0x704
    facts = tracker.evaluate(now, last_received_times)     # about every 0.1 s

`last_received_times` maps a CAN ID to the time (time.monotonic()) its last message arrived.
Everything takes `now` as a parameter so the tests can drive time.
"""

VERSION = "2.0.1"

# -----------------------------------------------------------------------------
# CAN IDs and the module numbers used by the heartbeats (0x700 + module number)
# -----------------------------------------------------------------------------
HEARTBEAT_ID_BASE = 0x700
MODULE_IMU, MODULE_AIR, MODULE_GPS, MODULE_BACKUP = 1, 2, 3, 4
MODULE_NAMES = {MODULE_IMU: "imu", MODULE_AIR: "air", MODULE_GPS: "gps", MODULE_BACKUP: "backup"}
FLIGHT_MODULES = ("imu", "air", "gps")     # the modules whose health decides the EFIS system state; the backup display does not

ID_AIR_DATA = 0x28      # airspeed, altitude, vertical speed
ID_OAT = 0x2A
ID_RAW_PRESSURE = 0x2B  # static and differential pressure
ID_AHRS = 0x48          # heading, pitch, roll, turn rate
ID_ACCEL = 0x49
ID_GPS_POSITION = 0x63
ID_GPS_SPEED = 0x64
ID_GPS_STATUS = 0x65

# Heartbeat states
STATE_STARTING, STATE_OK, STATE_DEGRADED, STATE_FAILED = 0, 1, 2, 3

# Sensors mask bits, per module (Protocol/can_messages.yaml), and the names used in the JSON
IMU_ISM330 = 0x01       # accelerometer and gyro
IMU_MAG = 0x02          # magnetometer
IMU_CAL = 0x04          # calibration loaded
AIR_PITOT = 0x01
AIR_AOA = 0x02
AIR_STATIC = 0x04
AIR_OAT = 0x08
GPS_NMEA = 0x01
GPS_FIX = 0x02
BACKUP_LCD = 0x01
BACKUP_ENCODER = 0x02

SENSOR_BITS = {
    MODULE_IMU: (("gyro_accel", IMU_ISM330), ("magnetometer", IMU_MAG), ("calibration", IMU_CAL)),
    MODULE_AIR: (("pitot", AIR_PITOT), ("aoa", AIR_AOA), ("static", AIR_STATIC), ("oat", AIR_OAT)),
    MODULE_GPS: (("nmea", GPS_NMEA), ("fix", GPS_FIX)),
    MODULE_BACKUP: (("lcd", BACKUP_LCD), ("encoder", BACKUP_ENCODER)),
}

# -----------------------------------------------------------------------------
# Timeouts (seconds without a message before the data is treated as lost).
# Decided 2026-10-17, see MODULE_STATUS_PLAN.md decision 8.
# -----------------------------------------------------------------------------
DATA_TIMEOUTS = {
    ID_AIR_DATA: 0.5,
    ID_AHRS: 0.5,
    ID_ACCEL: 0.5,
    ID_OAT: 1.5,
    ID_RAW_PRESSURE: 1.5,
    ID_GPS_POSITION: 3.5,
    ID_GPS_SPEED: 3.5,
    ID_GPS_STATUS: 3.5,
}
HEARTBEAT_TIMEOUT = 3.0     # a module is "missing" after this long without a heartbeat
STARTUP_GRACE = 5.0         # a module never heard from is only "missing" this long after the server started


class ModuleInfo:
    """What the EFIS knows about one module from its heartbeats."""

    def __init__(self, number):
        self.number = number
        self.name = MODULE_NAMES[number]
        self.version = None         # (major, minor, patch)
        self.state = None
        self.mask = None
        self.uptime = None
        self.last_heard = None      # time of the last heartbeat
        self.restarted_at = None    # time an uptime drop was seen
        self.mag_lost_since = None  # IMU only: time the magnetometer bit went clear


class StatusTracker:
    """Keeps the heartbeat state of the IMU, AIR and GPS modules and judges the data."""

    def __init__(self, start_time):
        self.start = start_time
        self.modules = {n: ModuleInfo(n) for n in MODULE_NAMES}

    # -------------------------------------------------------------------------
    # Heartbeats
    # -------------------------------------------------------------------------
    def on_heartbeat(self, can_id, payload, now):
        """Record a heartbeat frame. Returns True if it was a valid heartbeat."""
        number = can_id - HEARTBEAT_ID_BASE
        module = self.modules.get(number)
        if module is None or len(payload) < 8 or payload[0] != number:
            return False
        uptime = payload[6] | (payload[7] << 8)
        if module.uptime is not None and uptime < module.uptime:
            module.restarted_at = now
        module.version = (payload[1], payload[2], payload[3])
        module.state = payload[4]
        module.mask = payload[5]
        module.uptime = uptime
        module.last_heard = now

        if number == MODULE_IMU:
            if module.state != STATE_STARTING and not (module.mask & IMU_MAG):
                if module.mag_lost_since is None:
                    module.mag_lost_since = now
            else:
                module.mag_lost_since = None
        return True

    # -------------------------------------------------------------------------
    # Helpers
    # -------------------------------------------------------------------------
    def _present(self, number, now):
        m = self.modules[number]
        return m.last_heard is not None and (now - m.last_heard) <= HEARTBEAT_TIMEOUT

    def _missing(self, number, now):
        """True if the module should be shown as missing: it was heard and went quiet, or it
        has never been heard and the start-up grace period is over."""
        m = self.modules[number]
        if self._present(number, now):
            return False
        return m.last_heard is not None or (now - self.start) > STARTUP_GRACE

    def _mask_known(self, number, now):
        m = self.modules[number]
        return self._present(number, now) and m.state != STATE_STARTING and m.mask is not None

    def _bit(self, number, bit, now):
        """True if the module's sensors mask says the sensor is working. With no usable
        heartbeat (never heard, missing, or still starting) there is no information, so True:
        the freshness of the data decides."""
        if not self._mask_known(number, now):
            return True
        return bool(self.modules[number].mask & bit)

    @staticmethod
    def _fresh(can_id, now, last_seen):
        t = last_seen.get(can_id)
        return t is not None and (now - t) <= DATA_TIMEOUTS[can_id]

    def _lost(self, can_id, now, last_seen):
        """Data is lost: not fresh, and either seen before or the start-up grace is over."""
        if self._fresh(can_id, now, last_seen):
            return False
        return can_id in last_seen or (now - self.start) > STARTUP_GRACE

    # -------------------------------------------------------------------------
    # The facts
    # -------------------------------------------------------------------------
    def evaluate(self, now, last_seen):
        """Return the facts as a dict of plain Python types, ready for json:

        modules        per module (imu, air, gps, backup): present, missing, state, version, uptime, age (s since
                       the last heartbeat), restarted_age (s since an uptime drop, or None), and
                       sensors (named booleans from the mask, or None when the mask is unknown)
        valid          per instrument: True if the value can be trusted
        data_lost      per message group: True if it has stopped arriving (after the start-up grace)
        heading        valid, suspect (gyro-only heading) and gyro_seconds
        system_state   1 when all three modules are present and OK, else 2 (for the EFIS heartbeat)
        modules_ok_mask  bit 0 AIR, bit 1 IMU, bit 2 GPS present and OK (for the EFIS heartbeat)
        """
        fresh = lambda i: self._fresh(i, now, last_seen)
        bit = lambda mod, b: self._bit(mod, b, now)
        lost = lambda i: self._lost(i, now, last_seen)

        valid = {
            "airspeed": fresh(ID_AIR_DATA) and bit(MODULE_AIR, AIR_PITOT),
            "altitude": fresh(ID_AIR_DATA) and bit(MODULE_AIR, AIR_STATIC),
            "vsi": fresh(ID_AIR_DATA) and bit(MODULE_AIR, AIR_STATIC),
            "static_pressure": fresh(ID_RAW_PRESSURE) and bit(MODULE_AIR, AIR_STATIC),
            "differential_pressure": fresh(ID_RAW_PRESSURE) and bit(MODULE_AIR, AIR_PITOT),
            "oat": fresh(ID_OAT) and bit(MODULE_AIR, AIR_OAT),
            "attitude": fresh(ID_AHRS) and bit(MODULE_IMU, IMU_ISM330),
            "heading": fresh(ID_AHRS) and bit(MODULE_IMU, IMU_ISM330),
            "turn_rate": fresh(ID_AHRS) and bit(MODULE_IMU, IMU_ISM330),
            "acceleration": fresh(ID_ACCEL) and bit(MODULE_IMU, IMU_ISM330),
            "gps_position": (fresh(ID_GPS_POSITION) and fresh(ID_GPS_SPEED)
                             and bit(MODULE_GPS, GPS_NMEA)),
        }

        data_lost = {
            "air_data": lost(ID_AIR_DATA),
            "raw_pressure": lost(ID_RAW_PRESSURE),
            "oat": lost(ID_OAT),
            "attitude": lost(ID_AHRS),
            "acceleration": lost(ID_ACCEL),
            "gps_position": lost(ID_GPS_POSITION) or lost(ID_GPS_SPEED),
            "gps_status": lost(ID_GPS_STATUS),
        }

        # Heading with the magnetometer lost: the IMU still sends a gyro-only heading.
        imu = self.modules[MODULE_IMU]
        suspect = (valid["heading"] and self._present(MODULE_IMU, now)
                   and imu.mag_lost_since is not None)
        heading = {
            "valid": valid["heading"],
            "suspect": suspect,
            "gyro_seconds": round(now - imu.mag_lost_since, 1) if suspect else None,
        }

        modules = {}
        for n, m in self.modules.items():
            present = self._present(n, now)
            known = self._mask_known(n, now)
            modules[m.name] = {
                "present": present,
                "missing": self._missing(n, now),
                "state": m.state if present else None,
                "version": ".".join(str(v) for v in m.version) if m.version else None,
                "uptime": m.uptime,
                "age": round(now - m.last_heard, 1) if m.last_heard is not None else None,
                "restarted_age": (round(now - m.restarted_at, 1)
                                  if m.restarted_at is not None else None),
                "sensors": ({name: bool(m.mask & b) for name, b in SENSOR_BITS[n]}
                            if known else None),
            }

        # For the EFIS heartbeat (0x700)
        ok = {name: modules[name]["present"] and modules[name]["state"] == STATE_OK
              for name in FLIGHT_MODULES}
        return {
            "modules": modules,
            "valid": valid,
            "data_lost": data_lost,
            "heading": heading,
            "system_state": STATE_OK if all(ok.values()) else STATE_DEGRADED,
            "modules_ok_mask": (1 if ok["air"] else 0) | (2 if ok["imu"] else 0) | (4 if ok["gps"] else 0),
        }
