# Analisis de requisitos - Challenge 2

## Fechas clave

- Entrega de no tangibles: 29 de septiembre de 2026, 10:00 a. m.
- Sustentacion: 1 de octubre de 2026, entre 7:00 a. m. y 10:00 a. m.
- Video: maximo 10 minutos.

## Cambios principales frente al Challenge 1

El Challenge 2 conserva el objetivo de monitorear disponibilidad y escasez de agua, pero agrega exigencias nuevas:

1. Tablero de control local accesible por navegador.
2. Servidor web embebido dentro del microcontrolador.
3. Acceso restringido a dispositivos autorizados y conectados a la WLAN local.
4. No usar MQTT.
5. Medicion desde una ISR o desde un hilo/tarea diferente al hilo principal.
6. Prototipo terminado listo para instalar, no un montaje suelto en protoboard.
7. Banco de pruebas mas riguroso: calibracion, emulacion acelerada, validacion de alertas, pruebas de notificacion, robustez y repetibilidad.
8. Seccion explicita de mejoras respecto al Challenge 1.

## Estado actual del proyecto

| Requisito | Estado | Evidencia / accion |
|---|---|---|
| Microcontrolador sin Raspberry Pi | Cumplido | Se usa ESP32. |
| Medicion de nivel de agua | Cumplido en simulacion | HC-SR04. |
| Variables meteorologicas | Cumplido en simulacion | DHT22, BMP280 y LDR. |
| Logica de fusion | Cumplido en simulacion | Nivel + riesgo de evaporacion. |
| Alerta fisica | Cumplido en simulacion | LEDs y buzzer. |
| Visualizacion en dispositivo IoT | Cumplido en simulacion | LCD 16x2. |
| Tablero de control local | Cumplido en firmware simulado | Servidor HTTP en ESP32. |
| Historico reciente | Cumplido en firmware simulado | Arreglo circular de 30 muestras. |
| Desactivar alarma desde tablero | Cumplido en firmware simulado | Botones web para silenciar/reactivar. |
| Acceso restringido | Parcialmente cumplido | Autenticacion basica HTTP. Falta validacion en red fisica. |
| WLAN local | Parcialmente cumplido | En Wokwi se usa Wokwi-GUEST. En fisico debe configurarse red local. |
| No MQTT | Cumplido | Se usa HTTP local, no MQTT. |
| Medicion fuera del hilo principal | Cumplido | Tarea FreeRTOS `sensor-task`; `loop()` atiende servidor. |
| Prototipo listo para instalar | Pendiente fisico | Falta carcasa, alimentacion y montaje integrado. |
| Banco de pruebas | Pendiente documentacion/ejecucion | Falta tabla de pruebas, calibracion y resultados. |
| Video demostrativo | Pendiente | Debe mostrar extremo a extremo. |
| Wiki completa | Pendiente | Falta integrar secciones y evidencias finales. |

## Que toca modificar o completar

### 1. Simulacion

Ya se ajusto para Challenge 2:

- Servidor web embebido en ESP32.
- Tablero local con valores actuales.
- Historico reciente.
- Autenticacion basica.
- Desactivacion/reactivacion de alarma.
- Tarea separada para lectura de sensores.

Pendiente de verificar en Wokwi:

- Abrir monitor serial.
- Copiar la IP local del ESP32.
- Entrar al tablero desde el navegador.
- Probar usuario `autoridad` y clave `tomine`.
- Activar alerta y silenciar buzzer desde el tablero.

### 2. Prototipo fisico

Se debe construir como producto integrado:

- Caja protectora.
- ESP32 fijado dentro de la carcasa.
- LCD, LEDs y buzzer visibles en el panel frontal.
- HC-SR04 instalado apuntando hacia la superficie del agua.
- DHT22 protegido en caja ventilada.
- LDR mirando hacia la luz.
- BMP280 protegido dentro de la caja o en zona ventilada.
- Alimentacion por fuente USB, power bank o bateria.

No debe presentarse como montaje suelto en protoboard.

### 3. Banco de pruebas

Debe incluir:

- Calibracion del HC-SR04 contra una regla.
- Comparacion de DHT22 contra una referencia o lectura conocida.
- Prueba de radiacion con cambios de luz sobre el LDR.
- Pruebas de estados NORMAL, ALERTA y CRITICO.
- Medicion de latencia entre evento critico y alerta fisica.
- Medicion de latencia entre evento critico y actualizacion del tablero.
- Prueba de silenciar alarma desde el tablero.
- Prueba de perdida/reconexion WiFi.
- Prueba de acceso con y sin credenciales.

### 4. Wiki

Agregar o completar:

- Resumen y motivacion.
- Restricciones de diseno.
- Arquitectura hardware/software.
- Diagrama de bloques.
- Diagrama UML o diagrama de tareas del firmware.
- Esquematico o tabla de conexiones.
- Modelo de negocio.
- Banco de pruebas.
- Resultados y analisis.
- Autoevaluacion del protocolo de pruebas.
- Mejoras respecto al Challenge 1.
- Uso de IA.
- Roles y contribuciones.
- Anexos con codigo y simulacion.

## Recomendacion de enfoque

Para cumplir el reto de forma clara, el equipo deberia presentar el proyecto como una estacion local de monitoreo hidrico:

```text
Sensores -> ESP32 -> logica de fusion -> alertas in situ + tablero local WLAN
```

La demostracion ideal debe mostrar:

1. Cambio de nivel de agua.
2. Cambio de temperatura/humedad/radiacion.
3. Activacion de alerta fisica.
4. Actualizacion del tablero local.
5. Desactivacion de alarma desde el tablero.
6. Historico reciente de datos.
