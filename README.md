# Detector de nivel de agua

Prototipo IoT de monitoreo de nivel de agua y condiciones ambientales, diseñado como una estación local de bajo costo. El ESP32 mide nivel, temperatura, humedad, presión y luz; calcula un índice estimado de evaporación y mantiene las alertas físicas y el tablero local del dispositivo.

## Arquitectura actual

```text
Sensores -> ESP32 -> MQTT local -> Raspberry Pi / Mosquitto -> SQLite
                                                   ├-> ThingSpeak por HTTPS (opcional)
                                                   └-> Gemini cada 10 min -> tablero local de reportes IA
```

Gemini analiza la ventana reciente de datos guardados en SQLite y genera un reporte informativo. No decide el estado de alerta ni controla el buzzer. El índice de evaporación es una estimación normalizada, no una medición física en mm/día.

## Código y documentación

- [Firmware y simulación](CODIGO%20Y%20SIMULACIONES/README.md)
- [Gateway Raspberry Pi, instalación y tablero de IA](GATEWAY/README.md)
- [Documentación técnica y análisis del Challenge 2](DOCUMENTACION/README.md)
- [Análisis de requisitos del Challenge 2](DOCUMENTACION/Analisis_Challenge2.md)
