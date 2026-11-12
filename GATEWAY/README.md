# Gateway IoT para Raspberry Pi

La Raspberry Pi ejecuta Mosquitto, recibe la telemetría MQTT del ESP32, la guarda primero en SQLite y la reenvía a ThingSpeak por HTTPS. El firmware del ESP32 conserva el servidor web local autenticado; no se publica directamente a un broker público.

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
```

Mantener un único archivo `.env` por instalación y colocarlo en esta carpeta de la Raspberry Pi. El `.env` contiene secretos y está excluido de Git, así que debe configurarse en la Pi aparte del código. Completar allí `MQTT_PASSWORD`, `THINGSPEAK_CHANNEL_ID`, `THINGSPEAK_WRITE_API_KEY` y `GEMINI_API_KEY`. No se usa `.env.example`.

Variables adicionales para el análisis y tablero IA:

```dotenv
GEMINI_API_KEY=CLAVE_DE_AI_STUDIO
GEMINI_MODEL=gemini-3-flash-preview
AI_REPORT_INTERVAL_SECONDS=600
AI_REPORT_WINDOW_SECONDS=600
AI_REPORT_MIN_READINGS=3
AI_DASHBOARD_HOST=0.0.0.0
AI_DASHBOARD_PORT=8080
```

```bash
python gateway.py
```

SQLite se crea en `data/gateway.sqlite3`. Cada lectura se inserta antes de intentar el reenvío a ThingSpeak. Si no está configurada la Write API Key, el guardado local sigue funcionando y la publicación cloud queda omitida. QoS 1 del lado local puede reentregar mensajes; `device_id` + `message_id` deduplican el historial.

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

## 4. Variables cloud ThingSpeak

El Gateway publica por HTTPS en `api.thingspeak.com` usando `THINGSPEAK_WRITE_API_KEY`. Mapea los siete valores numéricos del firmware a ThingSpeak Field 1–7: nivel de agua (%), distancia a la superficie (cm), temperatura (°C), humedad relativa (%), presión (hPa), lectura LDR normalizada (%) y riesgo estimado de evaporación (%). `THINGSPEAK_CHANNEL_ID` identifica el canal y se usa para lecturas privadas desde ThingSpeak; la publicación usa la Write API Key.

El guardado local conserva todas las muestras (cada 2 s en el firmware). Para cumplir el intervalo mínimo de publicación de ThingSpeak, el reenvío se limita por defecto a un paquete cada 300 s (`THINGSPEAK_MIN_INTERVAL_SECONDS=300`). ThingSpeak aplica un intervalo mínimo entre actualizaciones; mantén al menos 15 s y confirma los límites de tu cuenta antes de reducirlo.

## 5. Reportes periódicos con Gemini y tablero local

El proceso `ai_dashboard.py` es independiente del receptor MQTT. Cada 10 minutos consulta en SQLite las lecturas recibidas durante la ventana de análisis, calcula estadísticas verificables y pide a Gemini un reporte en español. Por defecto requiere al menos tres lecturas por dispositivo. Gemini recibe estadísticas y una muestra distribuida de hasta 12 lecturas, no toda la base de datos. El reporte queda guardado en la tabla `ai_reports`. La llamada usa la API `generateContent` con `responseJsonSchema`; si el modelo devuelve texto libre, el tablero lo muestra como comentario y marca la tendencia como no determinada. Consulta la [documentación oficial de Gemini](https://ai.google.dev/gemini-api/docs/structured-output) para el formato de respuesta.

La base de datos y el tablero se quedan en la Raspberry Pi, pero las estadísticas y la muestra de lecturas de cada ventana se envían a la API de Gemini para generar el reporte. No se envían credenciales MQTT ni la clave de Gemini como parte de los datos.

Iniciar el servicio en otra terminal, manteniendo `gateway.py` activo:

```bash
cd GATEWAY
. .venv/bin/activate
python ai_dashboard.py
```

Abrir `http://IP_DE_LA_RASPBERRY:8080/` desde un equipo conectado a la WLAN local. Esta página muestra únicamente los reportes de Gemini y su estado; no ofrece controles para alarmas ni cambia las reglas del ESP32. `/healthz` permite revisar que el servidor responde. Para limitarlo a la propia Raspberry Pi, configurar `AI_DASHBOARD_HOST=127.0.0.1`; con `0.0.0.0` escucha en las interfaces de red de la Pi, por lo que debe mantenerse dentro de una WLAN confiable y no exponerse mediante redirección de puertos a Internet.

Al iniciar `ai_dashboard.py` se intenta generar un primer reporte de inmediato con las lecturas disponibles en la ventana reciente; después se repite cada intervalo. Si no hay lecturas se muestra un estado `no_data`; si hay menos del mínimo se guarda `insufficient_data`. En ambos casos vuelve a intentarlo en el siguiente ciclo. Usa `gemini-3-flash-preview`; ante HTTP 503 o timeout reintenta hasta dos veces con pausas crecientes y timeout de 60 segundos. El parser acepta JSON directo, cercado en Markdown o rodeado por texto breve. Si el servicio sigue indisponible o entrega una respuesta inválida, registra el error y el Gateway de telemetría continúa operando. Sin `GEMINI_API_KEY` no se envía telemetría al proveedor y el tablero informa que falta la configuración. Al arrancar, el proceso imprime la ruta SQLite y si detectó la variable de clave, pero nunca imprime su valor.

La respuesta incluye resumen, tendencia observada, observaciones, sugerencia para revisión humana e incertidumbre. No predice el nivel futuro, no calcula evaporación en mm/día y no activa ni silencia alarmas. Las alertas físicas continúan bajo las reglas locales del ESP32.

Para la Wiki: documentar modelo, prompt, ventana y estadísticas, fuente de datos, casos de referencia, resultados, latencia y cómo se revisaron las sugerencias generadas.
