# Evaporacion potencial en el prototipo IoT

## Que vamos a calcular

En el prototipo vamos a calcular un **indice de evaporacion potencial estimada**, expresado de 0 a 100.

Este valor no representa evaporacion exacta en `mm/dia`. Es una estimacion normalizada que indica que tan favorables son las condiciones ambientales para que ocurra evaporacion.

```text
0   = condiciones poco favorables para evaporacion
100 = condiciones muy favorables para evaporacion
```

En la pantalla del ESP32 y en el tablero local se puede mostrar como:

```text
EvapPot: 72%
```

o, de forma mas clara:

```text
RiesgoEvap: 72%
```

## Por que usamos un indice y no evaporacion exacta

La evaporacion potencial fisica normalmente se expresa en `mm/dia` y se calcula con modelos hidrometeorologicos completos, como Penman-Monteith o Hargreaves.

Estos modelos requieren variables adicionales que el prototipo no mide completamente, por ejemplo:

- radiacion neta,
- velocidad del viento,
- temperatura maxima y minima diaria,
- ubicacion geografica,
- dia del ano,
- parametros psicrometricos,
- datos calibrados del cuerpo de agua.

Como el objetivo del Challenge es construir un prototipo IoT funcional de bajo costo, se usa un indice simplificado. Este indice permite fusionar las variables ambientales criticas pedidas en el enunciado y generar alertas tempranas de forma clara.

Por eso, el sistema no afirma:

```text
Se evaporan X mm/dia.
```

Sino:

```text
Las condiciones actuales favorecen evaporacion baja, media o alta.
```

## Variables usadas

El enunciado pide analizar evaporacion potencial considerando variables meteorologicas como:

- radiacion solar,
- temperatura,
- humedad relativa,
- presion atmosferica.

Nuestro prototipo usa esas variables para construir el indice.

## Sensores usados

| Variable | Sensor | Funcion |
|---|---|---|
| Temperatura | DHT22 | Mide la temperatura ambiente. |
| Humedad relativa | DHT22 | Mide la humedad del aire. |
| Radiacion solar aproximada | LDR GL55 | Estima la intensidad de luz incidente. |
| Presion atmosferica | BMP280 | Mide la presion atmosferica. |
| Nivel de agua | HC-SR04 | Mide la distancia entre el sensor y la superficie del agua. |

Con estos sensores se cubren las variables principales:

```text
DHT22 -> temperatura y humedad
LDR -> radiacion solar aproximada
BMP280 -> presion atmosferica
HC-SR04 -> nivel de agua
```

## Formula propuesta

El indice se calcula asi:

```text
R = 0.35 Tn + 0.30 AS + 0.25 RS + 0.10 PB
```

Donde:

- `R`: indice de evaporacion potencial estimada.
- `Tn`: temperatura normalizada.
- `AS`: aire seco, calculado como `100 - humedad relativa`.
- `RS`: radiacion solar aproximada normalizada.
- `PB`: baja presion atmosferica normalizada.

## Interpretacion de cada termino

### Temperatura

Cuando la temperatura aumenta, hay mas energia termica disponible para favorecer la evaporacion.

Por eso tiene el peso mas alto:

```text
35%
```

### Humedad relativa

La humedad relativa se usa de forma inversa.

Si la humedad es baja, el aire esta mas seco y puede recibir mas vapor de agua. Por eso calculamos:

```text
AS = 100 - humedad relativa
```

Ejemplo:

```text
Humedad = 30%
AS = 70%
```

Eso significa que el aire esta seco y la evaporacion puede aumentar.

Peso usado:

```text
30%
```

### Radiacion solar

La radiacion solar aporta energia directa sobre la lamina de agua. En el prototipo se estima con una LDR.

Si la luz aumenta, el indice de evaporacion tambien aumenta.

Peso usado:

```text
25%
```

### Presion atmosferica

La presion atmosferica se usa como variable meteorologica de apoyo.

En el prototipo, una presion menor frente a una referencia de `1013.25 hPa` aumenta ligeramente el indice. Su peso es menor porque la evaporacion depende mas directamente de temperatura, humedad y radiacion.

Peso usado:

```text
10%
```

## Relacion con el nivel de agua

El indice de evaporacion por si solo no define el estado critico. Se combina con el nivel de agua.

La logica general es:

```text
Nivel bajo + evaporacion alta = riesgo critico
```

Estados:

```text
NORMAL:
Nivel de agua suficiente y evaporacion potencial estimada baja.

ALERTA:
Nivel medio/bajo o evaporacion potencial estimada media/alta.

CRITICO:
Nivel menor al 30% y evaporacion potencial estimada mayor o igual al 70%.
```

## Que se muestra en el ESP32

La pantalla integrada del ESP32 puede mostrar:

```text
Nivel: 68%
Dist: 32 cm
T: 27.5 C
H: 54%
Sol: 76%
Pres: 1010 hPa
RiesgoEvap: 42%
Estado: NORMAL
```

Tambien se muestra en el tablero local alojado en el ESP32.

## Justificacion para la sustentacion

Una forma clara de explicarlo es:

> En este prototipo no calculamos evaporacion potencial fisica en milimetros por dia, porque eso requiere un modelo hidrometeorologico mas completo y variables que no medimos completamente, como viento, radiacion neta y temperaturas maxima/minima diarias. En su lugar, calculamos un indice de evaporacion potencial estimada de 0 a 100, usando las variables que pide el enunciado: temperatura, humedad, radiacion solar y presion atmosferica. Este indice se fusiona con el nivel de agua para decidir si el sistema esta en estado normal, alerta o critico.

## Limitaciones

- La LDR no mide radiacion solar real en `W/m2`; solo aproxima intensidad luminica.
- El indice requiere calibracion si se quiere usar en campo real.
- No reemplaza modelos profesionales como Penman-Monteith.
- Sirve como herramienta de decision para el prototipo IoT y para activar alertas tempranas.

## Trabajo futuro

Para mejorar el calculo se podria:

- usar un sensor de radiacion solar calibrado,
- agregar anemometro para velocidad del viento,
- almacenar temperaturas maxima y minima diarias,
- calibrar el indice con datos historicos del embalse,
- estimar evaporacion en `mm/dia` con un modelo fisico.
