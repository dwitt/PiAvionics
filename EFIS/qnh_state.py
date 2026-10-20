"""The QNH (altimeter setting) shared between all devices on the CAN bus (EFIS/docs/QNH_PLAN.md).

No CAN and no websocket in here, so every rule can be tested alone. The server feeds it what it hears and does the sending.

Rules (see Protocol/can_messages.yaml, ID block 0x2C to 0x2F):
  1. Change: a person changes the QNH, the new value is sent at once (at most one frame per 20 ms).
  2. Hear: a valid frame from any ID in the block is adopted. The last frame heard wins.
  3. Speak on silence: a held, set value is sent after 1000 ms +/- 20 ms without a QNH frame.
  4. Never invent: the 29.92 of "not set" is never sent.
  5. Silence is not a fault.
  6. The AIR module reports the QNH it uses; a set value that differs from it for more than 2 s is a mismatch.
Nothing is stored. After a restart the value comes from the bus, or is 29.92 flagged not set.
"""

import random

VERSION = "1.0.1"

QNH_MIN = 2800                 # inHg x 100 (28.00)
QNH_MAX = 3100                 # inHg x 100 (31.00)
QNH_DEFAULT = 2992             # inHg x 100, the value of "not set"
INHG_TO_HPA = 1 / 2.95299875   # hPa per (inHg x 100)
ID_FIRST = 0x2C                # the block of sender IDs
ID_LAST = 0x2F
LISTEN_S = 2.0                 # at start, wait this long for a frame before settling on 29.92 not set
SILENCE_S = 1.0                # a set value is sent after this much silence...
SILENCE_JITTER_S = 0.020       # ...plus or minus this, drawn once for each silent period
MIN_SEND_GAP_S = 0.020         # guard: never more than 50 frames a second
MISMATCH_AFTER_S = 2.0         # AIR's value must disagree this long before it is flagged
AIR_REPORT_MAX_AGE_S = 1.5     # an AIR report older than this is not used (the 0x2B timeout)
AIR_NOT_SET_FLAG = 0x01        # 0x2B byte 7 bit 0
INVALID_16 = 0x7FFF


def valid_frame(can_id, qnh_hpa, qnh_x4):
    """True if a received QNH frame passes the receiver checks: ID in the block, 28.00 to 31.00 inHg, and the
    hPa field agrees with the inHg field to within 1 hPa. Anything else is ignored."""
    if not ID_FIRST <= can_id <= ID_LAST:
        return False
    if not QNH_MIN * 4 <= qnh_x4 <= QNH_MAX * 4:
        return False
    return abs(qnh_x4 / 4 * INHG_TO_HPA - qnh_hpa) <= 1.0


class QnhState:
    def __init__(self, now, rng=random.random):
        """now: time.monotonic() at start. rng: a function giving a float 0 to 1 (a test can replace it)."""
        self._rng = rng
        self.value = QNH_DEFAULT          # inHg x 100
        self.is_set = False               # False = 29.92 "not set"
        self._listen_until = now + LISTEN_S
        self._last_activity = now         # last QNH frame heard or sent
        self._delay = self._draw_delay()
        self._last_send = None
        self._pending = False             # a changed value waiting to go out (the 20 ms guard)
        self._page_reported = None        # the value the controller page reported last
        # the AIR module's report (0x2B bytes 5-7)
        self._air_value = None            # None = it sent the invalid marker
        self._air_not_set = False
        self._air_time = None
        self._mismatch_since = None

    def _draw_delay(self):
        return SILENCE_S - SILENCE_JITTER_S + self._rng() * 2 * SILENCE_JITTER_S

    def _activity(self, now):
        self._last_activity = now
        self._delay = self._draw_delay()

    # --- from the bus -------------------------------------------------------

    def on_can(self, can_id, qnh_hpa, qnh_x4, now):
        """A QNH frame from the bus. Returns the new value (inHg x 100) if it was adopted, else None."""
        if not valid_frame(can_id, qnh_hpa, qnh_x4):
            return None
        self.value = qnh_x4 // 4
        self.is_set = True
        self._pending = False             # whatever was waiting is older than this frame
        self._activity(now)
        return self.value

    def on_air_report(self, qnh_in_use, flags, now):
        """The AIR module's report from 0x2B bytes 5-6 (int16, 0x7FFF = invalid) and byte 7."""
        self._air_value = None if qnh_in_use == INVALID_16 else qnh_in_use
        self._air_not_set = bool(flags & AIR_NOT_SET_FLAG)
        self._air_time = now

    # --- from the page ------------------------------------------------------

    def on_user(self, qnh, now):
        """The controller page reported its QNH (inHg x 100). It is a change by a person only when it differs from the
        previous value the page reported, so a page repeating an old value cannot override a value that came from
        the bus. Before the page has reported anything it is compared with the value held here (the page sends the
        QNH only when it changes). Returns True if it was a change."""
        previous = self.value if self._page_reported is None else self._page_reported
        self._page_reported = qnh
        if qnh == previous:
            return False
        if not QNH_MIN <= qnh <= QNH_MAX:
            return False
        self.value = qnh
        self.is_set = True
        self._pending = True
        return True

    # --- sending ------------------------------------------------------------

    def due(self, now):
        """The value to send now (inHg x 100), or None. Call often (about every 20 ms) and after on_user."""
        if self._last_send is not None and now - self._last_send < MIN_SEND_GAP_S:
            return None
        if self._pending:
            return self._sent(now)
        if (self.is_set and now >= self._listen_until
                and now - self._last_activity >= self._delay):
            return self._sent(now)
        return None

    def _sent(self, now):
        self._pending = False
        self._last_send = now
        self._activity(now)
        return self.value

    # --- facts for the pages ------------------------------------------------

    def mismatch(self, now):
        """True if a set value differs from what the AIR module says it uses, for more than MISMATCH_AFTER_S."""
        air_known = (self._air_time is not None and now - self._air_time <= AIR_REPORT_MAX_AGE_S
                     and (self._air_value is not None or self._air_not_set))
        differs = False
        if self.is_set and air_known:
            differs = self._air_not_set or self._air_value != self.value
        if not differs:
            self._mismatch_since = None
            return False
        if self._mismatch_since is None:
            self._mismatch_since = now
        return now - self._mismatch_since > MISMATCH_AFTER_S
