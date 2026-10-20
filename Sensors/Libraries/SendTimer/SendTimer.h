// SendTimer.h - when to send a periodic CAN message, with a flat +/- 20 ms of jitter.
//
// Each message is sent every PERIOD +/- JITTER_MS: the wait is drawn once, when the message is sent, as
// (PERIOD - 20) + random(41) milliseconds, so the mean is PERIOD. (The old code drew random(50) again on every pass
// of loop(), so the frame left on the first pass after the period and the jitter was only 1 to 3 ms.)
// The same helper draws the QNH silence timer (EFIS/docs/QNH_PLAN.md).
//
//   SendTimer airTimer(100);                  // 80 to 120 ms
//   if (airTimer.due()) { ...send...; airTimer.sent(); }
//
// due() only asks; sent() restarts the wait. A message that is sometimes not sent (a condition fails) calls sent()
// only when it really went out.

#ifndef SEND_TIMER_H
#define SEND_TIMER_H

#include <Arduino.h>

#define SEND_TIMER_VERSION "1.0.0"

#define SEND_TIMER_JITTER_MS 20     // +/- this much about the period

class SendTimer {
public:
    // periodMs must be more than SEND_TIMER_JITTER_MS
    explicit SendTimer(uint32_t periodMs) : _period(periodMs), _interval(periodMs), _last(0) {}

    // true when the drawn wait has passed since the last send
    bool due() const { return (uint32_t)(millis() - _last) >= _interval; }

    // call when the message went out: restart the wait and draw the next one
    void sent() {
        _last = millis();
        _interval = nextInterval(_period);
    }

    // one wait in milliseconds, PERIOD - 20 to PERIOD + 20 (also used for other timers that need the same spread)
    static uint32_t nextInterval(uint32_t periodMs) {
        return periodMs - SEND_TIMER_JITTER_MS + (uint32_t)random(2 * SEND_TIMER_JITTER_MS + 1);
    }

private:
    uint32_t _period;
    uint32_t _interval;
    uint32_t _last;
};

#endif
