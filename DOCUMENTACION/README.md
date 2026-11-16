# Documentación técnica

Índice de documentos de diseño, componentes y validación del prototipo.

## Arquitectura y requisitos

- [Arquitectura y despliegue](Arquitectura_y_Despliegue.md): límites del sistema, flujo entre ESP32, Mosquitto, SQLite, ThingSpeak y Gemini; configuración y estado real de validación.
- [Análisis de requisitos Challenge 2](Analisis_Challenge2.md): evaluación del firmware frente a los requisitos documentados para Challenge 2. La arquitectura MQTT/Raspberry Pi es una ampliación y debe distinguirse de esa evaluación.
- [Estudio del embalse de Tominé](Estudio_Embalse_Tomine.md): contexto y parámetros de referencia del prototipo.

## Sensores, calibración y cálculos

- [Calibración de sensores](Calibracion.md): registros y estimaciones de error consignados por el equipo; interpretar como resultados de las mediciones documentadas allí.
- [Fórmula de evaporación](Formula_Evaporacion.md): variables, normalización y ponderaciones del índice de riesgo.
- [Prototipo de evaporación potencial](Evaporacion_Potencial_Prototipo.md): justificación y límites del indicador simplificado.
- Datasheets incluidos: [ESP32 HW-724](Microcontrolador%20ESP32%20HW-724/), [HC-SR04](Datasheet%20-%20Sensor%20de%20Distancia%20Ultrasonico%20HC-SR04.PDF), [DHT22](Datasheet%20-%20Sensor%20de%20Humedad%20y%20Temperatura%20DHT22.PDF), [BMP280](Datasheet%20-%20Sensor%20de%20Presion%20BMP280.pdf) y [LDR GL55](Datasheet%20-%20Sensor%20LDR%20GL55.PDF).
- Diagramas de cableado y componentes: archivos `Diagrama*.png`, `Diagrama del Circuito.jpeg` y `formula_evaporacion.svg` en esta carpeta.

## Documentación por componente

- [Firmware y simulación ESP32/Wokwi](../CODIGO%20Y%20SIMULACIONES/README.md)
- [Gateway Raspberry Pi, Mosquitto y servicios cloud](../GATEWAY/README.md)

## Nota sobre Challenge 3

El repositorio disponible no incluye el enunciado completo del Challenge 3. La sección “Configuración de Challenge 3” del README del firmware describe el aprovisionamiento de credenciales y la conexión al gateway, pero no permite hacer una matriz de cumplimiento del reto. La arquitectura actual y sus dependencias se documentan en [Arquitectura y despliegue](Arquitectura_y_Despliegue.md). Para una evaluación de requisitos de Challenge 3 hace falta incorporar el documento fuente del reto.
