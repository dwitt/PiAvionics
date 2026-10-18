"""Tests of the multi-client websocket handling in aio_server.py (EFIS/docs/MULTI_CLIENT_PLAN.md).
Needs the server's libraries, so it is skipped where they are missing. On the Pi:
    ~/EFIS/venv/bin/python -m unittest discover -s tests -v"""
VERSION = "1.1.1"

import asyncio
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
try:
    import aiohttp
    from aiohttp import web
    from aiohttp.test_utils import TestServer
    import aio_server as srv
except Exception as exc:
    srv = None


if srv is not None:
    class TestResponse(srv.MyWebSocketResponse):
        """Takes the role from the query string (?as=viewer) because every test client is local."""
        def _role(self, request):
            return request.query.get("as", "controller")


class SlowSocket:
    """Looks like a websocket that never accepts a message."""
    closed = False

    async def send_str(self, text):
        await asyncio.sleep(30)

    async def close(self):
        self.closed = True


@unittest.skipIf(srv is None, "aio_server cannot be imported here")
class ClientTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.saved = (srv.JSON_UPDATE_RATE, srv.CLIENT_SEND_TIMEOUT, srv.MAX_VIEWERS)
        srv.JSON_UPDATE_RATE = 0.02
        srv.CLIENT_SEND_TIMEOUT = 0.3
        self.handler = TestResponse()
        self.handler.backlight = None               # never touch the real backlight
        self.handler.data = srv.AvionicsData()
        self.data = self.handler.data
        # screen orientation: a fake runner records the rotations, the saved file is in a temp folder
        self.rotations = []

        async def fake_runner(transform):
            self.rotations.append(transform)
            return True
        self.tmp = tempfile.TemporaryDirectory()
        self.orientation_file = os.path.join(self.tmp.name, "display_orientation.json")
        self.handler.orientation = srv.display_orientation.DisplayOrientation(fake_runner, self.orientation_file)
        self.app = web.Application()
        self.app.add_routes([web.get("/ws", self.handler.handler)])
        self.server = TestServer(self.app)
        await self.server.start_server()
        self.session = aiohttp.ClientSession()
        self.sender = asyncio.create_task(srv.send_json(self.handler, self.data))

    async def asyncTearDown(self):
        self.sender.cancel()
        try:
            await self.sender
        except asyncio.CancelledError:
            pass
        await self.session.close()
        await self.server.close()
        self.tmp.cleanup()
        srv.JSON_UPDATE_RATE, srv.CLIENT_SEND_TIMEOUT, srv.MAX_VIEWERS = self.saved

    async def connect(self, role="controller"):
        return await self.session.ws_connect(self.server.make_url(f"/ws?as={role}"))

    async def receive_json(self, ws, timeout=1.0):
        msg = await ws.receive(timeout=timeout)
        self.assertEqual(msg.type, aiohttp.WSMsgType.TEXT)
        return json.loads(msg.data)

    # --- screen orientation commands (display_orientation.py) ---

    async def test_controller_can_start_keep_and_revert_an_orientation_trial(self):
        controller = await self.connect("controller")
        await controller.send_str('json{"orientation_request": "landscape"}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.rotations, ["90"])
        self.assertEqual(self.handler.orientation.trial["orientation"], "landscape")
        self.assertFalse(os.path.exists(self.orientation_file))     # a trial saves nothing
        await controller.send_str('json{"orientation_keep": true}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.handler.orientation.saved, "landscape")
        self.assertTrue(os.path.exists(self.orientation_file))
        await controller.send_str('json{"orientation_request": "portrait"}')
        await asyncio.sleep(0.2)
        await controller.send_str('json{"orientation_revert": true}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.rotations, ["90", "normal", "90"])
        self.assertEqual(self.handler.orientation.current, "landscape")
        await controller.close()

    async def test_viewer_cannot_change_the_orientation(self):
        viewer = await self.connect("viewer")
        await viewer.send_str('json{"orientation_request": "landscape", "orientation_keep": true}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.rotations, [])
        self.assertIsNone(self.handler.orientation.trial)
        await viewer.close()

    async def test_orientation_change_is_refused_when_flying(self):
        controller = await self.connect("controller")
        self.data.airspeed = 60
        await controller.send_str('json{"orientation_request": "landscape"}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.rotations, [])
        self.assertEqual(self.handler.orientation.facts()["refused"]["reason"], "airborne")
        self.data.airspeed = 0
        self.data.gps_speed = 45
        await controller.send_str('json{"orientation_request": "landscape"}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.rotations, [])                        # ground speed counts too
        await controller.close()

    async def test_a_route_command_does_not_touch_the_orientation(self):
        controller = await self.connect("controller")
        await controller.send_str('json{"qnh": 3010, "brightness": 40, "active_leg": 1}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.rotations, [])
        await controller.close()

    async def test_two_clients_both_receive_and_neither_is_dropped(self):
        a = await self.connect("controller")
        b = await self.connect("viewer")
        for _ in range(5):
            await self.receive_json(a)
            await self.receive_json(b)
        self.assertFalse(a.closed)
        self.assertFalse(b.closed)
        self.assertEqual(self.data.clients, {"controller": 1, "viewers": 1})
        await a.close()
        await b.close()

    async def test_viewer_commands_are_ignored(self):
        viewer = await self.connect("viewer")
        await viewer.send_str('json{"qnh": 3010, "brightness": 20, "active_leg": 2}')
        await asyncio.sleep(0.2)
        self.assertIsNone(self.data.controller_state)
        self.assertIsNone(self.handler.last_qnh)
        await viewer.close()

    async def test_controller_commands_work_and_are_shared_with_viewers(self):
        controller = await self.connect("controller")
        viewer = await self.connect("viewer")
        await controller.send_str('json{"qnh": 3010, "brightness": 40}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.handler.last_qnh, 3010)
        # messages already queued for the viewer were sent before the controller's setting arrived:
        # read on until one carries it
        message = await self.receive_json(viewer)
        for _ in range(100):
            if message["controller_state"] is not None:
                break
            message = await self.receive_json(viewer)
        self.assertEqual(message["controller_state"], {"qnh": 3010, "brightness": 40})
        await controller.close()
        await viewer.close()

    async def test_route_command_without_qnh_does_not_send_a_default_qnh(self):
        controller = await self.connect("controller")
        await controller.send_str('json{"qnh": 3010, "brightness": 40}')
        await controller.send_str('json{"active_leg": 1}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.handler.last_qnh, 3010)
        self.assertEqual(self.data.controller_state["qnh"], 3010)
        await controller.close()

    async def test_qnh_is_clamped(self):
        controller = await self.connect("controller")
        await controller.send_str('json{"qnh": 9999}')
        await asyncio.sleep(0.2)
        self.assertEqual(self.data.controller_state["qnh"], srv.QNH_MAX_INHG_X_100)
        await controller.close()

    async def test_new_controller_replaces_the_old_one_but_viewers_stay(self):
        old = await self.connect("controller")
        viewer = await self.connect("viewer")
        new = await self.connect("controller")
        msg = await old.receive(timeout=1.0)
        for _ in range(100):                                 # drain until the close arrives (bounded)
            if msg.type != aiohttp.WSMsgType.TEXT:
                break
            msg = await old.receive(timeout=1.0)
        self.assertIn(msg.type, (aiohttp.WSMsgType.CLOSE, aiohttp.WSMsgType.CLOSING, aiohttp.WSMsgType.CLOSED))
        await self.receive_json(viewer)                      # the viewer is still being served
        await self.receive_json(new)
        self.assertEqual(self.data.clients, {"controller": 1, "viewers": 1})
        await new.close()
        await viewer.close()

    async def test_too_many_viewers_are_refused(self):
        srv.MAX_VIEWERS = 2
        v1 = await self.connect("viewer")
        v2 = await self.connect("viewer")
        with self.assertRaises(aiohttp.WSServerHandshakeError) as caught:
            await self.connect("viewer")
        self.assertEqual(caught.exception.status, 503)
        controller = await self.connect("controller")        # the controller is never refused
        await self.receive_json(controller)
        for ws in (v1, v2, controller):
            await ws.close()

    async def test_disconnect_removes_the_client(self):
        for _ in range(5):                                   # repeated connect / disconnect
            ws = await self.connect("viewer")
            await self.receive_json(ws)
            await ws.close()
        await asyncio.sleep(0.2)
        self.assertEqual(len(self.handler._clients), 0)
        self.assertEqual(len(self.handler._send_tasks), 0)
        self.assertEqual(self.data.clients, {"controller": 0, "viewers": 0})

    async def test_a_stuck_client_does_not_slow_the_others_and_is_dropped(self):
        good = await self.connect("controller")
        stuck = SlowSocket()
        self.handler._clients[stuck] = {"role": "viewer", "remote": "test"}
        count = 0
        loop = asyncio.get_running_loop()
        start = loop.time()
        while loop.time() - start < 0.9:
            await self.receive_json(good, timeout=0.5)
            count += 1
        # at 50 messages a second the good client must get close to 45 in 0.9 s even though
        # the stuck client holds a send open for the whole 0.3 s timeout
        self.assertGreater(count, 30)
        self.assertTrue(stuck.closed)                        # dropped after the send timeout
        await good.close()

    async def test_status_facts_are_in_the_stream(self):
        ws = await self.connect("viewer")
        message = await self.receive_json(ws)
        self.assertIn("clients", message)
        await ws.close()


@unittest.skipIf(srv is None, "aio_server cannot be imported here")
class AddressTests(unittest.TestCase):
    def test_local_addresses(self):
        for address in ("127.0.0.1", "::1", "::ffff:127.0.0.1", "127.0.1.1"):
            self.assertTrue(srv.is_local_address(address), address)

    def test_other_addresses(self):
        for address in ("192.168.1.20", "10.0.0.5", "fe80::1", "::ffff:192.168.1.20", "", None, "not an address"):
            self.assertFalse(srv.is_local_address(address), address)


if __name__ == "__main__":
    unittest.main()
