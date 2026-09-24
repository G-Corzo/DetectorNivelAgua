#include <Arduino.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#define DHT_PIN 15
#define DHT_TYPE DHT22

#define ULTRASONIC_TRIG_PIN 5
#define ULTRASONIC_ECHO_PIN 18
#define LDR_PIN 35

#define LED_GREEN 25
#define LED_YELLOW 26
#define LED_RED 27
#define BUZZER_PIN 14
#define MUTE_BUTTON_PIN 33

#define I2C_SDA 21
#define I2C_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* DASHBOARD_USER = "autoridad";
const char* DASHBOARD_PASSWORD = "tomine";

const float TANK_DEPTH_CM = 100.0;
const uint16_t HISTORY_SIZE = 30;
const uint32_t SAMPLE_PERIOD_MS = 2000;
const uint32_t BUTTON_DEBOUNCE_MS = 300;

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_BMP280 bmp;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);

enum AlertState {
  NORMAL,
  ALERTA,
  CRITICO
};

struct SensorReadings {
  float waterDistance;
  float waterLevel;
  float temperature;
  float humidity;
  float pressure;
  float solarRadiation;
  float evaporationRisk;
  AlertState state;
  bool alarmMuted;
  uint32_t timestamp;
};

SensorReadings currentData = {};
SensorReadings history[HISTORY_SIZE];
uint16_t historyIndex = 0;
uint16_t historyCount = 0;

SemaphoreHandle_t dataMutex;
TaskHandle_t sensorTaskHandle;
bool bmpAvailable = false;
bool alarmMuted = false;
bool previousButtonState = HIGH;
uint32_t lastButtonToggle = 0;

SensorReadings getCurrentData();
void updateDisplay(const SensorReadings& data);

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

float readSolarRadiationPercent() {
  int rawValue = analogRead(LDR_PIN);
  float percent = (rawValue / 4095.0) * 100.0;
  return constrain(percent, 0, 100);
}

float calculateEvaporationRisk(float temperature, float humidity, float solarRadiation, float pressure) {
  float tempScore = constrain((temperature - 20.0) * 5.0, 0, 100);
  float dryAirScore = constrain(100.0 - humidity, 0, 100);
  float radiationScore = constrain(solarRadiation, 0, 100);
  float lowPressureScore = constrain((1013.25 - pressure) * 5.0, 0, 100);

  return (tempScore * 0.35) +
         (dryAirScore * 0.30) +
         (radiationScore * 0.25) +
         (lowPressureScore * 0.10);
}

AlertState evaluateState(const SensorReadings& data) {
  bool lowWater = data.waterLevel < 30;
  bool mediumWater = data.waterLevel >= 30 && data.waterLevel < 55;
  bool highEvaporationRisk = data.evaporationRisk >= 70;
  bool mediumEvaporationRisk = data.evaporationRisk >= 55;

  if (lowWater && highEvaporationRisk) {
    return CRITICO;
  }

  if (lowWater || mediumWater || mediumEvaporationRisk) {
    return ALERTA;
  }

  return NORMAL;
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

void applyAlertState(AlertState state, bool muted) {
  digitalWrite(LED_GREEN, state == NORMAL);
  digitalWrite(LED_YELLOW, state == ALERTA);
  digitalWrite(LED_RED, state == CRITICO);

  if (muted || state == NORMAL) {
    noTone(BUZZER_PIN);
  } else if (state == CRITICO) {
    tone(BUZZER_PIN, 1200);
  } else {
    tone(BUZZER_PIN, 700, 200);
  }
}

void setAlarmMuted(bool muted) {
  alarmMuted = muted;

  if (dataMutex != NULL) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    currentData.alarmMuted = alarmMuted;
    xSemaphoreGive(dataMutex);

    SensorReadings snapshot = getCurrentData();
    applyAlertState(snapshot.state, snapshot.alarmMuted);
    updateDisplay(snapshot);
  }
}

void handleMuteButton() {
  bool buttonState = digitalRead(MUTE_BUTTON_PIN);
  bool buttonPressed = previousButtonState == HIGH && buttonState == LOW;

  if (buttonPressed && millis() - lastButtonToggle > BUTTON_DEBOUNCE_MS) {
    setAlarmMuted(!alarmMuted);
    lastButtonToggle = millis();
  }

  previousButtonState = buttonState;
}

void showMessage(const char* firstLine, const char* secondLine = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(firstLine);
  display.println(secondLine);
  display.display();
}

void updateDisplay(const SensorReadings& data) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Estado: ");
  display.println(stateToLcdText(data.state));
  display.print("Nivel: ");
  display.print(data.waterLevel, 0);
  display.print("% D:");
  display.print(data.waterDistance, 0);
  display.println("cm");
  display.print("T:");
  display.print(data.temperature, 1);
  display.print(" H:");
  display.print(data.humidity, 0);
  display.println("%");
  display.print("Sol:");
  display.print(data.solarRadiation, 0);
  display.print("% P:");
  display.print(data.pressure, 0);
  display.println("hPa");
  display.print("Evap: ");
  display.print(data.evaporationRisk, 0);
  display.print("% Alarm:");
  display.println(data.alarmMuted ? "OFF" : "ON");
  display.print(WiFi.localIP());
  display.display();
}

bool readSensors(SensorReadings& data) {
  data.temperature = dht.readTemperature();
  data.humidity = dht.readHumidity();
  data.waterDistance = readWaterDistanceCm();
  data.waterLevel = calculateWaterLevelPercent(data.waterDistance);
  data.solarRadiation = readSolarRadiationPercent();
  data.pressure = bmpAvailable ? bmp.readPressure() / 100.0 : NAN;
  data.evaporationRisk = calculateEvaporationRisk(
    data.temperature,
    data.humidity,
    data.solarRadiation,
    data.pressure
  );
  data.state = evaluateState(data);
  data.alarmMuted = alarmMuted;
  data.timestamp = millis();

  return !isnan(data.temperature) &&
         !isnan(data.humidity) &&
         !isnan(data.waterDistance) &&
         !isnan(data.pressure);
}

void storeReading(const SensorReadings& data) {
  currentData = data;
  history[historyIndex] = data;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) {
    historyCount++;
  }
}

SensorReadings getCurrentData() {
  SensorReadings snapshot;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  snapshot = currentData;
  xSemaphoreGive(dataMutex);
  return snapshot;
}

bool requireAuth() {
  if (server.authenticate(DASHBOARD_USER, DASHBOARD_PASSWORD)) {
    return true;
  }

  server.requestAuthentication();
  return false;
}

String dashboardHtml(const SensorReadings& data) {
  String html;
  html.reserve(7000);
  html += F("<!doctype html><html lang='es'><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<meta http-equiv='refresh' content='5'>");
  html += F("<title>Tablero hidrico local</title><style>");
  html += F("body{font-family:Arial,sans-serif;margin:0;background:#eef4f8;color:#10202b}");
  html += F("header{background:#0c5c75;color:white;padding:18px 22px}");
  html += F("main{padding:18px;max-width:980px;margin:auto}");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px}");
  html += F(".card{background:white;border:1px solid #d4e0e6;border-radius:8px;padding:14px}");
  html += F(".value{font-size:28px;font-weight:700;margin-top:6px}");
  html += F(".state{font-size:34px;font-weight:800}.NORMAL{color:#16833a}.ALERTA{color:#a87400}.CRITICO{color:#b3261e}");
  html += F("button{border:0;border-radius:6px;background:#0c5c75;color:white;padding:12px 16px;font-weight:700}");
  html += F("table{width:100%;border-collapse:collapse;background:white;margin-top:12px}td,th{padding:8px;border-bottom:1px solid #dbe6eb;text-align:left}");
  html += F("</style></head><body><header><h1>Tablero de control local</h1><p>Prototipo IoT de monitoreo de disponibilidad hidrica</p></header><main>");
  html += "<section class='card'><div>Estado actual</div><div class='state ";
  html += stateToText(data.state);
  html += "'>";
  html += stateToText(data.state);
  html += F("</div></section><section class='grid'>");
  html += "<div class='card'>Nivel<div class='value'>" + String(data.waterLevel, 1) + F("%</div></div>");
  html += "<div class='card'>Distancia<div class='value'>" + String(data.waterDistance, 1) + F(" cm</div></div>");
  html += "<div class='card'>Temperatura<div class='value'>" + String(data.temperature, 1) + F(" C</div></div>");
  html += "<div class='card'>Humedad<div class='value'>" + String(data.humidity, 1) + F("%</div></div>");
  html += "<div class='card'>Presion<div class='value'>" + String(data.pressure, 1) + F(" hPa</div></div>");
  html += "<div class='card'>Radiacion<div class='value'>" + String(data.solarRadiation, 1) + F("%</div></div>");
  html += "<div class='card'>Riesgo evaporacion<div class='value'>" + String(data.evaporationRisk, 1) + F("%</div></div>");
  html += "<div class='card'>Alarma<div class='value'>";
  html += data.alarmMuted ? "Silenciada" : "Activa";
  html += F("</div></div></section><section class='card' style='margin-top:12px'>");
  html += F("<form action='/alarm/off' method='post'><button>Desactivar alarma fisica</button></form>");
  html += F("<form action='/alarm/on' method='post' style='margin-top:8px'><button>Reactivar alarma fisica</button></form>");
  html += F("</section><section><h2>Historico reciente</h2><table><tr><th>t (s)</th><th>Nivel</th><th>Temp</th><th>Hum</th><th>Pres</th><th>Rad</th><th>Riesgo</th><th>Estado</th></tr>");

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (uint16_t i = 0; i < historyCount; i++) {
    uint16_t pos = (historyIndex + HISTORY_SIZE - historyCount + i) % HISTORY_SIZE;
    SensorReadings row = history[pos];
    html += "<tr><td>" + String(row.timestamp / 1000) + "</td><td>" + String(row.waterLevel, 1);
    html += "%</td><td>" + String(row.temperature, 1) + "</td><td>" + String(row.humidity, 1);
    html += "</td><td>" + String(row.pressure, 1) + "</td><td>" + String(row.solarRadiation, 1) + "</td><td>" + String(row.evaporationRisk, 1);
    html += "</td><td>" + String(stateToText(row.state)) + "</td></tr>";
  }
  xSemaphoreGive(dataMutex);

  html += F("</table></section></main></body></html>");
  return html;
}

void handleDashboard() {
  if (!requireAuth()) {
    return;
  }

  SensorReadings data = getCurrentData();
  server.send(200, "text/html", dashboardHtml(data));
}

void handleStatusApi() {
  if (!requireAuth()) {
    return;
  }

  SensorReadings data = getCurrentData();
  String json;
  json.reserve(512);
  json += "{";
  json += "\"waterLevel\":" + String(data.waterLevel, 2) + ",";
  json += "\"waterDistance\":" + String(data.waterDistance, 2) + ",";
  json += "\"temperature\":" + String(data.temperature, 2) + ",";
  json += "\"humidity\":" + String(data.humidity, 2) + ",";
  json += "\"pressure\":" + String(data.pressure, 2) + ",";
  json += "\"solarRadiation\":" + String(data.solarRadiation, 2) + ",";
  json += "\"evaporationRisk\":" + String(data.evaporationRisk, 2) + ",";
  json += "\"state\":\"" + String(stateToText(data.state)) + "\",";
  json += "\"alarmMuted\":" + String(data.alarmMuted ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleAlarmOff() {
  if (!requireAuth()) {
    return;
  }

  setAlarmMuted(true);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleAlarmOn() {
  if (!requireAuth()) {
    return;
  }

  setAlarmMuted(false);
  server.sendHeader("Location", "/");
  server.send(303);
}

void setupDashboardServer() {
  server.on("/", HTTP_GET, handleDashboard);
  server.on("/api/status", HTTP_GET, handleStatusApi);
  server.on("/alarm/off", HTTP_POST, handleAlarmOff);
  server.on("/alarm/on", HTTP_POST, handleAlarmOn);
  server.begin();
}

void printSerialReport(const SensorReadings& data) {
  Serial.print("Nivel: ");
  Serial.print(data.waterLevel);
  Serial.print("% | Distancia: ");
  Serial.print(data.waterDistance);
  Serial.print(" cm | Temp: ");
  Serial.print(data.temperature);
  Serial.print(" C | Humedad: ");
  Serial.print(data.humidity);
  Serial.print("% | Presion: ");
  Serial.print(data.pressure);
  Serial.print(" hPa | Radiacion: ");
  Serial.print(data.solarRadiation);
  Serial.print("% | Riesgo evaporacion: ");
  Serial.print(data.evaporationRisk);
  Serial.print("% | Estado: ");
  Serial.print(stateToText(data.state));
  Serial.print(" | Alarma: ");
  Serial.println(data.alarmMuted ? "SILENCIADA" : "ACTIVA");
}

void sensorTask(void* parameter) {
  SensorReadings data;

  while (true) {
    if (readSensors(data)) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      storeReading(data);
      xSemaphoreGive(dataMutex);

      applyAlertState(data.state, data.alarmMuted);
      updateDisplay(data);
      printSerialReport(data);
    } else {
      Serial.println("Error leyendo sensores");
      showMessage("Error sensores", "Revise conexion");
    }

    vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Error iniciando pantalla OLED");
  }
  showMessage("Monitor hidrico", "Iniciando...");

  dht.begin();
  bmpAvailable = bmp.begin(0x76) || bmp.begin(0x77);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(MUTE_BUTTON_PIN, INPUT_PULLUP);
  previousButtonState = digitalRead(MUTE_BUTTON_PIN);

  if (!bmpAvailable) {
    showMessage("Error BMP280", "Revise I2C");
    Serial.println("Error iniciando BMP280");
    delay(2000);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  showMessage("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Tablero local: http://");
  Serial.println(WiFi.localIP());

  dataMutex = xSemaphoreCreateMutex();
  setupDashboardServer();

  xTaskCreatePinnedToCore(
    sensorTask,
    "sensor-task",
    8192,
    NULL,
    1,
    &sensorTaskHandle,
    1
  );

  Serial.println("Sistema IoT de monitoreo hidrico iniciado");
}

void loop() {
  handleMuteButton();
  server.handleClient();
  delay(5);
}
