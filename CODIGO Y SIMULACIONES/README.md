# Simulacion IoT - Detector de Nivel de Agua

Esta carpeta contiene la simulacion en Wokwi y PlatformIO del prototipo IoT para monitoreo de nivel de agua y condiciones ambientales.

La version ajustada para el Challenge 2 incluye tablero de control local alojado en el ESP32, historico reciente, autenticacion basica, alarma fisica y lectura de sensores en una tarea FreeRTOS separada del hilo principal.

## Componentes

- ESP32 DevKit C V4
- Sensor ultrasonico HC-SR04
- Sensor DHT22
- Sensor BMP280
- Sensor LDR
- Pantalla integrada del ESP32, simulada como OLED SSD1306 I2C
- LED verde
- LED amarillo
- LED rojo
- Buzzer
- Pulsador para silenciar/reactivar alarma
- Cables de conexion

## Red local y tablero

En la simulacion se usa la red de Wokwi:

```text
SSID: Wokwi-GUEST
Password: vacio
```

En el montaje real, estos valores deben reemplazarse por la WLAN entregada por la autoridad local o por una red local configurada para el prototipo.

El tablero se aloja directamente en el ESP32 mediante un servidor web embebido. Cuando la simulacion inicia, el monitor serial muestra una URL similar a:

```text
Tablero local: http://192.168.x.x
```

Credenciales del tablero:

```text
Usuario: autoridad
Clave: tomine
```

El tablero permite:

- Ver valores actuales de nivel, distancia, temperatura, humedad, presion, radiacion y riesgo de evaporacion.
- Ver historico reciente de mediciones.
- Consultar estado NORMAL, ALERTA o CRITICO.
- Desactivar y reactivar la alarma fisica.

El pulsador fisico conectado en GPIO 33 hace lo mismo que el tablero para la alarma:

- Si la alarma esta activa, la silencia.
- Si la alarma esta silenciada, la reactiva.
- No borra el estado de riesgo; solo apaga o habilita el buzzer fisico.

Tambien existe una salida JSON para pruebas:

```text
/api/status
```

## Pines

| Componente | Pin del componente | Pin ESP32 |
|---|---:|---:|
| DHT22 | DATA | GPIO 15 |
| HC-SR04 | TRIG | GPIO 5 |
| HC-SR04 | ECHO | GPIO 18 |
| BMP280 | SDA | GPIO 21 |
| BMP280 | SCL | GPIO 22 |
| LDR | AO | GPIO 35 |
| OLED integrada | SDA | GPIO 21 |
| OLED integrada | SCL | GPIO 22 |
| LED verde | Anodo | GPIO 25 |
| LED amarillo | Anodo | GPIO 26 |
| LED rojo | Anodo | GPIO 27 |
| Buzzer | Positivo | GPIO 14 |
| Pulsador alarma | Senal | GPIO 33 |

## Logica de nivel

El HC-SR04 se ubica en la parte superior del tanque o reservorio y mide la distancia hasta la superficie del agua.

- Distancia pequena: el agua esta cerca del sensor y el nivel es alto.
- Distancia grande: el agua esta lejos del sensor y el nivel es bajo.

Para la simulacion se usa una profundidad de tanque de 100 cm:

```text
Nivel de agua (%) = ((100 cm - distancia medida) / 100 cm) * 100
```

## Condiciones de alerta

La simulacion aplica una logica de fusion de senales basada en el estudio del embalse. El sistema no solo evalua si el nivel esta bajo, sino tambien si las condiciones ambientales favorecen evaporacion.

El riesgo de evaporacion se calcula con:

- Temperatura.
- Humedad relativa.
- Radiacion solar aproximada con LDR.
- Presion atmosferica medida por BMP280.

Estado NORMAL:

- Nivel de agua mayor o igual al 55%.
- Riesgo de evaporacion menor al 55%.
- LED verde encendido.
- Buzzer apagado.

Estado ALERTA:

- Nivel de agua entre 30% y 55%.
- O nivel de agua menor al 30%.
- O riesgo de evaporacion mayor o igual al 55%.
- LED amarillo encendido.
- Buzzer intermitente.

Estado CRITICO:

- Nivel de agua menor al 30%.
- Riesgo de evaporacion mayor o igual al 70%.
- LED rojo encendido.
- Buzzer continuo.

Formula usada para el riesgo de evaporacion:

```text
Riesgo evaporacion = temperatura normalizada * 0.35
                   + aire seco * 0.30
                   + radiacion solar * 0.25
                   + baja presion atmosferica * 0.10
```

## Arquitectura de software

Para cumplir el requisito de que la medicion no se ejecute en el hilo principal, el firmware usa dos flujos:

```text
Tarea sensor-task:
Lee sensores cada 2 segundos.
Calcula nivel, riesgo de evaporacion y estado.
Actualiza LEDs, buzzer, pantalla OLED e historico.

Loop principal:
Atiende el servidor web embebido.
Responde al tablero local.
Permite desactivar o reactivar la alarma desde la web y desde el pulsador fisico.
```

No se usa MQTT. La comunicacion con las autoridades se hace por HTTP local dentro de la WLAN.

## Uso

1. Abrir esta carpeta en VS Code.
2. Compilar con PlatformIO.
3. Ejecutar `Wokwi: Start Simulator`.
4. Cambiar la distancia del HC-SR04 para simular el nivel del agua.
5. Cambiar temperatura y humedad en el DHT22 para probar condiciones secas o calidas.
6. Cambiar la luz del LDR para simular mayor o menor radiacion solar.
7. Observar la pantalla integrada/OLED: muestra nivel, distancia, temperatura, humedad, radiacion, presion, riesgo e IP local.
8. Abrir el monitor serial, copiar la URL del tablero local y acceder con las credenciales.
9. Presionar el boton MUTE para silenciar o reactivar el buzzer sin apagar la medicion.

## Nota para montaje fisico

El HC-SR04 trabaja normalmente a 5V, mientras que el ESP32 usa entradas de 3.3V. En el montaje fisico, el pin ECHO debe conectarse al ESP32 mediante divisor de voltaje o adaptador de nivel logico.
