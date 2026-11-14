Para la calibracion de los distintos sensores que incorpora nuestra solucion hallamos el error promedio comparando los datos reales con los brindados por el sensor y después calculamos su promedio para contrarestarlo.
Empezando con el sensor de distancia HC-SR04 en donde a base de prueba y error obtivimos los siguientes resultados

| distancia real (cm) | OBS1 (cm)  | OBS2 (cm) | OBS3 (cm) | OBS4 (cm) | PROM HCSR (cm) | PROM ERROR |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 1 | 2.9 | 3.9 | 4.9 | 4.5 | 4.05 | -3.1 |
| 2 | 1.7 | 2.9 | 2 | 2.3 | 2.225 | -0.2 |
| 3 | 2 | 2.9 | 2 | 2.6 | 2.375 | 0.6 |
| 4 | 2.9 | 3.9 | 2.9 | 2.9 | 3.15 | 0.9 |
| 5 | 3.9 | 4.5 | 3.9 | 3.6 | 3.975 | 1.0 |
| 6 | 5.5 | 5.8 | 4.9 | 4.5 | 5.175 | 0.8 |
| 7 | 6.8 | 5.8 | 5.8 | 6.8 | 6.3 | 0.7 |
| 8 | 7.8 | 8.1 | 6.8 | 7.8 | 7.625 | 0.4 |
| 9 | 8.7 | 8.7 | 7.8 | 8.4 | 8.4 | 0.6 |
| 10 | 9.8 | 9.8 | 9.1 | 9.8 | 9.625 | 0.4 |
| 11 | 10.8 | 10.1 | 9.8 | 10.4 | 10.275 | 0.7 |
| 12 | 11.1 | 11.1 | 11.1 | 10.8 | 11.025 | 1.0 |
| 13 | 12.5 | 12.5 | 11.8 | 12.8 | 12.4 | 0.6 |
| 14 | 13.1 | 13.1 | 14.1 | 13.5 | 13.45 | 0.6 |
| 15 | 14.1 | 14.6 | 14.5 | 14.5 | 14.425 | 0.6 |
| 16 | 16.5 | 15.1 | 16.5 | 15.1 | 15.8 | 0.2 |
| 17 | 16.2 | 16.2 | 16.2 | 16.2 | 16.2 | 0.8 |
| 18 | 17.1 | 16.2 | 17.1 | 17.5 | 16.975 | 1.0 |
| 19 | 18.1 | 17.5 | 18.4 | 17.8 | 17.95 | 1.1 |
| 20 | 19.1 | 18.4 | 19.1 | 19.4 | 19 | 1.0 |
| 21 | 20.4 | 20.1 | 20.1 | 18.7 | 19.825 | 1.2 |
| 22 | 21 | 21 | 20.7 | 21.7 | 21.1 | 0.9 |
| 23 | 22.3 | 21.7 | 22.3 | 22 | 22.075 | 0.9 |
| 24 | 22.6 | 22.6 | 22.7 | 23.8 | 22.925 | 1.1 |
| 25 | 23.5 | 23.5 | 23.5 | 24.1 | 23.65 | 1.4 |
| 26 | 25 | 24.9 | 24.9 | 24.7 | 24.875 | 1.1 |
| 27 | 26.8 | 27.1 | 26.2 | 25.9 | 26.5 | 0.5 |
| 28 | 26.5 | 26.8 | 27.6 | 27.1 | 27 | 1.0 |
| 29 | 28 | 27.9 | 28 | 27.9 | 27.95 | 1.1 |
| 30 | 29.4 | 28.5 | 29.4 | 30.2 | 29.375 | 0.6 |

Gracias a estos datos pudimos identificar que el error del sensor es demasiado grande cuando se encuentra a menos de dos centimetros del objeto, por lo tanto, el
grupo procuró mantener el sensor a una distancia mayor. 
Continuando con la calibración, promediamos los errores sin contar las distancias menores o iguales a 2cm y encontramos un
error promedio de 0,8cm. Este valor se le suma de ahora en adelante a todas las lecturas provistas por este sensor para contrarrestar su error

Para los sensores de temperatura, presión y humedad. El dispositivo fue expuesta al clima ambiente de madrid cundinamarca y por medio del reporte meteorologico
calculamos el error (debido a que las fluctuaciones de estas tres variables en temperatura ambiente no varian drasticamente basto con una observación para calcular cada error)
como resultados obtuvimos un error de 47hPa para el sensor de presión, mientras que las de humedad y temperatura (provistas por el sensor DHT22) dieron un error despresiable.
