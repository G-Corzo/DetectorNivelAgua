# Gateway IoT para Raspberry Pi

La Raspberry Pi ejecuta Mosquitto, recibe la telemetría MQTT del ESP32, la guarda primero en SQLite y la reenvía a Ubidots por MQTT/TLS. El firmware del ESP32 conserva el servidor web local autenticado; no se publica directamente a un broker público.

## 1. Preparar Raspberry Pi OS

Conectar la Pi a la WLAN del prototipo y reservarle una IP fija/reserva DHCP. El ejemplo de firmware usa `192.168.1.50`; reemplazar esa dirección en `src/main.cpp` por la IP real de la Pi antes de compilar.

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients python3 python3-venv
cd ~/DetectorNivelAgua/GATEWAY
sudo systemctl stop mosquitto
sudo cp mosquitto.conf /etc/mosquitto/conf.d/gateway.conf
sudo cp acl /etc/mosquitto/acl
sudo mosquitto_passwd -c /etc/mosquitto/passwd iotnode
sudo mosquitto_passwd /etc/mosquitto/passwd gateway
sudo chown root:mosquitto /etc/mosquitto/passwd /etc/mosquitto/acl
sudo chmod 640 /etc/mosquitto/passwd /etc/mosquitto/acl
sudo systemctl enable --now mosquitto
sudo systemctl status mosquitto --no-pager
```

Usar la primera clave al configurar el firmware; la segunda debe coincidir con `MQTT_PASSWORD` del `.env`. No reenviar el puerto MQTT desde el router hacia Internet. Si la WLAN institucional aísla clientes, solicitar una red de laboratorio autorizada para permitir tráfico ESP32 ↔ Pi.

## 2. Instalar el Gateway

```bash
cd GATEWAY
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
nano .env
```

En `.env`, configurar `MQTT_PASSWORD`, `UBIDOTS_TOKEN` y `GEMINI_API_KEY` si se habilitará IA generativa. El token Ubidots y la clave Gemini se quedan en la Raspberry Pi; nunca deben quedar en el firmware ni subirse a Git.

```bash
python gateway.py
```

SQLite se crea en `data/gateway.sqlite3`. Cada lectura se inserta antes de intentar el reenvío a Ubidots. Si falta el token de Ubidots, el guardado local sigue funcionando y la publicación cloud queda omitida. QoS 1 del lado local puede reentregar mensajes; `device_id` + `message_id` deduplican el historial.

## 3. Probar el broker antes de tener hardware

En una terminal, suscribirse con el usuario gateway:

```bash
mosquitto_sub -h 127.0.0.1 -u gateway -P 'CLAVE_GATEWAY' -t 'sabana/borda/hidrico/#' -v
```

Publicar manualmente un mensaje (mantener el JSON en una sola línea):

```bash
mosquitto_pub -h 127.0.0.1 -u iotnode -P 'CLAVE_IOTNODE' -t sabana/borda/hidrico/sensores -m '{"device_id":"tomine-01","message_id":"manual-1","waterLevel":62.4,"waterDistance":37.6,"temperature":21.8,"humidity":68.2,"pressure":752.1,"solarRadiation":51.0,"evaporationRisk":42.7,"state":"NORMAL","alarmMuted":false,"timestamp":123}'
```

Ejecutar `python gateway.py` para comprobar persistencia y envío cloud. Para silenciar/reactivar el buzzer por el broker local:

```bash
python send_alarm_command.py mute
python send_alarm_command.py unmute
```

El ESP32 debe estar suscrito al tópico de comando y conectado a la WLAN del Gateway para recibirlo.

## 4. Variables cloud Ubidots

El Gateway publica en `industrial.api.ubidots.com:8883` con TLS y usuario = token Ubidots, password vacío. Mapea los campos ya existentes del firmware a las etiquetas `water_level_pct`, `water_distance_cm`, `temperature_c`, `humidity_pct`, `pressure_hpa`, `solar_radiation_pct`, `evaporation_risk_pct`, `alert_state_code` y `alarm_muted` en el dispositivo definido por `UBIDOTS_DEVICE_LABEL`.

## 5. IA

Con `GEMINI_API_KEY` configurada, las lecturas en `ALERTA` o `CRITICO` disparan, como máximo, una consulta por dispositivo cada cinco minutos. Se envían hasta 30 registros recientes y la respuesta se almacena en `ai_recommendations`. La recomendación es informativa, requiere revisión humana y no gobierna el buzzer ni sustituye las alertas locales. Sin clave o sin Internet, la adquisición y SQLite siguen activos.

Para la Wiki: documentar proveedor/modelo, prompts exactos, datos usados, verificación con casos conocidos y cómo se aplicó la sugerencia. La API Gemini debe revisarse/validarse según disponibilidad de cuenta y límites actuales.
