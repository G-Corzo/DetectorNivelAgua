# Gateway Raspberry Pi, MQTT, ThingSpeak y análisis IA

Esta carpeta contiene los servicios de la arquitectura ampliada. Mosquitto recibe telemetría MQTT del ESP32; `gateway.py` valida los mensajes, los guarda en SQLite y puede reenviar siete valores a ThingSpeak por HTTPS. `ai_dashboard.py` analiza las lecturas recientes guardadas y sirve un tablero local de reportes en el puerto 8080.

El gateway y los tableros son componentes separados del firmware. El ESP32 conserva sus reglas y alarmas locales incluso si la Raspberry Pi o los servicios cloud no están disponibles.

## Arquitectura del gateway

```text
ESP32 ── MQTT /1883 ──> Mosquitto ──> gateway.py ──> SQLite
                                               ├── HTTPS ──> ThingSpeak (opcional)
                                               └── ai_dashboard.py ──> Gemini (opcional)
                                                                        └── HTTP /8080
```

| Archivo | Responsabilidad |
|---|---|
| `mosquitto.conf` | Listener TCP, autenticación, ACL y persistencia de Mosquitto. |
| `acl` | Permisos MQTT por usuario (`iotnode` y `gateway`). |
| `gateway.py` | Suscripción MQTT, validación, deduplicación, SQLite y ThingSpeak. |
| `ai_dashboard.py` | Métricas, generación de reportes Gemini, almacenamiento de reportes y HTTP. |
| `send_alarm_command.py` | Publica comandos MQTT para silenciar/reactivar la alarma del nodo. |
| `demo_simulator.py` | Publica lecturas sintéticas por MQTT para probar el backend sin el ESP32. |
| `.env.example` | Plantilla pública sin secretos. Crear `.env` local desde esta plantilla si aún no existe. |

## 1. Preparar Mosquitto en Raspberry Pi OS

Conecta la Pi y el equipo del ESP32 a una WLAN de laboratorio que permita tráfico entre ellos. Reserva una dirección local para la Pi y configura esa IP como `MQTT_BROKER` en el archivo local `src/secrets.h` del firmware. La dirección de ejemplo `192.168.1.50` no es universal.

Instala Mosquitto y sus clientes:

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

Guarda las claves asignadas en el aprovisionamiento local del ESP32 (`iotnode`) y en `GATEWAY/.env` (`gateway`). La ACL permite al ESP32 publicar sensores/estado y leer comandos; `gateway` lee sensores/estado y publica comandos. No reenvíes el puerto 1883 desde el router hacia Internet.

## 2. Instalar el gateway Python

Desde la carpeta del repositorio en la Raspberry Pi:

```bash
cd DetectorNivelAgua/GATEWAY
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Si todavía no existe `.env`, créalo desde la plantilla y protege sus permisos:

```bash
cp .env.example .env
nano .env
chmod 600 .env
```

Configura MQTT local, clave de ThingSpeak si se utilizará y `GEMINI_API_KEY` si se ejecutarán reportes. No publiques ni compartas `.env`; está excluido por `.gitignore`. El archivo `.env.example` contiene nombres de variables y marcadores, no credenciales válidas.

En Windows para desarrollo local, los comandos equivalentes para instalar dependencias son:

```powershell
cd DetectorNivelAgua\GATEWAY
py -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

Si la política de PowerShell no permite activar el entorno, invoca directamente `.venv\Scripts\python.exe`. `ai_dashboard.py` requiere `requests`; el receptor MQTT y `demo_simulator.py` requieren además `paho-mqtt` y un broker Mosquitto alcanzable. Ejecutar el tablero por sí solo sirve la interfaz, pero para consultar telemetría debe existir la tabla creada por `gateway.py` y lecturas en ella.

Arranca el proceso receptor:

```bash
python gateway.py
```

Al iniciar, crea `data/gateway.sqlite3` y las tablas necesarias. Si no hay `THINGSPEAK_WRITE_API_KEY`, sigue guardando localmente e informa que el reenvío cloud está omitido. Mantén este proceso activo para recibir mensajes y escribir telemetría.

## 3. MQTT y esquema de telemetría

El tópico predeterminado es `sabana/borda/hidrico/sensores`. El gateway requiere un objeto JSON con estos campos numéricos: `waterLevel`, `waterDistance`, `temperature`, `humidity`, `pressure`, `solarRadiation` y `evaporationRisk`; además valida `state` como `NORMAL`, `ALERTA` o `CRITICO`. `device_id` y `message_id` identifican y deduplican muestras; `alarmMuted` y `timestamp` complementan la lectura.

Una lectura puede verse con `mosquitto_sub` desde la Pi:

```bash
mosquitto_sub -h 127.0.0.1 -u gateway -P 'CLAVE_GATEWAY' -t 'sabana/borda/hidrico/#' -v
```

Para una prueba aislada, publica un ejemplo compatible con el esquema:

```bash
mosquitto_pub -h 127.0.0.1 -u iotnode -P 'CLAVE_IOTNODE' \
  -t sabana/borda/hidrico/sensores \
  -m '{"device_id":"prueba-local","message_id":"prueba-1","waterLevel":62.4,"waterDistance":37.6,"temperature":21.8,"humidity":68.2,"pressure":1012.1,"solarRadiation":51.0,"evaporationRisk":42.7,"state":"NORMAL","alarmMuted":false,"timestamp":123}'
```

Las claves del ejemplo son marcadores; no incluyas claves reales en comandos que vayan a quedar grabados o publicados. El usuario gateway tiene permisos de lectura y no puede publicar en el tópico de sensores.

## 4. Probar sin ESP32 físico

`demo_simulator.py` crea una secuencia sintética (NORMAL, ALERTA, CRITICO y recuperación) y la publica por MQTT usando un ID aparte, `demo-tomine`. El gateway la valida y la guarda como cualquier mensaje que cumpla el esquema. El script no se conecta a sensores, no modifica SQLite directamente y no es la simulación Wokwi.

Configura la clave del usuario `iotnode` disponible para el emisor en el entorno (`DEMO_MQTT_PASSWORD` o `MQTT_IOTNODE_PASSWORD`) y ejecuta desde otra terminal con el entorno virtual activo:

```bash
python demo_simulator.py
```

Para repetir hasta `Ctrl+C`:

```bash
python demo_simulator.py --repeat
```

La frecuencia de la secuencia puede acelerarse con `--interval 1`. Si `THINGSPEAK_WRITE_API_KEY` está configurada en el `.env` del gateway, sus lecturas sintéticas también pueden ser reenviadas a ThingSpeak. Deja la clave cloud sin configurar durante una prueba que deba quedar solo en SQLite; identifica los datos como sintéticos si decides enviarlos a un canal.

Wokwi corre fuera de la WLAN privada. No asumas que el ESP32 virtual puede alcanzar una IP privada de la Raspberry Pi. Si la red no proporciona una ruta autorizada, muestra Wokwi y el backend como dos partes del diseño y usa el emisor sintético desde un equipo con acceso al broker para probar el backend.

## 5. Publicación a ThingSpeak

La clave `THINGSPEAK_WRITE_API_KEY` habilita publicación por HTTPS a `api.thingspeak.com/update.json`. `THINGSPEAK_CHANNEL_ID` sirve para identificar/consultar el canal privado; la escritura se autentica mediante Write API Key. El código en `gateway.py` mapea:

| Campo ThingSpeak | Variable | Unidad / significado |
|---|---|---|
| Field 1 | `waterLevel` | Nivel estimado (%) |
| Field 2 | `waterDistance` | Distancia a la superficie (cm) |
| Field 3 | `temperature` | Temperatura (°C) |
| Field 4 | `humidity` | Humedad relativa (%) |
| Field 5 | `pressure` | Presión (hPa) |
| Field 6 | `solarRadiation` | Porcentaje normalizado del LDR, no irradiancia calibrada |
| Field 7 | `evaporationRisk` | Índice de riesgo estimado (%) |

El gateway conserva las lecturas MQTT que recibe; limita el reenvío a una entrada cloud cada `THINGSPEAK_MIN_INTERVAL_SECONDS`, cuyo valor predeterminado es 300 segundos y mínimo de código 15 segundos. El intervalo efectivo debe respetar el plan y límites vigentes del canal. Por esto, una muestra cada 2 s en SQLite no implica una entrada ThingSpeak cada 2 s. La terminal informa un `entry_id` al confirmar escritura o un mensaje de error cuando no se pudo publicar.

## 6. Reportes de IA y tablero local

`ai_dashboard.py` es un proceso independiente; necesita que `gateway.py` haya inicializado la tabla `telemetry`. Cada ciclo consulta la ventana reciente (`AI_REPORT_WINDOW_SECONDS`, predeterminado 600 s), requiere tres lecturas (`AI_REPORT_MIN_READINGS`) y prepara métricas con una muestra distribuida de hasta 12 registros. Envía esas métricas y muestras a Gemini mediante `GEMINI_API_KEY`; no envía credenciales MQTT. Gemini devuelve resumen, tendencia observada, observaciones, acción sugerida para revisión humana e incertidumbre. El reporte queda en `ai_reports`.

Para el modelo predeterminado `gemini-3-flash-preview`, la solicitud usa `thinkingLevel=MINIMAL` y un máximo de 2048 tokens. Gemini 3 incluye los tokens de razonamiento interno en el límite de salida; si el modelo responde con `finishReason=MAX_TOKENS` o un JSON truncado, el ciclo marca la respuesta como inválida y no presenta el fragmento como resumen. El siguiente ciclo vuelve a intentar con nuevas lecturas. Consulta la [guía de razonamiento de Gemini](https://ai.google.dev/gemini-api/docs/thinking) y la [referencia de generateContent](https://ai.google.dev/api/generate-content) para los límites y el campo `finishReason`.

Inicia el tablero desde otra terminal, conservando activo `gateway.py`:

```bash
cd DetectorNivelAgua/GATEWAY
source .venv/bin/activate
python ai_dashboard.py
```

Abre `http://IP_DE_LA_RASPBERRY:8080/` desde un equipo autorizado en la WLAN. En la misma máquina puedes usar `http://localhost:8080/`. `/healthz` devuelve el estado HTTP del servidor. `AI_DASHBOARD_HOST=0.0.0.0` permite acceso desde otras interfaces de la Pi; mantén ese acceso dentro de una WLAN de confianza. Para acceso solo local usa `AI_DASHBOARD_HOST=127.0.0.1`. El puerto se configura como `AI_DASHBOARD_PORT`, predeterminado `8080`; este proyecto no configura 8000.

El proceso genera un intento al iniciar y repite cada `AI_REPORT_INTERVAL_SECONDS` (predeterminado 600 s). Sin muestras recientes aparecerá `no_data`; con menos de tres se registra `insufficient_data`; sin clave se informa que no está configurada. Esos estados no son reportes de análisis de telemetría. Errores de red, respuesta inválida o HTTP 503 se registran sin detener el receptor MQTT. El proceso imprime si la clave está configurada, nunca su valor.

Los datos agregados y la muestra reciente se envían a la API externa de Gemini. El tablero, SQLite y el gateway permanecen locales. El módulo no predice niveles futuros, no calcula evaporación física en mm/día y no controla alarmas.

## 7. Comandos de alarma

Con el ESP32 en línea, silencia y reactiva el buzzer desde la carpeta `GATEWAY`:

```bash
python send_alarm_command.py mute
python send_alarm_command.py unmute
```

Estos comandos publican en `sabana/borda/hidrico/command/alarm`; el ESP32 los recibe por MQTT. Cambian el silencio del buzzer, no el estado de alerta calculado.

## 8. Operación y diagnóstico

- **`Not authorized` en Mosquitto:** comprueba usuario/clave local y ACL; el emisor ESP32 debe usar `iotnode` y `gateway.py` el usuario `gateway`.
- **No aparece `MQTT local conectado`:** revisa host, puerto, servicio `mosquitto` y credenciales MQTT del gateway.
- **Mensajes inválidos:** inspecciona el esquema, nombres de campos, tipos numéricos y categorías de `state`.
- **SQLite tiene datos, ThingSpeak no:** valida Write API Key, canal, conectividad HTTPS y frecuencia permitida.
- **El tablero IA carga sin reportes:** revisa si hay lecturas en la ventana, mínimo de muestras y presencia de `GEMINI_API_KEY`; consulta el registro de `ai_dashboard.py`.
- **El ESP32 virtual no conecta al broker local:** comprueba la conectividad desde Wokwi; una IP privada de la WLAN no es pública ni automáticamente accesible desde el simulador.

## Relación con Challenge 3

La sección de configuración de firmware menciona Challenge 3, pero el enunciado completo no está incluido en la copia de este repositorio. Este manual describe cómo instalar y operar la arquitectura ampliada; no afirma cumplimiento del reto requisito por requisito. Consulta [`../DOCUMENTACION/Arquitectura_y_Despliegue.md`](../DOCUMENTACION/Arquitectura_y_Despliegue.md) para alcance y límites.
