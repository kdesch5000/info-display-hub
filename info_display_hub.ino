/*
 * Info Display Hub - Lilygo T-Display S3
 * =======================================
 * A WiFi-connected rotating info display with:
 *   - NTP clock
 *   - Weather display (OpenWeatherMap)
 *   - Sports scores placeholder
 *   - Bedroom Humidity (HA sensor with animated water droplet)
 *   - Espresso Stats (HA sensor with coffee cup + history)
 *   - Backyard Temperature (HA sensor with thermometer graphic)
 *   - Sump Pump Monitor (HA sensor with 7-day bar chart)
 *   - OTA firmware updates (web-based)
 *   - Web configuration portal
 *
 * First flash via USB-C, then all updates over WiFi.
 *
 * Libraries required (install via Arduino Library Manager):
 *   - TFT_eSPI (configure for T-Display S3)
 *   - ArduinoJson (by Benoit Blanchon)
 *   - AsyncTCP (https://github.com/me-no-dev/AsyncTCP)
 *   - ESPAsyncWebServer (https://github.com/me-no-dev/ESPAsyncWebServer)
 *   - ElegantOTA (by Ayush Sharma)
 *
 * Board setup in Arduino IDE:
 *   1. Add ESP32 board package via Board Manager
 *   2. Select "LilyGo T-Display-S3" or "ESP32S3 Dev Module"
 *   3. Set USB CDC On Boot: Enabled
 *   4. Set Flash Size: 16MB
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <math.h>

// ============================================================
// DISPLAY SETUP
// ============================================================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

#define SCREEN_WIDTH  170
#define SCREEN_HEIGHT 320

// ============================================================
// BUTTON PINS (T-Display S3 built-in buttons)
// ============================================================
#define BTN_LEFT  0
#define BTN_RIGHT 14

// ============================================================
// HARDCODED HA ENTITY IDS (purpose-built screens)
// ============================================================
#define HA_ENTITY_HUMIDITY  "sensor.my_humidifier_humidity"
#define HA_ENTITY_ESPRESSO  "sensor.kd_micra_total_coffees_made"
#define HA_ENTITY_BACKYARD  "sensor.backyard_temperature_sensor_temperature"
#define HA_ENTITY_SUMP      "sensor.pumpspy_battery_backup_main_last_cycle"

// ============================================================
// CONFIGURATION (stored in NVMe flash via Preferences)
// ============================================================
Preferences prefs;

struct Config {
  char wifi_ssid[64];
  char wifi_pass[64];
  char weather_api_key[64];
  char weather_city[64];
  char ha_url[128];         // Home Assistant URL, e.g. http://192.168.1.100:8123
  char ha_token[256];       // Home Assistant long-lived access token
  int  rotation_seconds;    // seconds per screen
  char timezone[48];        // POSIX timezone string
  bool screen_clock;
  bool screen_weather;
  bool screen_sports;
  bool screen_humidity;
  bool screen_espresso;
  bool screen_backyard;
  bool screen_sump;
} config;

void loadConfig() {
  prefs.begin("infohub", true);  // read-only
  strlcpy(config.wifi_ssid,      prefs.getString("wifi_ssid", "").c_str(),      sizeof(config.wifi_ssid));
  strlcpy(config.wifi_pass,      prefs.getString("wifi_pass", "").c_str(),      sizeof(config.wifi_pass));
  strlcpy(config.weather_api_key,prefs.getString("weather_key", "").c_str(),    sizeof(config.weather_api_key));
  strlcpy(config.weather_city,   prefs.getString("weather_city", "Chicago").c_str(), sizeof(config.weather_city));
  strlcpy(config.ha_url,         prefs.getString("ha_url", "").c_str(),         sizeof(config.ha_url));
  strlcpy(config.ha_token,       prefs.getString("ha_token", "").c_str(),       sizeof(config.ha_token));
  config.rotation_seconds =      prefs.getInt("rotate_sec", 5);
  strlcpy(config.timezone,       prefs.getString("timezone", "CST6CDT,M3.2.0,M11.1.0").c_str(), sizeof(config.timezone));
  config.screen_clock    = prefs.getBool("scr_clock", true);
  config.screen_weather  = prefs.getBool("scr_weather", true);
  config.screen_sports   = prefs.getBool("scr_sports", true);
  config.screen_humidity = prefs.getBool("scr_humid", false);
  config.screen_espresso = prefs.getBool("scr_espres", false);
  config.screen_backyard = prefs.getBool("scr_backyd", false);
  config.screen_sump     = prefs.getBool("scr_sump", false);
  prefs.end();
}

void saveConfig() {
  prefs.begin("infohub", false);  // read-write
  prefs.putString("wifi_ssid",    config.wifi_ssid);
  prefs.putString("wifi_pass",    config.wifi_pass);
  prefs.putString("weather_key",  config.weather_api_key);
  prefs.putString("weather_city", config.weather_city);
  prefs.putString("ha_url",       config.ha_url);
  prefs.putString("ha_token",     config.ha_token);
  prefs.putInt("rotate_sec",      config.rotation_seconds);
  prefs.putString("timezone",     config.timezone);
  prefs.putBool("scr_clock",      config.screen_clock);
  prefs.putBool("scr_weather",    config.screen_weather);
  prefs.putBool("scr_sports",     config.screen_sports);
  prefs.putBool("scr_humid",      config.screen_humidity);
  prefs.putBool("scr_espres",     config.screen_espresso);
  prefs.putBool("scr_backyd",     config.screen_backyard);
  prefs.putBool("scr_sump",       config.screen_sump);
  prefs.end();
}

// ============================================================
// WEB SERVER & OTA
// ============================================================
AsyncWebServer server(80);

// Embedded HTML for the config page
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Info Display Hub</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: 'Segoe UI', system-ui, sans-serif;
    background: #0f1117;
    color: #e0e0e0;
    min-height: 100vh;
    padding: 20px;
  }
  .container { max-width: 500px; margin: 0 auto; }
  h1 {
    font-size: 1.4em;
    color: #7eb8ff;
    margin-bottom: 6px;
    letter-spacing: -0.5px;
  }
  .subtitle { color: #666; font-size: 0.85em; margin-bottom: 24px; }
  .card {
    background: #1a1d27;
    border: 1px solid #2a2d3a;
    border-radius: 12px;
    padding: 20px;
    margin-bottom: 16px;
  }
  .card h2 {
    font-size: 0.9em;
    color: #7eb8ff;
    text-transform: uppercase;
    letter-spacing: 1px;
    margin-bottom: 16px;
    padding-bottom: 8px;
    border-bottom: 1px solid #2a2d3a;
  }
  label {
    display: block;
    font-size: 0.8em;
    color: #888;
    margin-bottom: 4px;
    margin-top: 12px;
  }
  label:first-of-type { margin-top: 0; }
  input[type="text"], input[type="password"], input[type="number"], select {
    width: 100%;
    padding: 10px 12px;
    background: #0f1117;
    border: 1px solid #2a2d3a;
    border-radius: 8px;
    color: #e0e0e0;
    font-size: 0.95em;
    outline: none;
    transition: border-color 0.2s;
  }
  input:focus, select:focus { border-color: #7eb8ff; }
  .toggle-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 8px 0;
  }
  .toggle-row span { font-size: 0.9em; }
  .toggle {
    position: relative;
    width: 44px; height: 24px;
  }
  .toggle input { opacity: 0; width: 0; height: 0; }
  .slider {
    position: absolute; inset: 0;
    background: #2a2d3a;
    border-radius: 12px;
    cursor: pointer;
    transition: 0.3s;
  }
  .slider:before {
    content: "";
    position: absolute;
    width: 18px; height: 18px;
    left: 3px; bottom: 3px;
    background: #666;
    border-radius: 50%;
    transition: 0.3s;
  }
  .toggle input:checked + .slider { background: #2d5a3d; }
  .toggle input:checked + .slider:before {
    transform: translateX(20px);
    background: #4ade80;
  }
  .btn {
    display: block;
    width: 100%;
    padding: 14px;
    background: #7eb8ff;
    color: #0f1117;
    border: none;
    border-radius: 10px;
    font-size: 1em;
    font-weight: 600;
    cursor: pointer;
    margin-top: 20px;
    transition: background 0.2s;
  }
  .btn:hover { background: #a0ccff; }
  .btn-ota {
    background: transparent;
    border: 1px solid #2a2d3a;
    color: #888;
    margin-top: 12px;
    font-size: 0.85em;
  }
  .btn-ota:hover { border-color: #7eb8ff; color: #7eb8ff; }
  .msg {
    text-align: center;
    padding: 12px;
    border-radius: 8px;
    margin-top: 12px;
    display: none;
    font-size: 0.9em;
  }
  .msg.ok { display: block; background: #1a2e1a; color: #4ade80; }
  .msg.err { display: block; background: #2e1a1a; color: #ff6b6b; }
  .hint { font-size: 0.75em; color: #555; margin-top: 4px; }
  .section-label { font-size: 0.75em; color: #555; margin-top: 8px; margin-bottom: 4px; }
</style>
</head>
<body>
<div class="container">
  <h1>Info Display Hub</h1>
  <p class="subtitle">Configure your display &mdash; settings persist across reboots</p>

  <form id="configForm">
    <div class="card">
      <h2>WiFi</h2>
      <label>Network Name (SSID)</label>
      <input type="text" name="wifi_ssid" id="wifi_ssid">
      <label>Password</label>
      <input type="password" name="wifi_pass" id="wifi_pass">
    </div>

    <div class="card">
      <h2>Weather</h2>
      <label>OpenWeatherMap API Key</label>
      <input type="text" name="weather_key" id="weather_key">
      <p class="hint">Free at openweathermap.org/api</p>
      <label>City</label>
      <input type="text" name="weather_city" id="weather_city">
    </div>

    <div class="card">
      <h2>Home Assistant</h2>
      <label>HA URL</label>
      <input type="text" name="ha_url" id="ha_url" placeholder="http://192.168.1.100:8123">
      <label>Long-Lived Access Token</label>
      <input type="password" name="ha_token" id="ha_token">
      <p class="hint">Required for Humidity, Espresso, Backyard Temp, and Sump Pump screens</p>
    </div>

    <div class="card">
      <h2>Display</h2>
      <label>Rotation Interval (seconds)</label>
      <input type="number" name="rotate_sec" id="rotate_sec" min="2" max="60">
      <label>Timezone (POSIX)</label>
      <input type="text" name="timezone" id="timezone">
      <p class="hint">Default: CST6CDT,M3.2.0,M11.1.0 (Chicago)</p>

      <label style="margin-top:20px; margin-bottom:12px; color:#7eb8ff;">Active Screens</label>
      <div class="toggle-row">
        <span>Clock</span>
        <label class="toggle"><input type="checkbox" name="scr_clock" id="scr_clock"><span class="slider"></span></label>
      </div>
      <div class="toggle-row">
        <span>Weather</span>
        <label class="toggle"><input type="checkbox" name="scr_weather" id="scr_weather"><span class="slider"></span></label>
      </div>
      <div class="toggle-row">
        <span>Sports Scores</span>
        <label class="toggle"><input type="checkbox" name="scr_sports" id="scr_sports"><span class="slider"></span></label>
      </div>

      <p class="section-label">Home Assistant Screens</p>
      <div class="toggle-row">
        <span>Bedroom Humidity</span>
        <label class="toggle"><input type="checkbox" name="scr_humidity" id="scr_humidity"><span class="slider"></span></label>
      </div>
      <div class="toggle-row">
        <span>Espresso Stats</span>
        <label class="toggle"><input type="checkbox" name="scr_espresso" id="scr_espresso"><span class="slider"></span></label>
      </div>
      <div class="toggle-row">
        <span>Backyard Temp</span>
        <label class="toggle"><input type="checkbox" name="scr_backyard" id="scr_backyard"><span class="slider"></span></label>
      </div>
      <div class="toggle-row">
        <span>Sump Pump</span>
        <label class="toggle"><input type="checkbox" name="scr_sump" id="scr_sump"><span class="slider"></span></label>
      </div>
    </div>

    <button type="submit" class="btn">Save &amp; Reboot</button>
    <div class="msg" id="statusMsg"></div>
  </form>

  <a href="/update" class="btn btn-ota">Firmware Update (OTA)</a>
</div>

<script>
  // Load current config on page load
  fetch('/api/config').then(r=>r.json()).then(c=>{
    document.getElementById('wifi_ssid').value = c.wifi_ssid || '';
    document.getElementById('wifi_pass').value = c.wifi_pass || '';
    document.getElementById('weather_key').value = c.weather_key || '';
    document.getElementById('weather_city').value = c.weather_city || '';
    document.getElementById('ha_url').value = c.ha_url || '';
    document.getElementById('ha_token').value = c.ha_token || '';
    document.getElementById('rotate_sec').value = c.rotate_sec || 5;
    document.getElementById('timezone').value = c.timezone || '';
    document.getElementById('scr_clock').checked = c.scr_clock;
    document.getElementById('scr_weather').checked = c.scr_weather;
    document.getElementById('scr_sports').checked = c.scr_sports;
    document.getElementById('scr_humidity').checked = c.scr_humidity;
    document.getElementById('scr_espresso').checked = c.scr_espresso;
    document.getElementById('scr_backyard').checked = c.scr_backyard;
    document.getElementById('scr_sump').checked = c.scr_sump;
  });

  document.getElementById('configForm').addEventListener('submit', function(e) {
    e.preventDefault();
    const msg = document.getElementById('statusMsg');
    const data = {
      wifi_ssid: document.getElementById('wifi_ssid').value,
      wifi_pass: document.getElementById('wifi_pass').value,
      weather_key: document.getElementById('weather_key').value,
      weather_city: document.getElementById('weather_city').value,
      ha_url: document.getElementById('ha_url').value,
      ha_token: document.getElementById('ha_token').value,
      rotate_sec: parseInt(document.getElementById('rotate_sec').value),
      timezone: document.getElementById('timezone').value,
      scr_clock: document.getElementById('scr_clock').checked,
      scr_weather: document.getElementById('scr_weather').checked,
      scr_sports: document.getElementById('scr_sports').checked,
      scr_humidity: document.getElementById('scr_humidity').checked,
      scr_espresso: document.getElementById('scr_espresso').checked,
      scr_backyard: document.getElementById('scr_backyard').checked,
      scr_sump: document.getElementById('scr_sump').checked,
    };
    fetch('/api/config', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify(data)
    }).then(r => {
      if (r.ok) {
        msg.className = 'msg ok';
        msg.textContent = 'Saved! Rebooting device...';
      } else {
        msg.className = 'msg err';
        msg.textContent = 'Error saving config.';
      }
    }).catch(()=>{
      msg.className = 'msg err';
      msg.textContent = 'Connection lost (device may be rebooting).';
    });
  });
</script>
</body>
</html>
)rawliteral";

void setupWebServer() {
  // Serve config page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", CONFIG_HTML);
  });

  // GET current config as JSON
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["wifi_ssid"]    = config.wifi_ssid;
    doc["wifi_pass"]    = config.wifi_pass;
    doc["weather_key"]  = config.weather_api_key;
    doc["weather_city"] = config.weather_city;
    doc["ha_url"]       = config.ha_url;
    doc["ha_token"]     = config.ha_token;
    doc["rotate_sec"]   = config.rotation_seconds;
    doc["timezone"]     = config.timezone;
    doc["scr_clock"]    = config.screen_clock;
    doc["scr_weather"]  = config.screen_weather;
    doc["scr_sports"]   = config.screen_sports;
    doc["scr_humidity"] = config.screen_humidity;
    doc["scr_espresso"] = config.screen_espresso;
    doc["scr_backyard"] = config.screen_backyard;
    doc["scr_sump"]     = config.screen_sump;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST updated config
  AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(
    "/api/config",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      JsonObject obj = json.as<JsonObject>();
      strlcpy(config.wifi_ssid,       obj["wifi_ssid"] | "",       sizeof(config.wifi_ssid));
      strlcpy(config.wifi_pass,       obj["wifi_pass"] | "",       sizeof(config.wifi_pass));
      strlcpy(config.weather_api_key, obj["weather_key"] | "",     sizeof(config.weather_api_key));
      strlcpy(config.weather_city,    obj["weather_city"] | "",    sizeof(config.weather_city));
      strlcpy(config.ha_url,          obj["ha_url"] | "",          sizeof(config.ha_url));
      strlcpy(config.ha_token,        obj["ha_token"] | "",        sizeof(config.ha_token));
      config.rotation_seconds =       obj["rotate_sec"] | 5;
      strlcpy(config.timezone,        obj["timezone"] | "CST6CDT,M3.2.0,M11.1.0", sizeof(config.timezone));
      config.screen_clock    = obj["scr_clock"]    | true;
      config.screen_weather  = obj["scr_weather"]  | true;
      config.screen_sports   = obj["scr_sports"]   | true;
      config.screen_humidity = obj["scr_humidity"]  | false;
      config.screen_espresso = obj["scr_espresso"]  | false;
      config.screen_backyard = obj["scr_backyard"]  | false;
      config.screen_sump     = obj["scr_sump"]      | false;

      saveConfig();
      request->send(200, "application/json", "{\"status\":\"ok\"}");

      // Reboot after a short delay so the response can be sent
      delay(500);
      ESP.restart();
    }
  );
  server.addHandler(handler);

  // ElegantOTA handles /update endpoint automatically
  ElegantOTA.begin(&server);

  server.begin();
  Serial.println("Web server started on port 80");
}

// ============================================================
// WIFI CONNECTION
// ============================================================
bool connectWiFi() {
  if (strlen(config.wifi_ssid) == 0) return false;

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 140);
  tft.print("Connecting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifi_ssid, config.wifi_pass);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 120);
    tft.setTextSize(2);
    tft.print("WiFi Connected");
    tft.setCursor(10, 150);
    tft.setTextSize(1);
    tft.printf("IP: %s", WiFi.localIP().toString().c_str());
    tft.setCursor(10, 170);
    tft.print("Config: http://");
    tft.print(WiFi.localIP().toString());
    delay(3000);
    return true;
  }

  Serial.println("\nWiFi connection failed.");
  return false;
}

// ============================================================
// SETUP AP MODE (fallback if no WiFi configured)
// ============================================================
void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("InfoDisplayHub", "configure");

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 80);
  tft.print("Setup Mode");
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 120);
  tft.print("Connect to WiFi:");
  tft.setCursor(10, 140);
  tft.setTextColor(TFT_YELLOW);
  tft.print("InfoDisplayHub");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 160);
  tft.print("Password: configure");
  tft.setCursor(10, 200);
  tft.print("Then browse to:");
  tft.setCursor(10, 220);
  tft.setTextColor(TFT_GREEN);
  tft.print("http://192.168.4.1");

  Serial.println("AP Mode: InfoDisplayHub / configure");
  Serial.println("Config at: http://192.168.4.1");
}

// ============================================================
// NTP TIME
// ============================================================
void setupTime() {
  configTzTime(config.timezone, "pool.ntp.org", "time.nist.gov");
  Serial.println("NTP time sync started");
}

// ============================================================
// DATA FETCHING
// ============================================================

// --- Weather ---
struct WeatherData {
  float temp;
  float feels_like;
  int humidity;
  char description[64];
  char icon[8];
  bool valid;
} weatherData = {0, 0, 0, "", "", false};

unsigned long lastWeatherFetch = 0;
const unsigned long WEATHER_INTERVAL = 300000;  // 5 minutes

void fetchWeather() {
  if (strlen(config.weather_api_key) == 0 || strlen(config.weather_city) == 0) return;

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=";
  url += config.weather_city;
  url += "&appid=";
  url += config.weather_api_key;
  url += "&units=imperial";

  http.begin(url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    if (!deserializeJson(doc, payload)) {
      weatherData.temp       = doc["main"]["temp"];
      weatherData.feels_like = doc["main"]["feels_like"];
      weatherData.humidity   = doc["main"]["humidity"];
      strlcpy(weatherData.description, doc["weather"][0]["description"] | "N/A", sizeof(weatherData.description));
      strlcpy(weatherData.icon, doc["weather"][0]["icon"] | "01d", sizeof(weatherData.icon));
      weatherData.valid = true;
      Serial.printf("Weather: %.1fF, %s\n", weatherData.temp, weatherData.description);
    }
  }
  http.end();
}

// --- Home Assistant Data Structs ---

struct HumidityData {
  float humidity;
  bool valid;
} humidityData = {0, false};

struct EspressoData {
  int totalCoffees;
  int yesterdayShots;
  bool stateValid;
  bool historyValid;
} espressoData = {0, 0, false, false};

struct BackyardTempData {
  float temperature;
  bool valid;
} backyardData = {0, false};

struct SumpData {
  int runsLast24h;
  int dailyRuns[7];       // [0]=today, [6]=6 days ago
  char lastRunTime[24];   // "3h 14m ago"
  bool stateValid;
  bool historyValid;
} sumpData = {0, {0}, "", false, false};

unsigned long lastHAStateFetch   = 0;
unsigned long lastHAHistoryFetch = 0;
const unsigned long HA_STATE_INTERVAL   = 30000;   // 30 seconds
const unsigned long HA_HISTORY_INTERVAL = 600000;  // 10 minutes

// --- HA Helper: fetch a single entity state ---
bool fetchHAState(const char* entityId, char* state, size_t stateLen) {
  if (strlen(config.ha_url) == 0 || strlen(config.ha_token) == 0) return false;
  HTTPClient http;
  String url = String(config.ha_url) + "/api/states/" + entityId;
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + config.ha_token);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    if (!deserializeJson(doc, payload)) {
      strlcpy(state, doc["state"] | "unknown", stateLen);
      ok = true;
    }
  }
  http.end();
  return ok;
}

// --- Batch state fetches for all HA screens ---
void fetchHAStates() {
  char buf[64];

  // Humidity
  if (fetchHAState(HA_ENTITY_HUMIDITY, buf, sizeof(buf))) {
    humidityData.humidity = atof(buf);
    humidityData.valid = true;
    Serial.printf("HA Humidity: %.0f%%\n", humidityData.humidity);
  }

  // Espresso total
  if (fetchHAState(HA_ENTITY_ESPRESSO, buf, sizeof(buf))) {
    espressoData.totalCoffees = atoi(buf);
    espressoData.stateValid = true;
    Serial.printf("HA Espresso total: %d\n", espressoData.totalCoffees);
  }

  // Backyard temp
  if (fetchHAState(HA_ENTITY_BACKYARD, buf, sizeof(buf))) {
    backyardData.temperature = atof(buf);
    backyardData.valid = true;
    Serial.printf("HA Backyard: %.1fF\n", backyardData.temperature);
  }

  // Sump pump last cycle
  if (fetchHAState(HA_ENTITY_SUMP, buf, sizeof(buf))) {
    computeSumpTimeAgo(buf);
    sumpData.stateValid = true;
    Serial.printf("HA Sump last run: %s\n", sumpData.lastRunTime);
  }
}

// --- Compute "Xh Ym ago" from ISO timestamp ---
void computeSumpTimeAgo(const char* isoTimestamp) {
  int y, mo, d, h, mi, s;
  if (sscanf(isoTimestamp, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
    struct tm entryTm = {0};
    entryTm.tm_year = y - 1900;
    entryTm.tm_mon = mo - 1;
    entryTm.tm_mday = d;
    entryTm.tm_hour = h;
    entryTm.tm_min = mi;
    entryTm.tm_sec = s;
    // HA returns UTC timestamps, convert using timegm equivalent
    time_t entryTime = mktime(&entryTm);
    // Adjust for local vs UTC offset since mktime assumes local
    struct tm nowTm;
    if (!getLocalTime(&nowTm)) return;
    time_t nowTime = mktime(&nowTm);

    // Both are now in the same basis (local mktime), so compute diff
    // The entry timestamp is UTC, so adjust by our timezone offset
    struct tm utcNow;
    getLocalTime(&utcNow);
    time_t localNow = mktime(&utcNow);
    struct tm gmNow;
    gmtime_r(&localNow, &gmNow);
    time_t utcNowT = mktime(&gmNow);
    long tzOffsetSec = (long)(localNow - utcNowT);

    // Entry time in local = entryTime + tzOffset
    long diffSec = nowTime - (entryTime + tzOffsetSec);
    if (diffSec < 0) diffSec = 0;

    int hours = diffSec / 3600;
    int mins = (diffSec % 3600) / 60;

    if (hours >= 24) {
      snprintf(sumpData.lastRunTime, sizeof(sumpData.lastRunTime), "%dd %dh ago", hours / 24, hours % 24);
    } else if (hours > 0) {
      snprintf(sumpData.lastRunTime, sizeof(sumpData.lastRunTime), "%dh %dm ago", hours, mins);
    } else {
      snprintf(sumpData.lastRunTime, sizeof(sumpData.lastRunTime), "%dm ago", mins);
    }
  } else {
    strlcpy(sumpData.lastRunTime, "unknown", sizeof(sumpData.lastRunTime));
  }
}

// --- Espresso History: compute yesterday's shot count ---
void fetchEspressoHistory() {
  if (strlen(config.ha_url) == 0 || strlen(config.ha_token) == 0) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  // Compute yesterday start (local midnight) and end
  time_t now_t = mktime(&timeinfo);
  time_t todayMidnight = now_t - (timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec);
  time_t yesterdayStart = todayMidnight - 86400;
  time_t yesterdayEnd = todayMidnight;

  // Format as ISO strings in UTC for the API
  // HA history API accepts local time strings too
  char startStr[26], endStr[26];
  struct tm ts;
  localtime_r(&yesterdayStart, &ts);
  strftime(startStr, sizeof(startStr), "%Y-%m-%dT%H:%M:%S", &ts);
  localtime_r(&yesterdayEnd, &ts);
  strftime(endStr, sizeof(endStr), "%Y-%m-%dT%H:%M:%S", &ts);

  HTTPClient http;
  String url = String(config.ha_url) + "/api/history/period/" + startStr
    + "?filter_entity_id=" + HA_ENTITY_ESPRESSO
    + "&end_time=" + endStr
    + "&minimal_response&no_attributes";

  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + config.ha_token);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err && doc.is<JsonArray>() && doc.size() > 0 && doc[0].is<JsonArray>()) {
      JsonArray entries = doc[0].as<JsonArray>();
      if (entries.size() >= 2) {
        // Find first and last valid numeric states
        int first = -1, last = -1;
        for (size_t i = 0; i < entries.size(); i++) {
          const char* st = entries[i]["state"] | "";
          if (st[0] >= '0' && st[0] <= '9') {
            int val = atoi(st);
            if (first < 0) first = val;
            last = val;
          }
        }
        if (first >= 0 && last >= 0) {
          espressoData.yesterdayShots = last - first;
        } else {
          espressoData.yesterdayShots = 0;
        }
      } else {
        espressoData.yesterdayShots = 0;
      }
      espressoData.historyValid = true;
      Serial.printf("HA Espresso yesterday: %d shots\n", espressoData.yesterdayShots);
    }
  }
  http.end();
}

// --- Sump Pump History: 7-day daily run counts ---
void fetchSumpHistory() {
  if (strlen(config.ha_url) == 0 || strlen(config.ha_token) == 0) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  // Compute 7 days ago at local midnight
  time_t now_t = mktime(&timeinfo);
  time_t todayMidnight = now_t - (timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec);
  time_t weekStart = todayMidnight - (6 * 86400);

  char startStr[26];
  struct tm ts;
  localtime_r(&weekStart, &ts);
  strftime(startStr, sizeof(startStr), "%Y-%m-%dT%H:%M:%S", &ts);

  HTTPClient http;
  String url = String(config.ha_url) + "/api/history/period/" + startStr
    + "?filter_entity_id=" + HA_ENTITY_SUMP
    + "&minimal_response&no_attributes";

  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + config.ha_token);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err && doc.is<JsonArray>() && doc.size() > 0 && doc[0].is<JsonArray>()) {
      JsonArray entries = doc[0].as<JsonArray>();

      // Zero out daily counts
      memset(sumpData.dailyRuns, 0, sizeof(sumpData.dailyRuns));
      int runs24h = 0;

      // Skip the first entry (it's the initial state, not a cycle event)
      for (size_t i = 1; i < entries.size(); i++) {
        const char* lc = entries[i]["last_changed"] | "";
        int ey, emo, ed, eh, emi, es;
        if (sscanf(lc, "%d-%d-%dT%d:%d:%d", &ey, &emo, &ed, &eh, &emi, &es) == 6) {
          struct tm entryTm = {0};
          entryTm.tm_year = ey - 1900;
          entryTm.tm_mon = emo - 1;
          entryTm.tm_mday = ed;
          entryTm.tm_hour = eh;
          entryTm.tm_min = emi;
          entryTm.tm_sec = es;
          time_t entryTime = mktime(&entryTm);

          // Determine day bucket (entry timestamps from HA are in UTC, but
          // last_changed with no timezone is local for history period queries)
          int daysAgo = (todayMidnight - entryTime) / 86400;
          if (entryTime >= todayMidnight) daysAgo = 0;
          if (daysAgo >= 0 && daysAgo < 7) {
            sumpData.dailyRuns[daysAgo]++;
          }

          // Last 24h count
          if ((now_t - entryTime) < 86400 && (now_t - entryTime) >= 0) {
            runs24h++;
          }
        }
      }
      sumpData.runsLast24h = runs24h;
      sumpData.historyValid = true;
      Serial.printf("HA Sump: %d runs in 24h, daily=[", runs24h);
      for (int i = 0; i < 7; i++) Serial.printf("%d ", sumpData.dailyRuns[i]);
      Serial.println("]");
    }
  }
  http.end();
}

// ============================================================
// DISPLAY SCREENS
// ============================================================

// Color palette
#define CLR_BG       0x0000   // Black
#define CLR_PRIMARY  0x5D7F   // Soft blue
#define CLR_ACCENT   0xFE20   // Warm amber
#define CLR_DIM      0x4228   // Dark gray
#define CLR_TEXT     0xFFFF   // White
#define CLR_GREEN    0x2E89   // Muted green
#define CLR_WATER    0x04DF   // Water blue
#define CLR_COFFEE   0x8A22   // Coffee brown
#define CLR_COLD     0x001F   // Icy blue
#define CLR_HOT      0xF800   // Red

void drawScreenClock() {
  sprite.fillSprite(CLR_BG);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    sprite.drawString("No time sync", 20, 140);
    sprite.pushSprite(0, 0);
    return;
  }

  // Time (large)
  char timeBuf[10];
  strftime(timeBuf, sizeof(timeBuf), "%I:%M", &timeinfo);
  // Remove leading zero from hour
  char *timeStr = timeBuf;
  if (timeStr[0] == '0') timeStr++;

  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(4);
  int tw = sprite.textWidth(timeStr);
  sprite.drawString(timeStr, (SCREEN_WIDTH - tw) / 2, 90);

  // AM/PM
  char ampm[4];
  strftime(ampm, sizeof(ampm), "%p", &timeinfo);
  sprite.setTextSize(2);
  sprite.setTextColor(CLR_DIM);
  tw = sprite.textWidth(ampm);
  sprite.drawString(ampm, (SCREEN_WIDTH - tw) / 2, 135);

  // Seconds
  char secBuf[4];
  strftime(secBuf, sizeof(secBuf), ":%S", &timeinfo);
  sprite.setTextSize(2);
  sprite.setTextColor(CLR_PRIMARY);
  tw = sprite.textWidth(secBuf);
  sprite.drawString(secBuf, (SCREEN_WIDTH - tw) / 2, 160);

  // Date
  char dateBuf[32];
  strftime(dateBuf, sizeof(dateBuf), "%A", &timeinfo);
  sprite.setTextSize(2);
  sprite.setTextColor(CLR_ACCENT);
  tw = sprite.textWidth(dateBuf);
  sprite.drawString(dateBuf, (SCREEN_WIDTH - tw) / 2, 200);

  strftime(dateBuf, sizeof(dateBuf), "%b %d, %Y", &timeinfo);
  sprite.setTextSize(2);
  sprite.setTextColor(CLR_DIM);
  tw = sprite.textWidth(dateBuf);
  sprite.drawString(dateBuf, (SCREEN_WIDTH - tw) / 2, 225);

  // Decorative line
  sprite.drawFastHLine(30, 80, 110, CLR_DIM);
  sprite.drawFastHLine(30, 260, 110, CLR_DIM);

  sprite.pushSprite(0, 0);
}

void drawScreenWeather() {
  sprite.fillSprite(CLR_BG);

  // Header
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("WEATHER", 20, 20);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString(config.weather_city, 20, 42);

  sprite.drawFastHLine(20, 58, 130, CLR_DIM);

  if (!weatherData.valid) {
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    sprite.drawString("No data", 30, 140);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Check API key", 30, 165);
    sprite.pushSprite(0, 0);
    return;
  }

  // Temperature (large)
  char tempBuf[10];
  snprintf(tempBuf, sizeof(tempBuf), "%.0f", weatherData.temp);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(5);
  int tw = sprite.textWidth(tempBuf);
  sprite.drawString(tempBuf, (SCREEN_WIDTH - tw) / 2 - 10, 80);

  // Degree symbol
  sprite.setTextSize(2);
  sprite.drawString("F", (SCREEN_WIDTH + tw) / 2 - 5, 82);

  // Description
  sprite.setTextColor(CLR_ACCENT);
  sprite.setTextSize(2);
  // Capitalize first letter
  char desc[64];
  strlcpy(desc, weatherData.description, sizeof(desc));
  if (desc[0] >= 'a' && desc[0] <= 'z') desc[0] -= 32;
  tw = sprite.textWidth(desc);
  if (tw > SCREEN_WIDTH - 20) {
    sprite.setTextSize(1);
    tw = sprite.textWidth(desc);
  }
  sprite.drawString(desc, (SCREEN_WIDTH - tw) / 2, 150);

  // Details
  sprite.drawFastHLine(20, 185, 130, CLR_DIM);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Feels like", 20, 200);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  snprintf(tempBuf, sizeof(tempBuf), "%.0fF", weatherData.feels_like);
  sprite.drawString(tempBuf, 20, 215);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Humidity", 100, 200);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  snprintf(tempBuf, sizeof(tempBuf), "%d%%", weatherData.humidity);
  sprite.drawString(tempBuf, 100, 215);

  sprite.pushSprite(0, 0);
}

void drawScreenSports() {
  sprite.fillSprite(CLR_BG);

  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("SCORES", 20, 20);
  sprite.drawFastHLine(20, 42, 130, CLR_DIM);

  // Placeholder - replace with your preferred sports API
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Add a sports API", 20, 80);
  sprite.drawString("in the code to", 20, 95);
  sprite.drawString("show live scores", 20, 110);

  sprite.setTextColor(CLR_ACCENT);
  sprite.setTextSize(2);
  sprite.drawString("GO PACK GO", 20, 150);

  // Example layout for when you add real data:
  sprite.drawFastHLine(20, 185, 130, CLR_DIM);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Example layout:", 20, 195);

  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  sprite.drawString("GB", 20, 215);
  sprite.drawString("24", 70, 215);
  sprite.drawString("CHI", 20, 240);
  sprite.setTextColor(CLR_DIM);
  sprite.drawString("17", 70, 240);

  sprite.setTextColor(CLR_GREEN);
  sprite.setTextSize(1);
  sprite.drawString("FINAL", 110, 225);

  sprite.pushSprite(0, 0);
}

// ============================================================
// HA SCREEN 1: BEDROOM HUMIDITY
// ============================================================
void drawScreenHumidity() {
  sprite.fillSprite(CLR_BG);

  // Header
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("HUMIDITY", 20, 20);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Bedroom", 20, 42);
  sprite.drawFastHLine(20, 58, 130, CLR_DIM);

  if (!humidityData.valid) {
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    sprite.drawString("No data", 30, 140);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Check HA config", 30, 165);
    sprite.pushSprite(0, 0);
    return;
  }

  float h = humidityData.humidity;

  // Color based on humidity level
  uint16_t dropColor;
  const char* statusText;
  if (h < 30) {
    dropColor = CLR_ACCENT;  // Orange = too dry
    statusText = "Too Dry";
  } else if (h <= 60) {
    dropColor = CLR_WATER;   // Blue = comfortable
    statusText = "Comfortable";
  } else {
    dropColor = 0x0410;      // Dark teal = too humid
    statusText = "Too Humid";
  }

  // --- Water droplet icon ---
  int cx = 85;  // center X
  int bulbY = 130;
  int bulbR = 22;
  int tipY = 80;

  // Droplet body (filled circle + triangle)
  sprite.fillCircle(cx, bulbY, bulbR, dropColor);
  sprite.fillTriangle(cx - bulbR, bulbY, cx + bulbR, bulbY, cx, tipY, dropColor);

  // Water level inside droplet (animated waves)
  // Fill level based on humidity: 0% = bottom of bulb, 100% = top of droplet
  int fillTop = bulbY + bulbR - (int)((h / 100.0f) * (bulbY + bulbR - tipY));
  float phase = millis() / 300.0f;

  // Draw animated wave lines inside the droplet
  for (int wy = fillTop; wy <= bulbY + bulbR; wy++) {
    // Compute the horizontal extent of the droplet at this Y
    int halfWidth;
    if (wy >= bulbY - bulbR && wy <= bulbY + bulbR) {
      // In the circle portion
      float dy = (float)(wy - bulbY);
      halfWidth = (int)sqrtf(bulbR * bulbR - dy * dy);
    } else if (wy < bulbY && wy >= tipY) {
      // In the triangle portion
      float t = (float)(wy - tipY) / (float)(bulbY - tipY);
      halfWidth = (int)(t * bulbR);
    } else {
      continue;
    }

    // Wave offset
    float wave = sinf(phase + wy * 0.15f) * 2.0f;
    int x1 = cx - halfWidth + 1;
    int x2 = cx + halfWidth - 1;

    // Only draw the fill below the wave surface
    if (wy == fillTop) {
      // Draw the wave surface line
      for (int px = x1; px <= x2; px++) {
        float waveAtPx = sinf(phase + px * 0.08f) * 3.0f;
        if (wy + (int)waveAtPx <= fillTop + 2) {
          sprite.drawPixel(px, wy + (int)waveAtPx, CLR_TEXT);
        }
      }
    }
  }

  // Droplet outline (slightly brighter)
  sprite.drawCircle(cx, bulbY, bulbR, CLR_TEXT);
  // Triangle outline edges
  sprite.drawLine(cx - bulbR, bulbY, cx, tipY, CLR_TEXT);
  sprite.drawLine(cx + bulbR, bulbY, cx, tipY, CLR_TEXT);

  // Small shine highlight on droplet
  sprite.fillCircle(cx - 8, bulbY - 10, 3, CLR_TEXT);

  // --- Large humidity value ---
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(4);
  char humBuf[10];
  snprintf(humBuf, sizeof(humBuf), "%.0f%%", h);
  int tw = sprite.textWidth(humBuf);
  sprite.drawString(humBuf, (SCREEN_WIDTH - tw) / 2, 175);

  // --- Status text ---
  sprite.setTextColor(dropColor);
  sprite.setTextSize(2);
  tw = sprite.textWidth(statusText);
  sprite.drawString(statusText, (SCREEN_WIDTH - tw) / 2, 225);

  // Decorative line
  sprite.drawFastHLine(30, 260, 110, CLR_DIM);

  sprite.pushSprite(0, 0);
}

// ============================================================
// HA SCREEN 2: ESPRESSO STATS
// ============================================================
void drawScreenEspresso() {
  sprite.fillSprite(CLR_BG);

  // Header
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("ESPRESSO", 20, 20);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("De'Longhi Micra", 20, 42);
  sprite.drawFastHLine(20, 58, 130, CLR_DIM);

  if (!espressoData.stateValid) {
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    sprite.drawString("No data", 30, 140);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Check HA config", 30, 165);
    sprite.pushSprite(0, 0);
    return;
  }

  // --- Coffee cup graphic ---
  int cupX = 55, cupY = 85;
  int cupW = 50, cupH = 40;

  // Cup body
  sprite.fillRoundRect(cupX, cupY, cupW, cupH, 4, CLR_COFFEE);
  // Cup rim highlight
  sprite.drawFastHLine(cupX, cupY, cupW, CLR_ACCENT);
  sprite.drawFastHLine(cupX, cupY + 1, cupW, CLR_ACCENT);

  // Handle (right side)
  for (int a = -40; a <= 40; a++) {
    float rad = a * 3.14159f / 180.0f;
    int hx = cupX + cupW + (int)(10 * cosf(rad));
    int hy = cupY + cupH / 2 + (int)(12 * sinf(rad));
    sprite.drawPixel(hx, hy, CLR_COFFEE);
    sprite.drawPixel(hx + 1, hy, CLR_COFFEE);
  }

  // Saucer
  sprite.fillRoundRect(cupX - 10, cupY + cupH + 2, cupW + 20, 6, 3, CLR_DIM);

  // Coffee fill inside cup
  int fillH = cupH - 8;
  sprite.fillRect(cupX + 3, cupY + 6, cupW - 6, fillH, 0x4100);  // dark coffee color

  // --- Animated steam ---
  float t = millis() / 200.0f;
  for (int s = 0; s < 3; s++) {
    int baseX = cupX + 10 + s * 15;
    for (int y = cupY - 5; y > cupY - 25; y--) {
      float age = (float)(cupY - 5 - y) / 20.0f;  // 0 at bottom, 1 at top
      float wave = sinf(t + y * 0.25f + s * 1.5f) * (2.0f + age * 3.0f);
      // Fade out as steam rises
      uint16_t steamColor = (age < 0.5f) ? CLR_DIM : 0x2104;
      sprite.drawPixel(baseX + (int)wave, y, steamColor);
      sprite.drawPixel(baseX + (int)wave + 1, y, steamColor);
    }
  }

  // --- Total coffees (large) ---
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(4);
  char totalBuf[10];
  snprintf(totalBuf, sizeof(totalBuf), "%d", espressoData.totalCoffees);
  int tw = sprite.textWidth(totalBuf);
  sprite.drawString(totalBuf, (SCREEN_WIDTH - tw) / 2, 155);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  tw = sprite.textWidth("total coffees");
  sprite.drawString("total coffees", (SCREEN_WIDTH - tw) / 2, 195);

  // --- Yesterday's shots ---
  sprite.drawFastHLine(30, 215, 110, CLR_DIM);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Yesterday", 20, 230);

  if (espressoData.historyValid) {
    sprite.setTextColor(CLR_ACCENT);
    sprite.setTextSize(3);
    char yBuf[16];
    snprintf(yBuf, sizeof(yBuf), "%d", espressoData.yesterdayShots);
    sprite.drawString(yBuf, 20, 248);

    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    const char* shotLabel = (espressoData.yesterdayShots == 1) ? "shot" : "shots";
    sprite.drawString(shotLabel, 20 + sprite.textWidth(yBuf) * 3 + 8, 258);
  } else {
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("loading...", 20, 250);
  }

  sprite.pushSprite(0, 0);
}

// ============================================================
// HA SCREEN 3: BACKYARD TEMPERATURE
// ============================================================
void drawScreenBackyard() {
  sprite.fillSprite(CLR_BG);

  // Header
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("BACKYARD", 20, 20);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Temperature", 20, 42);
  sprite.drawFastHLine(20, 58, 130, CLR_DIM);

  if (!backyardData.valid) {
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    sprite.drawString("No data", 30, 140);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Check HA config", 30, 165);
    sprite.pushSprite(0, 0);
    return;
  }

  float temp = backyardData.temperature;

  // Color based on temperature
  uint16_t tempColor;
  const char* statusText;
  if (temp <= 32) {
    tempColor = CLR_COLD;
    statusText = "Freezing";
  } else if (temp <= 50) {
    tempColor = CLR_PRIMARY;
    statusText = "Cold";
  } else if (temp <= 70) {
    tempColor = CLR_GREEN;
    statusText = "Cool";
  } else if (temp <= 85) {
    tempColor = CLR_ACCENT;
    statusText = "Warm";
  } else {
    tempColor = CLR_HOT;
    statusText = "Hot";
  }

  // --- Thermometer graphic (left side) ---
  int thX = 30;     // center X of thermometer
  int thTop = 75;   // top of tube
  int thBot = 185;  // bottom of tube (where bulb starts)
  int tubeW = 8;    // half-width of tube
  int bulbR = 16;   // bulb radius

  // Tube outline
  sprite.fillRect(thX - tubeW, thTop, tubeW * 2, thBot - thTop, CLR_DIM);
  sprite.fillCircle(thX, thBot, bulbR, CLR_DIM);

  // Mercury fill
  // Map temp: -20F = empty, 120F = full
  float fillPct = constrain((temp + 20.0f) / 140.0f, 0.0f, 1.0f);
  int mercuryTop = thBot - (int)(fillPct * (thBot - thTop - 4));
  sprite.fillRect(thX - tubeW + 3, mercuryTop, tubeW * 2 - 6, thBot - mercuryTop, tempColor);
  sprite.fillCircle(thX, thBot, bulbR - 4, tempColor);

  // Tube outline border
  sprite.drawRect(thX - tubeW, thTop, tubeW * 2, thBot - thTop, CLR_TEXT);
  sprite.drawCircle(thX, thBot, bulbR, CLR_TEXT);

  // Scale marks
  for (int i = 0; i <= 4; i++) {
    int markY = thTop + 4 + i * ((thBot - thTop - 8) / 4);
    sprite.drawFastHLine(thX + tubeW + 2, markY, 6, CLR_DIM);
  }

  // Temperature labels on scale
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("120", thX + tubeW + 10, thTop + 2);
  sprite.drawString("0", thX + tubeW + 10, thBot - 8);

  // --- Large temperature value (right side) ---
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(5);
  char tempBuf[10];
  snprintf(tempBuf, sizeof(tempBuf), "%.0f", temp);
  int tw = sprite.textWidth(tempBuf);
  sprite.drawString(tempBuf, 75, 110);

  // Degree + F
  sprite.setTextSize(2);
  sprite.setTextColor(CLR_DIM);
  sprite.drawString("o", 75 + tw * 5 / 4 + 2, 105);
  sprite.setTextColor(CLR_TEXT);
  sprite.drawString("F", 75 + tw * 5 / 4 + 14, 110);

  // --- Status text ---
  sprite.setTextColor(tempColor);
  sprite.setTextSize(2);
  tw = sprite.textWidth(statusText);
  sprite.drawString(statusText, (SCREEN_WIDTH - tw) / 2, 230);

  sprite.drawFastHLine(30, 260, 110, CLR_DIM);

  sprite.pushSprite(0, 0);
}

// ============================================================
// HA SCREEN 4: SUMP PUMP MONITOR
// ============================================================
void drawScreenSump() {
  sprite.fillSprite(CLR_BG);

  // Header
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("SUMP PUMP", 15, 20);
  sprite.drawFastHLine(15, 42, 140, CLR_DIM);

  if (!sumpData.stateValid) {
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    sprite.drawString("No data", 30, 140);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Check HA config", 30, 165);
    sprite.pushSprite(0, 0);
    return;
  }

  // --- Pump icon (simple) ---
  // Small pipe/pump graphic
  sprite.fillRect(20, 55, 8, 20, CLR_PRIMARY);        // vertical pipe
  sprite.fillRect(20, 55, 25, 6, CLR_PRIMARY);         // horizontal pipe top
  sprite.fillRoundRect(40, 58, 16, 14, 3, CLR_ACCENT); // pump body
  sprite.fillRect(56, 62, 12, 6, CLR_PRIMARY);         // output pipe

  // --- 24-hour run count ---
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Last 24 Hours", 80, 55);

  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(3);
  char countBuf[10];
  snprintf(countBuf, sizeof(countBuf), "%d", sumpData.runsLast24h);
  sprite.drawString(countBuf, 80, 68);

  int cw = sprite.textWidth(countBuf);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString(sumpData.runsLast24h == 1 ? "run" : "runs", 80 + cw * 3 + 8, 78);

  // --- Last run time ---
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  char lastBuf[40];
  snprintf(lastBuf, sizeof(lastBuf), "Last run: %s", sumpData.lastRunTime);
  sprite.drawString(lastBuf, 15, 100);

  // --- 7-Day Bar Chart ---
  sprite.drawFastHLine(15, 120, 140, CLR_DIM);
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(1);
  sprite.drawString("7-Day History", 15, 128);

  if (!sumpData.historyValid) {
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Loading history...", 15, 180);
    sprite.pushSprite(0, 0);
    return;
  }

  int chartX = 12;
  int chartY = 145;
  int chartH = 130;
  int barW = 16;
  int gap = 4;

  // Find max for scaling
  int maxVal = 1;
  for (int i = 0; i < 7; i++) {
    if (sumpData.dailyRuns[i] > maxVal) maxVal = sumpData.dailyRuns[i];
  }

  // Baseline
  sprite.drawFastHLine(chartX, chartY + chartH, 7 * (barW + gap), CLR_DIM);

  // Get day-of-week names
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  for (int i = 0; i < 7; i++) {
    int dayIdx = 6 - i;  // 6=oldest on left, 0=today on right
    int x = chartX + i * (barW + gap);
    int barH = 0;
    if (maxVal > 0) {
      barH = (sumpData.dailyRuns[dayIdx] * (chartH - 20)) / maxVal;
    }
    if (sumpData.dailyRuns[dayIdx] > 0 && barH < 4) barH = 4;  // min visible height

    // Bar color: today = accent, others = primary
    uint16_t color = (dayIdx == 0) ? CLR_ACCENT : CLR_PRIMARY;

    if (barH > 0) {
      sprite.fillRect(x, chartY + chartH - barH, barW, barH, color);
    }

    // Day label below baseline
    // Compute day of week for this bar
    time_t now_t = mktime(&timeinfo);
    time_t dayT = now_t - dayIdx * 86400;
    struct tm dayTm;
    localtime_r(&dayT, &dayTm);
    const char* dayNames[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
    sprite.setTextColor((dayIdx == 0) ? CLR_ACCENT : CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString(dayNames[dayTm.tm_wday], x + 1, chartY + chartH + 4);

    // Count above bar (if > 0)
    if (sumpData.dailyRuns[dayIdx] > 0) {
      char cStr[4];
      snprintf(cStr, sizeof(cStr), "%d", sumpData.dailyRuns[dayIdx]);
      sprite.setTextColor(CLR_TEXT);
      sprite.setTextSize(1);
      sprite.drawString(cStr, x + 3, chartY + chartH - barH - 12);
    }
  }

  // "Today" indicator
  sprite.setTextColor(CLR_ACCENT);
  sprite.setTextSize(1);
  int todayX = chartX + 6 * (barW + gap);
  sprite.drawString("^", todayX + 5, chartY + chartH + 14);

  sprite.pushSprite(0, 0);
}

// ============================================================
// NETWORK INFO SCREEN (both buttons held)
// ============================================================
void drawScreenIP() {
  sprite.fillSprite(CLR_BG);

  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("NETWORK", 20, 20);
  sprite.drawFastHLine(20, 42, 130, CLR_DIM);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("IP Address", 20, 70);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  sprite.drawString(WiFi.localIP().toString(), 20, 85);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Signal Strength", 20, 120);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  char rssi[16];
  snprintf(rssi, sizeof(rssi), "%d dBm", WiFi.RSSI());
  sprite.drawString(rssi, 20, 135);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Web Config", 20, 180);
  sprite.setTextColor(CLR_GREEN);
  sprite.setTextSize(1);
  String configUrl = "http://" + WiFi.localIP().toString();
  sprite.drawString(configUrl, 20, 195);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("OTA Update", 20, 225);
  sprite.setTextColor(CLR_GREEN);
  sprite.drawString(configUrl + "/update", 20, 240);

  sprite.setTextColor(CLR_DIM);
  sprite.drawString("Free heap: " + String(ESP.getFreeHeap()), 20, 280);

  sprite.pushSprite(0, 0);
}

// ============================================================
// SCREEN ROTATION LOGIC
// ============================================================

typedef void (*ScreenDrawFunc)();

struct Screen {
  ScreenDrawFunc draw;
  bool *enabled;
};

Screen allScreens[] = {
  { drawScreenClock,    &config.screen_clock },
  { drawScreenWeather,  &config.screen_weather },
  { drawScreenSports,   &config.screen_sports },
  { drawScreenHumidity, &config.screen_humidity },
  { drawScreenEspresso, &config.screen_espresso },
  { drawScreenBackyard, &config.screen_backyard },
  { drawScreenSump,     &config.screen_sump },
};
const int NUM_SCREENS = sizeof(allScreens) / sizeof(Screen);

int currentScreen = 0;
unsigned long lastScreenChange = 0;
bool paused = false;

int getNextActiveScreen(int from, int direction) {
  for (int i = 1; i <= NUM_SCREENS; i++) {
    int idx = (from + i * direction + NUM_SCREENS) % NUM_SCREENS;
    if (*(allScreens[idx].enabled)) return idx;
  }
  return from;  // no other active screen
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Info Display Hub ===");

  // Initialize display
  tft.init();
  tft.setRotation(0);  // Portrait, USB-C at bottom
  tft.fillScreen(TFT_BLACK);

  // Create sprite (double-buffered drawing = no flicker)
  sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
  sprite.setTextWrap(false);

  // Buttons
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  // Load saved config
  loadConfig();

  // Connect WiFi or start AP mode
  if (strlen(config.wifi_ssid) > 0) {
    if (connectWiFi()) {
      setupTime();
      fetchWeather();
      fetchHAStates();
      fetchEspressoHistory();
      fetchSumpHistory();
    }
  } else {
    startAPMode();
  }

  // Start web server (works in both STA and AP mode)
  setupWebServer();

  // Find first active screen
  for (int i = 0; i < NUM_SCREENS; i++) {
    if (*(allScreens[i].enabled)) {
      currentScreen = i;
      break;
    }
  }

  lastScreenChange = millis();
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // --- Button handling ---
  static bool lastBtnLeft = HIGH, lastBtnRight = HIGH;
  static unsigned long lastBtnTime = 0;
  bool btnLeft  = digitalRead(BTN_LEFT);
  bool btnRight = digitalRead(BTN_RIGHT);

  if (now - lastBtnTime > 200) {  // debounce
    // Left button: previous screen
    if (btnLeft == LOW && lastBtnLeft == HIGH) {
      currentScreen = getNextActiveScreen(currentScreen, -1);
      lastScreenChange = now;
      lastBtnTime = now;
    }
    // Right button: next screen
    if (btnRight == LOW && lastBtnRight == HIGH) {
      currentScreen = getNextActiveScreen(currentScreen, 1);
      lastScreenChange = now;
      lastBtnTime = now;
    }
    // Both buttons held: show network info
    if (btnLeft == LOW && btnRight == LOW) {
      drawScreenIP();
      delay(5000);
      lastScreenChange = now;
      lastBtnTime = now;
    }
  }
  lastBtnLeft  = btnLeft;
  lastBtnRight = btnRight;

  // --- Auto-rotate screens ---
  if (!paused && (now - lastScreenChange >= (unsigned long)config.rotation_seconds * 1000)) {
    currentScreen = getNextActiveScreen(currentScreen, 1);
    lastScreenChange = now;
  }

  // --- Periodic data fetches ---
  if (WiFi.status() == WL_CONNECTED) {
    if (now - lastWeatherFetch >= WEATHER_INTERVAL) {
      fetchWeather();
      lastWeatherFetch = now;
    }
    if (now - lastHAStateFetch >= HA_STATE_INTERVAL) {
      fetchHAStates();
      lastHAStateFetch = now;
    }
    if (now - lastHAHistoryFetch >= HA_HISTORY_INTERVAL) {
      fetchEspressoHistory();
      fetchSumpHistory();
      lastHAHistoryFetch = now;
    }
  }

  // --- Draw current screen ---
  allScreens[currentScreen].draw();

  // ElegantOTA processing
  ElegantOTA.loop();

  delay(100);  // ~10 fps, keeps things smooth without burning CPU
}
