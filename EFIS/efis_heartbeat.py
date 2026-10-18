"""The EFIS heartbeat (CAN 0x700): what the server tells the bus about itself once a second.

Standard library only, so it can be tested anywhere. Same layout as the module heartbeats
(Protocol/can_messages.yaml 0x700, EFIS/docs/SERVER_STEP_B_PLAN.md):
    byte 0     module number, 0 for the EFIS
    bytes 1-3  the EFIS software version (major, minor, patch)
    byte 4     state: 1 all well, 2 a module is degraded, failed or missing, 3 no display
    byte 5     sensors mask: bit 0 AIR, bit 1 IMU, bit 2 GPS seen healthy, bit 3 a display page is connected
    bytes 6-7  uptime in seconds since the server started (wraps at 65535, like the modules)

How it is used (aio_server.py):
    heartbeat = EfisHeartbeat(VERSION, time.monotonic())
    can_id, payload, facts = heartbeat.frame(modules_ok_mask, controller_clients, time.monotonic())
`facts` is the same information as a dict for the JSON the pages receive (the Modules page's EFIS row).
"""

VERSION = "1.0.0"

EFIS_HEARTBEAT_ID = 0x700
EFIS_MODULE_NUMBER = 0

STATE_OK, STATE_DEGRADED, STATE_NO_DISPLAY = 1, 2, 3

MASK_AIR, MASK_IMU, MASK_GPS, MASK_DISPLAY = 0x01, 0x02, 0x04, 0x08
MODULES_MASK = MASK_AIR | MASK_IMU | MASK_GPS

NO_DISPLAY_SECONDS = 30     # state 3 when no display page has been connected for this long (and at start-up for this long)


def parse_version(text):
    """'1.6.0' gives (1, 6, 0). Missing or odd parts count as 0 and every part is limited to one byte."""
    parts = []
    for piece in str(text).split(".")[:3]:
        try:
            parts.append(max(0, min(255, int(piece))))
        except ValueError:
            parts.append(0)
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts)


def pack(version, state, mask, uptime):
    """The 8 data bytes of the heartbeat."""
    uptime = int(uptime) & 0xFFFF
    return bytes([EFIS_MODULE_NUMBER, version[0], version[1], version[2], state, mask & 0xFF,
                  uptime & 0xFF, uptime >> 8])


class EfisHeartbeat:
    """Works out the heartbeat's state and mask and builds the frame."""

    def __init__(self, version_text, start_time):
        self.version = parse_version(version_text)
        self.version_text = ".".join(str(v) for v in self.version)
        self.start = start_time
        self.last_display_seen = start_time     # a display is assumed present for the first NO_DISPLAY_SECONDS

    def decide(self, modules_ok_mask, controller_clients, now):
        """(state, mask). `modules_ok_mask` is module_status's bit 0 AIR, bit 1 IMU, bit 2 GPS present and OK;
        `controller_clients` is the number of connected controller pages (viewers do not count)."""
        mask = modules_ok_mask & MODULES_MASK
        if controller_clients > 0:
            mask |= MASK_DISPLAY
            self.last_display_seen = now
        if controller_clients == 0 and (now - self.last_display_seen) > NO_DISPLAY_SECONDS:
            state = STATE_NO_DISPLAY
        elif (modules_ok_mask & MODULES_MASK) == MODULES_MASK:
            state = STATE_OK
        else:
            state = STATE_DEGRADED
        return state, mask

    def frame(self, modules_ok_mask, controller_clients, now):
        """(CAN id, 8 data bytes, facts dict for the JSON)."""
        state, mask = self.decide(modules_ok_mask, controller_clients, now)
        uptime = int(now - self.start) & 0xFFFF
        facts = {
            "version": self.version_text,
            "state": state,
            "mask": mask,
            "uptime": uptime,
            "display_clients": controller_clients,
        }
        return EFIS_HEARTBEAT_ID, pack(self.version, state, mask, uptime), facts
