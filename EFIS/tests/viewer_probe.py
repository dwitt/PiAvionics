"""Connects to the EFIS websocket as a viewer from another computer (standard library only), prints what the
server sends, and optionally sends a command that a viewer must NOT be able to use.

    python3 viewer_probe.py [host:port] [--try-command]

--try-command sends a QNH one hundredth of an inHg away from the controller's current value. If the server wrongly
accepted it the QNH on the bus would change by 0.01 inHg until the Pi's own page sends its value again (within a frame).
"""
VERSION = "1.0.0"

import base64
import json
import os
import socket
import struct
import sys
import time


def connect(host, port):
    sock = socket.create_connection((host, port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall((f"GET /ws HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    reply = b""
    while b"\r\n\r\n" not in reply:
        reply += sock.recv(1024)
    head, _, rest = reply.partition(b"\r\n\r\n")
    status = head.split(b"\r\n")[0].decode()
    return sock, status, rest


def read_exact(sock, buffer, n):
    while len(buffer) < n:
        chunk = sock.recv(65536)
        if not chunk:
            raise ConnectionError("closed")
        buffer += chunk
    return buffer[:n], buffer[n:]


def read_frame(sock, buffer):
    header, buffer = read_exact(sock, buffer, 2)
    opcode, length = header[0] & 0x0F, header[1] & 0x7F
    if length == 126:
        extra, buffer = read_exact(sock, buffer, 2)
        length = struct.unpack(">H", extra)[0]
    elif length == 127:
        extra, buffer = read_exact(sock, buffer, 8)
        length = struct.unpack(">Q", extra)[0]
    payload, buffer = read_exact(sock, buffer, length)
    return opcode, payload, buffer


def send_text(sock, text):
    payload = text.encode()
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    n = len(payload)
    head = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n))
    sock.sendall(head + mask + masked)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    hostport = args[0] if args else "pi-efis.local:8080"
    host, port = hostport.split(":")
    sock, status, buffer = connect(host, int(port))
    print("handshake:", status)
    if " 101 " not in status:
        return
    send_text(sock, "ready")
    messages = []
    deadline = time.time() + 3
    while time.time() < deadline and len(messages) < 40:
        opcode, payload, buffer = read_frame(sock, buffer)
        if opcode == 1:
            messages.append(json.loads(payload))
    first = messages[0]
    print(f"received {len(messages)} messages in about 3 s")
    print("clients:", first.get("clients"))
    print("controller_state:", first.get("controller_state"))
    print("modules:", {n: (m["state"], m["version"]) for n, m in first.get("modules", {}).items()})
    print("pitch/yaw/altitude:", first.get("pitch"), first.get("yaw"), first.get("altitude"))
    if "--try-command" in sys.argv:
        state = first.get("controller_state") or {}
        qnh = state.get("qnh")
        if qnh is None:
            print("no controller qnh known; not sending the test command")
        else:
            print(f"sending as a viewer: qnh {qnh + 1} (controller has {qnh}), brightness unchanged")
            send_text(sock, "json" + json.dumps({"qnh": qnh + 1, "brightness": state.get("brightness", 100)}))
            time.sleep(1.0)
            latest = None
            end = time.time() + 1.5
            while time.time() < end:
                opcode, payload, buffer = read_frame(sock, buffer)
                if opcode == 1:
                    latest = json.loads(payload)
            print("controller_state afterwards:", latest.get("controller_state"),
                  "-> IGNORED" if latest.get("controller_state", {}).get("qnh") == qnh else "-> CHANGED (BAD)")
    sock.close()


if __name__ == "__main__":
    main()
