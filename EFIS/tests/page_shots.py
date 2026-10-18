"""Take a screenshot of the EFIS page for each fake-feed scenario with a headless Chrome.

Start the fake feed first (tests/fake_feed.py), then, with the same venv (needs aiohttp):
    venv/bin/python tests/page_shots.py OUT_DIR [scenario ...]            (all scenarios when none are named)
    venv/bin/python tests/page_shots.py OUT_DIR pitot several --size 1024x576    (landscape layout)
Query options for the page can be added with --query, for example  --query "xw=8&xo=70&fs=22".
Use --dpr 1.25 to render with the Pi's device pixel ratio (the screenshot is then 1.25 times larger).
Taps made once after the page loads (to open a Menu page) with --taps "530,492;288,278" (x,y pairs in page pixels).
VERSION 1.2.0"""
import argparse
import asyncio
import base64
import json
import os
import subprocess
import tempfile
import urllib.request

import aiohttp

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
PORT = 8097


class Cdp:
    """A very small Chrome DevTools protocol client."""

    def __init__(self, ws):
        self.ws, self.n = ws, 0

    async def call(self, method, **params):
        self.n += 1
        await self.ws.send_str(json.dumps({"id": self.n, "method": method, "params": params}))
        while True:
            reply = json.loads((await self.ws.receive()).data)
            if reply.get("id") == self.n:
                return reply.get("result")


async def run(out_dir, scenarios, width, height, query, taps, dpr):
    os.makedirs(out_dir, exist_ok=True)
    chrome = subprocess.Popen([CHROME, "--headless=new", "--use-angle=swiftshader", "--enable-unsafe-swiftshader",
                               "--no-sandbox", f"--window-size={width},{height}", "--remote-debugging-port=9335",
                               "--user-data-dir=" + tempfile.mkdtemp(prefix="efis_shots_profile_"), "about:blank"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        page = None
        for _ in range(60):
            try:
                tabs = json.load(urllib.request.urlopen("http://127.0.0.1:9335/json"))
                page = [t for t in tabs if t["type"] == "page"][0]
                break
            except Exception:
                await asyncio.sleep(0.3)
        async with aiohttp.ClientSession() as session:
            async with session.ws_connect(page["webSocketDebuggerUrl"], max_msg_size=0) as ws:
                cdp = Cdp(ws)
                await cdp.call("Emulation.setDeviceMetricsOverride", width=width, height=height,
                               deviceScaleFactor=dpr, mobile=False)
                await cdp.call("Page.enable")
                url = f"http://localhost:{PORT}/" + ("?" + query if query else "")
                await cdp.call("Page.navigate", url=url)
                await asyncio.sleep(4)
                for x, y in taps:
                    for kind in ("mousePressed", "mouseReleased"):
                        await cdp.call("Input.dispatchMouseEvent", type=kind, x=x, y=y, button="left", clickCount=1)
                        await asyncio.sleep(0.05)
                    await asyncio.sleep(0.5)
                for name in scenarios:
                    urllib.request.urlopen(f"http://localhost:{PORT}/scenario?name={name}").read()
                    await asyncio.sleep(1.5)
                    result = await cdp.call("Page.captureScreenshot", format="png")
                    path = os.path.join(out_dir, f"{name}.png")
                    with open(path, "wb") as f:
                        f.write(base64.b64decode(result["data"]))
                    print(path)
    finally:
        chrome.terminate()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir")
    parser.add_argument("scenarios", nargs="*")
    parser.add_argument("--size", default="576x1024", help="window size WxH (default the Pi's portrait page)")
    parser.add_argument("--query", default="", help="page address options, for example xw=8&xo=70")
    parser.add_argument("--dpr", type=float, default=1.0, help="device pixel ratio (the Pi's is 1.25)")
    parser.add_argument("--taps", default="", help="taps after loading, x,y pairs separated by ;")
    args = parser.parse_args()
    taps = [tuple(int(v) for v in pair.split(",")) for pair in args.taps.split(";") if pair]
    listing = urllib.request.urlopen(f"http://localhost:{PORT}/scenario").read().decode()
    choices = [c.strip() for c in listing.split("choices:")[1].split(",")]
    scenarios = args.scenarios or choices
    width, height = (int(v) for v in args.size.split("x"))
    asyncio.run(run(args.out_dir, scenarios, width, height, args.query, taps, args.dpr))


if __name__ == "__main__":
    main()
