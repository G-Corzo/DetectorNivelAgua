#!/usr/bin/env python3
"""Publica el comando local de silenciar/reactivar el buzzer del ESP32."""

import argparse
import json
import os
from pathlib import Path

import paho.mqtt.client as mqtt

ROOT = Path(__file__).resolve().parent
env_file = ROOT / ".env"
if env_file.exists():
    for line in env_file.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("action", choices=("mute", "unmute"))
parser.add_argument("--device", default=os.getenv("DEVICE_ID", "tomine-01"))
args = parser.parse_args()

host = os.getenv("MQTT_HOST", "127.0.0.1")
port = int(os.getenv("MQTT_PORT", "1883"))
username = os.getenv("MQTT_USERNAME", "gateway")
password = os.getenv("MQTT_PASSWORD", "")
topic = os.getenv("MQTT_ALARM_COMMAND_TOPIC", "sabana/borda/hidrico/command/alarm")
payload = json.dumps({"muted": args.action == "mute"})

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"gateway-cmd-{args.device}")
if username:
    client.username_pw_set(username, password)
client.connect(host, port, keepalive=15)
client.loop_start()
try:
    result = client.publish(topic, payload, qos=1, retain=False)
    result.wait_for_publish(timeout=5)
    if not result.is_published():
        raise RuntimeError("el broker no confirmó la publicación")
    print(f"Comando publicado en {topic}: {payload}")
finally:
    client.loop_stop()
    client.disconnect()
