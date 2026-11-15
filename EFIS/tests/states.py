"""Drive the knob through the fake feed and take a screenshot at each 'shot:name' step.
usage: states.py OUT_DIR step step ...   (tests/fake_feed_knob.py must be running on port 8097)
steps: a control query for the feed (pressed=1, position=6, yaw=90, mag_track=102, gs=90, ...), shot:NAME,
       scen:NAME (feed scenario), tap:X,Y (CSS pixels), key:NAME, js:EXPRESSION, sleep:SECONDS, reload.
The page opens at 576 x 1024 CSS px, scale 1.25 (as the Pi). PAGE_QUERY=?... adds to the address."""

VERSION = "1.0.0"
import asyncio, base64, json, os, subprocess, sys, tempfile, urllib.request, aiohttp
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from page_shots import Cdp, CHROME
OUT, STEPS = sys.argv[1], sys.argv[2:]
os.makedirs(OUT, exist_ok=True)
async def main():
    chrome = subprocess.Popen([CHROME, "--headless=new", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--no-sandbox",
        "--window-size=576,1024", "--remote-debugging-port=9335", "--user-data-dir=" + tempfile.mkdtemp(), "about:blank"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try:
                page = [t for t in json.load(urllib.request.urlopen("http://127.0.0.1:9335/json")) if t["type"] == "page"][0]; break
            except Exception: await asyncio.sleep(0.3)
        async with aiohttp.ClientSession() as s, s.ws_connect(page["webSocketDebuggerUrl"], max_msg_size=0) as ws:
            cdp = Cdp(ws)
            await cdp.call("Emulation.setDeviceMetricsOverride", width=576, height=1024, deviceScaleFactor=1.25, mobile=False)
            urllib.request.urlopen("http://localhost:8097/control?position=0&pressed=0").read()
            await cdp.call("Page.enable")
            await cdp.call("Page.navigate", url="http://localhost:8097/" + os.environ.get("PAGE_QUERY", ""))
            await asyncio.sleep(4)
            for st in STEPS:
                if st.startswith("shot:"):
                    await asyncio.sleep(0.8)
                    r = await cdp.call("Page.captureScreenshot", format="png")
                    open(os.path.join(OUT, st[5:] + ".png"), "wb").write(base64.b64decode(r["data"]))
                elif st.startswith("js:"):
                    r = await cdp.call("Runtime.evaluate", expression=st[3:], awaitPromise=True, returnByValue=True)
                    print("js:", r.get("result", {}).get("value"), flush=True)
                elif st.startswith("tap:"):
                    tx, ty = (float(v) for v in st[4:].split(","))
                    for kind in ("mousePressed", "mouseReleased"):
                        await cdp.call("Input.dispatchMouseEvent", type=kind, x=tx, y=ty, button="left", clickCount=1)
                    await asyncio.sleep(0.5)
                elif st.startswith("key:"):
                    name = st[4:]
                    text = name if len(name) == 1 else ""
                    codes = {"Backspace": 8, "Tab": 9, "Enter": 13, "Escape": 27, "ArrowUp": 38, "ArrowDown": 40}
                    code = codes.get(name, ord(name.upper()) if len(name) == 1 else 0)
                    await cdp.call("Input.dispatchKeyEvent", type="keyDown", key=name, text=text, windowsVirtualKeyCode=code)
                    await cdp.call("Input.dispatchKeyEvent", type="keyUp", key=name, windowsVirtualKeyCode=code)
                    await asyncio.sleep(0.2)
                elif st == "reload":
                    await cdp.call("Page.reload", ignoreCache=True); await asyncio.sleep(4)
                elif st.startswith("scen:"):
                    urllib.request.urlopen("http://localhost:8097/scenario?name=" + st[5:]).read()
                elif st.startswith("sleep:"):
                    await asyncio.sleep(float(st[6:]))
                else:
                    urllib.request.urlopen("http://localhost:8097/control?" + st).read()
                    await asyncio.sleep(0.5)
    finally:
        chrome.terminate()
asyncio.run(main())
