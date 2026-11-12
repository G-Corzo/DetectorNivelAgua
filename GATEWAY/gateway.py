#!/usr/bin/env python3
"""Gateway: recibe telemetría MQTT, la almacena en SQLite y la reenvía a ThingSpeak."""

from __future__ import annotations

import json
import math
import os
import sqlite3
import threading
import time
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import paho.mqtt.client as mqtt
import requests


ROOT = Path(__file__).resolve().parent


def load_env_file(path: Path = ROOT / ".env") -> None:
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


load_env_file()

LOCAL_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
LOCAL_PORT = int(os.getenv("MQTT_PORT", "1883"))
LOCAL_USER = os.getenv("MQTT_USERNAME", "gateway")
LOCAL_PASSWORD = os.getenv("MQTT_PASSWORD", "")
TELEMETRY_TOPIC = os.getenv("MQTT_TELEMETRY_TOPIC", "sabana/borda/hidrico/sensores")
ALARM_TOPIC = os.getenv("MQTT_ALARM_COMMAND_TOPIC", "sabana/borda/hidrico/command/alarm")
DEVICE_ID = os.getenv("DEVICE_ID", "tomine-01")
DB_PATH = Path(os.getenv("DATABASE_PATH", "data/gateway.sqlite3"))
if not DB_PATH.is_absolute():
    DB_PATH = ROOT / DB_PATH

THINGSPEAK_WRITE_API_KEY = os.getenv("THINGSPEAK_WRITE_API_KEY", "").strip()
THINGSPEAK_READ_API_KEY = os.getenv("THINGSPEAK_READ_API_KEY", "").strip()
THINGSPEAK_CHANNEL_ID = os.getenv("THINGSPEAK_CHANNEL_ID", "").strip()
THINGSPEAK_MIN_INTERVAL_SECONDS = max(15, int(os.getenv("THINGSPEAK_MIN_INTERVAL_SECONDS", "300")))

REQUIRED_NUMERIC = (
    "waterLevel", "waterDistance", "temperature", "humidity", "pressure",
    "solarRadiation", "evaporationRisk",
)
THINGSPEAK_FIELDS = {
    "waterLevel": "field1",
    "waterDistance": "field2",
    "temperature": "field3",
    "humidity": "field4",
    "pressure": "field5",
    "solarRadiation": "field6",
    "evaporationRisk": "field7",
}

db_lock = threading.Lock()
last_cloud_publish: dict[str, float] = {}


def db_connect() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH, timeout=10)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    return conn


@contextmanager
def database():
    conn = db_connect()
    try:
        with conn:
            yield conn
    finally:
        conn.close()


def init_db() -> None:
    with database() as conn:
        conn.executescript("""
        CREATE TABLE IF NOT EXISTS telemetry (
            id INTEGER PRIMARY KEY,
            device_id TEXT NOT NULL,
            message_id TEXT NOT NULL,
            received_at TEXT NOT NULL,
            device_timestamp_s INTEGER,
            water_level REAL NOT NULL,
            water_distance REAL NOT NULL,
            temperature REAL NOT NULL,
            humidity REAL NOT NULL,
            pressure REAL NOT NULL,
            solar_radiation REAL NOT NULL,
            evaporation_risk REAL NOT NULL,
            alert_state TEXT NOT NULL,
            alarm_muted INTEGER NOT NULL,
            raw_json TEXT NOT NULL,
            UNIQUE(device_id, message_id)
        );
        CREATE INDEX IF NOT EXISTS idx_telemetry_device_received
          ON telemetry(device_id, received_at);
        CREATE TABLE IF NOT EXISTS ai_recommendations (
            id INTEGER PRIMARY KEY,
            device_id TEXT NOT NULL,
            created_at TEXT NOT NULL,
            alert_state TEXT NOT NULL,
            recommendation TEXT NOT NULL,
            source TEXT NOT NULL
        );
        """)


def normalize_reading(payload: dict[str, Any]) -> dict[str, Any]:
    device_id = str(payload.get("device_id") or DEVICE_ID).strip()
    if not device_id or len(device_id) > 64:
        raise ValueError("device_id inválido")
    values: dict[str, float] = {}
    for key in REQUIRED_NUMERIC:
        value = payload.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"campo numérico inválido: {key}")
        values[key] = float(value)
        if not math.isfinite(values[key]):
            raise ValueError(f"campo numérico no finito: {key}")
    if not 0 <= values["waterLevel"] <= 100:
        raise ValueError("waterLevel fuera de 0..100")
    if not 0 <= values["humidity"] <= 100:
        raise ValueError("humidity fuera de 0..100")
    if not 0 <= values["solarRadiation"] <= 100:
        raise ValueError("solarRadiation fuera de 0..100")
    if not 0 <= values["evaporationRisk"] <= 100:
        raise ValueError("evaporationRisk fuera de 0..100")
    state = str(payload.get("state", "")).upper()
    if state not in {"NORMAL", "ALERTA", "CRITICO"}:
        raise ValueError("state debe ser NORMAL, ALERTA o CRITICO")
    message_id = str(payload.get("message_id") or "").strip()
    if not message_id:
        # Compatibilidad temporal con el .ino inicial. Recomendar agregar message_id.
        message_id = f"{device_id}-{payload.get('timestamp', int(time.time()))}"
    timestamp = payload.get("timestamp")
    try:
        device_timestamp_s = int(timestamp) if timestamp is not None else None
    except (TypeError, ValueError):
        device_timestamp_s = None
    return {
        "device_id": device_id,
        "message_id": message_id,
        "received_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "device_timestamp_s": device_timestamp_s,
        **values,
        "alert_state": state,
        "alarm_muted": int(bool(payload.get("alarmMuted", False))),
        "raw_json": json.dumps(payload, ensure_ascii=False, separators=(",", ":")),
    }


def save_reading(row: dict[str, Any]) -> bool:
    with db_lock, database() as conn:
        cursor = conn.execute("""
          INSERT OR IGNORE INTO telemetry (
            device_id,message_id,received_at,device_timestamp_s,water_level,
            water_distance,temperature,humidity,pressure,solar_radiation,
            evaporation_risk,alert_state,alarm_muted,raw_json
          ) VALUES (
            :device_id,:message_id,:received_at,:device_timestamp_s,:waterLevel,
            :waterDistance,:temperature,:humidity,:pressure,:solarRadiation,
            :evaporationRisk,:alert_state,:alarm_muted,:raw_json
          )
        """, row)
        return cursor.rowcount == 1


def publish_cloud(row: dict[str, Any]) -> None:
    if not THINGSPEAK_WRITE_API_KEY:
        return
    now = time.monotonic()
    device_id = row["device_id"]
    last_publish = last_cloud_publish.get(device_id, 0.0)
    if last_publish and now - last_publish < THINGSPEAK_MIN_INTERVAL_SECONDS:
        return
    body = {cloud_name: row[source_name] for source_name, cloud_name in THINGSPEAK_FIELDS.items()}
    body["api_key"] = THINGSPEAK_WRITE_API_KEY
    try:
        response = requests.post(
            "https://api.thingspeak.com/update.json", data=body, timeout=15
        )
        response.raise_for_status()
        result = response.json()
        if not result.get("entry_id"):
            print("ThingSpeak rechazó la actualización; revisa la Write API Key y el límite de publicación.")
            return
        last_cloud_publish[device_id] = now
        print(f"ThingSpeak: lectura guardada, entry_id={result['entry_id']}")
    except (requests.RequestException, ValueError) as exc:
        print(f"ThingSpeak no disponible; la lectura quedó guardada localmente: {exc}")


def on_local_connect(client: mqtt.Client, _userdata: Any, _flags: Any, reason_code: Any, _properties: Any = None) -> None:
    if reason_code == 0:
        client.subscribe(TELEMETRY_TOPIC, qos=1)
        print(f"MQTT local conectado; suscrito a {TELEMETRY_TOPIC}")
    else:
        print(f"No se pudo conectar al broker local: {reason_code}")


def on_local_message(_client: mqtt.Client, _userdata: Any, message: mqtt.MQTTMessage) -> None:
    try:
        payload = json.loads(message.payload.decode("utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("el mensaje debe ser un objeto JSON")
        row = normalize_reading(payload)
        inserted = save_reading(row)
        if not inserted:
            print(f"Duplicado ignorado: {row['device_id']} / {row['message_id']}")
            return
        print(f"Guardado SQLite: {row['device_id']} {row['alert_state']} nivel={row['waterLevel']:.1f}%")
        publish_cloud(row)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError, KeyError) as exc:
        print(f"Mensaje MQTT inválido en {message.topic}: {exc}")


def main() -> None:
    init_db()
    if THINGSPEAK_WRITE_API_KEY:
        print("ThingSpeak habilitado; publicación HTTPS activa.")
    else:
        print("THINGSPEAK_WRITE_API_KEY vacío: almacenamiento local activo; reenvío cloud pendiente.")
    local = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"gateway-sub-{DEVICE_ID}")
    if LOCAL_USER:
        local.username_pw_set(LOCAL_USER, LOCAL_PASSWORD)
    local.on_connect = on_local_connect
    local.on_message = on_local_message
    local.reconnect_delay_set(min_delay=1, max_delay=30)
    local.connect(LOCAL_HOST, LOCAL_PORT, keepalive=30)
    print(f"SQLite: {DB_PATH}")
    try:
        local.loop_forever()
    except KeyboardInterrupt:
        print("Cerrando Gateway...")
    finally:
        local.disconnect()


if __name__ == "__main__":
    main()
