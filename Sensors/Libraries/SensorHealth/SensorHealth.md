# SensorHealth

Header-only helper that turns a stream of good/bad samples into a valid/invalid decision.
Version constant: `SENSOR_HEALTH_VERSION`.

```
SensorHealth gyro(20, 5, 100);        // invalid after 20 bad in a row, valid after 5 good, stuck after 100 identical
bool changed = gyro.update(readOk);   // true when valid <-> invalid just changed
if (gyro.retryDue(2000)) { gyro.lastRetry = millis(); /* re-initialise the chip */ }
```

- `update(ok)` counts a sample; returns true on a change of validity.
- `markInvalid()` declares it invalid at once (not found at start-up).
- `stuck(last, now, n)` is true after `stuckCount` calls with identical values (a locked-up chip).
- `retryDue(ms)`, `outageMs()`, `valid()`.
- Public counters for the `?` report: `failures`, `attempts`, `recoveries`, `lastAttempt`.

Thresholds are per sensor: at 100 Hz, 20 bad samples = 0.2 s. Sensors that are sampled more slowly (the
AIR RTD at 4 Hz) pass smaller counts.
