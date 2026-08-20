#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define DHT_PIN 15
#define DHT_TYPE DHT22

#define ULTRASONIC_TRIG_PIN 5
#define ULTRASONIC_ECHO_PIN 18

#define LED_GREEN 25
#define LED_YELLOW 26
#define LED_RED 27
#define BUZZER_PIN 14

#define I2C_SDA 21
#define I2C_SCL 22

const float TANK_DEPTH_CM = 100.0;

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

enum AlertState {
  NORMAL,
  ALERTA,
  CRITICO
};

AlertState currentState = NORMAL;

float readWaterDistanceCm() {
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ULTRASONIC_ECHO_PIN, HIGH, 30000);
  if (duration == 0) {
    return NAN;
  }

  return duration * 0.0343 / 2.0;
}

float calculateWaterLevelPercent(float distanceCm) {
  float constrainedDistance = constrain(distanceCm, 0, TANK_DEPTH_CM);
  float level = ((TANK_DEPTH_CM - constrainedDistance) / TANK_DEPTH_CM) * 100.0;
  return constrain(level, 0, 100);
}

AlertState evaluateState(float waterLevel, float temperature, float humidity) {
  bool lowWater = waterLevel < 30;
  bool mediumWater = waterLevel >= 30 && waterLevel < 55;
  bool highTemperature = temperature >= 30;
  bool lowHumidity = humidity <= 40;

  if (lowWater && highTemperature && lowHumidity) {
    return CRITICO;
  }

  if (lowWater || mediumWater || (highTemperature && lowHumidity)) {
    return ALERTA;
  }

  return NORMAL;
}

void applyAlertState(AlertState state) {
  digitalWrite(LED_GREEN, state == NORMAL);
  digitalWrite(LED_YELLOW, state == ALERTA);
  digitalWrite(LED_RED, state == CRITICO);

  if (state == CRITICO) {
    tone(BUZZER_PIN, 1200);
  } else if (state == ALERTA) {
    tone(BUZZER_PIN, 700, 200);
  } else {
    noTone(BUZZER_PIN);
  }
}

const char* stateToText(AlertState state) {
  switch (state) {
    case NORMAL:
      return "NORMAL";
    case ALERTA:
      return "ALERTA";
    case CRITICO:
      return "CRITICO";
    default:
      return "DESCONOCIDO";
  }
}

const char* stateToLcdText(AlertState state) {
  switch (state) {
    case NORMAL:
      return "OK";
    case ALERTA:
      return "ALER";
    case CRITICO:
      return "CRIT";
    default:
      return "ERR";
  }
}

void updateDisplay(float waterLevel, float waterDistance, float temperature, float humidity, AlertState state) {
  char firstLine[17];
  char secondLine[17];

  snprintf(firstLine, sizeof(firstLine), "N:%3.0f%% D:%3.0fcm", waterLevel, waterDistance);
  snprintf(secondLine, sizeof(secondLine), "T:%2.0f H:%2.0f %s", temperature, humidity, stateToLcdText(state));

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(firstLine);
  lcd.setCursor(0, 1);
  lcd.print(secondLine);
}

void setup() {
  Serial.begin(115200);

  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Monitor hidrico");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");

  dht.begin();

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);

  Serial.println("Sistema IoT de monitoreo hidrico iniciado");
}

void loop() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  float waterDistance = readWaterDistanceCm();
  float waterLevel = calculateWaterLevelPercent(waterDistance);

  if (isnan(temperature) || isnan(humidity) || isnan(waterDistance)) {
    Serial.println("Error leyendo sensores");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Error sensores");
    lcd.setCursor(0, 1);
    lcd.print("Revise conexion");
    delay(2000);
    return;
  }

  currentState = evaluateState(waterLevel, temperature, humidity);
  applyAlertState(currentState);
  updateDisplay(waterLevel, waterDistance, temperature, humidity, currentState);

  Serial.print("Nivel agua: ");
  Serial.print(waterLevel);
  Serial.print("% | Distancia: ");
  Serial.print(waterDistance);
  Serial.print(" cm | Temp: ");
  Serial.print(temperature);
  Serial.print(" C | Humedad: ");
  Serial.print(humidity);
  Serial.print("% | Estado: ");
  Serial.println(stateToText(currentState));

  delay(1000);
}
