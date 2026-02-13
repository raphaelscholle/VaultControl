// Simple ESP32 WROOM servo + analog read with calibration + web UI (PlatformIO + SPIFFS)
// Notes:
// - GPIO4 is ADC2. ADC2 cannot be used while WiFi is active on ESP32.
//   If analog readings are unstable/zero, move to an ADC1 pin (e.g., GPIO34/35/32/33).
// - GPIO12 is a strapping pin; ensure the servo or attached circuit doesn't pull it high at boot.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>

// ======= USER CONFIG =======
const char* WIFI_SSID = "valt42";
const char* WIFI_PASS = "fallout";

const int SERVO_PIN = 12;  // GPIO12
const int ANALOG_PIN = 4;  // GPIO4 (ADC2)

// ======= SERVO CONFIG =======
const int SERVO_CHANNEL = 0;
const int SERVO_FREQ_HZ = 50;  // 50 Hz typical
const int SERVO_RES_BITS = 16; // 16-bit PWM
const uint16_t SERVO_MIN_US = 500;     // pulse width at 0 deg
const uint16_t SERVO_MAX_US = 2500;    // pulse width at 180 deg
const uint16_t SERVO_ABS_MIN_US = 300; // safety clamp for calibration
const uint16_t SERVO_ABS_MAX_US = 3000;

// ======= ANALOG CONFIG =======
const int ANALOG_SAMPLES = 16; // simple averaging

// ======= GLOBALS =======
WebServer server(80);
Preferences prefs;

bool isCalibrating = false;
uint16_t calMin = 4095;
uint16_t calMax = 0;
int currentAngle = 90;
uint16_t servoMinUs = SERVO_MIN_US;
uint16_t servoMaxUs = SERVO_MAX_US;
uint32_t currentPulseUs = SERVO_MIN_US;

// ======= HELPERS =======
uint16_t analogReadAvg() {
  uint32_t sum = 0;
  for (int i = 0; i < ANALOG_SAMPLES; i++) {
    sum += analogRead(ANALOG_PIN);
    delay(2);
  }
  return (uint16_t)(sum / ANALOG_SAMPLES);
}

uint32_t usToDuty(uint32_t us) {
  const uint32_t maxDuty = (1UL << SERVO_RES_BITS) - 1;
  // duty = (us * freq * maxDuty) / 1e6
  return (uint32_t)((uint64_t)us * SERVO_FREQ_HZ * maxDuty / 1000000ULL);
}

uint16_t clampServoUs(int value) {
  if (value < SERVO_ABS_MIN_US) return SERVO_ABS_MIN_US;
  if (value > SERVO_ABS_MAX_US) return SERVO_ABS_MAX_US;
  return (uint16_t)value;
}

uint32_t angleToPulseUs(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  if (servoMaxUs <= servoMinUs) return servoMinUs;
  return servoMinUs + (servoMaxUs - servoMinUs) * (uint32_t)angle / 180U;
}

int pulseToAngle(uint32_t pulse) {
  if (servoMaxUs <= servoMinUs) return 0;
  if (pulse <= servoMinUs) return 0;
  if (pulse >= servoMaxUs) return 180;
  return (int)((pulse - servoMinUs) * 180UL / (servoMaxUs - servoMinUs));
}

void setServoPulseUs(uint32_t pulse) {
  uint16_t clamped = clampServoUs((int)pulse);
  currentPulseUs = clamped;
  currentAngle = pulseToAngle(clamped);
  uint32_t duty = usToDuty(clamped);
  ledcWrite(SERVO_CHANNEL, duty);
}

void setServoAngle(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  currentAngle = angle;
  uint32_t pulse = angleToPulseUs(angle);
  currentPulseUs = pulse;
  uint32_t duty = usToDuty(pulse);
  ledcWrite(SERVO_CHANNEL, duty);
}

float calibratedPercent(uint16_t raw) {
  if (calMax <= calMin) return 0.0f;
  float v = (float)(raw - calMin) / (float)(calMax - calMin);
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  return v * 100.0f;
}

void loadCalibration() {
  prefs.begin("cal", true);
  calMin = prefs.getUShort("min", 4095);
  calMax = prefs.getUShort("max", 0);
  prefs.end();
}

void saveCalibration() {
  prefs.begin("cal", false);
  prefs.putUShort("min", calMin);
  prefs.putUShort("max", calMax);
  prefs.end();
}

void loadServoCalibration() {
  prefs.begin("servo", true);
  servoMinUs = prefs.getUShort("minUs", SERVO_MIN_US);
  servoMaxUs = prefs.getUShort("maxUs", SERVO_MAX_US);
  prefs.end();
  if (servoMinUs < SERVO_ABS_MIN_US || servoMaxUs > SERVO_ABS_MAX_US || servoMaxUs <= servoMinUs) {
    servoMinUs = SERVO_MIN_US;
    servoMaxUs = SERVO_MAX_US;
  }
}

void saveServoCalibration() {
  prefs.begin("servo", false);
  prefs.putUShort("minUs", servoMinUs);
  prefs.putUShort("maxUs", servoMaxUs);
  prefs.end();
}

IPAddress currentIP() {
  if (WiFi.getMode() & WIFI_AP) {
    return WiFi.softAPIP();
  }
  return WiFi.localIP();
}

// ======= HTTP HANDLERS =======
void handleStatus() {
  uint16_t raw = analogReadAvg();
  if (isCalibrating) {
    if (raw < calMin) calMin = raw;
    if (raw > calMax) calMax = raw;
  }

  float cal = calibratedPercent(raw);
  IPAddress ip = currentIP();

  String json = "{";
  json += "\"angle\":" + String(currentAngle) + ",";
  json += "\"raw\":" + String(raw) + ",";
  json += "\"cal\":" + String(cal, 1) + ",";
  json += "\"min\":" + String(calMin) + ",";
  json += "\"max\":" + String(calMax) + ",";
  json += "\"calibrating\":" + String(isCalibrating ? "true" : "false") + ",";
  json += "\"wifi\":" + String(WiFi.getMode() != WIFI_OFF ? "true" : "false") + ",";
  json += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"ip\":\"" + ip.toString() + "\",";
  json += "\"servoMinUs\":" + String(servoMinUs) + ",";
  json += "\"servoMaxUs\":" + String(servoMaxUs) + ",";
  json += "\"pulse\":" + String(currentPulseUs);
  json += "}";

  server.send(200, "application/json", json);
}

void handleSet() {
  if (!server.hasArg("angle")) {
    server.send(400, "text/plain", "Missing angle");
    return;
  }
  int angle = server.arg("angle").toInt();
  setServoAngle(angle);
  server.send(200, "text/plain", "OK");
}

void handleCalibrate() {
  if (!server.hasArg("cmd")) {
    server.send(400, "text/plain", "Missing cmd");
    return;
  }
  String cmd = server.arg("cmd");
  cmd.toLowerCase();

  if (cmd == "start") {
    isCalibrating = true;
    calMin = 4095;
    calMax = 0;
  } else if (cmd == "stop") {
    isCalibrating = false;
    saveCalibration();
  } else if (cmd == "reset") {
    isCalibrating = false;
    calMin = 4095;
    calMax = 0;
    saveCalibration();
  }

  server.send(200, "text/plain", "OK");
}

void handleServoCalibrate() {
  if (server.hasArg("pulse")) {
    uint32_t pulse = (uint32_t)server.arg("pulse").toInt();
    setServoPulseUs(pulse);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (!server.hasArg("cmd")) {
    server.send(400, "text/plain", "Missing cmd");
    return;
  }

  String cmd = server.arg("cmd");
  cmd.toLowerCase();

  if (cmd == "save") {
    if (!server.hasArg("min") || !server.hasArg("max")) {
      server.send(400, "text/plain", "Missing min/max");
      return;
    }
    uint16_t minUs = clampServoUs(server.arg("min").toInt());
    uint16_t maxUs = clampServoUs(server.arg("max").toInt());
    if (maxUs <= minUs) {
      server.send(400, "text/plain", "Invalid range");
      return;
    }
    servoMinUs = minUs;
    servoMaxUs = maxUs;
    saveServoCalibration();
    setServoAngle(currentAngle);
  } else if (cmd == "reset") {
    servoMinUs = SERVO_MIN_US;
    servoMaxUs = SERVO_MAX_US;
    saveServoCalibration();
    setServoAngle(currentAngle);
  }

  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Servo setup
  ledcSetup(SERVO_CHANNEL, SERVO_FREQ_HZ, SERVO_RES_BITS);
  ledcAttachPin(SERVO_PIN, SERVO_CHANNEL);
  loadServoCalibration();
  setServoAngle(currentAngle);

  // Analog setup
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  loadCalibration();

  // Filesystem
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  // WiFi (SoftAP)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);

  // Web server
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/set", HTTP_GET, handleSet);
  server.on("/api/calibrate", HTTP_GET, handleCalibrate);
  server.on("/api/servo", HTTP_GET, handleServoCalibrate);
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.onNotFound([]() { server.send(404, "text/plain", "Not Found"); });
  server.begin();

  Serial.println("Ready. Connect to AP: " + String(WIFI_SSID));
  Serial.println("IP: " + WiFi.softAPIP().toString());
}

void loop() {
  server.handleClient();
}
