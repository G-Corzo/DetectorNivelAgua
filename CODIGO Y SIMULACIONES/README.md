# Firmware ESP32 y simulación Wokwi

Esta carpeta es el proyecto PlatformIO del nodo IoT. El circuito se describe en `diagram.json`; el firmware se encuentra en `src/main.cpp`; PlatformIO usa `platformio.ini`; y `wokwi.toml` indica a la extensión Wokwi qué binario usar.

## Arquitectura del nodo

```text
HC-SR04 + DHT22 + BMP280 + LDR
                 ↓
         ESP32 / sensorTask
                 ├── nivel y riesgo estimado
                 ├── estado local + LEDs + buzzer
                 ├── OLED + historial local de 30 muestras
                 ├── tablero HTTP autenticado + /api/status
                 └── MQTT hacia el gateway (arquitectura ampliada)
```

El estado de alerta y las salidas físicas se calculan localmente. MQTT es una ampliación para enviar telemetría al gateway Raspberry Pi; no es necesario para las alertas locales. La publicación del firmware usa PubSubClient en QoS 0.

## Componentes y conexiones

| Componente | Señal | GPIO ESP32 |
|---|---|---:|
| DHT22 | DATA | 15 |
| HC-SR04 | TRIG | 5 |
| HC-SR04 | ECHO | 18 |
| BMP280 y OLED SSD1306 | SDA | 21 |
| BMP280 y OLED SSD1306 | SCL | 22 |
| LDR | AO | 35 |
| LED verde | Ánodo | 25 |
| LED amarillo | Ánodo | 26 |
| LED rojo | Ánodo | 27 |
| Buzzer | Positivo | 14 |
| Botón de página/silencio | Señal | 33 |

Las conexiones completas se encuentran en `diagram.json`. En el montaje físico, el HC-SR04 normalmente entrega 5 V en ECHO; usa divisor de tensión o adaptador de nivel para proteger la entrada de 3,3 V del ESP32.

## Lectura y clasificación

- El nodo intenta leer sensores cada 2 segundos (`SAMPLE_PERIOD_MS`).
- El HC-SR04 mide distancia entre el sensor y la superficie. La calibración de firmware usa `TANK_DEPTH_CM = 138.5` cm y calcula `nivel = (profundidad − distancia) / profundidad × 100`.
- La conversión del LDR a porcentaje es una normalización del ADC, no una medición calibrada de irradiancia solar.
- El riesgo normalizado combina temperatura (35 %), aire seco (30 %), radiación aproximada (25 %) y baja presión (10 %). El nombre de la variable no significa evaporación medida en mm/día.
- NORMAL requiere nivel ≥55 % y riesgo <55 %. ALERTA se activa cuando el nivel es menor a 55 % o el riesgo ≥55 %. CRITICO requiere simultáneamente nivel <30 % y riesgo ≥70 %.
- NORMAL usa LED verde y buzzer apagado; ALERTA usa LED amarillo y sonido intermitente; CRITICO usa LED rojo y sonido continuo. El silencio desactiva el buzzer, pero conserva la clasificación de riesgo.

La lectura se implementa en `readSensors`; las conversiones en `calculateWaterLevelPercent` y `calculateEvaporationRisk`; la decisión en `evaluateState`; y la salida en `applyAlertState`. `sensorTask` ejecuta el ciclo de medición separado del `loop()` que atiende HTTP y MQTT.

## Tablero local del ESP32

El servidor web corre en el propio microcontrolador, por HTTP en el puerto 80:

- `/`: tablero de lecturas actuales e historial reciente.
- `/api/status`: estado actual en JSON.
- `/alarm/off` y `/alarm/on`: rutas POST para silenciar/reactivar el buzzer, si el usuario pasó autenticación.

Las funciones principales son `setupDashboardServer`, `handleDashboard`, `handleStatusApi`, `handleAlarmOff` y `handleAlarmOn`. Las credenciales las toma de `src/secrets.h`; no son las mismas que las del tablero de reportes IA del gateway. La autenticación HTTP básica no cifra el tráfico, por eso se debe limitar a una WLAN de confianza.

Al conectar a Wi-Fi, el monitor serial imprime la dirección IP del ESP32. El tablero se abre en `http://<IP-del-ESP32>/` desde un equipo con acceso a esa red. La salida JSON puede consultarse en `http://<IP-del-ESP32>/api/status`.

## Publicación y comandos MQTT

El firmware se conecta al broker de `MQTT_BROKER`, usando puerto 1883. Publica en `sabana/borda/hidrico/sensores` mensajes JSON con `device_id`, `message_id`, nivel, distancia, temperatura, humedad, presión, radiación, riesgo, estado, `alarmMuted` y timestamp.

- `connectMqtt`: conecta y establece estado de presencia; se suscribe al tópico de comando.
- `publishLatestReading`: publica los valores nuevos.
- `mqttCallback`: procesa `{"muted":true}` o `{"muted":false}`.

El cliente actual no persiste una cola local; si se pierde conectividad, no se garantiza la entrega de las muestras mientras está desconectado. El gateway deduplica mensajes recibidos con el mismo dispositivo e identificador.

## Configuración privada

1. Copia `src/secrets.h.example` a `src/secrets.h`.
2. Configura SSID/clave WLAN, usuario/clave del tablero, host/IP del broker y usuario/clave MQTT del ESP32.
3. Mantén `secrets.h` fuera de Git. El `.gitignore` de esta carpeta ya lo excluye.

La plantilla deja marcadores. El ejemplo de broker `192.168.1.50` es ilustrativo y debe cambiarse por la dirección real de la Raspberry Pi. El SSID `Wokwi-GUEST` puede usarse para conectividad simulada a Internet en Wokwi; no da acceso automáticamente a un broker que esté en una IP privada de la red local.

## Compilar y abrir la simulación

1. Abre esta carpeta en VS Code.
2. Completa `src/secrets.h` localmente.
3. Compila el entorno `esp32dev` desde PlatformIO.
4. Ejecuta `Wokwi: Start Simulator`.
5. Cambia distancia, temperatura, humedad y luz en los controles de los componentes para observar cómo reaccionan el cálculo y los estados.
6. Consulta monitor serial y, si la red simulada lo permite, abre el tablero HTTP en la IP que informa el ESP32.

Wokwi ejecuta la simulación en infraestructura externa; la comunicación desde Wokwi hasta un broker privado de Raspberry Pi requiere una ruta de red compatible. Para más información de Mosquitto, almacenamiento, ThingSpeak y reportes IA, consulta [`../GATEWAY/README.md`](../GATEWAY/README.md) y [`../DOCUMENTACION/Arquitectura_y_Despliegue.md`](../DOCUMENTACION/Arquitectura_y_Despliegue.md).

## Configuración para Challenge 3

El enunciado completo del Challenge 3 no se encuentra en este repositorio. Esta sección describe cómo aprovisionar esta versión del firmware para el gateway ampliado; no sustituye una matriz de requisitos del reto. El archivo `src/secrets.h.example` muestra las variables locales necesarias. El broker esperado es Mosquitto local, no un broker MQTT público.
