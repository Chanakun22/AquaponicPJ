from flask import Flask, jsonify, send_file, send_from_directory, request, session, redirect, url_for
from flask_socketio import SocketIO, emit
import paho.mqtt.client as mqtt
import copy
import json
import threading
import psutil
import time
from datetime import datetime, timedelta
import os
import re
from functools import wraps
from werkzeug.security import generate_password_hash, check_password_hash
from werkzeug.middleware.proxy_fix import ProxyFix

app = Flask(__name__)
APP_DIR = os.path.dirname(os.path.abspath(__file__))

# === Authentication Config ===
AUTH_FILE = os.path.join(APP_DIR, "auth_config.json")
SESSION_SECRET_ENV = "AQUAPONICS_SECRET_KEY"
SESSION_SECURE_ENV = "AQUAPONICS_SESSION_SECURE"
AUTH_BOOTSTRAP_PASSWORD_ENV = "AQUAPONICS_BOOTSTRAP_ADMIN_PASSWORD"
OTA_PASSWORD_ENV = "AQUAPONICS_OTA_PASSWORD"
ALLOWED_USER_ROLES = {"admin", "user"}
RATE_LIMIT_LOCK = threading.Lock()
RATE_LIMIT_BUCKETS = {}
LOGIN_RATE_LIMIT_MAX_ATTEMPTS = 5
LOGIN_RATE_LIMIT_WINDOW_SEC = 300
ADMIN_MUTATION_RATE_LIMIT_MAX_ATTEMPTS = 20
ADMIN_MUTATION_RATE_LIMIT_WINDOW_SEC = 300


def _read_env(name):
    value = os.environ.get(name, "")
    if isinstance(value, str):
        return value.strip()
    return ""


def _new_random_hex(byte_length=16):
    return os.urandom(byte_length).hex()


def _app_path(*parts):
    return os.path.join(APP_DIR, *parts)


def _send_local_file(filename):
    return send_file(_app_path(filename))


def _bootstrap_auth_config():
    bootstrap_password = _read_env(AUTH_BOOTSTRAP_PASSWORD_ENV)
    config = {
        "users": [],
        "bootstrap_required": True,
    }

    if bootstrap_password:
        config["users"] = [
            {
                "username": "admin",
                "password_hash": generate_password_hash(bootstrap_password),
                "role": "admin"
            }
        ]
        config["bootstrap_required"] = False

    return config


def auth_bootstrap_required(config):
    users = config.get("users", [])
    return bool(config.get("bootstrap_required")) and not users


def normalize_user_role(role, default="user"):
    role_value = str(role or default).strip().lower()
    if role_value in ALLOWED_USER_ROLES:
        return role_value
    return None


def ensure_auth_runtime_fields(config):
    changed = False

    if not isinstance(config.get("session_epoch"), str) or not config.get("session_epoch", "").strip():
        config["session_epoch"] = _new_random_hex()
        changed = True

    users = config.get("users", [])
    if isinstance(users, list) and users and config.get("bootstrap_required"):
        config["bootstrap_required"] = False
        changed = True

    return changed


def rotate_session_epoch(config):
    config["session_epoch"] = _new_random_hex()
    return config["session_epoch"]


def get_client_ip():
    forwarded_for = request.headers.get("X-Forwarded-For", "")
    if forwarded_for:
        return forwarded_for.split(",")[0].strip() or (request.remote_addr or "unknown")
    return request.remote_addr or "unknown"


def check_rate_limit(scope, limit, window_seconds):
    now = time.time()
    client_ip = get_client_ip()
    if scope == "login":
        bucket_key = f"{scope}:{client_ip}"
    else:
        bucket_key = f"{scope}:{client_ip}:{session.get('username', 'anonymous')}"

    with RATE_LIMIT_LOCK:
        timestamps = RATE_LIMIT_BUCKETS.get(bucket_key, [])
        timestamps = [stamp for stamp in timestamps if now - stamp < window_seconds]
        if len(timestamps) >= limit:
            retry_after = max(1, int(window_seconds - (now - timestamps[0])))
            RATE_LIMIT_BUCKETS[bucket_key] = timestamps
            return True, retry_after

        timestamps.append(now)
        RATE_LIMIT_BUCKETS[bucket_key] = timestamps

    return False, 0


def rate_limit(scope, limit, window_seconds, message=None):
    def decorator(f):
        @wraps(f)
        def decorated_function(*args, **kwargs):
            limited, retry_after = check_rate_limit(scope, limit, window_seconds)
            if limited:
                response = jsonify({
                    "status": "error",
                    "message": message or "Too many requests. Please try again later."
                })
                response.status_code = 429
                response.headers["Retry-After"] = str(retry_after)
                return response
            return f(*args, **kwargs)
        return decorated_function
    return decorator


def clear_current_session():
    session.clear()


def reject_authentication(message, status_code=401):
    clear_current_session()
    if request.path.startswith('/api/'):
        return jsonify({"status": "error", "message": message}), status_code
    return redirect('/login')


def validate_current_session():
    username = str(session.get("username", "")).strip()
    if not username:
        return False, "Authentication required", None

    current_auth_config = load_auth_config()
    if auth_bootstrap_required(current_auth_config):
        return False, "Admin bootstrap required", None

    expected_epoch = str(current_auth_config.get("session_epoch", "")).strip()
    session_epoch = str(session.get("auth_epoch", "")).strip()
    if not session_epoch or session_epoch != expected_epoch:
        return False, "Session expired. Please sign in again.", None

    user = next((candidate for candidate in current_auth_config.get("users", []) if candidate.get("username") == username), None)
    if not user:
        return False, "Session expired. Please sign in again.", None

    current_role = normalize_user_role(user.get("role", "user")) or "user"
    session_role = normalize_user_role(session.get("role", "user")) or "user"
    if session_role != current_role:
        return False, "Account permissions changed. Please sign in again.", None

    return True, user, current_auth_config

def load_auth_config():
    """Load user credentials from auth config file"""
    loaded_config = {}
    try:
        if os.path.exists(AUTH_FILE):
            with open(AUTH_FILE, "r") as f:
                loaded = json.load(f)
                if isinstance(loaded, dict):
                    loaded_config = loaded
    except Exception as e:
        print(f"Error loading auth config: {e}")

    users = loaded_config.get("users", [])
    if isinstance(users, list) and users:
        loaded_config["bootstrap_required"] = False
        if ensure_auth_runtime_fields(loaded_config):
            save_auth_config(loaded_config)
        return loaded_config

    bootstrap_config = _bootstrap_auth_config()
    merged = dict(loaded_config)
    merged["users"] = bootstrap_config["users"]
    merged["bootstrap_required"] = bootstrap_config["bootstrap_required"]
    ensure_auth_runtime_fields(merged)

    if merged["users"]:
        save_auth_config(merged)
        return merged

    print(
        f"WARNING: Admin bootstrap required. Set {AUTH_BOOTSTRAP_PASSWORD_ENV} and restart the service."
    )
    save_auth_config(merged)
    return merged

def save_auth_config(config):
    """Save auth config to file"""
    try:
        tmp_file = f"{AUTH_FILE}.tmp"
        with open(tmp_file, "w") as f:
            json.dump(config, f, indent=2)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_file, AUTH_FILE)
    except Exception as e:
        print(f"Error saving auth config: {e}")

def env_flag_enabled(name):
    value = _read_env(name).lower()
    return value in ("1", "true", "yes", "on")

def ensure_session_secret(config):
    env_secret = _read_env(SESSION_SECRET_ENV)
    if env_secret:
        return env_secret

    secret_key = config.get("session_secret", "")
    if isinstance(secret_key, str):
        secret_key = secret_key.strip()
    else:
        secret_key = ""

    if secret_key:
        return secret_key

    secret_key = os.urandom(32).hex()
    config["session_secret"] = secret_key
    save_auth_config(config)
    return secret_key

auth_config = load_auth_config()
app.wsgi_app = ProxyFix(app.wsgi_app, x_for=1, x_proto=1, x_host=1)
app.secret_key = ensure_session_secret(auth_config)
app.config.update(
    SESSION_COOKIE_HTTPONLY=True,
    SESSION_COOKIE_SAMESITE='Lax',
    SESSION_COOKIE_SECURE=env_flag_enabled(SESSION_SECURE_ENV),
    PREFERRED_URL_SCHEME='https',
    PERMANENT_SESSION_LIFETIME=timedelta(days=7)
)
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

def login_required(f):
    """Decorator to protect routes — redirects to /login if not authenticated"""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        is_valid, payload, _ = validate_current_session()
        if not is_valid:
            return reject_authentication(payload, 401)
        return f(*args, **kwargs)
    return decorated_function

def admin_required(f):
    """Decorator for admin-only routes — user role must be 'admin'"""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        is_valid, user, _ = validate_current_session()
        if not is_valid:
            return reject_authentication(user, 401)
        if normalize_user_role(user.get('role', 'user')) != 'admin':
            if request.path.startswith('/api/'):
                return jsonify({"status": "error", "message": "Admin access required"}), 403
            return redirect('/')
        return f(*args, **kwargs)
    return decorated_function

# === Settings File ===
SETTINGS_FILE = _app_path("settings.json")
FIRMWARE_CONFIG_FILE = os.path.normpath(os.path.join(APP_DIR, "..", "include", "config.h"))


def _read_firmware_pin_value(macro_name):
    try:
        with open(FIRMWARE_CONFIG_FILE, "r", encoding="utf-8") as f:
            content = f.read()
    except OSError:
        return None

    match = re.search(rf"^\s*#define\s+{re.escape(macro_name)}\s+(-?\d+)\b", content, re.MULTILINE)
    if not match:
        return None

    try:
        return int(match.group(1))
    except ValueError:
        return None


def _load_water_hardware_defaults():
    sump_low_pin = _read_firmware_pin_value("SUMP_LEVEL_LOW_PIN")
    sump_high_pin = _read_firmware_pin_value("SUMP_LEVEL_HIGH_PIN")
    overflow_pin = _read_firmware_pin_value("FISH_TANK_OVERFLOW_PIN")
    route_valve_pin = _read_firmware_pin_value("REFILL_ROUTE_VALVE_PIN")

    def _configured(pin_value):
        if pin_value is None:
            return None
        return pin_value >= 0

    low_configured = _configured(sump_low_pin)
    high_configured = _configured(sump_high_pin)

    return {
        "has_level_sensors": (
            low_configured and high_configured
            if low_configured is not None and high_configured is not None
            else None
        ),
        "has_overflow_sensor": _configured(overflow_pin),
        "has_route_valve": _configured(route_valve_pin),
    }


WATER_HARDWARE_DEFAULTS = _load_water_hardware_defaults()


def _apply_water_hardware_defaults(water_status):
    normalized = dict(water_status or {})
    for key, configured in WATER_HARDWARE_DEFAULTS.items():
        if configured is not None:
            normalized[key] = configured
    return normalized


WATER_CONFIG_KEYS = (
    "circulation_enabled",
    "refill_enabled",
    "manual_refill",
    "refill_max_runtime_ms",
    "refill_min_interval_ms",
    "preferred_route",
    "allow_direct_sump_refill",
    "fish_refill_interval_ms",
    "fish_refill_max_runtime_ms",
)

_water_runtime_status = {}


def _water_config_snapshot():
    water_settings = app_settings.get("water_system", {})
    if not isinstance(water_settings, dict):
        return {}

    return {
        key: water_settings.get(key)
        for key in WATER_CONFIG_KEYS
        if key in water_settings
    }


def _current_water_status():
    status = dict(_apply_water_hardware_defaults(_water_runtime_status))
    status.update(_water_config_snapshot())

    status.setdefault('status_seen', last_data.get('water_status_seen', False))
    status.setdefault('state', last_data.get('water_state', 'IDLE'))
    status.setdefault('state_label_th', last_data.get('water_state_label_th', 'พร้อมทำงาน'))
    status.setdefault('reason', last_data.get('water_reason', 'Waiting for ESP32 status'))
    status.setdefault('preferred_route', last_data.get('preferred_route', 'AUTO'))
    status.setdefault('active_route', last_data.get('active_route', 'NONE'))
    status.setdefault('allow_direct_sump_refill', last_data.get('allow_direct_sump_refill', False))
    status.setdefault('manual_refill', last_data.get('manual_refill', False))
    status.setdefault('alarm_active', last_data.get('water_alarm', False))
    status.setdefault('route_blocked', last_data.get('route_blocked', False))
    status.setdefault('route_valve_output', last_data.get('route_valve_output', False))
    status.setdefault('has_route_valve', last_data.get('has_route_valve', False))
    status.setdefault('circulation_output', last_data.get('circulation_output', False))
    status.setdefault('refill_output', last_data.get('refill_output', False))
    status.setdefault('circulation_pump_output', last_data.get('circulation_pump_output', False))
    status.setdefault('fish_tank_refill_output', last_data.get('fish_tank_refill_output', False))
    status.setdefault('mix_tank_refill_output', last_data.get('mix_tank_refill_output', False))
    status.setdefault('water_dilution_active', last_data.get('water_dilution_active', False))
    status.setdefault('mix_tank_settling_active', last_data.get('mix_tank_settling_active', False))
    status.setdefault('mix_tank_control_zone', last_data.get('mix_tank_control_zone', True))
    status.setdefault('dilution_hold_remaining_ms', last_data.get('dilution_hold_remaining_ms', 0))
    status.setdefault('fish_refill_ready', last_data.get('fish_refill_ready', True))
    status.setdefault('fish_refill_wait_remaining_ms', last_data.get('fish_refill_wait_remaining_ms', 0))
    status.setdefault('sump_low', last_data.get('sump_low', False))
    status.setdefault('sump_high', last_data.get('sump_high', False))
    status.setdefault('has_level_sensors', last_data.get('has_level_sensors'))
    status.setdefault('overflow_alarm', last_data.get('fish_overflow', False))
    status.setdefault('has_overflow_sensor', last_data.get('has_overflow_sensor'))
    status.setdefault('status_updated_at', None)

    return status


def _deep_merge_dict(base, overrides):
    merged = copy.deepcopy(base)
    if not isinstance(overrides, dict):
        return merged

    for key, value in overrides.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = _deep_merge_dict(merged[key], value)
        else:
            merged[key] = value
    return merged


def _coerce_int(value, fallback, minimum=None, maximum=None):
    try:
        normalized = int(value)
    except (TypeError, ValueError):
        normalized = fallback

    if minimum is not None and normalized < minimum:
        normalized = minimum
    if maximum is not None and normalized > maximum:
        normalized = maximum
    return normalized


def _normalize_fish_feeder_settings(fish_feeder, current=None):
    current = current if isinstance(current, dict) else {}
    fish_feeder = fish_feeder if isinstance(fish_feeder, dict) else {}

    command_source = str(
        fish_feeder.get("command_source", current.get("command_source", "local_web"))
    ).lower()
    if command_source not in ["netpie", "local_web"]:
        command_source = current.get("command_source", "local_web")
        if command_source not in ["netpie", "local_web"]:
            command_source = "local_web"

    feed_time = str(fish_feeder.get("feed_time", current.get("feed_time", "08:00"))).strip() or "08:00"

    normalized = dict(current)
    normalized.update({
        "command_source": command_source,
        "enabled": bool(fish_feeder.get("enabled", current.get("enabled", False))),
        "feed_day": _coerce_int(fish_feeder.get("feed_day", current.get("feed_day", 7)), 7, 0, 7),
        "feed_time": feed_time,
        "duration_ms": _coerce_int(
            fish_feeder.get("duration_ms", current.get("duration_ms", 2000)),
            current.get("duration_ms", 2000),
            250,
            10000,
        ),
    })

    for key in ["state", "running", "has_output", "last_feed_at", "reason"]:
        if key in fish_feeder:
            normalized[key] = fish_feeder[key]

    return normalized

def load_settings():
    """Load settings from JSON file"""
    default = {
        "thresholds": {
            "ph_min": 6.0, "ph_max": 7.5,
            "water_temp_min": 22, "water_temp_max": 30,
            "air_temp_min": 25, "air_temp_max": 35,
            "humidity_min": 50, "humidity_max": 80,
            "tds_min": 300, "tds_max": 600
        },
        "notifications": {
            "line_token": "",
            "alert_cooldown_min": 15,
            "enabled": False
        },
        "display": {
            "graph_days": 3,
            "device_name": "Aquaponics System"
        },
        "sensor_config": {
            "tds": True,
            "ph": True,
            "water": True,
            "air": True,
            "light": True
        },
        "water_system": {
            "circulation_enabled": True,
            "refill_enabled": False,
            "manual_refill": False,
            "refill_max_runtime_ms": 120000,
            "refill_min_interval_ms": 300000,
            "preferred_route": "AUTO",
            "active_route": "NONE",
            "allow_direct_sump_refill": False,
            "fish_refill_interval_ms": 604800000,
            "fish_refill_max_runtime_ms": 30000,
            "state": "IDLE",
            "state_label_th": "พร้อมทำงาน",
            "reason": "Waiting for ESP32 status",
            "alarm_active": False,
            "circulation_pump_output": False,
            "fish_tank_refill_output": False,
            "mix_tank_refill_output": False,
            "water_dilution_active": False,
            "mix_tank_settling_active": False,
            "mix_tank_control_zone": True,
            "dilution_hold_remaining_ms": 0,
            "fish_refill_ready": True,
            "fish_refill_wait_remaining_ms": 0,
            "sump_low": False,
            "sump_high": False,
            "overflow_alarm": False,
            "route_blocked": False,
            "route_valve_output": False,
            "has_route_valve": False,
            "has_level_sensors": None,
            "has_overflow_sensor": None,
            "status_seen": False,
            "status_updated_at": None
        },
        "fan_control": {
            "enabled": False,
            "auto_mode": True,
            "manual_state": False,
            "temp_on_c": 32.0,
            "temp_off_c": 30.0,
            "humidity_on_pct": 80.0,
            "humidity_off_pct": 75.0,
            "state": "DISABLED",
            "running": False,
            "has_output": False,
            "reason": "Waiting for ESP32 status"
        },
        "light_control": {
            "command_source": "netpie",
            "enabled": False,
            "manual_state": False,
            "on_day": 0,
            "on_time": "06:00",
            "off_day": 0,
            "off_time": "18:00",
            "running": False,
            "ntp_synced": False,
            "has_output": True,
            "reason": "Waiting for ESP32 status"
        },
        "fish_feeder": {
            "command_source": "local_web",
            "enabled": False,
            "feed_day": 7,
            "feed_time": "08:00",
            "duration_ms": 2000,
            "state": "DISABLED",
            "running": False,
            "has_output": False,
            "last_feed_at": "Never",
            "reason": "Waiting for ESP32 status"
        },
        "tds_calibration": {
            "low_ppm": 500,
            "low_voltage": 0.0,
            "high_ppm": 1000,
            "high_voltage": 0.0,
            "calibrated": False
        },
        "ph_calibration": {
            "cal401_done": False,
            "cal686_done": False,
            "cal918_done": False,
            "last_voltage": 0.0,
            "last_ph": 0.0
        },
        "camera": {
            "width": 1280,
            "height": 720,
            "framerate": 15,
            "quality": 80
        },
        "secure": {
            "ota_password": "",
            "esp_ip": "192.168.10.10",
            "terminal_ip": "192.168.10.2"
        }
    }
    try:
        if os.path.exists(SETTINGS_FILE):
            with open(SETTINGS_FILE, "r") as f:
                loaded = json.load(f)
                merged = _deep_merge_dict(default, loaded)
                merged["water_system"] = _apply_water_hardware_defaults(merged.get("water_system", {}))
                merged["fish_feeder"] = _normalize_fish_feeder_settings(
                    merged.get("fish_feeder", {}),
                    default["fish_feeder"],
                )
                return merged
    except Exception as e:
        print(f"Error loading settings: {e}")
    default["water_system"] = _apply_water_hardware_defaults(default.get("water_system", {}))
    return default



# Load settings on startup
app_settings = load_settings()

# === Line Notify ===
import requests

# Track last alert time per sensor to implement cooldown
_last_alert_time = {}

def send_line_notify(message):
    """Send notification via Line Notify"""
    global app_settings
    token = app_settings.get("notifications", {}).get("line_token", "")
    if not token:
        print("⚠️ Line Notify: No token configured")
        return False
    
    try:
        # Use LINE Messaging API (Broadcast)
        url = "https://api.line.me/v2/bot/message/broadcast"
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {token}"
        }
        payload = {
            "messages": [
                {
                    "type": "text",
                    "text": message
                }
            ]
        }
        
        response = requests.post(url, headers=headers, json=payload, timeout=5)
        
        if response.status_code == 200:
            print(f"✅ LINE Alert sent: {message}")
            save_log(f"📱 LINE Alert: {message}")
            return True
        else:
            print(f"❌ LINE Alert failed: {response.status_code} - {response.text}")
            return False
    except Exception as e:
        print(f"❌ LINE Error: {e}")
        return False

def check_thresholds(data):
    """Check sensor data against thresholds and send alerts"""
    global app_settings, _last_alert_time
    
    # Check if notifications are enabled
    notifications = app_settings.get("notifications", {})
    if not notifications.get("enabled", False):
        return
    
    thresholds = app_settings.get("thresholds", {})
    cooldown_min = notifications.get("alert_cooldown_min", 15)
    now = time.time()
    
    alerts = []
    
    # Check each sensor
    checks = [
        ("ph", data.get("ph", 0), thresholds.get("ph_min", 0), thresholds.get("ph_max", 14), "pH"),
        ("water_temp", data.get("water_temp", 0), thresholds.get("water_temp_min", 0), thresholds.get("water_temp_max", 100), "อุณหภูมิน้ำ"),
        ("air_temp", data.get("air_temp", 0), thresholds.get("air_temp_min", 0), thresholds.get("air_temp_max", 100), "อุณหภูมิอากาศ"),
        ("humidity", data.get("humidity", 0), thresholds.get("humidity_min", 0), thresholds.get("humidity_max", 100), "ความชื้น"),
        ("tds", data.get("tds", 0), thresholds.get("tds_min", 0), thresholds.get("tds_max", 2000), "TDS"),
    ]
    
    for key, value, min_val, max_val, name in checks:
        if value is None or value == 0:
            continue
        
        # Check if out of range
        if value < min_val or value > max_val:
            # Check cooldown
            last_time = _last_alert_time.get(key, 0)
            if now - last_time > cooldown_min * 60:
                if value < min_val:
                    alerts.append(f"⚠️ {name} ต่ำเกินไป: {value} (ต่ำกว่า {min_val})")
                else:
                    alerts.append(f"⚠️ {name} สูงเกินไป: {value} (สูงกว่า {max_val})")
                _last_alert_time[key] = now
    
    # Send alerts if any
    if alerts:
        message = "\n🌿 Aquaponics Alert!\n" + "\n".join(alerts)
        send_line_notify(message)

# เก็บข้อมูลล่าสุดไว้ในตัวแปร Memory
last_auto_state = "IDLE"
last_data = {
    # ESP32 Data
    "water_temp": 0, "air_temp": 0, "humidity": 0,
    "tds": 0, "ph": 0, "light": 0,
    "fan_enabled": False, "fan_auto_mode": True, "fan_manual_state": False,
    "fan_running": False, "fan_state": "DISABLED", "fan_reason": "Waiting for ESP32 status", "fan_has_output": False,
    "uptime_sec": 0, "wifi_rssi": 0, "free_heap": 0,
    
    # Pi Server Data
    "pi_cpu_percent": 0,
    "pi_ram_percent": 0,
    "pi_ram_total": 0,
    "pi_ram_used": 0,
    "pi_temp": 0
}

# === System Monitor Function ===
def get_pi_temp():
    try:
        with open("/sys/class/thermal/thermal_zone0/temp", "r") as f:
            temp = float(f.read()) / 1000
            return temp
    except:
        return 0

# === Database Setup (SQLite) ===
import sqlite3

DB_FILE = _app_path("aquaponics.db")
db_lock = threading.Lock()  # ป้องกัน concurrent write จาก MQTT Thread + Web Thread

def init_db():
    with db_lock:
        conn = None
        try:
            conn = sqlite3.connect(DB_FILE)
            cursor = conn.cursor()
            
            # Sensor Data Table
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS sensors (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                    water_temp REAL,
                    air_temp REAL,
                    humidity REAL,
                    tds REAL,
                    ph REAL,
                    light REAL
                )
            ''')
            
            # Settings History Table
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS settings_history (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                    settings_json TEXT
                )
            ''')
            
            # Activity Logs Table (Admin Audit Trail)
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS activity_logs (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                    username TEXT,
                    action TEXT,
                    detail TEXT,
                    ip_address TEXT
                )
            ''')
            
            conn.commit()
            print("✅ Database initialized")
        except Exception as e:
            print(f"❌ Database Error: {e}")
        finally:
            if conn:
                conn.close()

# Initialize DB on start
init_db()

def log_activity(username, action, detail="", ip=""):
    """Log an important admin action to the database"""
    with db_lock:
        conn = None
        try:
            conn = sqlite3.connect(DB_FILE)
            cursor = conn.cursor()
            cursor.execute(
                'INSERT INTO activity_logs (username, action, detail, ip_address) VALUES (?, ?, ?, ?)',
                (username, action, detail, ip)
            )
            conn.commit()
        except Exception as e:
            print(f"❌ Activity log error: {e}")
        finally:
            if conn:
                conn.close()

def save_settings_to_db(settings):
    """Save settings snapshot to database"""
    with db_lock:
        conn = None
        try:
            conn = sqlite3.connect(DB_FILE)
            cursor = conn.cursor()
            cursor.execute('INSERT INTO settings_history (settings_json) VALUES (?)', (json.dumps(settings),))
            conn.commit()
            # print("💾 Settings saved to DB history")
        except Exception as e:
            print(f"❌ Failed to save settings to DB: {e}")
        finally:
            if conn:
                conn.close()

def save_settings(settings):
    """Save settings to JSON file atomically and Database"""
    try:
        settings_to_save = _deep_merge_dict(load_settings(), settings)

        # 1. Save to File (Actual Config) - Atomic Write
        tmp_file = f"{SETTINGS_FILE}.tmp"
        with open(tmp_file, "w") as f:
            json.dump(settings_to_save, f, indent=2)
            f.flush()
            os.fsync(f.fileno()) # Ensure data is written to disk
        
        # Atomically replace the old file with the new one
        os.replace(tmp_file, SETTINGS_FILE)
            
        # 2. Save to DB (History/Backup)
        save_settings_to_db(settings_to_save)
        
        return True
    except Exception as e:
        print(f"Error saving settings: {e}")
        # Clean up tmp file if error occurred
        if os.path.exists(f"{SETTINGS_FILE}.tmp"):
            try:
                os.remove(f"{SETTINGS_FILE}.tmp")
            except:
                pass
        return False

# === Log Storage (In-Memory + File) ===
from collections import deque
LOG_FILE = _app_path("system.log")
log_buffer = deque(maxlen=50)  # Keep last 50 logs in memory for web

def save_log(msg):
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    log_entry = f"[{timestamp}] {msg}"
    
    # Add to memory
    log_buffer.appendleft(log_entry)
    
    # Add to file
    try:
        with open(LOG_FILE, "a") as f:
            f.write(log_entry + "\n")
    except Exception as e:
        print(f"Error saving log: {e}")

# === MQTT Functions ===
def on_connect(client, userdata, flags, rc):
    print(f"✅ MQTT Connected with result code {rc}")
    client.subscribe("aquaponics/sensors")
    client.subscribe("aquaponics/logs")
    client.subscribe("aquaponics/status/sensors")
    client.subscribe("aquaponics/status/ph_cal")
    client.subscribe("aquaponics/status/fan_control")
    client.subscribe("aquaponics/status/water_system")
    client.subscribe("aquaponics/status/light_control")
    client.subscribe("aquaponics/status/fish_feeder")
    client.subscribe("aquaponics/test/result")


ESP_HEARTBEAT_TOPICS = {
    "aquaponics/sensors",
    "aquaponics/logs",
    "aquaponics/status/sensors",
    "aquaponics/status/ph_cal",
    "aquaponics/status/fan_control",
    "aquaponics/status/water_system",
    "aquaponics/status/light_control",
    "aquaponics/status/fish_feeder",
    "aquaponics/test/result",
}


def mark_esp_heartbeat(topic, is_retained=False):
    global last_esp_update, esp_online

    if topic not in ESP_HEARTBEAT_TOPICS or is_retained:
        return

    last_esp_update = time.time()
    if not esp_online:
        esp_online = True
        save_log("✅ ESP32 Reconnected!")


def on_message(client, userdata, msg):
    global last_data, last_esp_update, esp_online, app_settings, _water_runtime_status
    try:
        topic = msg.topic
        payload = msg.payload.decode()
        mark_esp_heartbeat(topic, msg.retain)
        
        if topic == "aquaponics/logs":
            print(f"📝 Log: {payload}")
            save_log(payload)
            return
        
        # Forward HW test results to WebSocket clients
        if topic == "aquaponics/test/result":
            try:
                result_data = json.loads(payload)
                socketio.emit('hwtest_result', result_data)
                print(f"🔧 HW Test Result: {result_data}")
            except Exception as e:
                print(f"❌ HW Test result parse error: {e}")
            return
            
        if topic == "aquaponics/status/sensors":
            try:
                new_config = json.loads(payload)
                # Update local settings if different
                if app_settings.get("sensor_config") != new_config:
                    app_settings["sensor_config"] = new_config
                    save_settings(app_settings)
                    print(f"🔄 Synced Sensor Config from ESP32: {new_config}")
            except Exception as e:
                print(f"❌ Error syncing sensor config: {e}")
            return
        
        if topic == "aquaponics/status/ph_cal":
            try:
                ph_status = json.loads(payload)
                app_settings.setdefault("ph_calibration", {})
                app_settings["ph_calibration"]["last_voltage"] = ph_status.get("ph_voltage", 0)
                app_settings["ph_calibration"]["last_ph"] = ph_status.get("ph_value", 0)
                app_settings["ph_calibration"]["cal401_done"] = ph_status.get(
                    "cal401_done",
                    app_settings["ph_calibration"].get("cal401_done", False)
                )
                app_settings["ph_calibration"]["cal686_done"] = ph_status.get(
                    "cal686_done",
                    ph_status.get("calibrated", app_settings["ph_calibration"].get("cal686_done", False))
                )
                app_settings["ph_calibration"]["cal918_done"] = ph_status.get(
                    "cal918_done",
                    app_settings["ph_calibration"].get("cal918_done", False)
                )
                save_settings(app_settings)
                print(f"🔄 pH Calibration status updated: {ph_status}")
                save_log(f"pH Calibration updated: voltage={ph_status.get('ph_voltage')}mV, pH={ph_status.get('ph_value')}")
            except Exception as e:
                print(f"❌ Error handling pH cal status: {e}")
            return

        if topic == "aquaponics/status/water_system":
            try:
                water_status = json.loads(payload)
                previous_water = _current_water_status()
                _water_runtime_status = _apply_water_hardware_defaults({
                    **water_status,
                    "status_seen": True,
                    "status_updated_at": int(time.time()),
                })
                current_water = _current_water_status()
                print(f"🔄 Water system status updated: {water_status}")
                socketio.emit('water_status_update', current_water)

                if water_status.get("alarm_active") and (
                    not previous_water.get("alarm_active") or
                    previous_water.get("reason") != water_status.get("reason")
                ):
                    save_log(f"🚨 Water system alarm: {water_status.get('reason', 'Unknown alarm')}")
            except Exception as e:
                print(f"❌ Error handling water system status: {e}")
            return

        if topic == "aquaponics/status/fan_control":
            try:
                fan_status = json.loads(payload)
                app_settings.setdefault("fan_control", {})
                app_settings["fan_control"].update(fan_status)
                print(f"🔄 Fan control status updated: {fan_status}")
            except Exception as e:
                print(f"❌ Error handling fan control status: {e}")
            return

        if topic == "aquaponics/status/light_control":
            try:
                light_status = json.loads(payload)
                app_settings.setdefault("light_control", {})
                app_settings["light_control"].update(light_status)
                print(f"🔄 Light control status updated: {light_status}")
            except Exception as e:
                print(f"❌ Error handling light control status: {e}")
            return

        if topic == "aquaponics/status/fish_feeder":
            try:
                feeder_status = json.loads(payload)
                app_settings.setdefault("fish_feeder", {})
                app_settings["fish_feeder"].update(feeder_status)
                print(f"🔄 Fish feeder status updated: {feeder_status}")
            except Exception as e:
                print(f"❌ Error handling fish feeder status: {e}")
            return
        


        # It's sensor data
        data = json.loads(payload)
        for key in data:
            last_data[key] = data[key]

        if any(key in data for key in [
            "water_status_seen", "water_state", "water_reason", "sump_low", "sump_high",
            "has_level_sensors", "fish_overflow", "has_overflow_sensor", "active_route"
        ]):
            current_water = _current_water_status()
            _water_runtime_status.update(_apply_water_hardware_defaults({
                "status_seen": bool(data.get("water_status_seen", current_water.get("status_seen", False))),
                "status_updated_at": int(time.time()),
                "state": data.get("water_state", current_water.get("state", "IDLE")),
                "state_label_th": data.get("water_state_label_th", current_water.get("state_label_th", "พร้อมทำงาน")),
                "reason": data.get("water_reason", current_water.get("reason", "Waiting for ESP32 status")),
                "preferred_route": data.get("preferred_route", current_water.get("preferred_route", "AUTO")),
                "active_route": data.get("active_route", current_water.get("active_route", "NONE")),
                "allow_direct_sump_refill": data.get("allow_direct_sump_refill", current_water.get("allow_direct_sump_refill", False)),
                "manual_refill": data.get("manual_refill", current_water.get("manual_refill", False)),
                "alarm_active": data.get("water_alarm", current_water.get("alarm_active", False)),
                "route_blocked": data.get("route_blocked", current_water.get("route_blocked", False)),
                "route_valve_output": data.get("route_valve_output", current_water.get("route_valve_output", False)),
                "has_route_valve": data.get("has_route_valve", current_water.get("has_route_valve", False)),
                "circulation_output": data.get("circulation_output", current_water.get("circulation_output", False)),
                "refill_output": data.get("refill_output", current_water.get("refill_output", False)),
                "circulation_pump_output": data.get("circulation_pump_output", current_water.get("circulation_pump_output", False)),
                "fish_tank_refill_output": data.get("fish_tank_refill_output", current_water.get("fish_tank_refill_output", False)),
                "mix_tank_refill_output": data.get("mix_tank_refill_output", current_water.get("mix_tank_refill_output", False)),
                "water_dilution_active": data.get("water_dilution_active", current_water.get("water_dilution_active", False)),
                "mix_tank_settling_active": data.get("mix_tank_settling_active", current_water.get("mix_tank_settling_active", False)),
                "mix_tank_control_zone": data.get("mix_tank_control_zone", current_water.get("mix_tank_control_zone", True)),
                "dilution_hold_remaining_ms": data.get("dilution_hold_remaining_ms", current_water.get("dilution_hold_remaining_ms", 0)),
                "fish_refill_ready": data.get("fish_refill_ready", current_water.get("fish_refill_ready", True)),
                "fish_refill_wait_remaining_ms": data.get("fish_refill_wait_remaining_ms", current_water.get("fish_refill_wait_remaining_ms", 0)),
                "sump_low": data.get("sump_low", current_water.get("sump_low", False)),
                "sump_high": data.get("sump_high", current_water.get("sump_high", False)),
                "has_level_sensors": data.get("has_level_sensors", current_water.get("has_level_sensors")),
                "overflow_alarm": data.get("fish_overflow", current_water.get("overflow_alarm", False)),
                "has_overflow_sensor": data.get("has_overflow_sensor", current_water.get("has_overflow_sensor")),
            }))
            
        # Check for automation state change (for LINE notify)
        new_auto_state = data.get("auto_state")
        if new_auto_state:
            global last_auto_state
            if new_auto_state == "EXECUTING" and last_auto_state != "EXECUTING":
                # State just changed to running!
                reason = data.get("auto_reason", "Auto Action Triggered")
                message = f"\n🤖 Automation Engine\nStarted Auto Dosing: {reason}"
                send_line_notify(message)
                save_log(f"🤖 Automation: Started Auto Dosing ({reason})")
            last_auto_state = new_auto_state
        
        # Preserve the last good reading when a sensor key is absent from a packet.
        # The firmware may omit a value temporarily while waiting for the next valid read.
        SENSOR_KEYS = ["water_temp", "air_temp", "humidity", "tds", "ph", "light"]
        sensor_config = app_settings.get("sensor_config", {})
        sensor_config_keys = {
            "water_temp": "water",
            "air_temp": "air",
            "humidity": "air",
            "tds": "tds",
            "ph": "ph",
            "light": "light",
        }
        for sk in SENSOR_KEYS:
            config_key = sensor_config_keys.get(sk)
            if config_key and sensor_config.get(config_key) is False:
                last_data[sk] = None
            
        # === Save to DB (Filtered) ===
        # Save only every 60 seconds to save space
        save_data_to_db(data)
        
        # === Check Thresholds & Send Alerts ===
        check_thresholds(data)

        socketio.emit('dashboard_update', build_dashboard_data())
            
    except Exception as e:
        print(f"❌ Error parsing MQTT: {e}")

# === DB Throttling ===
last_db_save = 0

def save_data_to_db(data):
    global last_db_save
    now = time.time()
    
    if now - last_db_save < 60: # 60 seconds interval
        return

    def sensor_db_value(payload, key):
        return payload[key] if key in payload else None

    with db_lock:
        conn = None
        try:
            conn = sqlite3.connect(DB_FILE)
            cursor = conn.cursor()
            cursor.execute('''
                INSERT INTO sensors (water_temp, air_temp, humidity, tds, ph, light)
                VALUES (?, ?, ?, ?, ?, ?)
            ''', (
                sensor_db_value(data, "water_temp"),
                sensor_db_value(data, "air_temp"),
                sensor_db_value(data, "humidity"),
                sensor_db_value(data, "tds"),
                sensor_db_value(data, "ph"),
                sensor_db_value(data, "light")
            ))
            conn.commit()
            last_db_save = now
            # print("💾 Data saved to DB") 
        except Exception as e:
            print(f"DB Insert Error: {e}")
        finally:
            if conn:
                conn.close()

def normalize_history_value(sensor_key, value):
    if value is None:
        return None

    invalid_exact_values = {
        "water_temp": {0, 85.0, -127},
        "air_temp": {0},
        "humidity": {0},
        "ph": {0},
    }

    if sensor_key in invalid_exact_values and value in invalid_exact_values[sensor_key]:
        return None

    if sensor_key == "tds" and value <= 0:
        return None

    if sensor_key == "ph" and (value <= 0 or value > 14):
        return None

    if sensor_key == "humidity" and (value < 0 or value > 100):
        return None

    return value

# === Start MQTT in Background Thread ===
mqtt_client = None  # Global reference for publishing
MQTT_RETRY_DELAY_SEC = 5


def on_disconnect(client, userdata, rc):
    if rc != 0:
        message = f"MQTT disconnected unexpectedly (rc={rc}); waiting for reconnect"
        print(f"⚠️ {message}")
        save_log(message)



def start_mqtt():
    global mqtt_client
    while True:
        client = mqtt.Client()
        client.on_connect = on_connect
        client.on_message = on_message
        client.on_disconnect = on_disconnect
        client.reconnect_delay_set(min_delay=1, max_delay=30)
        mqtt_client = client

        try:
            # "localhost" เพราะ Mosquitto อยู่เครื่องเดียวกับ app.py
            print("🔌 Connecting to local MQTT broker...")
            client.connect("localhost", 1883, 60)
            client.loop_forever()

            message = "MQTT loop stopped unexpectedly; retrying"
            print(f"⚠️ {message}")
            save_log(message)
        except Exception as e:
            print(f"❌ Could not connect to MQTT Broker: {e}")
            save_log(f"MQTT Error: {e}")
        finally:
            try:
                client.disconnect()
            except Exception:
                pass

        time.sleep(MQTT_RETRY_DELAY_SEC)

# === Auth Routes (Public) ===
@app.route('/login')
def login_page():
    if 'username' in session:
        return redirect('/')
    return _send_local_file('login.html')

@app.route('/api/login', methods=['POST'])
@rate_limit(
    'login',
    LOGIN_RATE_LIMIT_MAX_ATTEMPTS,
    LOGIN_RATE_LIMIT_WINDOW_SEC,
    message='Too many login attempts. Please wait a few minutes before trying again.'
)
def api_login():
    global auth_config
    try:
        data = request.get_json()
        username = data.get('username', '').strip()
        password = data.get('password', '')
        ip = request.remote_addr

        if not username or not password:
            return jsonify({"status": "error", "message": "กรุณากรอก username และ password"}), 400

        # Find user
        auth_config = load_auth_config()
        if auth_bootstrap_required(auth_config):
            return jsonify({
                "status": "error",
                "message": (
                    f"Admin bootstrap required. Set {AUTH_BOOTSTRAP_PASSWORD_ENV} on the Pi and restart the service."
                )
            }), 503

        user = next((u for u in auth_config.get('users', []) if u['username'] == username), None)

        if user and check_password_hash(user['password_hash'], password):
            session['username'] = username
            session['role'] = normalize_user_role(user.get('role', 'user')) or 'user'
            session['auth_epoch'] = auth_config.get('session_epoch', '')
            session.permanent = True
            app.permanent_session_lifetime = timedelta(days=7)
            log_activity(username, 'login', 'Login successful', ip)
            print(f"✅ Login: {username} from {ip}")
            return jsonify({"status": "ok", "redirect": "/"})
        else:
            log_activity(username or '(unknown)', 'login', 'Login failed — wrong credentials', ip)
            print(f"❌ Login failed: {username} from {ip}")
            return jsonify({"status": "error", "message": "Username หรือ Password ไม่ถูกต้อง"}), 401
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/logout', methods=['POST'])
def api_logout():
    username = session.get('username', 'unknown')
    ip = request.remote_addr
    log_activity(username, 'logout', 'User logged out', ip)
    session.clear()
    return jsonify({"status": "ok"})

@app.route('/api/me')
@login_required
def api_me():
    """Return current user info for frontend role-based UI"""
    return jsonify({
        "username": session.get('username', ''),
        "role": session.get('role', 'user')
    })

# === Web Server Routes ===
# Public (no auth needed)
@app.route('/base.css')
def serve_base_css():
    return _send_local_file('base.css')

@app.route('/header.js')
def serve_header_js():
    return _send_local_file('header.js')

@app.route('/pwa/<path:filename>')
def serve_pwa(filename):
    return send_from_directory(_app_path('pwa'), filename)

@app.route('/static/<path:filename>')
def serve_static_assets(filename):
    return send_from_directory(_app_path('static'), filename)

# Protected pages
@app.route('/')
@login_required
def index():
    return _send_local_file('index.html')

@app.route('/graphs')
@login_required
def graphs_page():
    return _send_local_file('graphs.html')

@app.route('/full_logs')
@login_required
def full_logs_page():
    return _send_local_file('full_logs.html')

@app.route('/api/sensors')
@login_required
def get_sensors():
    return jsonify(last_data)

@app.route('/settings')
@admin_required
def settings_page():
    return _send_local_file('settings.html')

@app.route('/live')
@login_required
def live_page():
    return _send_local_file('live.html')

@app.route('/cam-stream')
@login_required
def cam_stream():
    """Proxy the camera stream from localhost:8081 for remote access"""
    try:
        from flask import Response
        import requests
        req = requests.get('http://127.0.0.1:8081/stream', stream=True, timeout=5)
        return Response(req.iter_content(chunk_size=5120), content_type=req.headers.get('Content-Type', 'multipart/x-mixed-replace; boundary=frame'))
    except Exception as e:
        print(f"❌ Camera Proxy Error: {e}")
        return "Camera stream not available locally (port 8081)", 502

@app.route('/api/camera_status')
@admin_required
def camera_status():
    """Check whether the local camera server is returning a valid JPEG frame."""
    try:
        resp = requests.get('http://127.0.0.1:8081/snapshot', timeout=4)
        content_type = resp.headers.get('Content-Type', '')
        is_ready = resp.status_code == 200 and 'image/jpeg' in content_type and len(resp.content) > 0
        return jsonify({
            'status': 'ok',
            'ready': is_ready,
            'http_status': resp.status_code,
            'content_type': content_type
        }), 200 if is_ready else 503
    except Exception as e:
        return jsonify({
            'status': 'error',
            'ready': False,
            'message': str(e)
        }), 503

@app.route('/api/settings', methods=['GET'])
@admin_required
def get_settings():
    global app_settings
    if isinstance(app_settings.get('automation'), dict):
        app_settings['automation'].pop('target_ph', None)
    return jsonify(app_settings)

@app.route('/api/water_system/status', methods=['GET'])
@admin_required
def get_water_system_status():
    return jsonify(_current_water_status())

@app.route('/api/settings', methods=['POST'])
@admin_required
def post_settings():
    global app_settings
    try:
        new_settings = request.get_json()
        if new_settings:
            if isinstance(new_settings.get("automation"), dict):
                new_settings["automation"] = {
                    "enabled": bool(new_settings["automation"].get("enabled", False)),
                    "target_tds": float(new_settings["automation"].get("target_tds", 800)),
                }

            current_settings = _deep_merge_dict(load_settings(), app_settings)
            app_settings = _deep_merge_dict(current_settings, new_settings)
            if isinstance(app_settings.get("automation"), dict):
                app_settings["automation"].pop("target_ph", None)

            app_settings["fish_feeder"] = _normalize_fish_feeder_settings(
                app_settings.get("fish_feeder", {}),
                current_settings.get("fish_feeder", {}),
            )

            # Check if sensor config changed and publish to MQTT
            if "sensor_config" in new_settings:
                try:
                    sensor_payload = json.dumps(new_settings["sensor_config"])
                    if mqtt_client and mqtt_client.is_connected():
                         mqtt_client.publish("aquaponics/config/sensors", sensor_payload, qos=1, retain=True)
                         print(f"📤 Sensor Config sent to ESP32: {sensor_payload}")
                except Exception as ex:
                    print(f"MQTT Publish Error: {ex}")

            if "automation" in new_settings:
                try:
                    automation_payload = json.dumps(new_settings["automation"])
                    if mqtt_client and mqtt_client.is_connected():
                        mqtt_client.publish("aquaponics/config/automation", automation_payload, qos=1)
                        print(f"📤 Automation Config sent to ESP32: {automation_payload}")
                except Exception as ex:
                    print(f"MQTT Publish Error: {ex}")

            if "fan_control" in new_settings:
                try:
                    fan_payload = json.dumps(new_settings["fan_control"])
                    if mqtt_client and mqtt_client.is_connected():
                        mqtt_client.publish("aquaponics/config/fan_control", fan_payload, qos=1)
                        print(f"📤 Fan Control Config sent to ESP32: {fan_payload}")
                except Exception as ex:
                    print(f"MQTT Publish Error: {ex}")

            if "light_control" in new_settings:
                try:
                    light_payload = {
                        "command_source": new_settings["light_control"].get("command_source", "netpie"),
                        "enabled": new_settings["light_control"].get("enabled", False),
                        "manual_state": new_settings["light_control"].get("manual_state", False),
                        "on_day": new_settings["light_control"].get("on_day", 0),
                        "on_time": new_settings["light_control"].get("on_time", "06:00"),
                        "off_day": new_settings["light_control"].get("off_day", 0),
                        "off_time": new_settings["light_control"].get("off_time", "18:00")
                    }
                    if mqtt_client and mqtt_client.is_connected():
                        mqtt_client.publish("aquaponics/config/light_control", json.dumps(light_payload), qos=1)
                        print(f"📤 Light Control Config sent to ESP32: {light_payload}")
                except Exception as ex:
                    print(f"MQTT Publish Error: {ex}")

            if "fish_feeder" in new_settings:
                try:
                    feeder_payload = {
                        "command_source": new_settings["fish_feeder"].get("command_source", "local_web"),
                        "enabled": new_settings["fish_feeder"].get("enabled", False),
                        "feed_day": new_settings["fish_feeder"].get("feed_day", 7),
                        "feed_time": new_settings["fish_feeder"].get("feed_time", "08:00"),
                        "duration_ms": new_settings["fish_feeder"].get("duration_ms", 2000)
                    }
                    if mqtt_client and mqtt_client.is_connected():
                        mqtt_client.publish("aquaponics/config/fish_feeder", json.dumps(feeder_payload), qos=1)
                        print(f"📤 Fish Feeder Config sent to ESP32: {feeder_payload}")
                except Exception as ex:
                    print(f"MQTT Publish Error: {ex}")

            if "water_system" in new_settings:
                try:
                    water_payload = {
                        "circulation_enabled": new_settings["water_system"].get("circulation_enabled", True),
                        "refill_enabled": new_settings["water_system"].get("refill_enabled", False),
                        "manual_refill": new_settings["water_system"].get("manual_refill", False),
                        "refill_max_runtime_ms": new_settings["water_system"].get("refill_max_runtime_ms", 120000),
                        "refill_min_interval_ms": new_settings["water_system"].get("refill_min_interval_ms", 300000),
                        "preferred_route": new_settings["water_system"].get("preferred_route", "SUMP_DIRECT"),
                        "allow_direct_sump_refill": new_settings["water_system"].get("allow_direct_sump_refill", False),
                        "fish_refill_interval_ms": new_settings["water_system"].get("fish_refill_interval_ms", 604800000),
                        "fish_refill_max_runtime_ms": new_settings["water_system"].get("fish_refill_max_runtime_ms", 30000),
                        "clear_alarm": False
                    }
                    if mqtt_client and mqtt_client.is_connected():
                        mqtt_client.publish("aquaponics/config/water_system", json.dumps(water_payload), qos=1)
                        print(f"📤 Water System Config sent to ESP32: {water_payload}")
                except Exception as ex:
                    print(f"MQTT Publish Error: {ex}")

            if save_settings(app_settings):
                log_activity(session.get('username', '?'), 'settings', 'Settings updated', request.remote_addr)
                return jsonify({"status": "ok", "message": "Settings saved"})
            else:
                return jsonify({"status": "error", "message": "Failed to save"}), 500
        return jsonify({"status": "error", "message": "Invalid data"}), 400
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# === TDS Calibration API ===
@app.route('/api/tds_voltage')
@admin_required
def get_tds_voltage():
    """Get current TDS voltage from ESP32 sensor data"""
    voltage = last_data.get("tds_voltage", 0)
    return jsonify({"voltage": voltage})

@app.route('/api/tds_calibrate', methods=['POST'])
@admin_required
def post_tds_calibrate():
    """Save TDS calibration and send to ESP32 via MQTT"""
    global app_settings, mqtt_client
    try:
        data = request.get_json()
        low_temp = float(data.get("low_temp", 25.0))
        low_ppm = float(data.get("low_ppm", 0))
        low_voltage = float(data.get("low_voltage", 0))
        high_temp = float(data.get("high_temp", 25.0))
        high_ppm = float(data.get("high_ppm", 0))
        high_voltage = float(data.get("high_voltage", 0))
        
        # Validate
        if low_voltage <= 0 or high_voltage <= 0:
            return jsonify({"status": "error", "message": "Invalid voltage values"}), 400
        if low_voltage == high_voltage:
            return jsonify({"status": "error", "message": "Low and High voltage cannot be the same"}), 400
            
        # Save to settings
        app_settings["tds_calibration"] = {
            "low_temp": low_temp,
            "low_ppm": low_ppm,
            "low_voltage": low_voltage,
            "high_temp": high_temp,
            "high_ppm": high_ppm,
            "high_voltage": high_voltage,
            "calibrated": True
        }
        save_settings(app_settings)
        log_activity(session.get('username', '?'), 'calibration', f'TDS Cal: Low={low_ppm}ppm, High={high_ppm}ppm', request.remote_addr)
        
        # Publish to ESP32 via MQTT
        if mqtt_client and mqtt_client.is_connected():
            import json
            payload = json.dumps({
                "low_ppm": low_ppm,
                "low_voltage": low_voltage,
                "high_ppm": high_ppm,
                "high_voltage": high_voltage
            })
            mqtt_client.publish("aquaponics/config/tds_cal", payload)
            print(f"📤 TDS Calibration sent to ESP32: {payload}")
            save_log(f"TDS Calibration updated: Low={low_ppm}ppm, High={high_ppm}ppm")
        
        return jsonify({"status": "ok", "message": "TDS Calibration saved and sent to ESP32"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# === pH Calibration API ===
@app.route('/api/ph_voltage')
@admin_required
def get_ph_voltage():
    """Get current pH voltage and value from ESP32 sensor data"""
    voltage = last_data.get("ph_voltage", 0)
    ph_value = last_data.get("ph_value", 0)
    return jsonify({"voltage": voltage, "ph_value": ph_value})

@app.route('/api/ph_calibrate', methods=['POST'])
@admin_required
def post_ph_calibrate():
    """Send pH calibration command to ESP32 via MQTT"""
    global app_settings, mqtt_client
    try:
        data = request.get_json()
        action = data.get("action", "")
        action_aliases = {
            "cal4": "cal401",
            "cal7": "cal686"
        }
        action = action_aliases.get(action, action)
        
        if action not in ["cal401", "cal686", "cal918", "clear"]:
            return jsonify({"status": "error", "message": "Invalid action. Use cal401, cal686, cal918, or clear"}), 400
        
        # Publish to ESP32 via MQTT
        if mqtt_client and mqtt_client.is_connected():
            payload = json.dumps({"action": action})
            mqtt_client.publish("aquaponics/config/ph_cal", payload, qos=1)
            print(f"📤 pH Calibration command sent to ESP32: {action}")
            save_log(f"pH Calibration triggered: {action}")
            log_activity(session.get('username', '?'), 'calibration', f'pH calibration: {action}', request.remote_addr)
            
            # Update local settings
            app_settings.setdefault("ph_calibration", {})
            if action == "cal686":
                app_settings["ph_calibration"]["cal686_done"] = True
            elif action == "cal401":
                app_settings["ph_calibration"]["cal401_done"] = True
            elif action == "cal918":
                app_settings["ph_calibration"]["cal918_done"] = True
            elif action == "clear":
                app_settings["ph_calibration"] = {
                    "cal401_done": False, "cal686_done": False, "cal918_done": False,
                    "last_voltage": 0, "last_ph": 0
                }
            save_settings(app_settings)
            
            return jsonify({"status": "ok", "message": f"pH {action} command sent to ESP32"})
        else:
            return jsonify({"status": "error", "message": "MQTT not connected"}), 503
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# === Heartbeat Monitor ===
last_esp_update = 0
esp_online = False

def monitor_heartbeat():
    global last_esp_update, esp_online
    while True:
        try:
            now = time.time()
            # If no data for 15 seconds, mark as offline
            if esp_online and (now - last_esp_update > 15):
                esp_online = False
                msg = "⚠️ ALERT: ESP32 Connection Lost!"
                print(msg)
                save_log(msg)
            
            time.sleep(5) # Check every 5 seconds
        except Exception as e:
            print(f"Monitor Error: {e}")
            time.sleep(5)

@app.route('/api/health')
@login_required
def get_health():
    # Update Real-time Pi Stats
    last_data["pi_cpu_percent"] = psutil.cpu_percent()
    mem = psutil.virtual_memory()
    last_data["pi_ram_total"] = mem.total
    last_data["pi_ram_used"] = mem.used
    last_data["pi_ram_percent"] = mem.percent
    last_data["pi_temp"] = get_pi_temp()
    
    # Add Heartbeat Status
    last_data["esp_status"] = "ONLINE" if esp_online else "OFFLINE"
    last_data["last_seen_sec"] = int(time.time() - last_esp_update) if last_esp_update > 0 else -1
    
    return jsonify(last_data) 

@app.route('/api/info')
@login_required
def get_info():
    return jsonify({"firmware": "Pi-Server-v2 (Monitoring)", "status": "online"})

@app.route('/api/health/details')
@login_required
def get_health_details():
    """Check Pi services and ESP32 sensors status"""
    import subprocess, time as _time

    result = {"services": [], "sensors": []}

    # === Pi Services ===
    services = [
        ("aquaponics", "Web Server"),
        ("aquaponics-hotspot", "Hotspot (AP)"),
        ("aquaponics-cam", "Camera"),
        ("dnsmasq", "DHCP Server"),
        ("mosquitto", "MQTT Broker"),
        ("tailscaled", "Tailscale VPN"),
    ]

    for svc_name, display_name in services:
        try:
            out = subprocess.run(
                ['systemctl', 'is-active', svc_name],
                capture_output=True, text=True, timeout=3
            )
            status = out.stdout.strip()
            ok = status == 'active'

            # Camera special case: app.py starts cam_server directly,
            # so systemd may show 'activating' while camera works fine
            if not ok and svc_name == 'aquaponics-cam':
                proc = subprocess.run(
                    ['pgrep', '-f', 'cam_server'],
                    capture_output=True, timeout=3
                )
                if proc.returncode == 0:
                    ok = True
                    status = 'running'
        except Exception:
            status = 'error'
            ok = False

        result["services"].append({
            "name": display_name,
            "service": svc_name,
            "status": status,
            "ok": ok
        })

    # === ESP32 Connection ===
    esp_age = int(_time.time() - last_esp_update) if last_esp_update > 0 else -1
    result["esp_online"] = esp_online
    result["esp_last_seen"] = esp_age

    # === ESP32 Sensors ===
    # (key, name, unit, min_val, max_val, error_values)
    sensor_checks = [
        ("water_temp", "Water Temp", "°C", 5, 50, [85.0, -127, 0]),
        ("air_temp", "Air Temp", "°C", 5, 60, [0]),
        ("humidity", "Humidity", "%", 10, 100, [0]),
        ("tds", "TDS", "ppm", 1, 5000, []),
        ("ph", "pH", "", 1, 14, [0]),
        ("light", "Light", "lux", 0, 100000, []),
    ]

    for key, name, unit, min_val, max_val, error_vals in sensor_checks:
        value = last_data.get(key, None)
        # Sensor OK if: ESP online, value in range, AND not a known error value
        ok = (esp_online
              and value is not None
              and isinstance(value, (int, float))
              and min_val <= value <= max_val
              and value not in error_vals)
        result["sensors"].append({
            "name": name,
            "key": key,
            "value": value if value is not None else "N/A",
            "unit": unit if value is not None else "",
            "ok": ok
        })

    return jsonify(result)

@app.route('/api/logs')
@login_required
def get_logs():
    return jsonify(list(log_buffer))

@app.route('/logs_view')
@login_required
def logs_view():
    return _send_local_file('full_logs.html')

@app.route('/graphs_view')
@login_required
def graphs_view():
    return _send_local_file('graphs.html')

# === Camera Settings API ===
@app.route('/api/automation/config', methods=['POST'])
@admin_required
def automation_config():
    """Receive automation targets from Web Dashboard and publish to MQTT"""
    global app_settings
    try:
        data = request.get_json()
        enabled = data.get('enabled', False)
        target_tds = float(data.get('target_tds', 800))
        
        # Save to local settings file
        app_settings['automation'] = {
            'enabled': enabled,
            'target_tds': target_tds
        }
        save_settings(app_settings)
        
        # Publish to ESP32
        payload = {
            "enabled": enabled,
            "target_tds": target_tds
        }
        if mqtt_client:
            mqtt_client.publish("aquaponics/config/automation", json.dumps(payload), qos=1)
            
        save_log(f"⚙️ Action: Automation Target Set -> Enabled: {enabled}, Target TDS: {target_tds}")
        return jsonify({"status": "ok", "message": "Automation settings applied successfully"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 400

@app.route('/api/fan_control/config', methods=['POST'])
@admin_required
def fan_control_config():
    """Receive exhaust fan settings from Web Dashboard and publish to MQTT"""
    global app_settings
    try:
        data = request.get_json()
        enabled = bool(data.get('enabled', False))
        auto_mode = bool(data.get('auto_mode', True))
        manual_state = bool(data.get('manual_state', False))
        temp_on_c = float(data.get('temp_on_c', 32.0))
        temp_off_c = float(data.get('temp_off_c', 30.0))
        humidity_on_pct = float(data.get('humidity_on_pct', 80.0))
        humidity_off_pct = float(data.get('humidity_off_pct', 75.0))

        if temp_off_c >= temp_on_c:
            return jsonify({'status': 'error', 'message': 'temp_off_c must be lower than temp_on_c'}), 400
        if humidity_off_pct >= humidity_on_pct:
            return jsonify({'status': 'error', 'message': 'humidity_off_pct must be lower than humidity_on_pct'}), 400

        app_settings.setdefault('fan_control', {})
        app_settings['fan_control'].update({
            'enabled': enabled,
            'auto_mode': auto_mode,
            'manual_state': manual_state,
            'temp_on_c': temp_on_c,
            'temp_off_c': temp_off_c,
            'humidity_on_pct': humidity_on_pct,
            'humidity_off_pct': humidity_off_pct
        })
        save_settings(app_settings)

        payload = {
            'enabled': enabled,
            'auto_mode': auto_mode,
            'manual_state': manual_state,
            'temp_on_c': temp_on_c,
            'temp_off_c': temp_off_c,
            'humidity_on_pct': humidity_on_pct,
            'humidity_off_pct': humidity_off_pct
        }

        if mqtt_client and mqtt_client.is_connected():
            mqtt_client.publish('aquaponics/config/fan_control', json.dumps(payload), qos=1)

        save_log(
            f"🌬️ Fan control -> enabled={enabled}, auto={auto_mode}, manual={manual_state}, "
            f"temp={temp_on_c}/{temp_off_c}, humidity={humidity_on_pct}/{humidity_off_pct}"
        )
        log_activity(session.get('username', '?'), 'fan_control', 'Fan control updated', request.remote_addr)
        return jsonify({'status': 'ok', 'message': 'Fan control settings applied successfully'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 400

@app.route('/api/light_control/config', methods=['POST'])
@admin_required
def light_control_config():
    """Receive light control settings from Web Dashboard and publish to MQTT"""
    global app_settings
    try:
        data = request.get_json()
        command_source = str(data.get('command_source', 'netpie')).lower()
        enabled = bool(data.get('enabled', False))
        manual_state = bool(data.get('manual_state', False))
        on_day = int(data.get('on_day', 0))
        on_time = str(data.get('on_time', '06:00'))
        off_day = int(data.get('off_day', 0))
        off_time = str(data.get('off_time', '18:00'))

        if command_source not in ['netpie', 'local_web']:
            return jsonify({'status': 'error', 'message': 'Invalid command_source'}), 400

        app_settings.setdefault('light_control', {})
        app_settings['light_control'].update({
            'command_source': command_source,
            'enabled': enabled,
            'manual_state': manual_state,
            'on_day': on_day,
            'on_time': on_time,
            'off_day': off_day,
            'off_time': off_time
        })
        save_settings(app_settings)

        payload = {
            'command_source': command_source,
            'enabled': enabled,
            'manual_state': manual_state,
            'on_day': on_day,
            'on_time': on_time,
            'off_day': off_day,
            'off_time': off_time
        }

        if mqtt_client and mqtt_client.is_connected():
            mqtt_client.publish('aquaponics/config/light_control', json.dumps(payload), qos=1)

        save_log(
            f"💡 Light control -> source={command_source}, enabled={enabled}, manual={manual_state}, "
            f"on={on_day} {on_time}, off={off_day} {off_time}"
        )
        log_activity(session.get('username', '?'), 'light_control', 'Light control updated', request.remote_addr)
        return jsonify({'status': 'ok', 'message': 'Light control settings applied successfully'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 400

@app.route('/api/fish_feeder/config', methods=['POST'])
@admin_required
def fish_feeder_config():
    """Receive fish feeder settings from Web Dashboard and publish to MQTT"""
    global app_settings
    try:
        data = request.get_json() or {}
        normalized = _normalize_fish_feeder_settings(data, app_settings.get('fish_feeder', {}))
        command_source = normalized['command_source']
        enabled = normalized['enabled']
        feed_day = normalized['feed_day']
        feed_time = normalized['feed_time']
        duration_ms = normalized['duration_ms']
        trigger_feed = bool(data.get('trigger_feed', False))

        if command_source not in ['netpie', 'local_web']:
            return jsonify({'status': 'error', 'message': 'Invalid command_source'}), 400

        app_settings.setdefault('fish_feeder', {})
        app_settings['fish_feeder'].update(normalized)
        save_settings(app_settings)

        payload = {
            'command_source': command_source,
            'enabled': enabled,
            'feed_day': feed_day,
            'feed_time': feed_time,
            'duration_ms': duration_ms,
            'trigger_feed': trigger_feed
        }

        if mqtt_client and mqtt_client.is_connected():
            mqtt_client.publish('aquaponics/config/fish_feeder', json.dumps(payload), qos=1)

        save_log(
            f"🐟 Fish feeder -> source={command_source}, enabled={enabled}, schedule={feed_day} {feed_time}, "
            f"duration={duration_ms}ms, trigger={trigger_feed}"
        )
        log_activity(session.get('username', '?'), 'fish_feeder', 'Fish feeder updated', request.remote_addr)
        return jsonify({'status': 'ok', 'message': 'Fish feeder settings applied successfully'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 400

@app.route('/api/water_system/config', methods=['POST'])
@admin_required
def water_system_config():
    """Receive water system settings from Web Dashboard and publish to MQTT"""
    global app_settings
    try:
        data = request.get_json()
        circulation_enabled = data.get('circulation_enabled', True)
        refill_enabled = data.get('refill_enabled', False)
        manual_refill = data.get('manual_refill', False)
        clear_alarm = data.get('clear_alarm', False)
        refill_max_runtime_ms = int(data.get('refill_max_runtime_ms', 120000))
        refill_min_interval_ms = int(data.get('refill_min_interval_ms', 300000))
        preferred_route = str(data.get('preferred_route', 'AUTO')).upper()
        allow_direct_sump_refill = bool(data.get('allow_direct_sump_refill', False))
        fish_refill_interval_ms = int(data.get('fish_refill_interval_ms', 604800000))
        fish_refill_max_runtime_ms = int(data.get('fish_refill_max_runtime_ms', 30000))

        if preferred_route not in ['AUTO', 'FISH_TANK', 'SUMP_DIRECT']:
            return jsonify({'status': 'error', 'message': 'Invalid preferred_route'}), 400

        app_settings.setdefault('water_system', {})
        app_settings['water_system'].update({
            'circulation_enabled': circulation_enabled,
            'refill_enabled': refill_enabled,
            'manual_refill': manual_refill,
            'refill_max_runtime_ms': refill_max_runtime_ms,
            'refill_min_interval_ms': refill_min_interval_ms,
            'preferred_route': preferred_route,
            'allow_direct_sump_refill': allow_direct_sump_refill,
            'fish_refill_interval_ms': fish_refill_interval_ms,
            'fish_refill_max_runtime_ms': fish_refill_max_runtime_ms
        })
        save_settings(app_settings)

        payload = {
            'circulation_enabled': circulation_enabled,
            'refill_enabled': refill_enabled,
            'manual_refill': manual_refill,
            'clear_alarm': clear_alarm,
            'refill_max_runtime_ms': refill_max_runtime_ms,
            'refill_min_interval_ms': refill_min_interval_ms,
            'preferred_route': preferred_route,
            'allow_direct_sump_refill': allow_direct_sump_refill,
            'fish_refill_interval_ms': fish_refill_interval_ms,
            'fish_refill_max_runtime_ms': fish_refill_max_runtime_ms
        }

        if mqtt_client and mqtt_client.is_connected():
            mqtt_client.publish('aquaponics/config/water_system', json.dumps(payload), qos=1)

        socketio.emit('water_status_update', _current_water_status())

        save_log(
            f"💧 Water system config -> circ={circulation_enabled}, refill={refill_enabled}, "
            f"manual={manual_refill}, route={preferred_route}, direct={allow_direct_sump_refill}, "
            f"clear_alarm={clear_alarm}, max={refill_max_runtime_ms}ms, gap={refill_min_interval_ms}ms, "
            f"fish_int={fish_refill_interval_ms}ms, fish_max={fish_refill_max_runtime_ms}ms"
        )
        log_activity(session.get('username', '?'), 'water_system', 'Water system config updated', request.remote_addr)
        return jsonify({'status': 'ok', 'message': 'Water system settings applied successfully'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 400

# === Hardware Test API ===
@app.route('/hwtest')
@admin_required
def hwtest_page():
    return _send_local_file('hardware_test.html')

@app.route('/api/hwtest/command', methods=['POST'])
@admin_required
def hwtest_command():
    """Send hardware test command to ESP32 via MQTT"""
    try:
        data = request.get_json()
        cmd = data.get('cmd', '')
        duration = data.get('duration', 3000)
        
        if not cmd:
            return jsonify({"status": "error", "message": "Missing cmd"}), 400
        
        payload = {"cmd": cmd, "duration": duration}
        if mqtt_client:
            mqtt_client.publish("aquaponics/test/command", json.dumps(payload), qos=1)
            save_log(f"🔧 HW Test: {cmd} (duration={duration}ms)")
            return jsonify({"status": "ok", "cmd": cmd})
        else:
            return jsonify({"status": "error", "message": "MQTT not connected"}), 503
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 400

# === Camera Settings API ===
@app.route('/api/camera_restart', methods=['POST'])
@admin_required
def restart_camera():
    """Save camera settings and restart cam_server.py"""
    global app_settings
    try:
        data = request.get_json()
        width = int(data.get('width', 1280))
        height = int(data.get('height', 720))
        framerate = int(data.get('framerate', 15))
        quality = int(data.get('quality', 80))
        
        # Validate
        valid_resolutions = [(640, 480), (1280, 720), (1920, 1080)]
        if (width, height) not in valid_resolutions:
            return jsonify({'status': 'error', 'message': 'Invalid resolution'}), 400
        if framerate < 1 or framerate > 30:
            return jsonify({'status': 'error', 'message': 'FPS must be 1-30'}), 400
        if quality < 1 or quality > 100:
            return jsonify({'status': 'error', 'message': 'Quality must be 1-100'}), 400
        
        # Save to settings
        app_settings['camera'] = {
            'width': width,
            'height': height,
            'framerate': framerate,
            'quality': quality
        }
        save_settings(app_settings)
        save_log(f"📷 Camera settings updated: {width}x{height} @ {framerate}fps q={quality}")
        
        # Restart camera server
        import subprocess
        try:
            subprocess.run(['sudo', 'pkill', '-f', 'cam_server.py'], timeout=5)
            import time as _time
            _time.sleep(1)
            cam_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'start_cam.sh')
            subprocess.Popen(['bash', cam_script])
            print(f"📷 Camera restarted: {width}x{height} @ {framerate}fps q={quality}")
        except Exception as ex:
            print(f"⚠️ Camera restart error: {ex}")
        
        return jsonify({'status': 'ok', 'message': f'Camera restarting with {width}x{height} @ {framerate}fps q={quality}'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

@app.route('/api/full_logs_file')
@login_required
def get_full_logs_file():
    try:
        with open(LOG_FILE, "r") as f:
            return f.read()
    except:
        return "No logs found."

@app.route('/api/dashboard_snapshot')
@login_required
def get_dashboard_snapshot():
    return jsonify(build_dashboard_data())

@app.route('/api/clear_logs', methods=['POST'])
@admin_required
def clear_logs_file():
    try:
        open(LOG_FILE, 'w').close()
        log_buffer.clear()
        log_activity(session.get('username', '?'), 'settings', 'System logs cleared', request.remote_addr)
        return jsonify({"status": "success", "message": "Logs cleared"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/history')
@login_required
def get_history():
    global app_settings
    try:
        with db_lock:
            conn = None
            try:
                conn = sqlite3.connect(DB_FILE)
                cursor = conn.cursor()
                
                # Read graph_days from settings (default 3)
                days = app_settings.get("display", {}).get("graph_days", 3)
                limit = days * 24 * 60  # 1 record per minute
                
                cursor.execute('''
                    SELECT timestamp, water_temp, air_temp, humidity, tds, ph, light
                    FROM sensors 
                    ORDER BY id DESC 
                    LIMIT ?
                ''', (limit,))
                rows = cursor.fetchall()
            finally:
                if conn:
                    conn.close()
        
        # Reverse to chronological order (oldest first)
        rows.reverse()
        
        # Format for Chart.js
        labels = []
        data = {
            "water_temp": [],
            "air_temp": [],
            "humidity": [],
            "tds": [],
            "ph": [],
            "light": []
        }
        
        for r in rows:
            # timestamp is r[0] e.g. "2023-10-27 10:00:00"
            # SQLite stores as UTC. Convert directly to Thai Time (UTC+7)
            try:
                # Parse string to datetime
                dt_utc = datetime.strptime(r[0], "%Y-%m-%d %H:%M:%S")
                # Add 7 hours
                dt_thai = dt_utc + timedelta(hours=7)
                # Format specific for graph label (HH:MM) or include Date if needed
                # User wants 3 days, so maybe show date if it's a new day? 
                # For now let's just do HH:MM and maybe Date/Time if tooltip? 
                # ChartJS labels are usually X axis.
                # Let's use simple HH:MM first as requested.
                t_str = dt_thai.strftime("%d/%m %H:%M") # dd/mm HH:MM is better for 3 days
            except:
                t_str = r[0] # Fallback
            
            labels.append(t_str)
            data["water_temp"].append(normalize_history_value("water_temp", r[1]))
            data["air_temp"].append(normalize_history_value("air_temp", r[2]))
            data["humidity"].append(normalize_history_value("humidity", r[3]))
            data["tds"].append(normalize_history_value("tds", r[4]))
            data["ph"].append(normalize_history_value("ph", r[5]))
            data["light"].append(normalize_history_value("light", r[6]))
            
        return jsonify({"labels": labels, "datasets": data})

    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# === WebSocket Support ===
def build_dashboard_data():
    """Build combined data payload for WebSocket broadcast"""
    # Update Pi stats
    last_data["pi_cpu_percent"] = psutil.cpu_percent()
    mem = psutil.virtual_memory()
    last_data["pi_ram_total"] = mem.total
    last_data["pi_ram_used"] = mem.used
    last_data["pi_ram_percent"] = mem.percent
    last_data["pi_temp"] = get_pi_temp()
    last_data["esp_status"] = "ONLINE" if esp_online else "OFFLINE"
    last_data["last_seen_sec"] = int(time.time() - last_esp_update) if last_esp_update > 0 else -1

    dashboard_sensors = dict(last_data)
    water_status = _current_water_status()
    dashboard_sensors.update({
        "active_route": water_status.get("active_route", dashboard_sensors.get("active_route", "NONE")),
        "preferred_route": water_status.get("preferred_route", dashboard_sensors.get("preferred_route", "AUTO")),
        "allow_direct_sump_refill": water_status.get("allow_direct_sump_refill", dashboard_sensors.get("allow_direct_sump_refill", False)),
        "manual_refill": water_status.get("manual_refill", dashboard_sensors.get("manual_refill", False)),
        "route_blocked": water_status.get("route_blocked", dashboard_sensors.get("route_blocked", False)),
        "route_valve_output": water_status.get("route_valve_output", dashboard_sensors.get("route_valve_output", False)),
        "has_route_valve": water_status.get("has_route_valve", dashboard_sensors.get("has_route_valve", False)),
        "circulation_pump_output": water_status.get("circulation_pump_output", dashboard_sensors.get("circulation_pump_output", False)),
        "fish_tank_refill_output": water_status.get("fish_tank_refill_output", dashboard_sensors.get("fish_tank_refill_output", False)),
        "mix_tank_refill_output": water_status.get("mix_tank_refill_output", dashboard_sensors.get("mix_tank_refill_output", False)),
        "water_dilution_active": water_status.get("water_dilution_active", dashboard_sensors.get("water_dilution_active", False)),
        "mix_tank_settling_active": water_status.get("mix_tank_settling_active", dashboard_sensors.get("mix_tank_settling_active", False)),
        "mix_tank_control_zone": water_status.get("mix_tank_control_zone", dashboard_sensors.get("mix_tank_control_zone", True)),
        "dilution_hold_remaining_ms": water_status.get("dilution_hold_remaining_ms", dashboard_sensors.get("dilution_hold_remaining_ms", 0)),
        "fish_refill_ready": water_status.get("fish_refill_ready", dashboard_sensors.get("fish_refill_ready", True)),
        "fish_refill_wait_remaining_ms": water_status.get("fish_refill_wait_remaining_ms", dashboard_sensors.get("fish_refill_wait_remaining_ms", 0)),
        "sump_low": water_status.get("sump_low", dashboard_sensors.get("sump_low", False)),
        "sump_high": water_status.get("sump_high", dashboard_sensors.get("sump_high", False)),
        "has_level_sensors": water_status.get("has_level_sensors", dashboard_sensors.get("has_level_sensors")),
        "has_overflow_sensor": water_status.get("has_overflow_sensor", dashboard_sensors.get("has_overflow_sensor")),
        "water_status_seen": water_status.get("status_seen", dashboard_sensors.get("water_status_seen", False)),
        "fish_overflow": water_status.get("overflow_alarm", dashboard_sensors.get("fish_overflow", False)),
        "water_alarm": water_status.get("alarm_active", dashboard_sensors.get("water_alarm", False)),
        "water_state": water_status.get("state", dashboard_sensors.get("water_state", "IDLE")),
        "water_state_label_th": water_status.get("state_label_th", dashboard_sensors.get("water_state_label_th", "พร้อมทำงาน")),
        "water_reason": water_status.get("reason", dashboard_sensors.get("water_reason", "Waiting for ESP32 status")),
    })
    
    return {
        "sensors": dashboard_sensors,
        "health": dict(last_data),
        "info": {"firmware": "Pi-Server-v2 (Monitoring)", "status": "online"},
        "logs": list(log_buffer)
    }

def ws_broadcast():
    """Background thread: broadcast dashboard data to all WebSocket clients"""
    print("📡 WebSocket broadcast thread started")
    while True:
        socketio.sleep(2)
        try:
            data = build_dashboard_data()
            socketio.emit('dashboard_update', data)
        except Exception as e:
            print(f"WS Broadcast Error: {e}")

@socketio.on('connect')
def handle_ws_connect():
    """Send initial data when a client connects via WebSocket"""
    print("🔌 WebSocket client connected")
    try:
        data = build_dashboard_data()
        emit('dashboard_update', data)
    except Exception as e:
        print(f"WS Initial send error: {e}")

@socketio.on('disconnect')
def handle_ws_disconnect():
    print("🔌 WebSocket client disconnected")

# === OTA Upload via Web ===
import uuid
import tempfile

ota_tasks = {}

@app.route('/ota')
@admin_required
def ota_page():
    return _send_local_file('ota.html')

@app.route('/api/ota/upload', methods=['POST'])
@admin_required
def ota_upload():
    """Receive firmware.bin from browser, then flash to ESP32 via espota.py"""
    try:
        if 'firmware' not in request.files:
            return jsonify({"status": "error", "message": "ไม่มีไฟล์ firmware"}), 400

        firmware = request.files['firmware']
        if not firmware.filename.endswith('.bin'):
            return jsonify({"status": "error", "message": "กรุณาใช้ไฟล์ .bin เท่านั้น"}), 400

        firmware_path = os.path.join(tempfile.gettempdir(), 'firmware_ota.bin')
        firmware.save(firmware_path)
        file_size = os.path.getsize(firmware_path)

        print(f"📦 OTA: Received firmware {firmware.filename} ({file_size} bytes)")
        save_log(f"📦 OTA Upload: {firmware.filename} ({file_size} bytes)")

        task_id = str(uuid.uuid4())[:8]
        ota_tasks[task_id] = {
            "status": "flashing",
            "message": "",
            "logs": [f"📦 Firmware received: {file_size} bytes",
                     "🔄 Starting espota flash to ESP32..."]
        }

        secure_settings = load_settings().get("secure", {})
        esp_ip = str(secure_settings.get("esp_ip", "192.168.10.10")).strip() or "192.168.10.10"
        ota_password = _read_env(OTA_PASSWORD_ENV) or str(secure_settings.get("ota_password", "")).strip()
        if not ota_password:
            return jsonify({
                "status": "error",
                "message": (
                    f"OTA password is not configured. Set secure.ota_password or {OTA_PASSWORD_ENV}."
                )
            }), 503
        espota_script = _app_path('espota.py')

        def run_ota():
            try:
                cmd = [
                    'python3', espota_script,
                    '-i', esp_ip, '-p', '3232',
                    '--auth=' + ota_password,
                    '-f', firmware_path, '-d', '-r'
                ]
                ota_tasks[task_id]["logs"].append(f"🚀 Flashing to {esp_ip}:3232...")
                print(f"🚀 OTA Command: {' '.join(cmd)}")

                result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)

                if result.returncode == 0:
                    ota_tasks[task_id]["status"] = "success"
                    ota_tasks[task_id]["message"] = "Flash สำเร็จ!"
                    ota_tasks[task_id]["logs"].append("✅ Flash สำเร็จ! ESP32 กำลัง reboot...")
                    print("✅ OTA Flash successful!")
                    save_log("✅ OTA Flash successful!")
                else:
                    error_msg = result.stderr.strip() or result.stdout.strip() or "Unknown error"
                    ota_tasks[task_id]["status"] = "error"
                    ota_tasks[task_id]["message"] = error_msg
                    ota_tasks[task_id]["logs"].append(f"❌ Flash failed: {error_msg}")
                    print(f"❌ OTA Flash failed: {error_msg}")
                    save_log(f"❌ OTA Flash failed: {error_msg}")

            except subprocess.TimeoutExpired:
                ota_tasks[task_id]["status"] = "error"
                ota_tasks[task_id]["message"] = "Timeout"
                ota_tasks[task_id]["logs"].append("❌ Timeout: Flash ใช้เวลานานเกินไป")
            except Exception as e:
                ota_tasks[task_id]["status"] = "error"
                ota_tasks[task_id]["message"] = str(e)
                ota_tasks[task_id]["logs"].append(f"❌ Error: {e}")

            try:
                os.remove(firmware_path)
            except:
                pass

        thread = threading.Thread(target=run_ota)
        thread.daemon = True
        thread.start()

        log_activity(session.get('username', '?'), 'ota', f'OTA firmware uploaded ({file_size} bytes)', request.remote_addr)
        return jsonify({"status": "uploading", "task_id": task_id})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/ota/status/<task_id>')
@admin_required
def ota_status(task_id):
    """Check OTA flash status"""
    task = ota_tasks.get(task_id)
    if not task:
        return jsonify({"status": "error", "message": "Task not found"}), 404
    return jsonify(task)

# === WiFi Management ===
import re

@app.route('/wifi')
@admin_required
def wifi_page():
    return _send_local_file('wifi.html')

@app.route('/api/wifi/status')
@admin_required
def wifi_status():
    """Get current WiFi connection info"""
    try:
        result = {}

        # Get current SSID
        ssid_out = subprocess.run(['iwgetid', '-r'], capture_output=True, text=True, timeout=5)
        result['ssid'] = ssid_out.stdout.strip() if ssid_out.returncode == 0 else ''
        result['connected'] = bool(result['ssid'])

        # Get IP — ใช้ ip addr show เฉพาะ interface เพื่อหลีกเลี่ยง Tailscale/VPN IP
        wlan0_ip = None
        ap0_ip = None
        try:
            out = subprocess.run(['ip', '-4', 'addr', 'show', 'wlan0'], capture_output=True, text=True, timeout=2)
            m = re.search(r'inet (\d+\.\d+\.\d+\.\d+)', out.stdout)
            if m:
                wlan0_ip = m.group(1)
        except Exception:
            pass
        try:
            out2 = subprocess.run(['ip', '-4', 'addr', 'show', 'ap0'], capture_output=True, text=True, timeout=2)
            m2 = re.search(r'inet (\d+\.\d+\.\d+\.\d+)', out2.stdout)
            if m2:
                ap0_ip = m2.group(1)
        except Exception:
            pass
        # Priority: wlan0 (home WiFi) > ap0 (hotspot fallback)
        result['ip'] = wlan0_ip or ap0_ip or ''

        # Get signal strength
        sig_out = subprocess.run(['iwconfig', 'wlan0'], capture_output=True, text=True, timeout=5)
        sig_match = re.search(r'Signal level=(-?\d+)', sig_out.stdout)
        result['signal'] = int(sig_match.group(1)) if sig_match else 0

        return jsonify(result)
    except Exception as e:
        return jsonify({"connected": False, "ssid": "", "ip": "", "signal": 0, "error": str(e)})

@app.route('/api/wifi/netstats')
@login_required
def wifi_netstats():
    """Get network throughput bytes and ping latency"""
    stats = {"rx_bytes": 0, "tx_bytes": 0, "ping_ms": None}
    try:
        # Read interface bytes
        with open('/sys/class/net/wlan0/statistics/rx_bytes') as f:
            stats['rx_bytes'] = int(f.read().strip())
        with open('/sys/class/net/wlan0/statistics/tx_bytes') as f:
            stats['tx_bytes'] = int(f.read().strip())
    except Exception:
        pass
    try:
        # Ping 1.1.1.1
        p = subprocess.run(['ping', '-c', '1', '-W', '2', '1.1.1.1'],
                           capture_output=True, text=True, timeout=5)
        m = re.search(r'time=([0-9.]+)', p.stdout)
        if m:
            stats['ping_ms'] = round(float(m.group(1)), 1)
    except Exception:
        pass
    return jsonify(stats)

@app.route('/api/wifi/scan')
@admin_required
def wifi_scan():
    """Scan for available WiFi networks"""
    try:
        result = subprocess.run(
            ['sudo', 'iwlist', 'wlan0', 'scan'],
            capture_output=True, text=True, timeout=30
        )

        networks = []
        current_ssid = ''

        # Get current SSID
        ssid_out = subprocess.run(['iwgetid', '-r'], capture_output=True, text=True, timeout=5)
        if ssid_out.returncode == 0:
            current_ssid = ssid_out.stdout.strip()

        # Parse iwlist output
        cells = result.stdout.split('Cell ')
        for cell in cells[1:]:
            ssid_match = re.search(r'ESSID:"(.+?)"', cell)
            signal_match = re.search(r'Signal level=(-?\d+)', cell)
            enc_match = re.search(r'Encryption key:(on|off)', cell)

            if ssid_match and ssid_match.group(1):
                ssid = ssid_match.group(1)
                signal = int(signal_match.group(1)) if signal_match else -100
                encrypted = enc_match.group(1) == 'on' if enc_match else True

                # Skip duplicates (keep strongest signal)
                existing = next((n for n in networks if n['ssid'] == ssid), None)
                if existing:
                    if signal > existing['signal']:
                        existing['signal'] = signal
                    continue

                networks.append({
                    'ssid': ssid,
                    'signal': signal,
                    'encrypted': encrypted,
                    'active': ssid == current_ssid
                })

        # Sort: active first, then by signal strength
        networks.sort(key=lambda x: (-x['active'], -x['signal']))

        return jsonify({"networks": networks})
    except Exception as e:
        return jsonify({"networks": [], "error": str(e)}), 500

@app.route('/api/wifi/connect', methods=['POST'])
@admin_required
def wifi_connect():
    """Connect to a WiFi network using nmcli (NetworkManager)"""
    try:
        data = request.get_json()
        ssid = data.get('ssid', '').strip()
        password = data.get('password', '')

        if not ssid:
            return jsonify({"status": "error", "message": "SSID is required"}), 400

        print(f"📶 WiFi: Connecting to {ssid} via nmcli...")
        save_log(f"📶 WiFi config changed to: {ssid}")

        # Step 1: Delete existing WiFi connections (to allow switch)
        list_out = subprocess.run(
            ['nmcli', '-t', '-f', 'NAME,TYPE', 'connection', 'show'],
            capture_output=True, text=True, timeout=5
        )
        for line in list_out.stdout.strip().split('\n'):
            parts = line.split(':')
            if len(parts) >= 2 and parts[1] == '802-11-wireless':
                conn_name = parts[0]
                subprocess.run(
                    ['sudo', 'nmcli', 'connection', 'delete', conn_name],
                    capture_output=True, timeout=5
                )
                print(f"  🗑️ Deleted old connection: {conn_name}")

        # Step 2: Connect using nmcli device wifi connect
        import time as _time
        if password:
            connect_out = subprocess.run(
                ['sudo', 'nmcli', 'device', 'wifi', 'connect', ssid,
                 'password', password, 'ifname', 'wlan0'],
                capture_output=True, text=True, timeout=30
            )
        else:
            connect_out = subprocess.run(
                ['sudo', 'nmcli', 'device', 'wifi', 'connect', ssid,
                 'ifname', 'wlan0'],
                capture_output=True, text=True, timeout=30
            )

        print(f"  nmcli output: {connect_out.stdout.strip()}")
        if connect_out.stderr:
            print(f"  nmcli stderr: {connect_out.stderr.strip()}")

        # Step 3: Wait and verify
        _time.sleep(5)
        check = subprocess.run(['iwgetid', '-r'], capture_output=True, text=True, timeout=5)
        new_ssid = check.stdout.strip()

        if new_ssid == ssid:
            print(f"✅ WiFi connected to {ssid}")
            save_log(f"✅ WiFi connected to {ssid}")
            return jsonify({"status": "ok", "message": f"เชื่อมต่อ {ssid} สำเร็จ!"})
        else:
            print(f"⚠️ WiFi connection result: current={new_ssid}, target={ssid}")
            return jsonify({"status": "ok", "message": f"ส่งคำสั่งเชื่อมต่อ {ssid} แล้ว — อาจใช้เวลาสักครู่"})

    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# =============================================================================
# Web Terminal — WebSocket-to-Telnet Relay for ESP32 CLI
# =============================================================================
import socket as _socket

# Store active telnet connections per WebSocket session
_telnet_sessions = {}

@app.route('/terminal')
@admin_required
def terminal_page():
    return _send_local_file('terminal.html')

@socketio.on('terminal_connect')
def handle_terminal_connect(data):
    """Open TCP connection to ESP32 Telnet and start reader thread"""
    sid = request.sid
    ip = data.get('ip', '192.168.10.2')
    port = int(data.get('port', 23))

    # Close existing session if any
    if sid in _telnet_sessions:
        try:
            _telnet_sessions[sid].close()
        except:
            pass
        del _telnet_sessions[sid]

    try:
        sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect((ip, port))
        sock.settimeout(0.5)  # Non-blocking reads with short timeout
        _telnet_sessions[sid] = sock

        emit('terminal_connected', {'ip': ip, 'port': port})
        print(f"🖥️ Terminal connected to ESP32 at {ip}:{port} (sid={sid[:8]})")

        # Start background reader thread
        def reader():
            while sid in _telnet_sessions:
                try:
                    data = _telnet_sessions[sid].recv(4096)
                    if not data:
                        # Connection closed by ESP32
                        break
                    text = data.decode('utf-8', errors='replace')
                    socketio.emit('terminal_data', {'text': text}, to=sid)
                except _socket.timeout:
                    continue
                except Exception as e:
                    break

            # Cleanup
            if sid in _telnet_sessions:
                try:
                    _telnet_sessions[sid].close()
                except:
                    pass
                del _telnet_sessions[sid]
            socketio.emit('terminal_disconnected', {'reason': 'ESP32 connection closed'}, to=sid)
            print(f"🖥️ Terminal reader ended (sid={sid[:8]})")

        t = threading.Thread(target=reader, daemon=True)
        t.start()

    except _socket.timeout:
        emit('terminal_error', {'message': f'Connection timeout — ESP32 at {ip}:{port} not reachable'})
    except ConnectionRefusedError:
        emit('terminal_error', {'message': f'Connection refused — ESP32 at {ip}:{port} (Telnet not running?)'})
    except Exception as e:
        emit('terminal_error', {'message': f'Connection failed: {str(e)}'})

@socketio.on('terminal_input')
def handle_terminal_input(data):
    """Send user input to ESP32 via TCP"""
    sid = request.sid
    if sid not in _telnet_sessions:
        emit('terminal_error', {'message': 'Not connected'})
        return
    try:
        text = data.get('text', '')
        _telnet_sessions[sid].sendall(text.encode('utf-8'))
    except Exception as e:
        emit('terminal_error', {'message': f'Send failed: {str(e)}'})

@socketio.on('terminal_disconnect')
def handle_terminal_disconnect():
    """Close TCP connection to ESP32"""
    sid = request.sid
    if sid in _telnet_sessions:
        try:
            _telnet_sessions[sid].close()
        except:
            pass
        del _telnet_sessions[sid]
        emit('terminal_disconnected', {'reason': 'User disconnected'})
        print(f"🖥️ Terminal disconnected by user (sid={sid[:8]})")

@socketio.on('disconnect')
def handle_ws_disconnect():
    """Cleanup telnet session when WebSocket disconnects"""
    sid = request.sid
    if sid in _telnet_sessions:
        try:
            _telnet_sessions[sid].close()
        except:
            pass
        del _telnet_sessions[sid]
        print(f"🖥️ Terminal session cleaned up (sid={sid[:8]})")

# =============================================================================
# Admin Panel — Activity Logs & User Management
# =============================================================================

@app.route('/admin/logs')
@admin_required
def admin_logs_page():
    return _send_local_file('admin_logs.html')

@app.route('/admin/users')
@admin_required
def admin_users_page():
    return _send_local_file('admin_users.html')

@app.route('/api/admin/logs')
@admin_required
def get_admin_logs():
    """Get activity logs with optional filters"""
    action_filter = request.args.get('action', '')
    date_filter = request.args.get('date', '')
    limit = int(request.args.get('limit', 200))

    with db_lock:
        conn = None
        try:
            conn = sqlite3.connect(DB_FILE)
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()

            query = 'SELECT * FROM activity_logs WHERE 1=1'
            params = []

            if action_filter:
                query += ' AND action = ?'
                params.append(action_filter)
            if date_filter:
                query += ' AND date(timestamp) = ?'
                params.append(date_filter)

            query += ' ORDER BY id DESC LIMIT ?'
            params.append(limit)

            cursor.execute(query, params)
            rows = cursor.fetchall()

            logs = []
            for row in rows:
                ts_raw = row['timestamp']
                # Convert UTC to Thai time (UTC+7)
                try:
                    dt = datetime.strptime(ts_raw, "%Y-%m-%d %H:%M:%S")
                    dt_thai = dt + timedelta(hours=7)
                    ts_display = dt_thai.strftime("%d/%m/%Y %H:%M:%S")
                except:
                    ts_display = ts_raw

                logs.append({
                    'id': row['id'],
                    'timestamp': ts_display,
                    'username': row['username'],
                    'action': row['action'],
                    'detail': row['detail'],
                    'ip': row['ip_address']
                })

            return jsonify({'logs': logs, 'total': len(logs)})
        except Exception as e:
            return jsonify({'logs': [], 'error': str(e)}), 500
        finally:
            if conn:
                conn.close()

@app.route('/api/admin/users', methods=['GET'])
@admin_required
def get_admin_users():
    """List all users (without password hashes)"""
    global auth_config
    auth_config = load_auth_config()
    users = [{'username': u['username'], 'role': u.get('role', 'user')} for u in auth_config.get('users', [])]
    return jsonify({'users': users})

@app.route('/api/admin/users', methods=['POST'])
@admin_required
@rate_limit(
    'admin_user_mutation',
    ADMIN_MUTATION_RATE_LIMIT_MAX_ATTEMPTS,
    ADMIN_MUTATION_RATE_LIMIT_WINDOW_SEC,
    message='Too many user-management changes. Please wait before trying again.'
)
def add_admin_user():
    """Add a new user"""
    global auth_config
    try:
        data = request.get_json()
        username = data.get('username', '').strip()
        password = data.get('password', '')
        role = normalize_user_role(data.get('role', 'user'))

        if not username or not password:
            return jsonify({'status': 'error', 'message': 'Username and password are required'}), 400
        if len(password) < 4:
            return jsonify({'status': 'error', 'message': 'Password must be at least 4 characters'}), 400
        if role is None:
            return jsonify({'status': 'error', 'message': 'Role must be admin or user'}), 400

        auth_config = load_auth_config()

        # Check if username already exists
        if any(u['username'] == username for u in auth_config.get('users', [])):
            return jsonify({'status': 'error', 'message': f'User "{username}" already exists'}), 400

        auth_config['users'].append({
            'username': username,
            'password_hash': generate_password_hash(password),
            'role': role
        })
        rotate_session_epoch(auth_config)
        save_auth_config(auth_config)
        session['auth_epoch'] = auth_config.get('session_epoch', '')
        log_activity(session.get('username', '?'), 'user_mgmt', f'Added user: {username} (role={role})', request.remote_addr)
        print(f"👤 User added: {username} ({role})")
        return jsonify({'status': 'ok', 'message': f'User "{username}" added'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

@app.route('/api/admin/users/role', methods=['POST'])
@admin_required
@rate_limit(
    'admin_user_mutation',
    ADMIN_MUTATION_RATE_LIMIT_MAX_ATTEMPTS,
    ADMIN_MUTATION_RATE_LIMIT_WINDOW_SEC,
    message='Too many user-management changes. Please wait before trying again.'
)
def update_admin_user_role():
    """Update a user's role while ensuring at least one admin remains."""
    global auth_config
    try:
        data = request.get_json()
        username = data.get('username', '').strip()
        role = normalize_user_role(data.get('role', ''))

        if not username or role is None:
            return jsonify({'status': 'error', 'message': 'Username and valid role are required'}), 400

        auth_config = load_auth_config()
        users = auth_config.get('users', [])
        user = next((u for u in users if u['username'] == username), None)
        if not user:
            return jsonify({'status': 'error', 'message': f'User "{username}" not found'}), 404

        current_role = normalize_user_role(user.get('role', 'user')) or 'user'
        if current_role == role:
            return jsonify({'status': 'ok', 'message': 'Role unchanged'})

        admin_count = sum(1 for candidate in users if normalize_user_role(candidate.get('role', 'user')) == 'admin')
        if current_role == 'admin' and role != 'admin' and admin_count <= 1:
            return jsonify({'status': 'error', 'message': 'Cannot remove the last admin account'}), 400

        if username == session.get('username') and role != 'admin':
            return jsonify({'status': 'error', 'message': 'You cannot remove your own admin role while logged in'}), 400

        user['role'] = role
        rotate_session_epoch(auth_config)
        save_auth_config(auth_config)
        if username == session.get('username'):
            session['role'] = role
        session['auth_epoch'] = auth_config.get('session_epoch', '')
        log_activity(session.get('username', '?'), 'user_mgmt', f'Changed role for: {username} -> {role}', request.remote_addr)
        print(f"🛡️ Role changed for {username}: {current_role} -> {role}")
        return jsonify({'status': 'ok', 'message': f'Role updated for "{username}"'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

@app.route('/api/admin/users/password', methods=['POST'])
@admin_required
@rate_limit(
    'admin_user_mutation',
    ADMIN_MUTATION_RATE_LIMIT_MAX_ATTEMPTS,
    ADMIN_MUTATION_RATE_LIMIT_WINDOW_SEC,
    message='Too many user-management changes. Please wait before trying again.'
)
def change_user_password():
    """Change a user's password"""
    global auth_config
    try:
        data = request.get_json()
        username = data.get('username', '').strip()
        new_password = data.get('password', '')

        if not username or not new_password:
            return jsonify({'status': 'error', 'message': 'Username and password are required'}), 400
        if len(new_password) < 4:
            return jsonify({'status': 'error', 'message': 'Password must be at least 4 characters'}), 400

        auth_config = load_auth_config()
        user = next((u for u in auth_config.get('users', []) if u['username'] == username), None)
        if not user:
            return jsonify({'status': 'error', 'message': f'User "{username}" not found'}), 404

        user['password_hash'] = generate_password_hash(new_password)
        rotate_session_epoch(auth_config)
        save_auth_config(auth_config)
        session['auth_epoch'] = auth_config.get('session_epoch', '')
        log_activity(session.get('username', '?'), 'user_mgmt', f'Changed password for: {username}', request.remote_addr)
        print(f"🔑 Password changed for: {username}")
        return jsonify({'status': 'ok', 'message': f'Password updated for "{username}"'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

@app.route('/api/admin/users/delete', methods=['POST'])
@admin_required
@rate_limit(
    'admin_user_mutation',
    ADMIN_MUTATION_RATE_LIMIT_MAX_ATTEMPTS,
    ADMIN_MUTATION_RATE_LIMIT_WINDOW_SEC,
    message='Too many user-management changes. Please wait before trying again.'
)
def delete_admin_user():
    """Delete a user (cannot delete 'admin')"""
    global auth_config
    try:
        data = request.get_json()
        username = data.get('username', '').strip()

        if not username:
            return jsonify({'status': 'error', 'message': 'Username is required'}), 400
        if username == 'admin':
            return jsonify({'status': 'error', 'message': 'Cannot delete the admin account'}), 400

        auth_config = load_auth_config()
        original_len = len(auth_config.get('users', []))
        auth_config['users'] = [u for u in auth_config.get('users', []) if u['username'] != username]

        if len(auth_config['users']) == original_len:
            return jsonify({'status': 'error', 'message': f'User "{username}" not found'}), 404

        rotate_session_epoch(auth_config)
        save_auth_config(auth_config)
        session['auth_epoch'] = auth_config.get('session_epoch', '')
        log_activity(session.get('username', '?'), 'user_mgmt', f'Deleted user: {username}', request.remote_addr)
        print(f"🗑️ User deleted: {username}")
        return jsonify({'status': 'ok', 'message': f'User "{username}" deleted'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

if __name__ == '__main__':
    # Start MQTT Thread
    mqtt_thread = threading.Thread(target=start_mqtt)
    mqtt_thread.daemon = True
    mqtt_thread.start()
    
    # Start Heartbeat Monitor Thread
    monitor_thread = threading.Thread(target=monitor_heartbeat)
    monitor_thread.daemon = True
    monitor_thread.start()

    # Start WebSocket Broadcast Thread
    socketio.start_background_task(ws_broadcast)

    # Auto-start Camera
    try:
        import subprocess
        print("🎥 Starting Camera Service...")
        subprocess.Popen(["./start_cam.sh"], shell=True)
    except Exception as e:
        print(f"❌ Camera Start Error: {e}")
    
    print("🚀 Starting Web Server on port 80 (WebSocket enabled)...")
    socketio.run(app, host='0.0.0.0', port=80, debug=False, allow_unsafe_werkzeug=True)
