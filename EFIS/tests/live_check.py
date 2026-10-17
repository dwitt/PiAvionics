"""Read-only live check on the Pi: listens on can0 with the real server code and prints what the
EFIS would send (modules, validity, annunciations) every second. Does not start the web server and
sends nothing on the bus, so it can run next to the live EFIS service.

    ~/EFIS/venv/bin/python live_check.py [seconds]      (run from a folder that holds aio_server.py)
"""
VERSION = "1.0.0"

import asyncio
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import can
import aio_server as srv


async def main(seconds):
    bus = can.Bus(interface="socketcan", channel="can0", bitrate=250000)
    reader = can.AsyncBufferedReader()
    notifier = can.Notifier(bus, [reader], timeout=0.1, loop=asyncio.get_event_loop())
    data = srv.AvionicsData()
    times = {}
    tasks = [asyncio.create_task(srv.process_can_messages(reader, data, times)),
             asyncio.create_task(srv.monitor_status(data, times))]
    start = time.monotonic()
    while time.monotonic() - start < seconds:
        await asyncio.sleep(1.0)
        if data.valid is None:
            continue
        mods = " ".join(
            f"{n}:{'MISSING' if m['missing'] else ('s' + str(m['state']) + ' v' + str(m['version']) if m['present'] else '?')}"
            for n, m in data.modules.items())
        bad = [k for k, v in data.valid.items() if not v]
        lost = [k for k, v in data.data_lost.items() if v]
        sensors = {n: [k for k, v in m['sensors'].items() if not v] for n, m in data.modules.items() if m['sensors']}
        sensors = {n: v for n, v in sensors.items() if v}
        hs = data.heading_status
        print(f"{time.monotonic() - start:5.1f}s  {mods:38s} invalid={bad or '-'}  "
              f"sensors_lost={sensors or '-'} data_lost={lost or '-'}  gyro_hdg={'%.0fs' % hs['gyro_seconds'] if hs['suspect'] else '-'}"
              f"  asi={data.airspeed} alt={data.altitude} pitch={data.pitch} yaw={data.yaw}", flush=True)
    for t in tasks:
        t.cancel()
    notifier.stop()
    bus.shutdown()
    # the full JSON the display would get (without the internal fields)
    d = {k: v for k, v in data.__dict__.items() if not k.startswith("_")}
    print("JSON bytes:", len(json.dumps(d)))


if __name__ == "__main__":
    asyncio.run(main(float(sys.argv[1]) if len(sys.argv) > 1 else 10))
