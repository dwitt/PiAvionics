#!/bin/bash
# flash_remote.sh - compile a sensor sketch on this Mac and flash a Feather RP2040
# that is plugged into the Pi EFIS's USB port.
#
# Usage:   Sensors/flash_remote.sh <SketchFolder> <usb-serial-number> [user@host]
# Example: Sensors/flash_remote.sh IMU_AHRS DF625857C7793E2F
#
# The board is picked by its USB serial number (the "SerialNumber" shown by
# ioreg / lsusb, and part of /dev/serial/by-id/...), not by ttyACM number.
# Needs on the Pi: picotool (sudo apt install picotool). Ssh must work without a password.
#
# A board whose firmware is running is rebooted into BOOTSEL by the 1200-baud
# touch. A board with broken firmware must be put in BOOTSEL by hand (hold the
# BOOTSEL button while plugging it in); the script then loads it directly.

VERSION="1.0.0"

set -euo pipefail

FQBN="rp2040:rp2040:adafruit_feather"
SKETCH="${1:?usage: flash_remote.sh <SketchFolder> <usb-serial-number> [user@host]}"
SERIAL="${2:?usage: flash_remote.sh <SketchFolder> <usb-serial-number> [user@host]}"
HOST="${3:-david@pi-efis.local}"

SENSORS_DIR="$(cd "$(dirname "$0")" && pwd)"
SKETCH_DIR="$SENSORS_DIR/$SKETCH"
[ -d "$SKETCH_DIR" ] || { echo "No sketch folder: $SKETCH_DIR" >&2; exit 1; }

BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

echo "== Compiling $SKETCH"
arduino-cli compile --fqbn "$FQBN" --libraries "$SENSORS_DIR/Libraries" \
	--output-dir "$BUILD_DIR" "$SKETCH_DIR" 2>&1 | grep -E "Sketch uses|Global variables|error|Error" || true
UF2="$BUILD_DIR/$SKETCH.ino.uf2"
[ -f "$UF2" ] || { echo "Compile failed: no $UF2" >&2; exit 1; }

REMOTE_UF2="/tmp/flash_remote_$SERIAL.uf2"
echo "== Copying to $HOST"
scp -q "$UF2" "$HOST:$REMOTE_UF2"

echo "== Flashing board $SERIAL"
ssh "$HOST" bash -s -- "$SERIAL" "$REMOTE_UF2" <<'REMOTE'
set -euo pipefail
SERIAL="$1"; UF2="$2"

# 1. If the board is running, touch its port at 1200 baud to reboot into BOOTSEL
PORT=$(ls /dev/serial/by-id/*"$SERIAL"* 2>/dev/null | head -1 || true)
if [ -n "$PORT" ]; then
	echo "   running firmware on $(basename "$(readlink -f "$PORT")"): $(basename "$PORT")"
	stty -F "$PORT" 1200
	sleep 1
else
	echo "   no running port for $SERIAL, assuming it is already in BOOTSEL"
fi

# 2. Wait for the board to appear in BOOTSEL
for i in $(seq 1 20); do
	picotool info --ser "$SERIAL" >/dev/null 2>&1 && break
	sleep 0.5
	[ "$i" = 20 ] && { echo "   board $SERIAL never showed up in BOOTSEL" >&2; exit 1; }
done

# 3. Load, verify and run
LOG=$(mktemp)
if picotool load --ser "$SERIAL" -v -x "$UF2" >"$LOG" 2>&1; then
	echo "   loaded and verified"
else
	tr '\r' '\n' <"$LOG" | grep -v -E '^(Loading into|Verifying)'
	rm -f "$LOG"; exit 1
fi
rm -f "$LOG"

# 4. Wait for it to come back and ask it who it is
for i in $(seq 1 20); do
	PORT=$(ls /dev/serial/by-id/*"$SERIAL"* 2>/dev/null | head -1 || true)
	[ -n "$PORT" ] && break
	sleep 0.5
done
[ -n "$PORT" ] || { echo "   board did not come back" >&2; exit 1; }
echo "   back as $(basename "$PORT")"
sleep 1
python3 - "$PORT" <<'PY'
import os, sys, time, termios, select
fd = os.open(sys.argv[1], os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
a = termios.tcgetattr(fd); a[4] = a[5] = termios.B115200; termios.tcsetattr(fd, termios.TCSANOW, a)
# The board needs about 5 s to finish its startup LED sequence before it
# answers '?', so ask repeatedly for up to 12 s.
ident = []
end_all = time.time() + 12
while not ident and time.time() < end_all:
    try: os.write(fd, b"?")
    except BlockingIOError: pass	# board not reading yet, try again
    buf = b""; end = time.time() + 1.5
    while time.time() < end:
        if select.select([fd], [], [], 0.2)[0]:
            try: buf += os.read(fd, 4096)
            except BlockingIOError: pass
    # The reply starts with a line like "IMU_AHRS v1.0.2 built ..."
    ident = [l for l in buf.decode(errors="replace").splitlines() if " v" in l and "built" in l]
print("   " + (ident[-1] if ident else "(no identity line in reply to ?)"))
PY
rm -f "$UF2"
REMOTE
echo "== Done"
