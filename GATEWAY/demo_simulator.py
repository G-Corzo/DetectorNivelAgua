#!/usr/bin/env python3
"""Publica una secuencia de telemetría sintética para grabar una demostración.

Las lecturas usan el mismo formato MQTT del ESP32, pero un ID separado
(demo-tomine). No modifica firmware ni SQLite directamente. ThingSpeak se
omite por defecto para evitar mezclar datos sintéticos en el canal real.
"""

from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path
from typing import Any

import paho.mqtt.client as mqtt


ROOT = Path(__file__).resolve().parent


def load_env_file() -> None:
    path = ROOT / ".env"
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def build_scenario() -> list[dict[str, Any]]:
    """Tres etapas con valores coherentes con las reglas del firmware."""
    readings: list[dict[str, Any]] = []
    # Estabilidad inicial: nivel alto y ambiente templado.
    for _ in range(6):
        readings.append({"waterDistance": 32.0, "temperature": 22.0, "humidity": 68.0,
                         "pressure": 1012.0, "solarRadiation": 35.0})
    # Descenso gradual y ambiente más cálido/seco: alerta.
    for i in range(8):
        readings.append({"waterDistance": 67.0 + i * 2.0, "temperature": 31.0,
                         "humidity": 35.0, "pressure": 1005.0, "solarRadiation": 82.0})
    # Nivel bajo junto con alto riesgo estimado: crítico.
    for _ in range(8):
        readings.append({"waterDistance": 105.0, "temperature": 38.0, "humidity": 15.0,
                         "pressure": 995.0, "solarRadiation": 100.0})
    # Recuperación gradual para cerrar la grabación con estado normal.
    for i in range(8):
        readings.append({"waterDistance": 100.0 - i * 9.0, "temperature": 24.0,
                         "humidity": 60.0, "pressure": 1011.0, "solarRadiation": 35.0})
    return readings


def risk_score(reading: dict[str, float]) -> float:
    temp = min(100.0, max(0.0, (reading["temperature"] - 20.0) * 5.0))
    dry_air = min(100.0, max(0.0, 100.0 - reading["humidity"]))
    radiation = min(100.0, max(0.0, reading["solarRadiation"]))
    low_pressure = min(100.0, max(0.0, (1013.25 - reading["pressure"]) * 5.0))
    return temp * .35 + dry_air * .30 + radiation * .25 + low_pressure * .10


def main() -> None:
    load_env_file()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interval", type=float, default=2.0, help="segundos entre lecturas (default: 2)")
    parser.add_argument("--repeat", action="store_true", help="reinicia la secuencia al terminar")
    parser.add_argument("--device", default="demo-tomine", help="ID independiente para la demostración")
    args = parser.parse_args()
    if args.interval <= 0:
        parser.error("--interval debe ser mayor que cero")

    host = os.getenv("MQTT_HOST", "127.0.0.1")
    port = int(os.getenv("MQTT_PORT", "1883"))
    # En la ACL del proyecto iotnode puede publicar telemetría. Gateway normalmente
    # solo tiene permiso de lectura para ese tópico.
    username = os.getenv("DEMO_MQTT_USERNAME", "iotnode")
    password = os.getenv("DEMO_MQTT_PASSWORD", "")
    topic = os.getenv("MQTT_TELEMETRY_TOPIC", "sabana/borda/hidrico/sensores")
    if not args.allow_cloud and os.getenv("THINGSPEAK_WRITE_API_KEY", "").strip():
        print("Aviso: el Gateway reenviará lecturas a ThingSpeak. Deja THINGSPEAK_WRITE_API_KEY vacío durante la demo.")

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"{args.device}-sim")
    if username:
        client.username_pw_set(username, password or os.getenv("MQTT_IOTNODE_PASSWORD", ""))
    client.connect(host, port, keepalive=30)
    client.loop_start()
    print(f"Simulación conectada a {host}:{port}, tópico {topic}, dispositivo {args.device}.")
    print("Secuencia: NORMAL → ALERTA → CRITICO → recuperación. Ctrl+C para detener.")
    sequence = 0
    scenario = build_scenario()
    try:
        while True:
            for stage, reading in enumerate(scenario):
                sequence += 1
                level = min(100.0, max(0.0, (138.5 - reading["waterDistance"]) / 138.5 * 100.0))
                risk = risk_score(reading)
                if level < 30.0 and risk >= 70.0:
                    state = "CRITICO"
                elif level < 55.0 or risk >= 55.0:
                    state = "ALERTA"
                else:
                    state = "NORMAL"
                payload = {
                    "device_id": args.device,
                    "message_id": f"{args.device}-{sequence}",
                    "waterLevel": round(level, 1),
                    **reading,
                    "evaporationRisk": round(risk, 1),
                    "state": state,
                    "alarmMuted": False,
                    "timestamp": int(time.time()),
                }
                info = client.publish(topic, json.dumps(payload, separators=(",", ":")), qos=1, retain=False)
                info.wait_for_publish(timeout=5)
                if not info.is_published():
                    raise RuntimeError("el broker no confirmó la lectura")
                print(f"[{sequence:02}] {state:7} nivel={level:5.1f}% riesgo={risk:5.1f}% distancia={reading['waterDistance']:5.1f} cm")
                time.sleep(args.interval)
            if not args.repeat:
                break
    except KeyboardInterrupt:
        print("\nSimulación detenida.")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
