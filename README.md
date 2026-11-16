# Detector de nivel de agua

Prototipo IoT para monitorear el nivel de agua y variables ambientales. El firmware del ESP32 calcula el estado localmente y contempla un tablero web, alarmas y publicación MQTT. La arquitectura ampliada agrega un gateway Mosquitto en Raspberry Pi, persistencia SQLite, reenvío opcional a ThingSpeak y reportes de análisis con Gemini.

## Arquitectura

```text
HC-SR04 + DHT22 + BMP280 + LDR
              │
              ▼
 ESP32: muestreo, cálculos, OLED, LEDs, buzzer y tablero HTTP local
              │ MQTT local (QoS 0 desde PubSubClient)
              ▼
 Raspberry Pi: Mosquitto → gateway.py → SQLite
                                  ├── HTTPS opcional → ThingSpeak
                                  └── ai_dashboard.py → Gemini → tablero local :8080
```

Las alertas y las reglas de clasificación se ejecutan en el ESP32. El análisis de Gemini es informativo, se aplica a datos ya guardados y no controla actuadores. La publicación a ThingSpeak depende de configurar una Write API Key. El índice denominado riesgo de evaporación es normalizado; no equivale a evaporación física en mm/día.

## Módulos del repositorio

- [Firmware ESP32 y simulación Wokwi](CODIGO%20Y%20SIMULACIONES/README.md): diagrama, pines, lógica, configuración local y ejecución.
- [Gateway Raspberry Pi](GATEWAY/README.md): Mosquitto, ACL, instalación, SQLite, ThingSpeak, análisis IA y operación.
- [Arquitectura y despliegue](DOCUMENTACION/Arquitectura_y_Despliegue.md): límites del sistema, flujo de datos, servicios, configuración y estado de validación.
- [Documentación técnica](DOCUMENTACION/README.md): índice de calibración, sensores, fórmulas y análisis de requisitos.
- [Análisis de requisitos Challenge 2](DOCUMENTACION/Analisis_Challenge2.md): evaluación histórica del reto y separación entre el diseño autónomo y la arquitectura MQTT ampliada.

## Estado de implementación

El firmware, el gateway MQTT, el almacenamiento SQLite, el adaptador opcional de ThingSpeak y el servicio de reportes están implementados en el repositorio. Su funcionamiento extremo a extremo requiere el ESP32 o una fuente MQTT, un broker Mosquitto accesible, la configuración local y, según la función, conexión a ThingSpeak o Gemini. Wokwi representa el circuito, pero la conexión desde el simulador a un broker privado depende de la topología de red y no se debe asumir como automática.

El repositorio contiene una guía de configuración de Challenge 3 en el README del firmware; no contiene el enunciado completo del Challenge 3. Por eso no se afirma aquí cumplimiento requisito por requisito de ese reto.

## Seguridad y secretos

Los valores privados se configuran en `CODIGO Y SIMULACIONES/src/secrets.h` y `GATEWAY/.env`; ambos archivos están excluidos de Git. Se deben crear desde sus plantillas y no se deben copiar al repositorio. Mantén Mosquitto dentro de la red autorizada, con autenticación y ACL, y no redirijas el puerto 1883 desde el router hacia Internet.
