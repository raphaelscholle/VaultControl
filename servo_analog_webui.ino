// Simple ESP32 WROOM servo + analog read with calibration + web UI
// Notes:
// - GPIO4 is ADC2. ADC2 cannot be used while WiFi is active on ESP32.
//   If analog readings are unstable/zero, move to an ADC1 pin (e.g., GPIO34/35/32/33).
// - GPIO12 is a strapping pin; ensure the servo or attached circuit doesn't pull it high at boot.

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ======= USER CONFIG =======
const char* WIFI_SSID = "VaultControl";
const char* WIFI_PASS = "vaultcontrol123";

const int SERVO_PIN = 12;  // GPIO12
const int ANALOG_PIN = 4;  // GPIO4 (ADC2)

// ======= SERVO CONFIG =======
const int SERVO_CHANNEL = 0;
const int SERVO_FREQ_HZ = 50;  // 50 Hz typical
const int SERVO_RES_BITS = 16; // 16-bit PWM
const int SERVO_MIN_US = 500;  // pulse width at 0 deg
const int SERVO_MAX_US = 2500; // pulse width at 180 deg

// ======= ANALOG CONFIG =======
const int ANALOG_SAMPLES = 16; // simple averaging

// ======= GLOBALS =======
WebServer server(80);
Preferences prefs;

bool isCalibrating = false;
uint16_t calMin = 4095;
uint16_t calMax = 0;
int currentAngle = 90;

// ======= HTML UI =======
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>VaultControl</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@300;400;600;700&display=swap');
    :root {
      --bg1: #0d1b1f;
      --bg2: #1a2b33;
      --accent: #ffa24b;
      --accent2: #45c2b1;
      --panel: rgba(255, 255, 255, 0.08);
      --panel-border: rgba(255, 255, 255, 0.18);
      --text: #f7f7f7;
      --muted: #b7c1c7;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Space Grotesk", system-ui, -apple-system, sans-serif;
      color: var(--text);
      background: radial-gradient(1200px 800px at 10% -10%, #203642, transparent 60%),
                  radial-gradient(900px 700px at 110% 10%, #123126, transparent 55%),
                  linear-gradient(160deg, var(--bg1), var(--bg2));
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 24px;
    }
    .wrap {
      width: min(920px, 100%);
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 20px;
      animation: fadeIn 700ms ease-out;
    }
    .card {
      background: var(--panel);
      border: 1px solid var(--panel-border);
      border-radius: 18px;
      padding: 22px;
      backdrop-filter: blur(10px);
      box-shadow: 0 20px 40px rgba(0, 0, 0, 0.25);
      position: relative;
      overflow: hidden;
    }
    .card::after {
      content: "";
      position: absolute;
      inset: -50% auto auto -50%;
      width: 200px;
      height: 200px;
      background: radial-gradient(circle, rgba(255, 162, 75, 0.25), transparent 70%);
      transform: rotate(35deg);
      pointer-events: none;
    }
    h1 {
      grid-column: 1 / -1;
      margin: 0 0 8px 0;
      font-size: clamp(24px, 3vw, 32px);
      letter-spacing: 0.5px;
    }
    .muted { color: var(--muted); }
    .row { display: flex; gap: 12px; align-items: center; }
    .label { font-size: 13px; text-transform: uppercase; letter-spacing: 1.2px; color: var(--muted); }
    .value { font-size: 28px; font-weight: 700; }
    .slider {
      width: 100%;
      margin-top: 12px;
      accent-color: var(--accent);
    }
    .btn {
      border: 0;
      background: linear-gradient(135deg, var(--accent), #ffd07c);
      color: #2a1b0e;
      padding: 12px 16px;
      border-radius: 12px;
      font-weight: 700;
      cursor: pointer;
      transition: transform 120ms ease, box-shadow 120ms ease;
    }
    .btn.secondary {
      background: linear-gradient(135deg, var(--accent2), #9ef2e7);
      color: #042520;
    }
    .btn.ghost {
      background: transparent;
      color: var(--text);
      border: 1px solid var(--panel-border);
    }
    .btn:active { transform: scale(0.98); }
    .btn:hover { box-shadow: 0 12px 20px rgba(0,0,0,0.18); }
    .status {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-top: 14px;
      font-size: 13px;
    }
    .pill {
      padding: 6px 10px;
      border-radius: 999px;
      background: rgba(255,255,255,0.1);
      border: 1px solid var(--panel-border);
    }
    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
      margin-top: 14px;
    }
    .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; }
    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(10px); }
      to { opacity: 1; transform: translateY(0); }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>VaultControl <span class="muted">ESP32</span></h1>

    <div class="card">
      <div class="label">Servo Angle</div>
      <div class="value" id="angleVal">90?</div>
      <input class="slider" id="angle" type="range" min="0" max="180" value="90" />
      <div class="status">
        <div class="pill" id="servoPulse">pulse: -- us</div>
        <div class="pill" id="wifi">WiFi: --</div>
      </div>
    </div>

    <div class="card">
      <div class="label">Analog Read</div>
      <div class="value" id="raw">--</div>
      <div class="muted" id="cal">cal: --%</div>
      <div class="grid">
        <div class="pill mono" id="min">min: --</div>
        <div class="pill mono" id="max">max: --</div>
      </div>
    </div>

    <div class="card">
      <div class="label">Calibration</div>
      <p class="muted">Move the sensor through its full range, then stop & save.</p>
      <div class="row">
        <button class="btn" id="startBtn">Start</button>
        <button class="btn secondary" id="stopBtn">Stop & Save</button>
        <button class="btn ghost" id="resetBtn">Reset</button>
      </div>
      <div class="status">
        <div class="pill" id="calState">Idle</div>
        <div class="pill" id="ip">IP: --</div>
      </div>
    </div>
  </div>

  <script>
    const $ = (id) => document.getElementById(id);

    async function fetchStatus() {
      const res = await fetch('/status');
      const s = await res.json();
      $('angle').value = s.angle;
      $('angleVal').textContent = s.angle + '?';
      $('raw').textContent = s.raw;
      $('cal').textContent = 'cal: ' + s.cal.toFixed(1) + '%';
      $('min').textContent = 'min: ' + s.min;
      $('max').textContent = 'max: ' + s.max;
      $('calState').textContent = s.calibrating ? 'Calibrating' : 'Idle';
      $('wifi').textContent = 'WiFi: ' + (s.wifi ? 'OK' : 'OFF');
      $('ip').textContent = 'IP: ' + s.ip;
      $('servoPulse').textContent = 'pulse: ' + s.pulse + ' us';
    }

    $('angle').addEventListener('input', async (e) => {
      const angle = e.target.value;
      $('angleVal').textContent = angle + '?';
      await fetch('/set?angle=' + angle);
    });

    $('startBtn').addEventListener('click', () => fetch('/calibrate?cmd=start'));
    $('stopBtn').addEventListener('click', () => fetch('/calibrate?cmd=stop'));
    $('resetBtn').addEventListener('click', () => fetch('/calibrate?cmd=reset'));

    setInterval(fetchStatus, 700);
    fetchStatus();
  </script>
</body>
</html>
)HTML";

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

uint32_t angleToPulseUs(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  return SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * (uint32_t)angle / 180U;
}

void setServoAngle(int angle) {
  currentAngle = angle;
  uint32_t pulse = angleToPulseUs(angle);
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

// ======= HTTP HANDLERS =======
void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  uint16_t raw = analogReadAvg();
  if (isCalibrating) {
    if (raw < calMin) calMin = raw;
    if (raw > calMax) calMax = raw;
  }

  float cal = calibratedPercent(raw);
  uint32_t pulse = angleToPulseUs(currentAngle);

  String json = "{";
  json += "\"angle\":" + String(currentAngle) + ",";
  json += "\"raw\":" + String(raw) + ",";
  json += "\"cal\":" + String(cal, 1) + ",";
  json += "\"min\":" + String(calMin) + ",";
  json += "\"max\":" + String(calMax) + ",";
  json += "\"calibrating\":" + String(isCalibrating ? "true" : "false") + ",";
  json += "\"wifi\":" + String(WiFi.isConnected() ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"pulse\":" + String(pulse);
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

void setup() {
  Serial.begin(115200);
  delay(200);

  // Servo setup
  ledcSetup(SERVO_CHANNEL, SERVO_FREQ_HZ, SERVO_RES_BITS);
  ledcAttachPin(SERVO_PIN, SERVO_CHANNEL);
  setServoAngle(currentAngle);

  // Analog setup
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  loadCalibration();

  // WiFi (SoftAP)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);

  // Web server
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/set", handleSet);
  server.on("/calibrate", handleCalibrate);
  server.begin();

  Serial.println("Ready. Connect to AP: " + String(WIFI_SSID));
  Serial.println("IP: " + WiFi.softAPIP().toString());
}

void loop() {
  server.handleClient();
}
