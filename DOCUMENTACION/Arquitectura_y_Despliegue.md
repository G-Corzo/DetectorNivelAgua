# Arquitectura y despliegue

## Alcance

El proyecto tiene dos niveles que conviene distinguir:

1. **Nodo ESP32:** adquisición, cálculos, clasificación local, salidas de alarma y tablero web del dispositivo.
2. **Arquitectura ampliada:** envío MQTT a Mosquitto, gateway Python en Raspberry Pi, persistencia SQLite, reenvío opcional a ThingSpeak y análisis periódico opcional con Gemini.

La segunda capa requiere que el nodo, el broker y la Raspberry Pi puedan comunicarse por la red configurada. No es necesaria para que el ESP32 aplique sus reglas locales. La integración no se considera validada extremo a extremo hasta observar una lectura en el receptor, en SQLite y en el destino cloud configurado.

## Diagrama de componentes

```text
┌───────────────────────────────┐
│ ESP32                         │
│ sensores → cálculo → alerta   │
│ OLED + LEDs + buzzer          │
│ tablero HTTP + MQTT           │
└──────────────┬────────────────┘
               │ MQTT local, tópico de sensores
               ▼
┌───────────────────────────────┐
│ Raspberry Pi                  │
│ Mosquitto → gateway.py        │
│                    └→ SQLite  │
│                         ├→ HTTPS ThingSpeak (opcional)
│                         └→ ai_dashboard.py (opcional)
└───────────────────────────────┘
                                    │ métricas + muestra reciente
                                    ▼
                                 Gemini API
                                    │ reporte JSON
                                    ▼
                              tablero local :8080
```

El tablero HTTP del ESP32 y el tablero de reportes de IA son servicios distintos. El primero muestra lecturas actuales e historial reciente y tiene controles de alarma. El segundo consulta reportes almacenados en SQLite, es de solo lectura y no activa actuadores.

## Flujo de una lectura

1. `sensorTask` del ESP32 toma muestras cada 2 segundos y actualiza el registro compartido, OLED, salidas de alarma e historial circular local de 30 lecturas.
2. El ciclo principal atiende la WLAN, el servidor HTTP y MQTT. `publishLatestReading` publica una lectura nueva al tópico `sabana/borda/hidrico/sensores`.
3. Mosquitto autentica al publicador `iotnode` y al lector `gateway`; `GATEWAY/acl` limita los tópicos autorizados.
4. `gateway.py` decodifica y valida los campos requeridos. Inserta cada mensaje en `data/gateway.sqlite3` con unicidad por `device_id` y `message_id`.
5. Si está configurada `THINGSPEAK_WRITE_API_KEY`, el gateway envía siete campos numéricos a ThingSpeak por HTTPS. El intervalo de publicación se configura con `THINGSPEAK_MIN_INTERVAL_SECONDS` (predeterminado 300 s); SQLite puede conservar más lecturas que las que se envían al canal.
6. Si se inicia `ai_dashboard.py`, el proceso agrupa las lecturas recientes por dispositivo, calcula métricas y una muestra distribuida de hasta 12 registros, solicita un análisis a Gemini y guarda la respuesta en `ai_reports`.

## Firmware ESP32

Código principal: [`CODIGO Y SIMULACIONES/src/main.cpp`](../CODIGO%20Y%20SIMULACIONES/src/main.cpp).

- `readSensors`: recopila distancia, temperatura, humedad, presión y lectura LDR normalizada.
- `calculateWaterLevelPercent`: convierte distancia a nivel usando `TANK_DEPTH_CM = 138.5 cm`.
- `calculateEvaporationRisk`: combina temperatura, aire seco, radiación y presión en un indicador normalizado. No representa evaporación física en mm/día.
- `evaluateState`: devuelve NORMAL, ALERTA o CRITICO según nivel y riesgo.
- `applyAlertState`: configura LEDs y buzzer conforme al estado; silenciar el buzzer no elimina el estado de riesgo.
- `sensorTask`: actualiza medidas y la historia circular fuera del ciclo principal.
- `setupDashboardServer`, `handleDashboard` y `handleStatusApi`: implementan el tablero HTTP autenticado, `/` y `/api/status`.
- `connectMqtt` y `publishLatestReading`: mantienen la sesión MQTT y publican JSON.
- `mqttCallback`: recibe el comando para silenciar/reactivar la alarma cuando el ESP32 está conectado al broker.

La WLAN, credenciales del tablero y credenciales MQTT se definen localmente en `src/secrets.h`, creado a partir de `src/secrets.h.example`. El archivo real está ignorado por Git.

## Gateway Raspberry Pi

El directorio [`GATEWAY/`](../GATEWAY/) contiene:

- `mosquitto.conf`: listener MQTT y política de autenticación.
- `acl`: tópicos que puede leer o publicar cada usuario.
- `gateway.py`: servicio receptor, validación, persistencia y ThingSpeak.
- `ai_dashboard.py`: proceso de reportes y servidor HTTP de solo lectura.
- `send_alarm_command.py`: herramienta para publicar comandos de alarma por MQTT.
- `demo_simulator.py`: generador de datos sintéticos para probar MQTT desde un equipo con acceso al broker; no simula los sensores dentro de Wokwi.
- `.env.example`: plantilla compartible de variables; `.env` es privado e ignorado.

La base SQLite se crea en `GATEWAY/data/gateway.sqlite3`, también ignorada por Git. La tabla `telemetry` se inicializa al arrancar `gateway.py`; `ai_dashboard.py` crea la tabla `ai_reports`. Para reportes con datos, deben existir lecturas recientes en `telemetry` y una `GEMINI_API_KEY` válida.

## Configuración de red

En la instalación prevista, la Raspberry Pi tiene una dirección reservada en la WLAN y Mosquitto escucha en TCP 1883. El ESP32 usa esa dirección como `MQTT_BROKER`. El ejemplo de plantilla contiene una dirección ilustrativa; se debe reemplazar por la dirección realmente asignada.

Wokwi ejecuta la simulación fuera de la red privada de la Pi. Una IP privada de Raspberry Pi no es automáticamente accesible desde Wokwi; la conectividad necesita una ruta de red compatible. No se debe publicar el broker privado mediante port forwarding del router. Si no existe ruta desde el simulador, se puede demostrar la capa de backend mediante `demo_simulator.py` ejecutado desde un equipo autorizado que alcance el broker, identificando las lecturas como sintéticas.

## Servicios y puertos

| Servicio | Puerto | Dónde corre | Función |
|---|---:|---|---|
| HTTP del ESP32 | 80 | ESP32 | Tablero local autenticado y API de estado. |
| MQTT Mosquitto | 1883/TCP | Raspberry Pi | Mensajes de telemetría y comandos dentro de la WLAN. |
| Tablero de reportes IA | 8080/TCP | Proceso Python en Raspberry Pi | Consulta de reportes guardados; no controla alarmas. |
| ThingSpeak | HTTPS/443 | Servicio externo | Recepción cloud opcional por API. |
| Gemini API | HTTPS/443 | Servicio externo | Generación opcional del reporte a partir de métricas/muestra. |

El puerto 8000 no está configurado en este proyecto; el puerto del tablero IA es 8080 (`AI_DASHBOARD_PORT`).

## Configuración de secretos

- `CODIGO Y SIMULACIONES/src/secrets.h`: WLAN, usuario/clave del tablero, host del broker y credenciales MQTT del ESP32.
- `GATEWAY/.env`: credenciales del usuario MQTT `gateway`, ubicación de la base, claves de ThingSpeak/Gemini y variables de servicio.
- Nunca subir ni mostrar esos archivos. Las plantillas `secrets.h.example` y `.env.example` no deben contener secretos reales.

## Estados de validación conocidos

- El código del firmware, gateway y análisis IA está presente en el repositorio.
- `demo_simulator.py` puede producir lecturas sintéticas MQTT con el esquema esperado, si se proporciona una conexión y credenciales válidas.
- El registro de `gateway.sqlite3` y cualquier build local son archivos de ejecución; no se versionan.
- La ejecución simultánea de ESP32/Wokwi → broker privado → gateway → ThingSpeak/Gemini depende de conectividad, credenciales y límites externos. No se debe describir como observada si no se validó en el entorno correspondiente.
- El README del firmware incluye una sección de preparación para Challenge 3, pero el enunciado completo no está incluido en esta copia del repositorio. No es posible evaluar cumplimiento de Challenge 3 requisito por requisito sin el documento fuente.
