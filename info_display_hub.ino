/*
 * Info Display Hub - Lilygo T-Display S3
 * =======================================
 * A WiFi-connected rotating info display with:
 *   - NTP clock
 *   - Weather display (OpenWeatherMap)
 *   - Sports scores placeholder
 *   - Home Assistant sensor readout
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
  char ha_entity[64];       // HA entity to display, e.g. sensor.temperature
  int  rotation_seconds;    // seconds per screen
  char timezone[48];        // POSIX timezone string
  bool screen_clock;
  bool screen_weather;
  bool screen_sports;
  bool screen_ha;
} config;

void loadConfig() {
  prefs.begin("infohub", true);  // read-only
  strlcpy(config.wifi_ssid,      prefs.getString("wifi_ssid", "").c_str(),      sizeof(config.wifi_ssid));
  strlcpy(config.wifi_pass,      prefs.getString("wifi_pass", "").c_str(),      sizeof(config.wifi_pass));
  strlcpy(config.weather_api_key,prefs.getString("weather_key", "").c_str(),    sizeof(config.weather_api_key));
  strlcpy(config.weather_city,   prefs.getString("weather_city", "Chicago").c_str(), sizeof(config.weather_city));
  strlcpy(config.ha_url,         prefs.getString("ha_url", "").c_str(),         sizeof(config.ha_url));
  strlcpy(config.ha_token,       prefs.getString("ha_token", "").c_str(),       sizeof(config.ha_token));
  strlcpy(config.ha_entity,      prefs.getString("ha_entity", "").c_str(),      sizeof(config.ha_entity));
  config.rotation_seconds =      prefs.getInt("rotate_sec", 5);
  strlcpy(config.timezone,       prefs.getString("timezone", "CST6CDT,M3.2.0,M11.1.0").c_str(), sizeof(config.timezone));
  config.screen_clock   = prefs.getBool("scr_clock", true);
  config.screen_weather = prefs.getBool("scr_weather", true);
  config.screen_sports  = prefs.getBool("scr_sports", true);
  config.screen_ha      = prefs.getBool("scr_ha", false);
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
  prefs.putString("ha_entity",    config.ha_entity);
  prefs.putInt("rotate_sec",      config.rotation_seconds);
  prefs.putString("timezone",     config.timezone);
  prefs.putBool("scr_clock",      config.screen_clock);
  prefs.putBool("scr_weather",    config.screen_weather);
  prefs.putBool("scr_sports",     config.screen_sports);
  prefs.putBool("scr_ha",         config.screen_ha);
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
      <label>Entity ID</label>
      <input type="text" name="ha_entity" id="ha_entity" placeholder="sensor.living_room_temp">
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
      <div class="toggle-row">
        <span>Home Assistant</span>
        <label class="toggle"><input type="checkbox" name="scr_ha" id="scr_ha"><span class="slider"></span></label>
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
    document.getElementById('ha_entity').value = c.ha_entity || '';
    document.getElementById('rotate_sec').value = c.rotate_sec || 5;
    document.getElementById('timezone').value = c.timezone || '';
    document.getElementById('scr_clock').checked = c.scr_clock;
    document.getElementById('scr_weather').checked = c.scr_weather;
    document.getElementById('scr_sports').checked = c.scr_sports;
    document.getElementById('scr_ha').checked = c.scr_ha;
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
      ha_entity: document.getElementById('ha_entity').value,
      rotate_sec: parseInt(document.getElementById('rotate_sec').value),
      timezone: document.getElementById('timezone').value,
      scr_clock: document.getElementById('scr_clock').checked,
      scr_weather: document.getElementById('scr_weather').checked,
      scr_sports: document.getElementById('scr_sports').checked,
      scr_ha: document.getElementById('scr_ha').checked,
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
    doc["ha_entity"]    = config.ha_entity;
    doc["rotate_sec"]   = config.rotation_seconds;
    doc["timezone"]     = config.timezone;
    doc["scr_clock"]    = config.screen_clock;
    doc["scr_weather"]  = config.screen_weather;
    doc["scr_sports"]   = config.screen_sports;
    doc["scr_ha"]       = config.screen_ha;
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
      strlcpy(config.ha_entity,       obj["ha_entity"] | "",       sizeof(config.ha_entity));
      config.rotation_seconds =       obj["rotate_sec"] | 5;
      strlcpy(config.timezone,        obj["timezone"] | "CST6CDT,M3.2.0,M11.1.0", sizeof(config.timezone));
      config.screen_clock   = obj["scr_clock"]   | true;
      config.screen_weather = obj["scr_weather"] | true;
      config.screen_sports  = obj["scr_sports"]  | true;
      config.screen_ha      = obj["scr_ha"]      | false;

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

// --- Home Assistant ---
struct HAData {
  char state[64];
  char friendly_name[64];
  char unit[16];
  bool valid;
} haData = {"", "", "", false};

unsigned long lastHAFetch = 0;
const unsigned long HA_INTERVAL = 30000;  // 30 seconds

void fetchHA() {
  if (strlen(config.ha_url) == 0 || strlen(config.ha_token) == 0 || strlen(config.ha_entity) == 0) return;

  HTTPClient http;
  String url = String(config.ha_url) + "/api/states/" + config.ha_entity;

  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + config.ha_token);
  http.addHeader("Content-Type", "application/json");
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    if (!deserializeJson(doc, payload)) {
      strlcpy(haData.state, doc["state"] | "unknown", sizeof(haData.state));
      strlcpy(haData.friendly_name, doc["attributes"]["friendly_name"] | config.ha_entity, sizeof(haData.friendly_name));
      strlcpy(haData.unit, doc["attributes"]["unit_of_measurement"] | "", sizeof(haData.unit));
      haData.valid = true;
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
  // ESPN, The Score, or sportsdata.io all have free tiers
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

void drawScreenHA() {
  sprite.fillSprite(CLR_BG);

  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("HOME", 20, 20);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Home Assistant", 20, 42);
  sprite.drawFastHLine(20, 58, 130, CLR_DIM);

  if (!haData.valid) {
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    sprite.drawString("No data", 30, 140);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Check HA config", 30, 165);
    sprite.pushSprite(0, 0);
    return;
  }

  // Entity name
  sprite.setTextColor(CLR_ACCENT);
  sprite.setTextSize(2);
  int tw = sprite.textWidth(haData.friendly_name);
  if (tw > SCREEN_WIDTH - 20) sprite.setTextSize(1);
  sprite.drawString(haData.friendly_name, 20, 100);

  // State value (large)
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(4);
  char stateBuf[80];
  snprintf(stateBuf, sizeof(stateBuf), "%s%s", haData.state, haData.unit);
  tw = sprite.textWidth(stateBuf);
  if (tw > SCREEN_WIDTH - 20) {
    sprite.setTextSize(3);
    tw = sprite.textWidth(stateBuf);
  }
  sprite.drawString(stateBuf, (SCREEN_WIDTH - tw) / 2, 150);

  // Entity ID
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString(config.ha_entity, 20, 260);

  sprite.pushSprite(0, 0);
}

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
  { drawScreenClock,   &config.screen_clock },
  { drawScreenWeather, &config.screen_weather },
  { drawScreenSports,  &config.screen_sports },
  { drawScreenHA,      &config.screen_ha },
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
      fetchHA();
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
    if (now - lastHAFetch >= HA_INTERVAL) {
      fetchHA();
      lastHAFetch = now;
    }
  }

  // --- Draw current screen ---
  allScreens[currentScreen].draw();

  // ElegantOTA processing
  ElegantOTA.loop();

  delay(100);  // ~10 fps, keeps things smooth without burning CPU
}
