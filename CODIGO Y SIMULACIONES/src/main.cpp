#include <Arduino.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <stdio.h>
#include <string.h>
#include "secrets.h"

#define DHT_PIN 15
#define DHT_TYPE DHT22

#define ULTRASONIC_TRIG_PIN 5
#define ULTRASONIC_ECHO_PIN 18
#define LDR_PIN 35

#define LED_GREEN 27
#define LED_YELLOW 26
#define LED_RED 25
#define BUZZER_PIN 14

#define PAGE_BUTTON_PIN 33 

#define I2C_SDA 21
#define I2C_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

const uint16_t MQTT_PORT = 1883;
const char* DEVICE_ID = "tomine-01";
const char* MQTT_TELEMETRY_TOPIC = "sabana/borda/hidrico/sensores";
const char* MQTT_STATUS_TOPIC = "sabana/borda/hidrico/status";
const char* MQTT_ALARM_COMMAND_TOPIC = "sabana/borda/hidrico/command/alarm";

// =========================================================================
// CALIBRACIÓN DEL EMBALSE Y SENSORES
const float TANK_DEPTH_CM = 138.5; 
const float OFFSET_HCSR = 0.82;
const float OFFSET_BPM = 47.0;
const float DEFAULT_PRESSURE_HPA = 752.0;
// =========================================================================

const uint16_t HISTORY_SIZE = 30;
const uint32_t SAMPLE_PERIOD_MS = 2000;
const uint32_t BUTTON_DEBOUNCE_MS = 300;
const uint32_t CHART_UPDATE_INTERVAL_MS = 20000;

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_BMP280 bmp;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

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
  float rawPressure;
  uint32_t lastValidPressureTime;
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
SemaphoreHandle_t i2cMutex;
TaskHandle_t sensorTaskHandle;
bool bmpAvailable = false;
bool alarmMuted = false;
bool otaInitialized = false;

uint8_t currentDisplayPage = 0;
const uint8_t MAX_PAGES = 4;
bool previousPageButtonState = HIGH;
uint32_t lastPageButtonToggle = 0;
bool wifiWasConnected = false;
uint32_t lastMqttReconnectAttempt = 0;
uint32_t lastWifiReconnectAttempt = 0;
uint32_t lastPublishedTimestamp = UINT32_MAX;
uint32_t messageSequence = 0;

SensorReadings getCurrentData();
void updateDisplay(const SensorReadings& data);
void setAlarmMuted(bool muted);
const char* stateToText(AlertState state);

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, MQTT_ALARM_COMMAND_TOPIC) != 0 || length >= 80) return;
  char command[80];
  memcpy(command, payload, length);
  command[length] = '\0';
  char* value = strstr(command, "\"muted\"");
  if (value == nullptr) return;
  value = strchr(value, ':');
  if (value == nullptr) return;
  do { ++value; } while (*value == ' ' || *value == '\t');
  if (strncmp(value, "true", 4) == 0) setAlarmMuted(true);
  else if (strncmp(value, "false", 5) == 0) setAlarmMuted(false);
}

bool connectMqtt() {
  if (mqttClient.connected()) return true;
  String clientId = String("esp32-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool connected = mqttClient.connect(
    clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
    MQTT_STATUS_TOPIC, 1, true, "offline"
  );
  if (connected) {
    mqttClient.publish(MQTT_STATUS_TOPIC, "online", true);
    mqttClient.subscribe(MQTT_ALARM_COMMAND_TOPIC, 1);
    Serial.println("MQTT conectado al Gateway");
  } else {
    Serial.printf("MQTT no conectado; estado=%d\n", mqttClient.state());
  }
  return connected;
}

void publishLatestReading() {
  if (!mqttClient.connected()) return;
  SensorReadings data = getCurrentData();
  if (data.timestamp == 0 || data.timestamp == lastPublishedTimestamp) return;

  float safePressure = isnan(data.pressure) ? DEFAULT_PRESSURE_HPA : data.pressure;
  float safeEvap = isnan(data.evaporationRisk) ? 0.0 : data.evaporationRisk;

  char payload[480];
  uint32_t sequence = ++messageSequence;
  int length = snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",\"message_id\":\"%s-%lu\",\"waterLevel\":%.1f,\"waterDistance\":%.1f,\"temperature\":%.1f,\"humidity\":%.1f,\"pressure\":%.1f,\"solarRadiation\":%.1f,\"evaporationRisk\":%.1f,\"state\":\"%s\",\"alarmMuted\":%s,\"timestamp\":%lu}",
    DEVICE_ID, DEVICE_ID, (unsigned long)sequence,
    data.waterLevel, data.waterDistance, data.temperature, data.humidity,
    safePressure, data.solarRadiation, safeEvap,
    stateToText(data.state), data.alarmMuted ? "true" : "false",
    (unsigned long)(data.timestamp / 1000));
  if (length > 0 && length < (int)sizeof(payload) &&
      mqttClient.publish(MQTT_TELEMETRY_TOPIC, payload, false)) {
    lastPublishedTimestamp = data.timestamp;
  }
}

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

  float distance = duration * 0.0343 / 2.0;
  return distance + OFFSET_HCSR;
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
  float validPressure = isnan(pressure) ? DEFAULT_PRESSURE_HPA : pressure;
  float tempScore = constrain((temperature - 20.0) * 5.0, 0, 100);
  float dryAirScore = constrain(100.0 - humidity, 0, 100);
  float radiationScore = constrain(solarRadiation, 0, 100);
  float lowPressureScore = constrain((1013.25 - validPressure) * 5.0, 0, 100);

  return constrain(
    (tempScore * 0.35) +
    (dryAirScore * 0.30) +
    (radiationScore * 0.25) +
    (lowPressureScore * 0.10),
    0.0, 100.0
  );
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
    case NORMAL: return "NORMAL";
    case ALERTA: return "ALERTA";
    case CRITICO: return "CRITICO";
    default: return "DESCONOCIDO";
  }
}

const char* stateToLcdText(AlertState state) {
  switch (state) {
    case NORMAL: return "OK";
    case ALERTA: return "ALER";
    case CRITICO: return "CRIT";
    default: return "ERR";
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

void handlePageButton() {
  bool buttonState = digitalRead(PAGE_BUTTON_PIN);
  bool stateChanged = (buttonState != previousPageButtonState);

  if (stateChanged && (millis() - lastPageButtonToggle > BUTTON_DEBOUNCE_MS)) {
    currentDisplayPage = (currentDisplayPage + 1) % MAX_PAGES;
    lastPageButtonToggle = millis();
    updateDisplay(getCurrentData());
  }

  previousPageButtonState = buttonState;
}

void showMessage(const char* firstLine, const char* secondLine = "") {
  if (i2cMutex != NULL) xSemaphoreTake(i2cMutex, portMAX_DELAY);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(firstLine);
  display.println(secondLine);
  display.display();
  if (i2cMutex != NULL) xSemaphoreGive(i2cMutex);
}

void updateDisplay(const SensorReadings& data) {
  if (i2cMutex != NULL) xSemaphoreTake(i2cMutex, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Est:");
  display.print(stateToLcdText(data.state));
  display.print(" P");
  display.print(currentDisplayPage + 1);
  display.print("/");
  display.print(MAX_PAGES);
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE); 

  display.setCursor(0, 15);

  switch (currentDisplayPage) {
    case 0:
      display.setTextSize(1);
      display.println("Nivel del Agua:");
      display.setCursor(0, 30);
      display.setTextSize(3); 
      display.print(data.waterLevel, 0);
      display.println("%");
      break;

    case 1:
      display.setTextSize(1);
      display.println("Temperatura:");
      display.setTextSize(2);
      display.print(data.temperature, 1);
      display.println(" C");
      display.setTextSize(1);
      display.println("Humedad:");
      display.setTextSize(2);
      display.print(data.humidity, 0);
      display.println(" %");
      break;

    case 2:
      display.setTextSize(1);
      display.print("Radiacion: ");
      display.print(data.solarRadiation, 0);
      display.println(" %");
      display.println("");
      display.print("Presion:   ");
      display.print(data.pressure, 0);
      display.println(" hPa");
      display.println("");
      display.print("Riesgo Evp:");
      display.print(data.evaporationRisk, 0);
      display.println(" %");
      break;

    case 3:
      display.setTextSize(1);
      display.print("Red:");
      {
        String redStr = WiFi.SSID();
        if (redStr.length() == 0) redStr = WIFI_SSID;
        if (redStr.length() > 16) redStr = redStr.substring(0, 16);
        display.println(redStr);
      }
      display.print("IP: ");
      display.println(WiFi.localIP());
      display.print("Alrm:");
      display.println(data.alarmMuted ? "SILENCIO" : "ACTIVA");
      display.print("Dist:");
      display.print(data.waterDistance, 1);
      display.println(" cm");
      break;
  }
  
  display.display();
  if (i2cMutex != NULL) xSemaphoreGive(i2cMutex);
}

bool readSensors(SensorReadings& data) {
  static float ultimaPresionValida = DEFAULT_PRESSURE_HPA;
  static uint32_t tiempoUltimaPresion = 0;

  uint32_t ahora = millis();
  data.temperature = dht.readTemperature();
  data.humidity = dht.readHumidity();
  data.waterDistance = readWaterDistanceCm();
  data.waterLevel = calculateWaterLevelPercent(data.waterDistance);
  data.solarRadiation = readSolarRadiationPercent();

  float lecturaPresion = NAN;
  if (bmpAvailable) {
    if (i2cMutex != NULL) xSemaphoreTake(i2cMutex, portMAX_DELAY);
    lecturaPresion = (bmp.readPressure() / 100.0) + OFFSET_BPM;
    if (i2cMutex != NULL) xSemaphoreGive(i2cMutex);
  }
  data.rawPressure = lecturaPresion;
  if (!isnan(lecturaPresion) && lecturaPresion > 500.0) {
    ultimaPresionValida = lecturaPresion;
    tiempoUltimaPresion = ahora;
  }
  data.pressure = ultimaPresionValida;
  data.lastValidPressureTime = tiempoUltimaPresion;

  data.evaporationRisk = calculateEvaporationRisk(
    data.temperature,
    data.humidity,
    data.solarRadiation,
    data.pressure
  );
  data.state = evaluateState(data);
  data.alarmMuted = alarmMuted;
  data.timestamp = ahora;

  return !isnan(data.temperature) &&
         !isnan(data.humidity) &&
         !isnan(data.waterDistance);
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
  html.reserve(12000);
  html += F("<!doctype html><html lang='es'><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Tablero hidrico local</title>");
  html += F("<script src='https://cdn.jsdelivr.net/npm/chart.js'></script><style>");
  html += F("body{font-family:Arial,sans-serif;margin:0;background:#eef4f8;color:#10202b}");
  html += F("header{background:#0c5c75;color:white;padding:18px 22px}");
  html += F("main{padding:18px;max-width:980px;margin:auto}");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px}");
  html += F(".card{background:white;border:1px solid #d4e0e6;border-radius:8px;padding:14px}");
  html += F(".value{font-size:28px;font-weight:700;margin-top:6px}");
  html += F(".state{font-size:34px;font-weight:800}.NORMAL{color:#16833a}.ALERTA{color:#a87400}.CRITICO{color:#b3261e}");
  html += F("button{border:0;border-radius:6px;background:#0c5c75;color:white;padding:12px 16px;font-weight:700;cursor:pointer}");
  html += F("table{width:100%;border-collapse:collapse;background:white;margin-top:12px}td,th{padding:8px;border-bottom:1px solid #dbe6eb;text-align:left}");
  html += F(".chart-container{position:relative;height:300px;width:100%;}");
  html += F("</style></head><body><header><h1>Tablero de control local</h1><p>Prototipo IoT de monitoreo de disponibilidad hidrica</p></header><main>");
  html += "<section class='card'><div>Estado actual</div><div id='val-state' class='state ";
  html += stateToText(data.state);
  html += "'>";
  html += stateToText(data.state);
  html += F("</div></section><section class='grid' style='margin-top:12px'>");
  html += "<div class='card'>Nivel<div class='value' id='val-level'>" + String(data.waterLevel, 1) + F("%</div></div>");
  html += "<div class='card'>Distancia<div class='value' id='val-dist'>" + String(data.waterDistance, 1) + F(" cm</div></div>");
  html += "<div class='card'>Temperatura<div class='value' id='val-temp'>" + String(data.temperature, 1) + F(" C</div></div>");
  html += "<div class='card'>Humedad<div class='value' id='val-hum'>" + String(data.humidity, 1) + F("%</div></div>");
  html += "<div class='card'>Presion<div class='value' id='val-pres'>" + String(data.pressure, 1) + F(" hPa</div></div>");
  html += "<div class='card'>Radiacion<div class='value' id='val-rad'>" + String(data.solarRadiation, 1) + F("%</div></div>");
  html += "<div class='card'>Riesgo evaporacion<div class='value' id='val-evap'>" + String(data.evaporationRisk, 1) + F("%</div></div>");
  html += "<div class='card'>Alarma<div class='value' id='val-alarm'>";
  html += data.alarmMuted ? "Silenciada" : "Activa";
  html += F("</div></div></section><section class='card' style='margin-top:12px'>");
  html += F("<form action='/alarm/off' method='post' style='display:inline-block'><button>Desactivar alarma fisica</button></form> ");
  html += F("<form action='/alarm/on' method='post' style='display:inline-block;margin-left:8px'><button>Reactivar alarma fisica</button></form>");
  html += F("</section>");

  html += F("<section class='card' style='margin-top:12px'>");
  html += F("<h2>Evolucion del Nivel de Agua (%)</h2>");
  html += F("<div class='chart-container'><canvas id='waterChart'></canvas></div>");
  html += F("</section>");

  html += F("<section style='margin-top:12px;overflow-x:auto;'><h2>Historico reciente (Ultimas 30 lecturas)</h2>");
  html += F("<table id='historyTable'><thead><tr><th>t (s)</th><th>Nivel</th><th>Temp</th><th>Hum</th><th>Pres</th><th>Rad</th><th>Riesgo</th><th>Estado</th></tr></thead><tbody>");

  String chartDataJS = "[";
  String chartLabelsJS = "[";

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (uint16_t i = 0; i < historyCount; i++) {
    uint16_t pos = (historyIndex + HISTORY_SIZE - historyCount + i) % HISTORY_SIZE;
    SensorReadings row = history[pos];
    html += "<tr><td>" + String(row.timestamp / 1000) + "</td><td>" + String(row.waterLevel, 1);
    html += "%</td><td>" + String(row.temperature, 1) + "</td><td>" + String(row.humidity, 1);
    html += "</td><td>" + String(row.pressure, 1) + "</td><td>" + String(row.solarRadiation, 1) + "</td><td>" + String(row.evaporationRisk, 1);
    html += "</td><td>" + String(stateToText(row.state)) + "</td></tr>";

    chartDataJS += String(row.waterLevel, 1);
    chartLabelsJS += "'" + String(row.timestamp / 1000) + "s'";
    if (i < historyCount - 1) {
      chartDataJS += ",";
      chartLabelsJS += ",";
    }
  }
  xSemaphoreGive(dataMutex);

  chartDataJS += "]";
  chartLabelsJS += "]";
  html += F("</tbody></table></section></main>");

  html += F("<script>");
  html += "let lastChartUpdate = Date.now();";
  html += "let lastTimestamp = 0;";
  html += "const chartUpdateInterval = " + String(CHART_UPDATE_INTERVAL_MS) + ";";
  html += "if (typeof Chart !== 'undefined') {";
  html += "  const ctx = document.getElementById('waterChart').getContext('2d');";
  html += "  window.myLineChart = new Chart(ctx, {";
  html += "    type: 'line',";
  html += "    data: { labels: " + chartLabelsJS + ", datasets: [{ label: 'Nivel de Agua (%)', data: " + chartDataJS + ",";
  html += "      borderColor: '#0c5c75', backgroundColor: 'rgba(12, 92, 117, 0.2)', borderWidth: 2, pointBackgroundColor: '#b3261e', pointRadius: 4, fill: true, tension: 0.3 }] },";
  html += "    options: { responsive: true, maintainAspectRatio: false, scales: { y: { beginAtZero: true, max: 100 } }, plugins: { legend: { display: false } } }";
  html += "  });";
  html += "}";
  html += F("setInterval(async () => {");
  html += F("  try {");
  html += F("    const res = await fetch('/api/status');");
  html += F("    if (!res.ok) return;");
  html += F("    const d = await res.json();");
  html += F("    document.getElementById('val-level').textContent = d.waterLevel.toFixed(1) + '%';");
  html += F("    document.getElementById('val-dist').textContent = d.waterDistance.toFixed(1) + ' cm';");
  html += F("    document.getElementById('val-temp').textContent = d.temperature.toFixed(1) + ' C';");
  html += F("    document.getElementById('val-hum').textContent = d.humidity.toFixed(1) + '%';");
  html += F("    document.getElementById('val-pres').textContent = d.pressure.toFixed(1) + ' hPa';");
  html += F("    document.getElementById('val-rad').textContent = d.solarRadiation.toFixed(1) + '%';");
  html += F("    document.getElementById('val-evap').textContent = d.evaporationRisk.toFixed(1) + '%';");
  html += F("    document.getElementById('val-alarm').textContent = d.alarmMuted ? 'Silenciada' : 'Activa';");
  html += F("    const st = document.getElementById('val-state'); st.textContent = d.state; st.className = 'state ' + d.state;");
  html += F("    if (d.timestamp && d.timestamp !== lastTimestamp) {");
  html += F("      lastTimestamp = d.timestamp;");
  html += F("      const tbody = document.getElementById('historyTable').getElementsByTagName('tbody')[0];");
  html += F("      const row = tbody.insertRow(-1);");
  html += F("      row.innerHTML = `<td>${d.timestamp}</td><td>${d.waterLevel.toFixed(1)}%</td><td>${d.temperature.toFixed(1)}</td><td>${d.humidity.toFixed(1)}</td><td>${d.pressure.toFixed(1)}</td><td>${d.solarRadiation.toFixed(1)}</td><td>${d.evaporationRisk.toFixed(1)}</td><td>${d.state}</td>`;");
  html += F("      if (tbody.rows.length > 30) tbody.deleteRow(0);");
  html += F("    }");
  html += F("    if (window.myLineChart && Date.now() - lastChartUpdate >= chartUpdateInterval) {");
  html += F("      lastChartUpdate = Date.now();");
  html += F("      window.myLineChart.data.labels.push(d.timestamp + 's');");
  html += F("      window.myLineChart.data.datasets[0].data.push(d.waterLevel);");
  html += F("      if (window.myLineChart.data.labels.length > 30) { window.myLineChart.data.labels.shift(); window.myLineChart.data.datasets[0].data.shift(); }");
  html += F("      window.myLineChart.update();");
  html += F("    }");
  html += F("  } catch (e) {}");
  html += F("}, 2000);</script></body></html>");
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
  json += "\"alarmMuted\":" + String(data.alarmMuted ? "true" : "false") + ",";
  json += "\"timestamp\":" + String(data.timestamp / 1000);
  json += "}";
  server.send(200, "application/json", json);
}

void handleAlarmOff() {
  if (!requireAuth()) { return; }
  setAlarmMuted(true);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleAlarmOn() {
  if (!requireAuth()) { return; }
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

void setupOTA() {
  ArduinoOTA.setHostname("esp32-hidrico");
  ArduinoOTA.setPassword(DASHBOARD_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("Iniciando actualizacion OTA..."); });
  ArduinoOTA.onEnd([]() { Serial.println("\nActualizacion OTA completada con exito."); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progreso OTA: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("Error OTA [%u]\n", error); });
  ArduinoOTA.begin();
  otaInitialized = true;
}

void printSerialReport(const SensorReadings& data) {
  Serial.println("=========================================");
  Serial.println("       DATOS DE SENSORES EN TIEMPO REAL  ");
  Serial.println("=========================================");
  Serial.print("Nivel de Agua:       "); Serial.print(data.waterLevel); Serial.println(" %");
  Serial.print("Distancia HC-SR04:   "); Serial.print(data.waterDistance); Serial.println(" cm");
  Serial.print("Temperatura DHT22:   "); Serial.print(data.temperature); Serial.println(" C");
  Serial.print("Humedad DHT22:       "); Serial.print(data.humidity); Serial.println(" %");
  Serial.print("Presion Filtrada:    "); Serial.print(data.pressure); Serial.println(" hPa");
  Serial.print("Presion Cruda BMP:   "); Serial.print(data.rawPressure); Serial.println(" hPa");
  Serial.print("Tiempo sin act. BMP: ");
  if (data.lastValidPressureTime == 0) {
    Serial.println("Sin lecturas validas aun");
  } else {
    unsigned long segundosInactivo = (millis() - data.lastValidPressureTime) / 1000;
    Serial.print(segundosInactivo); Serial.println(" segundos");
  }
  Serial.print("Radiacion (LDR):     "); Serial.print(data.solarRadiation); Serial.println(" %");
  Serial.print("Riesgo Evaporacion:  "); Serial.print(data.evaporationRisk); Serial.println(" %");
  Serial.print("Estado de Alerta:    "); Serial.println(stateToText(data.state));
  Serial.print("Alarma Sonora:       "); Serial.println(data.alarmMuted ? "SILENCIADA" : "ACTIVA");
  Serial.println("=========================================\n");
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
      Serial.println("Error leyendo sensores. Verifique las conexiones.");
      showMessage("Error sensores", "Revise conexion");
    }

    vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
  }
}

void setup() {
  Serial.begin(115200);

  dataMutex = xSemaphoreCreateMutex();
  i2cMutex = xSemaphoreCreateMutex();

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Error iniciando pantalla OLED externa");
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
  
  pinMode(PAGE_BUTTON_PIN, INPUT_PULLUP);
  previousPageButtonState = digitalRead(PAGE_BUTTON_PIN);

  if (!bmpAvailable) {
    showMessage("Error BMP280", "Usando pres. base");
    Serial.println("Error iniciando BMP280; usando presion de respaldo");
    delay(2000);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  showMessage("Conectando Wi-Fi", WIFI_SSID);

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
  handlePageButton(); 
  server.handleClient();
  uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    wifiWasConnected = false;
    if (now - lastWifiReconnectAttempt >= 10000) {
      lastWifiReconnectAttempt = now;
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  } else {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      if (!otaInitialized) setupOTA();
      Serial.print("Tablero local: http://");
      Serial.println(WiFi.localIP());
      Serial.print("Gateway MQTT: ");
      Serial.println(MQTT_BROKER);
    }
    ArduinoOTA.handle();
    if (!mqttClient.connected() && now - lastMqttReconnectAttempt >= 5000) {
      lastMqttReconnectAttempt = now;
      connectMqtt();
    }
    if (mqttClient.connected()) {
      mqttClient.loop();
      publishLatestReading();
    }
  }
  delay(5);
}
