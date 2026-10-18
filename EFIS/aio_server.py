""" Asynchronous I/O based webserver to handle page and websocket"""
# pyright: reportMissingImports=false

VERSION = "1.5.0"

import asyncio
import ipaddress
import json
import struct
import time
import datetime
import signal
from pathlib import Path
from aiohttp import web #pylint: disable=import-error
import aiohttp #pylint: disable=import-error
import can #pylint: disable=import-error

from enum import Enum
from flightplan import FlightPlan, compute_navigation
import module_status
import display_orientation

#import board #pylint: disable=import-error
from adafruit_extended_bus import ExtendedI2C as I2C
from adafruit_seesaw import seesaw, rotaryio, digitalio #pylint: disable=import-error

from rpi_backlight import Backlight
import os

# =============================================================================
# DEBUGGING CONSTANTS
# =============================================================================
DEBUG = False
DEBUG_CAN = False
DEBUG_JSON = False
DEBUG_QNH = False
DEBUG_GPS_TIME = False
DEBUG_BRIGHTNESS = False
DEBUG_DISABLE_ENCODER = False
DEBUG_DISABLE_CAN = False
DEBUG_WEBSOCKET = False
DEBUG_NAV = False

# =============================================================================
# CAN BUS CONFIGURATION
# =============================================================================
CAN_QNH_PERIOD = 1000  # ms between messages (1 second between transmitting qnh)
CAN_BITRATE = 250000
CAN_CHANNEL = 'can0'
CAN_TIMEOUT = 0.1

# CAN message IDs - using enum for type safety
class CAN_MSG_ID(Enum):
    """CAN message ID enumeration"""
    ALTITUDE_AIRSPEED_VSI = 0x28
    OAT = 0x2A
    QNH = 0x2E
    STATIC_PRESSURE = 0x2B
    AHRS_ORIENT = 0x48
    AHRS_ACCEL = 0x49
    ENCODER = 0x38
    GPS1 = 0x63
    GPS2 = 0x64
    GPS3 = 0x65
    MAGX = 0x81
    MAGY = 0x82
    MAGZ = 0x83
    TIME_SYNC = 0x19
    IMU_HEARTBEAT = 0x701
    AIR_HEARTBEAT = 0x702
    GPS_HEARTBEAT = 0x703

# =============================================================================
# HARDWARE CONFIGURATION
# =============================================================================

# Backlight configuration
BACKLIGHT_PATH = "/sys/class/backlight/10-0045"
BACKLIGHT_DIR = "/sys/class/backlight"  # Base directory for backlight devices

# Encoder configuration
ENCODER_I2C_BUS = 1  # I2C bus number for rotary encoder
ENCODER_I2C_ADDRESS = 0x36  # Default I2C address for rotary encoder
ENCODER_THRESHOLD = 100  # Minimum change in encoder position to register
ENCODER_MAX_PLAUSIBLE = 2**30  # Reads near +/-2^31 are corrupted I2C transfers (sign-bit glitch)
ENCODER_REBASELINE_COUNT = 5  # Consecutive rejected reads before accepting a new baseline
ENCODER_POLL_INTERVAL = 0.02  # Encoder poll period in seconds (50 Hz)
ENCODER_BUTTON_PIN = 24  # GPIO pin for encoder button
SEESAW_EXPECTED_PRODUCT_ID = 4991  # Expected product ID for seesaw encoder

# =============================================================================
# TIMEOUT AND RATE CONFIGURATION
# =============================================================================
# How long each CAN message may be missing before its data is treated as lost is in
# module_status.DATA_TIMEOUTS (0.5 s for attitude and air data, 1.5 s OAT, 3.5 s GPS) and
# module_status.HEARTBEAT_TIMEOUT (3 s, module missing).
# While False, data that is invalid or timed out is only flagged in the JSON ("valid",
# "data_lost"); the old fields keep their last value, so the current display (which cannot
# handle null in most places) keeps working. Attitude and acceleration are cleared as before.
# Set True once the display handles null/invalid values (display step A of MODULE_STATUS_PLAN.md).
NULL_INVALID_VALUES = True    # a value that cannot be trusted is sent as null (the page ignores it anyway)
STATUS_UPDATE_PERIOD = 0.1  # seconds between status evaluations
DECLINATION_UNKNOWN = 0x7F  # GPS status byte 3 until the GPS module has calculated the declination
ORIENTATION_UPDATE_PERIOD = 0.2  # seconds between display orientation checks (trial timeout)

# Display clients (EFIS/docs/MULTI_CLIENT_PLAN.md). The page served to this computer's own browser
# (the Pi's kiosk Chromium) is the controller: its commands are processed. Any other page is a
# viewer: it receives the data but everything it sends is ignored.
MAX_VIEWERS = 4              # further viewers are refused (HTTP 503)
CLIENT_SEND_TIMEOUT = 0.5    # seconds; a client that cannot take one message in this time is dropped
JSON_UPDATE_RATE = 0.05  # seconds between JSON updates (20Hz = 50ms)

# =============================================================================
# FLIGHT PLAN CONFIGURATION
# =============================================================================
FLIGHTPLAN_DIR = Path.home() / 'flightplans'
FLIGHTPLAN_POLL_INTERVAL = 5.0  # seconds between checking for new .fpl files

# =============================================================================
# DATA CONVERSION AND SCALING FACTORS
# =============================================================================

# QNH conversion and configuration
QNH_TO_HPA_CONVERSION = 2.95299875  # 1 inHg = 2.95299875 hPa (hectopascals)
QNH_MIN_INHG_X_100 = 2800  # Minimum QNH in inHg × 100 format (28.00 inHg)
QNH_MAX_INHG_X_100 = 3100  # Maximum QNH in inHg × 100 format (31.00 inHg)
QNH_DEFAULT_INHG_X_100 = 2992  # Default QNH: 29.92 inHg × 100
QNH_ACCURACY_MULTIPLIER = 4  # QNH is multiplied by 4 to create inHg × 400 for accuracy preservation in short int

# AHRS scaling factor - AHRS values are sent as 10x actual value
AHRS_SCALING_FACTOR = 10
# *****************************************************************************
# *** CLASS for Avionics Data                                               ***
# *****************************************************************************

class AvionicsData:
    """Class to store all the avionics data received over the CAN bus."""
    # An instance of this class will be used to share the data between the
    # process_can_messages and send_json functions
    def __init__(self):
        # Initialize default values for all data attributes
        self.altitude = None
        self.airspeed = None
        self.vsi = None
        self.static_pressure = None
        self.temperature = None
        self.differential_pressure = None
        self.oat = None             # Outside Air Temperature (°C, from MAX31865 RTD)
        self.can_qnh = None
        self.can_qnh_hpa = None
        self.yaw = None
        self.pitch = None
        self.roll = None
        self.turn_rate = None
        self.accx = None
        self.accy = None
        self.accz = None
        self.calib = None
        self.latitude = None
        self.longitude = None
        self.gps_speed = None
        self.gps_altitude = None
        self.true_track = None
        self.mag_track = None        # GPS track corrected to magnetic with the module's WMM2025 declination (the HSI uses this one)
        self.gps_fix_quality = None  # 0=No fix, 1=GPS, 2=DGPS (WAAS)
        self.gps_fix_3d = None       # 1=No fix, 2=2D, 3=3D
        self.gps_satellites = None   # Number of satellites in use
        self.gps_hdop = None         # Horizontal dilution of precision
        self.gps_vdop = None         # Vertical dilution of precision
        self.gps_declination = None  # magnetic declination from the GPS module, whole degrees, East + (None until it has one)
        self.magx = None
        self.magy = None
        self.magz = None
        self.tm_year = None
        self.tm_mon = None
        self.tm_mday = None
        self.tm_hour = None
        self.tm_min = None
        self.tm_sec = None
        self.position = None
        self.pressed = None
        # Module health and data validity (module_status.py, evaluated about every 0.1 s)
        self.modules = None         # per module: present, missing, state, version, uptime, age, restarted_age, sensors
        self.valid = None           # per instrument: True if the value can be trusted
        self.data_lost = None       # per message group: True if it has stopped arriving
        self.heading_status = None  # valid, suspect (gyro-only heading), gyro_seconds
        self.system_state = None    # 1 all modules OK, 2 otherwise (for the EFIS heartbeat)
        self.controller_state = None  # last qnh / brightness the controlling page sent, for viewer pages
        self.clients = None         # {"controller": n, "viewers": n} connected display pages
        self.display = None         # screen orientation, saved setting and any trial (display_orientation.py)
        # The web page decides what to show (wording, colours, flashing) from these facts.
        # Navigation (computed from flight plan + GPS position)
        self.dtk = None         # desired track (degrees)
        self.bearing = None     # bearing to active waypoint (degrees)
        self.xtrack = None      # cross-track error (NM, +right/-left)
        self.dist = None        # distance to active waypoint (NM)
        self.to_from = None     # 'TO' or 'FROM'
        self.wpt_id = None      # active waypoint identifier
        self.route_name = None  # flight plan route name
        self.route_waypoints = None  # list of {id, type} dicts for route display
        self.active_leg = None       # index of active waypoint in route
        self.direct_to_active = False  # True when Direct-To navigation is active
        # Internal (not serialized to JSON)
        self._flight_plan = None  # FlightPlan instance
        self._dtk_true = None      # desired track and bearing as worked out from the flight plan, in TRUE degrees;
        self._bearing_true = None  # update_status() sends them as dtk and bearing in magnetic degrees
        self._status = module_status.StatusTracker(time.monotonic())  # heartbeats and validity
        self._modules_ok_mask = 0  # bit 0 AIR, 1 IMU, 2 GPS seen healthy (for the EFIS heartbeat 0x700)

# *****************************************************************************

def is_local_address(remote):
    """True if `remote` (a client address string) is this computer: 127.0.0.1, ::1 or ::ffff:127.0.0.1."""
    try:
        address = ipaddress.ip_address(remote)
    except (ValueError, TypeError):
        return False
    if address.version == 6 and address.ipv4_mapped is not None:
        address = address.ipv4_mapped
    return address.is_loopback

# *****************************************************************************
# *** CLASS Web Socket Response Handler
# *** Created so that the web socket response object can be exposed for
# *** later use sending data to the socket.
# *****************************************************************************

class MyWebSocketResponse:
    """Class to handle websocket responses"""
    def __init__(self):
        # Connected display pages: websocket -> {"role": "controller" or "viewer", "remote": address}
        self._clients = {}
        self._send_tasks = {}   # websocket -> the send in progress for it (see send_json)
        self.can_bus = None
        self.data = None
        self.last_qnh = None
        self.orientation = None     # display_orientation.DisplayOrientation, set in main()
        self._orientation_tasks = set()   # keeps the orientation commands from being garbage collected
        self.can_qnh_timestamp = int(time.monotonic_ns() / 1000000)
        # Initialize backlight with error handling
        self.backlight = None
        self.last_brightness = None
        try:
            # Check if the backlight path exists
            if not os.path.exists(BACKLIGHT_PATH):
                print(f"Warning: Backlight path {BACKLIGHT_PATH} does not exist.")
                print("Available backlight devices:")
                if os.path.exists(BACKLIGHT_DIR):
                    for item in os.listdir(BACKLIGHT_DIR):
                        print(f"  - {os.path.join(BACKLIGHT_DIR, item)}")
                else:
                    print(f"  Backlight directory {BACKLIGHT_DIR} does not exist")
                print("Continuing without backlight control.")
            else:
                self.backlight = Backlight(backlight_sysfs_path=BACKLIGHT_PATH)
                # Test if backlight is accessible
                current_brightness = self.backlight.brightness
                print(f"Backlight initialized successfully. Current brightness: {current_brightness}%")
        except PermissionError as e:
            print(f"Permission error accessing backlight: {e}")
            print("You may need to create a udev rule:")
            print("  echo 'SUBSYSTEM==\"backlight\",RUN+=\"/bin/chmod 666 /sys/class/backlight/%k/brightness /sys/class/backlight/%k/bl_power\"' | sudo tee -a /etc/udev/rules.d/backlight-permissions.rules")
            print("Then reboot or run: sudo udevadm control --reload-rules && sudo udevadm trigger")
            print("Continuing without backlight control.")
        except Exception as e:
            print(f"Warning: Could not initialize backlight: {e}")
            print("Continuing without backlight control.")
            self.backlight = None

    @property
    def web_socket(self):
        """The controller's websocket (the Pi's own page) if one is connected, else None"""
        for web_socket, info in self._clients.items():
            if info["role"] == "controller" and not web_socket.closed:
                return web_socket
        return None

    @property
    def clients(self):
        """All open websockets (controller and viewers)"""
        return [web_socket for web_socket in self._clients if not web_socket.closed]

    def _role(self, request):
        """'controller' for a page opened on this computer, otherwise 'viewer'"""
        return "controller" if is_local_address(request.remote) else "viewer"

    def _update_client_counts(self):
        """Keep data.clients up to date for the status page and the EFIS heartbeat"""
        if self.data is None:
            return
        roles = [info["role"] for web_socket, info in self._clients.items() if not web_socket.closed]
        self.data.clients = {"controller": roles.count("controller"), "viewers": roles.count("viewer")}

    # Function that is called when a request to create a websocket is received
    async def handler(self, request):
        """Handler to process web socket requests"""
        if DEBUG_QNH:
            print("handler called")
        role = self._role(request)

        if role == "viewer":
            viewers = sum(1 for web_socket, info in self._clients.items()
                          if info["role"] == "viewer" and not web_socket.closed)
            if viewers >= MAX_VIEWERS:
                print(f"Display viewer from {request.remote} refused: {MAX_VIEWERS} viewers already connected",
                      flush=True)
                return web.Response(status=503, text="Too many viewers")
        else:
            # A page that was reloaded leaves its old socket behind: only one controller at a time
            for web_socket, info in list(self._clients.items()):
                if info["role"] == "controller" and not web_socket.closed:
                    await web_socket.close()

        # Create a websocket response object and prepare it for use
        web_socket = web.WebSocketResponse()
        await web_socket.prepare(request)

        self._clients[web_socket] = {"role": role, "remote": request.remote}
        self._update_client_counts()
        print(f"Display {role} connected from {request.remote}", flush=True)
        try:
            # Iterate over the messages (msg) return by the Web Socket (web_socket)
            # I expect we should sit in this loop until told to close the socket
            async for msg in web_socket:
                if DEBUG_WEBSOCKET:
                    print("websocket message received")
                if msg.type == aiohttp.WSMsgType.TEXT:
                    # Check message length before slicing to avoid IndexError
                    if len(msg.data) >= 5 and msg.data[0:5] == 'close':
                        if DEBUG_WEBSOCKET:
                            print("websocket close message")
                        await web_socket.close()
                    elif len(msg.data) >= 5 and msg.data[0:5] == 'ready':
                        # do nothing really other than recognize that ready
                        # was sent
                        if DEBUG_WEBSOCKET:
                            print("websocket ready message")
                    # The efis-display.js will send json data prefixed with 'json'
                    elif len(msg.data) >= 4 and msg.data[0:4] == 'json':
                        if role == "controller":
                            if DEBUG_WEBSOCKET:
                                print("json message")
                            # process the json send from the efis-display.js
                            self.process_json_data(msg)
                        elif DEBUG_WEBSOCKET:
                            print("json message from a viewer ignored")
                elif msg.type == aiohttp.WSMsgType.ERROR:
                    if DEBUG_WEBSOCKET:
                        print('ws connection closed with exception %s' % web_socket.exception())
            if DEBUG_WEBSOCKET:
                print('websocket connection closed')
        finally:
            # Always execute this code
            # Close the web socket and forget it
            if DEBUG_WEBSOCKET:
                print('websocket connection closed in finally')
            await web_socket.close()
            self._clients.pop(web_socket, None)
            self._update_client_counts()
            print(f"Display {role} from {request.remote} disconnected", flush=True)

        # Return the websocket response object - required by aiohttp's API
        return web_socket

    def process_json_data(self, web_socket_message):
        """Process any json that is contained in the web socket message"""

        if DEBUG_CAN:
            print("websocket message with json received")

        # decode the json
        try:
            dict_object = json.loads(web_socket_message.data[4:])
            try:
                if self.data is not None and self.data.controller_state is None:
                    self.data.controller_state = {}

                # Get QNH. It is only processed when the message carries one: a route command
                # (active_leg, direct_to) has no qnh and must not send a default to the CAN bus.
                # QNH is received in inHg × 100 format (e.g., 2992 = 29.92 inHg)
                qnh = dict_object.get('qnh', None)
                if qnh is not None:
                    # Validate and clamp QNH to reasonable range (28.00-31.00 inHg)
                    # Convert to int to handle any float values, then clamp
                    qnh = int(qnh)
                    qnh = max(QNH_MIN_INHG_X_100, min(QNH_MAX_INHG_X_100, qnh))

                    if DEBUG_QNH:
                        print(f"QNH from json= {qnh} (clamped to range {QNH_MIN_INHG_X_100}-{QNH_MAX_INHG_X_100})")

                    # Process QNH (qnh is in inHg × 100 format)
                    self.process_qnh(qnh)
                    if self.data is not None:
                        self.data.controller_state["qnh"] = qnh
                
                # Process brightness if provided
                brightness = dict_object.get('brightness', None)
                if brightness is not None:
                    if DEBUG_BRIGHTNESS:
                        print(f'Brightness received from client: {brightness}')
                    if self.data is not None:
                        try:
                            self.data.controller_state["brightness"] = max(0, min(100, int(brightness)))
                        except (ValueError, TypeError):
                            pass
                    
                    if self.backlight is not None:
                        try:
                            # Ensure brightness is in valid range (0-100)
                            brightness = max(0, min(100, int(brightness)))
                            # Only write to backlight if value has changed
                            if brightness != self.last_brightness:
                                self.backlight.brightness = brightness
                                self.last_brightness = brightness
                                if DEBUG_BRIGHTNESS:
                                    print(f'Backlight brightness set to: {brightness}%')
                        except (ValueError, TypeError, AttributeError) as e:
                            print(f"Error setting backlight brightness: {e}")
                    else:
                        if DEBUG_BRIGHTNESS:
                            print("Backlight not available, skipping brightness update")

                # Process active_leg selection (leg mode from route overlay)
                active_leg = dict_object.get('active_leg', None)
                if active_leg is not None and self.data._flight_plan is not None:
                    active_leg = int(active_leg)
                    fp = self.data._flight_plan
                    if 1 <= active_leg < len(fp.waypoints):
                        fp.active_leg = active_leg
                        fp.direct_to_origin = None  # clear any Direct-To
                        self.data.active_leg = active_leg
                        self.data.direct_to_active = False
                        print(f"Leg selected: {fp.active_waypoint_id} (leg {active_leg})")

                # Process Direct-To command
                direct_to = dict_object.get('direct_to', None)
                if direct_to is not None and self.data._flight_plan is not None:
                    fp = self.data._flight_plan
                    index = int(direct_to)
                    if self.data.latitude is not None and self.data.longitude is not None:
                        lat = self.data.latitude / 1_000_000
                        lon = self.data.longitude / 1_000_000
                        if fp.activate_direct_to(index, lat, lon):
                            self.data.active_leg = fp.active_leg
                            self.data.direct_to_active = True

                # Process Cancel Direct-To command
                if dict_object.get('cancel_direct_to', False) and self.data._flight_plan is not None:
                    self.data._flight_plan.cancel_direct_to()
                    self.data.direct_to_active = False

                # Display orientation commands from the Menu (only the controller's messages get here)
                if self.orientation is not None:
                    if 'orientation_request' in dict_object:
                        self._start_orientation(self.orientation_request(dict_object['orientation_request']))
                    if dict_object.get('orientation_keep', False):
                        self._start_orientation(self.orientation_keep())
                    if dict_object.get('orientation_revert', False):
                        self._start_orientation(self.orientation_revert())

            except (KeyError, ValueError, TypeError) as e:
                # Data not received or invalid format, ignore it
                if DEBUG:
                    print(f"Error processing JSON data: {e}")
        except (json.JSONDecodeError, IndexError, AttributeError) as e:
            # If we didn't get valid JSON data, just ignore it
            if DEBUG:
                print(f"Error decoding JSON message: {e}")

    def _start_orientation(self, coroutine):
        """Run an orientation command in the background (the message handler is not async)"""
        try:
            task = asyncio.get_running_loop().create_task(coroutine)
        except RuntimeError:        # no running loop (not expected in the server)
            coroutine.close()
            return
        self._orientation_tasks.add(task)
        task.add_done_callback(self._orientation_tasks.discard)

    async def orientation_request(self, name):
        """Start a trial of a new screen orientation (refused while flying or when already trying one)"""
        speeds = (self.data.airspeed if self.data is not None else None,
                  self.data.gps_speed if self.data is not None else None)
        ok, reason = await self.orientation.request(name, speeds)
        print(f"Display orientation trial of {name}: " + ("started" if ok else f"refused ({reason})"), flush=True)

    async def orientation_keep(self):
        if await self.orientation.keep():
            print(f"Display orientation {self.orientation.saved} kept and saved", flush=True)

    async def orientation_revert(self):
        if await self.orientation.revert():
            print(f"Display orientation trial reverted to {self.orientation.saved}", flush=True)

    def process_qnh(self, qnh):
        """Determine if qnh needs to be sent on the can bus based on
        whether it has changed or if sufficient time has elapsed.
        If it needs to be sent out pack it into the message and send it.
        
        Args:
            qnh (int): QNH value in inHg × 100 format (e.g., 2992 = 29.92 inHg)
        
        Note:
            QNH is sent on CAN bus if it has changed from the last sent value
            or if CAN_QNH_PERIOD milliseconds have elapsed since the last send.
        """
        current_time_millis = int(time.monotonic_ns() / 1000000)
        if (qnh != self.last_qnh or
            current_time_millis > self.can_qnh_timestamp + CAN_QNH_PERIOD):

            if DEBUG_CAN:
                print("prepare to send QNH on CAN")
                print(f"qnh = {qnh}")
            if DEBUG_QNH:
                print(", sending via CAN")

            message = self.pack_can_qnh_msg(qnh)
            self.can_qnh_timestamp = current_time_millis
            self.last_qnh = qnh
            #print(f"QNH: {qnh}, Last QNH:{self.last_qnh}")
            #print(f"Sending can message {message}")

            if DEBUG_CAN:
                print(self.can_bus)

            # Check if CAN bus is available before sending
            if self.can_bus is not None:
                try:
                    self.can_bus.send(message)
                    if DEBUG_CAN:
                        print(f"Message sent on {self.can_bus.channel_info}")
                except Exception as e:
                    print(f"Error sending CAN message: {e}")
            else:
                if DEBUG_CAN:
                    print("CAN bus not available, cannot send QNH message")

    def pack_can_qnh_msg(self, qnh):
        """ Pack the qnh value into a message for sending on the CAN bus.
        
        Args:
            qnh: QNH value in inHg × 100 format (e.g., 2992 = 29.92 inHg)
            
        Returns:
            can.Message: Packed CAN message ready to send
        """
        # Convert from inHg × 100 to hPa (qnh is in inHg × 100, so divide by conversion factor)
        qnh_hpa = int(qnh / QNH_TO_HPA_CONVERSION)
        # qnh is in inHg × 100, so qnh*QNH_ACCURACY_MULTIPLIER gives inHg × 400 for accuracy preservation
        qnh_message = struct.pack("<hhBBBB",
                                    qnh_hpa,
                                    int(qnh * QNH_ACCURACY_MULTIPLIER),
                                    0,0,0,0)
        message = can.Message(arbitration_id=CAN_MSG_ID.QNH.value,
                              data=qnh_message,
                              is_extended_id=False)
        if DEBUG_CAN:
            print(message)
        return message
    
# *****************************************************************************

# -----------------------------------------------------------------------------
# --- Asynchronous function to create the web and websocket servers         ---
# -----------------------------------------------------------------------------
# --- handler = the response handler to be used for the websocket serve     ---
# -----------------------------------------------------------------------------
async def create_servers(websocket_handler):
    """Create web server to handle requests for both the web page and the
        websocket"""

    # -------------------------------------------------------------------------
    # --- Create the web server                                             ---
    # -------------------------------------------------------------------------

    # Notes: using web as that is how aiohttp was imported
    # Create an application instance
    server = web.Application()

    # --- Add the routes used to respond to various requests
    # --- For the index file at '/' use the get_index response function
    # --- For /support/ just return the files from the directory
    # --- For /ws which is the web socket use the MyWebSocketResponse handler
    server.add_routes([web.get('/', get_index),
                       web.static('/support/','./support/'),
                       web.get('/ws', websocket_handler)])

    # Create the application runner
    runner = web.AppRunner(server)

    # Start the application
    await runner.setup()

    # create the site
    site = web.TCPSite(runner)  # Try this so we listen on all addresses
    # The following should be used for the production version
    #site = web.TCPSite(runner, 'localhost', 8080)

    # Start the Site
    await site.start()

    print("aio-Server.py has started the web server.")

# -----------------------------------------------------------------------------
# --- handler for get_index                                                ---
# -----------------------------------------------------------------------------

async def get_index(request):
    """Return index.html file when the root directory is requested"""
    if request.path == '/':
        # Use __file__ to get path relative to script location for predictable behavior
        # This is more reliable than Path.cwd() which depends on where the script is run from
        script_dir = Path(__file__).parent
        index_path = script_dir / 'index.html'
        
        # Validate that the path is within the expected directory (security check)
        # Ensure the resolved path is still within the script directory
        try:
            resolved_path = index_path.resolve()
            script_dir_resolved = script_dir.resolve()
            if not str(resolved_path).startswith(str(script_dir_resolved)):
                if DEBUG:
                    print(f"Security warning: index.html path outside script directory: {resolved_path}")
                return web.Response(status=403, text="Access denied")
        except (OSError, ValueError) as e:
            if DEBUG:
                print(f"Error resolving path: {e}")
            return web.Response(status=500, text="Error resolving file path")
        
        # Check if file exists before trying to open it
        if not index_path.exists():
            if DEBUG:
                print(f"Warning: index.html not found at {index_path}")
            return web.Response(status=404, text="index.html not found")
        
        # Use context manager to ensure file is properly closed
        try:
            with open(index_path, 'r', encoding='utf-8') as index_file:
                index_content = index_file.read()
            return web.Response(text=index_content, content_type="text/html")
        except (IOError, OSError) as e:
            if DEBUG:
                print(f"Error reading index.html: {e}")
            return web.Response(status=500, text="Error reading index.html")
    return web.Response(status=404)




# -----------------------------------------------------------------------------
# --- CAN Message Processing Helper Functions                                 ---
# -----------------------------------------------------------------------------

def validate_message_length(msg, expected_length, message_name):
    """Validate CAN message data length
    
    Args:
        msg: CAN message object
        expected_length: Expected minimum data length in bytes
        message_name: Name of message type for error messages
        
    Returns:
        bool: True if valid, False otherwise
    """
    if len(msg.data) < expected_length:
        if DEBUG_CAN:
            print(f"Invalid data length for {message_name} message: {len(msg.data)} bytes, expected {expected_length}")
        return False
    return True

# -----------------------------------------------------------------------------
# --- Individual CAN Message Handler Functions                                ---
# -----------------------------------------------------------------------------

async def process_altitude_message(msg, data, last_received_times):
    """Process altitude/airspeed/VSI message
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)
        
    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 8, "altitude"):
        return False
    
    # Calculate altitude manually (may be used for validation/debugging)
    data.altitude = msg.data[2] | (msg.data[3]<<8) | (msg.data[4]<<16) | (msg.data[5]<<24)
    
    try:
        # struct.unpack for altitude is here - unpacks airspeed (h), altitude (l), vsi (h)
        (data.airspeed, data.altitude, data.vsi) = struct.unpack("<hlh", msg.data)
        if DEBUG:
            print(data.vsi, data.airspeed)
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking altitude message: {e}")
        return False

async def process_static_pressure_message(msg, data, last_received_times):
    """Process static pressure and temperature message
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)
        
    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 8, "static pressure"):
        return False
    
    try:
        (static_pressure, temperature, differential_pressure, _, _, _) = (
            struct.unpack("<hbhbbb", msg.data)
        )
        data.static_pressure = static_pressure
        data.temperature = temperature
        data.differential_pressure = differential_pressure
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking static pressure message: {e}")
        return False

async def process_oat_message(msg, data, last_received_times):
    """Process OAT (Outside Air Temperature) message from MAX31865 RTD sensor"""
    if not validate_message_length(msg, 8, "OAT"):
        return False
    try:
        (oat_raw, humidity, _, _, _, _, _) = struct.unpack("<hBBBBBB", msg.data)
        data.oat = oat_raw / 10.0  # Convert from °C × 10 to °C
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking OAT message: {e}")
        return False

async def process_qnh_message(msg, data, last_received_times):
    """Process QNH message
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)
        
    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 8, "QNH"):
        return False
    
    try:
        (qnh_hpa, qnhx4, dummy_3, dummy_4, dummy_5, dummy_6) = (
            struct.unpack("<hhBBBB", msg.data))
        # qnhx4 is inHg × 400, divide by QNH_ACCURACY_MULTIPLIER to convert to inHg
        data.can_qnh = qnhx4 / QNH_ACCURACY_MULTIPLIER
        data.can_qnh_hpa = qnh_hpa
        if DEBUG_JSON:
            print(data.can_qnh)
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking QNH message: {e}")
        return False

async def process_ahrs_orient_message(msg, data, last_received_times):
    """Process AHRS orientation message (roll, pitch, yaw)
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp
        
    Returns:
        bool: True if successful, False otherwise
    """
    if DEBUG_CAN:
        print("orient")
    
    if not validate_message_length(msg, 8, "AHRS orient"):
        return False
    
    try:
        (data.yaw, data.pitch, data.roll, data.turn_rate) = (
            struct.unpack("<hhhh", msg.data)
        )
        # Data received from Arduino already converted from sensor native frame (NWU)
        # to aircraft standard (NED). Only scaling needed here.
        # AHRS values are sent as 10x actual value, divide by AHRS_SCALING_FACTOR
        data.yaw = data.yaw / AHRS_SCALING_FACTOR
        data.pitch = data.pitch / AHRS_SCALING_FACTOR
        data.roll = data.roll / AHRS_SCALING_FACTOR
        data.turn_rate = data.turn_rate / AHRS_SCALING_FACTOR
        last_received_times[CAN_MSG_ID.AHRS_ORIENT.value] = time.monotonic()
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking AHRS orient message: {e}")
        return False

async def process_ahrs_accel_message(msg, data, last_received_times):
    """Process AHRS accelerometer message
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp
        
    Returns:
        bool: True if successful, False otherwise
    """
    if DEBUG_CAN:
        print("accel")
    
    if not validate_message_length(msg, 8, "AHRS accel"):
        return False
    
    try:
        (data.accx, data.accy, data.accz, data.calib) = (
            struct.unpack("<hhhh", msg.data)
        )
        last_received_times[CAN_MSG_ID.AHRS_ACCEL.value] = time.monotonic()
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking AHRS accel message: {e}")
        return False

async def process_gps1_message(msg, data, last_received_times):
    """Process GPS1 message (latitude, longitude)
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)
        
    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 8, "GPS1"):
        return False
    
    try:
        (latitude, longitude) = struct.unpack("<ll", msg.data)
        #TODO: Check if we are carrying enough significant digits
        #       in the following calculations
        data.latitude = latitude  # / 10^6
        data.longitude = longitude  # / 10^6

        # Compute flight plan navigation if a plan is loaded
        if data._flight_plan is not None:
            nav = compute_navigation(latitude, longitude, data._flight_plan)
            if nav:
                data._dtk_true = nav['dtk']          # true degrees; update_status() converts to magnetic
                data._bearing_true = nav['bearing']
                data.xtrack = nav['xtrack']
                data.dist = nav['dist']
                data.to_from = nav['to_from']
                data.wpt_id = nav['wpt_id']
                data.route_name = nav['route_name']
                # Keep active_leg and direct-to state in sync
                data.active_leg = data._flight_plan.active_leg
                data.direct_to_active = data._flight_plan.direct_to_active
                if DEBUG_NAV:
                    print(f"Nav: {nav['wpt_id']} DTK={nav['dtk']} "
                          f"BRG={nav['bearing']} XTK={nav['xtrack']} "
                          f"DST={nav['dist']} {nav['to_from']}")
            else:
                data._dtk_true = data._bearing_true = None
                data.dtk = data.bearing = data.xtrack = None
                data.dist = data.to_from = data.wpt_id = data.route_name = None

        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking GPS1 message: {e}")
        return False

async def process_gps2_message(msg, data, last_received_times):
    """Process GPS2 message (speed, altitude, track)
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)
        
    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 8, "GPS2"):
        return False
    
    try:
        (data.gps_speed, data.gps_altitude, data.true_track, data.mag_track) = (
            struct.unpack("<hhhh", msg.data)
        )
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking GPS2 message: {e}")
        return False

async def process_gps3_message(msg, data, last_received_times):
    """Process GPS3 message (GPS status: fix quality, satellites, HDOP, VDOP)

    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)

    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 8, "GPS3"):
        return False

    try:
        (fq, fq3d, sats, declination, hdop_raw, vdop_raw) = (
            struct.unpack("<BBBbhh", msg.data)
        )
        # byte 3: magnetic declination in whole degrees, East positive; 0x7F = not calculated yet
        # (modules before GPS_Module 1.0.9 sent 0 here, which reads as a declination of 0)
        data.gps_declination = None if declination == DECLINATION_UNKNOWN else declination
        data.gps_fix_quality = fq
        data.gps_fix_3d = fq3d
        data.gps_satellites = sats
        data.gps_hdop = round(hdop_raw / 100.0, 1)
        data.gps_vdop = round(vdop_raw / 100.0, 1)
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking GPS3 message: {e}")
        return False

async def process_magx_message(msg, data, last_received_times):
    """Process magnetometer X message
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)
        
    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 4, "MAGX"):
        return False
    
    try:
        # keep only the first element of the tuple returned
        data.magx = struct.unpack("<f", msg.data)[0]
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking MAGX message: {e}")
        return False

async def process_magy_message(msg, data, last_received_times):
    """Process magnetometer Y message
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)
        
    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 4, "MAGY"):
        return False
    
    try:
        # keep only the first element of the tuple returned
        data.magy = struct.unpack("<f", msg.data)[0]
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking MAGY message: {e}")
        return False

async def process_magz_message(msg, data, last_received_times):
    """Process magnetometer Z message
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)
        
    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 4, "MAGZ"):
        return False
    
    try:
        # keep only the first element of the tuple returned
        data.magz = struct.unpack("<f", msg.data)[0]
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking MAGZ message: {e}")
        return False

async def process_time_sync_message(msg, data, last_received_times):
    """Process time sync message
    
    Args:
        msg: CAN message object
        data: AvionicsData instance to update
        last_received_times: Dictionary to update with receive timestamp (unused here)
        
    Returns:
        bool: True if successful, False otherwise
    """
    if not validate_message_length(msg, 8, "TIME_SYNC"):
        return False
    
    try:
        (data.tm_year, data.tm_mon, data.tm_mday,
         data.tm_hour, data.tm_min, data.tm_sec, _, _) = (
            struct.unpack("<bbbbbbbb", msg.data)
        )
        data.tm_year = 2000 + data.tm_year
        
        gps_datetime = datetime.datetime(
            data.tm_year,
            data.tm_mon,
            data.tm_mday,
            data.tm_hour,
            data.tm_min,
            data.tm_sec,
            tzinfo=None
        )
        if DEBUG_GPS_TIME:
            print(f"{gps_datetime}")
        return True
    except struct.error as e:
        if DEBUG_CAN:
            print(f"Error unpacking TIME_SYNC message: {e}")
        return False
    except (ValueError, TypeError) as e:
        if DEBUG_GPS_TIME:
            print(f"Error creating GPS datetime: {e}")
        return False

async def process_heartbeat_message(msg, data, last_received_times):
    """Process a module heartbeat (0x701 IMU, 0x702 AIR, 0x703 GPS).

    The tracker keeps the state, sensors mask, version and uptime of each module.
    Returns True if it was a valid heartbeat."""
    return data._status.on_heartbeat(msg.arbitration_id, bytes(msg.data), time.monotonic())

# -----------------------------------------------------------------------------
# --- CAN Message Handler Dispatch Table                                      ---
# -----------------------------------------------------------------------------

# Message handler dispatch table - maps CAN message IDs to handler functions
# Note: Some handlers require last_received_times parameter, handled in process_can_messages
CAN_MESSAGE_HANDLERS = {
    CAN_MSG_ID.ALTITUDE_AIRSPEED_VSI.value: process_altitude_message,
    CAN_MSG_ID.OAT.value: process_oat_message,
    CAN_MSG_ID.STATIC_PRESSURE.value: process_static_pressure_message,
    CAN_MSG_ID.QNH.value: process_qnh_message,
    CAN_MSG_ID.AHRS_ORIENT.value: process_ahrs_orient_message,
    CAN_MSG_ID.AHRS_ACCEL.value: process_ahrs_accel_message,
    CAN_MSG_ID.GPS1.value: process_gps1_message,
    CAN_MSG_ID.GPS2.value: process_gps2_message,
    CAN_MSG_ID.GPS3.value: process_gps3_message,
    CAN_MSG_ID.MAGX.value: process_magx_message,
    CAN_MSG_ID.MAGY.value: process_magy_message,
    CAN_MSG_ID.MAGZ.value: process_magz_message,
    CAN_MSG_ID.TIME_SYNC.value: process_time_sync_message,
    CAN_MSG_ID.IMU_HEARTBEAT.value: process_heartbeat_message,
    CAN_MSG_ID.AIR_HEARTBEAT.value: process_heartbeat_message,
    CAN_MSG_ID.GPS_HEARTBEAT.value: process_heartbeat_message,
}

# -----------------------------------------------------------------------------
# --- Asynchronous process to get a message from the CAN bus buffer and      ---
# --- process it.                                                           ---
# -----------------------------------------------------------------------------

async def process_can_messages(reader, data, last_received_times):
    """Process the CAN messages when they are received
    
    Args:
        reader: CAN bus AsyncBufferedReader
        data: AvionicsData instance to update with received data
        last_received_times: Dictionary mapping message IDs to last receive timestamp
    """
    while True:  # loop here forever - keep processing messages
        if DEBUG_CAN:
            print("Looking for CAN msg")
        
        try:
            msg = await reader.get_message()
        except Exception as e:
            # Handle errors reading from CAN bus (connection lost, bus error, etc.)
            if DEBUG_CAN:
                print(f"Error reading CAN message: {e}")
            # Wait a bit before retrying to avoid tight error loop
            await asyncio.sleep(0.1)
            continue
        
        if DEBUG_CAN:
            print("got msg")
        
        # Look up handler for this message type
        handler = CAN_MESSAGE_HANDLERS.get(msg.arbitration_id)
        
        if handler:
            try:
                # All handlers have the same signature: (msg, data, last_received_times)
                success = await handler(msg, data, last_received_times)
                
                # Update last received time for messages that don't update it themselves
                # (AHRS_ORIENT and AHRS_ACCEL update it in their handlers)
                if success and msg.arbitration_id not in [CAN_MSG_ID.AHRS_ORIENT.value, CAN_MSG_ID.AHRS_ACCEL.value]:
                    last_received_times[msg.arbitration_id] = time.monotonic()
            except Exception as e:
                if DEBUG_CAN:
                    print(f"Error processing CAN message 0x{msg.arbitration_id:02X}: {e}")
        else:
            if DEBUG_CAN:
                print(f"Unknown CAN message ID: 0x{msg.arbitration_id:02X}")
        
        await asyncio.sleep(0)  # let another process run
    
def to_magnetic(true_degrees, declination):
    """Convert true degrees to magnetic: magnetic = true - declination (declination East positive).
    None if either is unknown."""
    if true_degrees is None or declination is None:
        return None
    return round((true_degrees - declination) % 360, 1)


def update_status(data, last_received_times, now):
    """Judge the module heartbeats and data freshness and store the result in `data`.

    Fills data.modules, data.valid, data.data_lost, data.heading_status and data.system_state
    with facts only; the web page decides how to show them. Attitude and acceleration are
    cleared (None) when invalid, as the old timeout monitor did. Other values are cleared only
    when NULL_INVALID_VALUES is True.
    """
    snapshot = data._status.evaluate(now, last_received_times)
    valid = snapshot["valid"]

    data.modules = snapshot["modules"]
    data.valid = valid
    data.data_lost = snapshot["data_lost"]
    data.heading_status = snapshot["heading"]
    data.system_state = snapshot["system_state"]
    data._modules_ok_mask = snapshot["modules_ok_mask"]     # for the EFIS heartbeat, not sent to the page

    if not valid["attitude"]:
        data.pitch = None
        data.roll = None
    if not valid["heading"]:
        data.yaw = None
    if not valid["acceleration"]:
        data.accx = None
        data.accy = None
        data.accz = None

    if NULL_INVALID_VALUES:
        if not valid["airspeed"]:
            data.airspeed = None
        if not valid["altitude"]:
            data.altitude = None
        if not valid["vsi"]:
            data.vsi = None
        if not valid["static_pressure"]:
            data.static_pressure = None
        if not valid["differential_pressure"]:
            data.differential_pressure = None
        if not valid["oat"]:
            data.oat = None
        if not valid["turn_rate"]:
            data.turn_rate = None
        if not valid["gps_position"]:
            data.latitude = None
            data.longitude = None
            data.gps_speed = None
            data.gps_altitude = None
            data.true_track = None
            data.mag_track = None
            # the navigation worked out from the position
            data.xtrack = None
            data.dist = None
            data.bearing = None
            data.to_from = None

    # The flight plan courses are worked out in true degrees but the HSI is magnetic, so the desired track and
    # bearing are sent as magnetic once the GPS module has told us the declination (ISSUES.md #15). Until then
    # there is no usable course, so the navigation that goes with it (cross-track, TO/FROM) is held back too.
    # (The leg's course does not depend on the position, but the bearing to the waypoint does.)
    data.dtk = to_magnetic(data._dtk_true, data.gps_declination)
    if NULL_INVALID_VALUES and not valid["gps_position"]:
        data.bearing = None
    else:
        data.bearing = to_magnetic(data._bearing_true, data.gps_declination)
    if data._dtk_true is not None and data.gps_declination is None:
        data.xtrack = None
        data.to_from = None
    return snapshot

async def monitor_status(data, last_received_times):
    """Evaluate module health and data freshness about every STATUS_UPDATE_PERIOD seconds.

    Replaces the old monitor_timeout(), which only handled the AHRS messages and ran
    without ever sleeping (ISSUES.md #2).

    Args:
        data: AvionicsData instance to update
        last_received_times: Dictionary mapping message IDs to last receive time
            (time.monotonic())
    """
    while True:
        update_status(data, last_received_times, time.monotonic())
        await asyncio.sleep(STATUS_UPDATE_PERIOD)

async def monitor_orientation(data, orientation):
    """Apply the saved screen orientation at start, then every ORIENTATION_UPDATE_PERIOD seconds end a trial
    that ran out of time and publish the orientation facts for the pages (display_orientation.py)."""
    await orientation.start()
    while True:
        await orientation.tick()
        data.display = orientation.facts()
        await asyncio.sleep(ORIENTATION_UPDATE_PERIOD)

# -----------------------------------------------------------------------------
# --- Send regular updates to the client using json                         ---
# -----------------------------------------------------------------------------
async def send_json(web_socket_response, data):
    """
    Coroutine to send the data object as json to the web socket handler.
    
    Continuously sends avionics data to connected websocket clients at a
    rate-limited interval. The send rate is controlled by JSON_UPDATE_RATE
    constant (default: 0.05 seconds = 20Hz).
    
    Args:
        web_socket_response (MyWebSocketResponse): The websocket response handler
            that contains the websocket connection
        data (AvionicsData): The avionics data object containing all sensor
            and flight data to be sent to clients
    
    Note:
        This function runs indefinitely until the program is stopped.
        It only sends data when a websocket connection exists and is not closed.
        The send rate is limited to prevent flooding the websocket connection.
    """

    while True: # Loop here forever
        clients = web_socket_response.clients
        if DEBUG:
            print("Json Loop")
            print(f"Display clients: {len(clients)}")
        # Send json data only when at least one WebSocket is open
        if clients:
            if DEBUG_JSON:
                print("Send Json")
                print("Json Send qnh = ",data.can_qnh)
                print("Json Alt = ", data.altitude)
            d = {k: v for k, v in data.__dict__.items() if not k.startswith('_')}
            text = json.dumps(d)
            # Every client gets the same message. Each client's send is its own task with its own
            # timeout, and a client whose last send has not finished is skipped this time, so a
            # slow client can never hold up the Pi's own display (it is dropped after the timeout).
            for ws in clients:
                task = web_socket_response._send_tasks.get(ws)
                if task is not None and not task.done():
                    continue
                task = asyncio.create_task(send_to_client(web_socket_response, ws, text))
                web_socket_response._send_tasks[ws] = task
                task.add_done_callback(
                    lambda t, ws=ws: web_socket_response._send_tasks.pop(ws, None)
                    if web_socket_response._send_tasks.get(ws) is t else None)
        # Rate limit: wait before next send to prevent flooding the websocket
        await asyncio.sleep(JSON_UPDATE_RATE)


async def send_to_client(web_socket_response, web_socket, text):
    """Send one message to one client. A client that fails or cannot take the message within
    CLIENT_SEND_TIMEOUT is closed and forgotten; its page reconnects by itself."""
    try:
        await asyncio.wait_for(web_socket.send_str(text), CLIENT_SEND_TIMEOUT)
    except (asyncio.TimeoutError, ConnectionResetError, ConnectionAbortedError,
            aiohttp.ClientError, RuntimeError) as e:
        info = web_socket_response._clients.get(web_socket)
        if info is not None:        # (a client that already disconnected on its own is not news)
            print(f"Display {info['role']} from {info['remote']} dropped: {type(e).__name__}", flush=True)
        try:
            await web_socket.close()
        except Exception:    # already gone
            pass


# -----------------------------------------------------------------------------
# --- Monitor flight plan directory for new/changed .fpl files              ---
# -----------------------------------------------------------------------------
async def monitor_flight_plan(data):
    """Monitor ~/flightplans for new/changed .fpl files and load the newest."""
    fp = FlightPlan()
    last_loaded_file = None
    last_mtime = 0

    while True:
        try:
            if FLIGHTPLAN_DIR.exists():
                fpl_files = sorted(
                    FLIGHTPLAN_DIR.glob('*.fpl'),
                    key=lambda f: f.stat().st_mtime,
                    reverse=True
                )
                if fpl_files:
                    newest = fpl_files[0]
                    mtime = newest.stat().st_mtime
                    if newest != last_loaded_file or mtime != last_mtime:
                        if fp.load(newest):
                            data._flight_plan = fp
                            data.route_waypoints = [
                                {'id': w['id'], 'type': w['type']}
                                for w in fp.waypoints
                            ]
                            data.active_leg = fp.active_leg
                            last_loaded_file = newest
                            last_mtime = mtime
                        else:
                            data._flight_plan = None
                            data.route_waypoints = None
                            data.active_leg = None
                else:
                    if last_loaded_file is not None:
                        print("No flight plan files found")
                        data._flight_plan = None
                        data.route_waypoints = None
                        data.active_leg = None
                        last_loaded_file = None
        except Exception as e:
            print(f"Error checking flight plans: {e}")

        await asyncio.sleep(FLIGHTPLAN_POLL_INTERVAL)


async def read_input(encoder, button, data):
    """
    Read the rotary encoder position and button status and store them in the
    data object.

    Should be called by run or gather

    Keyword arguments:
    encoder -- an adafruit rotaryio object
    button -- an adafruit digitalio object
    data -- an object in which the data can be stored
    """
    last_encoder_position = None
    consecutive_rejects = 0
    error_count = 0
    while True:
        try:
            new_encoder_position = -encoder.position
            if abs(new_encoder_position) >= ENCODER_MAX_PLAUSIBLE:
                # Corrupted I2C read (sign-bit glitch lands near +/-2^31).
                # Discard without touching the baseline.
                pass
            elif (last_encoder_position is None or
                  abs(new_encoder_position - last_encoder_position) < ENCODER_THRESHOLD):
                data.position = new_encoder_position
                last_encoder_position = new_encoder_position
                consecutive_rejects = 0
            else:
                # Plausible value but far from the baseline. One or two of
                # these are glitches; a steady stream means the baseline
                # itself is bad (poisoned by a corrupt read), so re-baseline.
                consecutive_rejects += 1
                if consecutive_rejects >= ENCODER_REBASELINE_COUNT:
                    data.position = new_encoder_position
                    last_encoder_position = new_encoder_position
                    consecutive_rejects = 0
            data.pressed = not button.value
            error_count = 0  # Reset error count on successful read
        except Exception as e:
            error_count += 1
            if DEBUG and error_count % 100 == 0:  # Only print every 100 errors to avoid spam
                print(f"Error reading encoder: {e}")
            # Keep the last known good values
            await asyncio.sleep(0.1)  # Add a small delay when errors occur
            continue
        await asyncio.sleep(ENCODER_POLL_INTERVAL)

def connect_to_rotary_encoder(addr=ENCODER_I2C_ADDRESS):
    """
    Use Seesaw to connect to an adafruit rotary encoder and return the encoder
    and the button. Returns (None, None) if the encoder is not connected.
    
    Args:
        addr (int): I2C address of the encoder (default: ENCODER_I2C_ADDRESS)
        
    Returns:
        tuple: (encoder, button) or (None, None) if connection fails
    """
    # create seesaw connection to I2C
    #my_seesaw = seesaw.Seesaw(board.I2C(), addr)
    print(f"connect_to_rotary_encoder(): bus={ENCODER_I2C_BUS} addr=0x{addr:02X}")
    try:
        my_seesaw = seesaw.Seesaw(I2C(ENCODER_I2C_BUS), addr)

        seesaw_product = (my_seesaw.get_version() >> 16) & 0xFFFF
        if DEBUG:
            print("Found product {}".format(seesaw_product))
        if seesaw_product != SEESAW_EXPECTED_PRODUCT_ID:
            print(f"Wrong firmware loaded?  Expected {SEESAW_EXPECTED_PRODUCT_ID}")

        my_seesaw.pin_mode(ENCODER_BUTTON_PIN, my_seesaw.INPUT_PULLUP)
        button = digitalio.DigitalIO(my_seesaw, 24)
        encoder = rotaryio.IncrementalEncoder(my_seesaw)

        return(encoder, button)
    except (OSError, ValueError) as e:
        print(f"Warning: Could not connect to rotary encoder at 0x{addr:02X}: {e}")
        print("Continuing without rotary encoder support.")
        return (None, None)

# -----------------------------------------------------------------------------
# --- Cleanup handlers for graceful shutdown                                 ---
# -----------------------------------------------------------------------------

# Global variables to hold CAN bus resources for cleanup
_can_bus = None
_can_notifier = None

def cleanup_can_resources():
    """Clean up CAN bus resources (notifier and bus)"""
    global _can_bus, _can_notifier
    
    notifier = _can_notifier
    bus = _can_bus
    
    # Clear globals first to prevent double cleanup
    _can_notifier = None
    _can_bus = None
    
    if notifier is not None:
        try:
            notifier.stop()
            if DEBUG_CAN:
                print("CAN notifier stopped")
        except Exception as e:
            if DEBUG_CAN:
                print(f"Error stopping CAN notifier: {e}")
    
    if bus is not None:
        try:
            bus.shutdown()
            if DEBUG_CAN:
                print("CAN bus closed")
        except Exception as e:
            if DEBUG_CAN:
                print(f"Error closing CAN bus: {e}")

# Global variable to store running tasks for cancellation
_running_tasks = None

# -----------------------------------------------------------------------------
# --- Main Loop - Called from the end of this file                          ---
# -----------------------------------------------------------------------------

async def main():
    """
    The main function for the program declared as a coroutine so that it can
    be run asynchronously.
    """
    global _running_tasks
    
    # Set up async signal handlers for graceful shutdown
    loop = asyncio.get_event_loop()
    
    def shutdown_handler():
        """Async signal handler for graceful shutdown"""
        if DEBUG:
            print("Shutdown requested, cancelling tasks...")
        # Cancel all running tasks (they should be Task objects now)
        if _running_tasks is not None:
            for task in _running_tasks:
                # Check if it's a Task object (has .done() method)
                if hasattr(task, 'done') and not task.done():
                    task.cancel()
                    if DEBUG:
                        print(f"Cancelled task: {task.get_name()}")
        # Cleanup CAN resources
        cleanup_can_resources()
    
    # Register async signal handlers (works better with asyncio)
    try:
        loop.add_signal_handler(signal.SIGINT, shutdown_handler)
        loop.add_signal_handler(signal.SIGTERM, shutdown_handler)
    except (NotImplementedError, RuntimeError):
        # Windows or signal handler already registered, fall back to signal.signal
        def sync_handler(signum, frame):
            shutdown_handler()
        signal.signal(signal.SIGINT, sync_handler)
        signal.signal(signal.SIGTERM, sync_handler)
    
    # --- Initialize Variables
    encoder = None
    button = None

    # --- Create the instance of the encoder and button
    if not DEBUG_DISABLE_ENCODER:
        # if DEBUG_DISABLE_ENCODER is False
        (encoder, button) = connect_to_rotary_encoder()

    # --- Create an instance of the AvionicsData class to store the data in
    # This object is used to store avionics data and is passed to:
    # - process_can_messages() to update data from CAN bus
    # - monitor_status() to check module heartbeats and stale data
    # - send_json() to send data to websocket clients
    # - read_input() to store encoder/button input
    # - web_socket_response.data to make data accessible to websocket handler
    avionics_data = AvionicsData()

    # --- Create an instance of the MyWebSocketResponse Class
    # --- This allows us to access the websocket once it is instantiated using
    # --- the ws object of the handler in the send_json coroutine
    web_socket_response = MyWebSocketResponse()

    # --- Create the html and web socket servers and provide the web_socket
    # --- handler. Once the servers are started they will call
    # --- the MyWebSocketResponse.handler when ever a request comes in
    await create_servers(web_socket_response.handler)

    # -------------------------------------------------------------------------
    # --- create can bus interface
    # -------------------------------------------------------------------------

    reader = None
    bus = None
    if not DEBUG_DISABLE_CAN:
        bus = can.Bus(interface='socketcan', channel=CAN_CHANNEL, bitrate=CAN_BITRATE)
        # Store bus globally for cleanup handler
        global _can_bus, _can_notifier
        _can_bus = bus
        
        # create a buffered reader
        reader = can.AsyncBufferedReader()
        # create a listener on the can bus
        listeners = [reader]
        # get the event loop
        loop = asyncio.get_event_loop()
        # create a notifier to let us know when messages arrive
        # Store notifier globally for cleanup handler
        _can_notifier = can.Notifier(bus=bus, listeners=listeners, timeout=CAN_TIMEOUT,
                                     loop=loop)

        # --- update the web socket response handler so that it can communicate
        # --- with the CAN bus and see the avionics data

        web_socket_response.can_bus = bus
    
    web_socket_response.data = avionics_data

    # Screen orientation (portrait / landscape): the saved setting and the Menu's trial with a timed revert
    orientation = display_orientation.DisplayOrientation(display_orientation.run_wlr_randr)
    web_socket_response.orientation = orientation

    # -------------------------------------------------------------------------
    # --- create a dictionary to keep track of when CAN messages are
    # --- received. 
    # -------------------------------------------------------------------------

    last_received_times = {}

    # -------------------------------------------------------------------------

    # We should now be able to use the reader to get messages
    # Build the list of coroutines to run based on what's enabled
    coroutines = [
    #    process_can_messages(reader, avionics_data, last_received_times),
        monitor_status(avionics_data, last_received_times),
        send_json(web_socket_response, avionics_data),
        monitor_flight_plan(avionics_data),
        monitor_orientation(avionics_data, orientation),
    #    read_input(encoder, button, avionics_data)
    ]
    
    if not DEBUG_DISABLE_CAN and reader is not None:
        coroutines.append(process_can_messages(reader, avionics_data, last_received_times))
    
    if encoder is not None and button is not None:
        # handle the case here we were not able to connect to the encoder
        coroutines.append(read_input(encoder, button, avionics_data))
   
    # Create Task objects from coroutines so they can be cancelled
    tasks = [asyncio.create_task(coro) for coro in coroutines]
    
    # Store tasks globally so signal handler can cancel them
    global _running_tasks
    _running_tasks = tasks
    
    # Run all tasks concurrently - this will run indefinitely
    # Use try/finally to ensure cleanup happens even if tasks are cancelled
    try:
        await asyncio.gather(*tasks, return_exceptions=True)
    except (KeyboardInterrupt, asyncio.CancelledError):
        if DEBUG:
            print("Tasks cancelled, cleaning up...")
    finally:
        # Cancel any remaining tasks
        for task in tasks:
            if not task.done():
                task.cancel()
        # Wait for tasks to finish cancelling
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)
        # Clean up CAN bus resources on shutdown
        cleanup_can_resources()
        _running_tasks = None

# -----------------------------------------------------------------------------
# --- This Python Script starts here.                                       ---
# --- main() is call using asyncio                                          ---
# -----------------------------------------------------------------------------

if __name__ == "__main__":

    asyncio.run(main())