# Simulacion IoT - Detector de Nivel de Agua

Esta carpeta contiene la simulacion en Wokwi y PlatformIO del prototipo IoT para monitoreo de nivel de agua y condiciones ambientales.

## Componentes

- ESP32 DevKit C V4
- Sensor ultrasonico HC-SR04
- Sensor DHT22
- Pantalla LCD 16x2 con interfaz I2C
- LED verde
- LED amarillo
- LED rojo
- Buzzer
- Cables de conexion

## Pines

| Componente | Pin del componente | Pin ESP32 |
|---|---:|---:|
| DHT22 | DATA | GPIO 15 |
| HC-SR04 | TRIG | GPIO 5 |
| HC-SR04 | ECHO | GPIO 18 |
| LCD I2C | SDA | GPIO 21 |
| LCD I2C | SCL | GPIO 22 |
| LED verde | Anodo | GPIO 25 |
| LED amarillo | Anodo | GPIO 26 |
| LED rojo | Anodo | GPIO 27 |
| Buzzer | Positivo | GPIO 14 |

## Logica de nivel

El HC-SR04 se ubica en la parte superior del tanque o reservorio y mide la distancia hasta la superficie del agua.

- Distancia pequena: el agua esta cerca del sensor y el nivel es alto.
- Distancia grande: el agua esta lejos del sensor y el nivel es bajo.

Para la simulacion se usa una profundidad de tanque de 100 cm:

```text
Nivel de agua (%) = ((100 cm - distancia medida) / 100 cm) * 100
```

## Condiciones de alerta

Estado NORMAL:

- Nivel de agua mayor o igual al 55%.
- LED verde encendido.
- Buzzer apagado.

Estado ALERTA:

- Nivel de agua entre 30% y 55%.
- O nivel de agua menor al 30% sin que se cumplan todas las condiciones criticas.
- O temperatura alta y humedad baja al mismo tiempo.
- LED amarillo encendido.
- Buzzer intermitente.

Estado CRITICO:

- Nivel de agua menor al 30%.
- Temperatura mayor o igual a 30 grados Celsius.
- Humedad menor o igual al 40%.
- LED rojo encendido.
- Buzzer continuo.

## Uso

1. Abrir esta carpeta en VS Code.
2. Compilar con PlatformIO.
3. Ejecutar `Wokwi: Start Simulator`.
4. Cambiar la distancia del HC-SR04 para simular el nivel del agua.
5. Cambiar temperatura y humedad en el DHT22 para probar los estados de alerta.

## Nota para montaje fisico

El HC-SR04 trabaja normalmente a 5V, mientras que el ESP32 usa entradas de 3.3V. En el montaje fisico, el pin ECHO debe conectarse al ESP32 mediante divisor de voltaje o adaptador de nivel logico.
