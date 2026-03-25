// v2.6 - 2026-03-24 - Add Now Brewing coffee screen
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
 *   - Ring Camera Snapshots (HA camera_proxy, cycles through cameras)
 *   - Now Brewing (coffee name + roaster image via URL)
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
 *   - TJpg_Decoder (by Bodmer)
 *   - PNGdec (by Larry Bank)
 *
 * Board setup in Arduino IDE:
 *   1. Add ESP32 board package via Board Manager
 *   2. Select "LilyGo T-Display-S3" or "ESP32S3 Dev Module"
 *   3. Set USB CDC On Boot: Enabled
 *   4. Set Flash Size: 16MB
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <math.h>
#include <TJpg_Decoder.h>
#include <PNGdec.h>

// ============================================================
// COLOR PALETTE (defined early so all functions can use them)
// ============================================================
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

// ============================================================
// DISPLAY SETUP
// ============================================================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 170

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

// Ring camera snapshot entities
const char* RING_CAM_ENTITIES[] = {
  "camera.front_door_snapshot",
  "camera.basement_door_snapshot",
  "camera.alley_garage_door_snapshot",
  "camera.gangway_snapshot",
  "camera.backyard_deck_snapshot",
};
const char* RING_CAM_NAMES[] = {
  "Front Door",
  "Basement Door",
  "Alley Garage",
  "Gangway",
  "Backyard Deck",
};
const int RING_CAM_COUNT = 5;

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
  bool screen_ring;
  bool screen_brewing;
  char coffee_name[64];
  char coffee_img[256];     // URL to JPEG image of coffee bag
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
  config.screen_ring     = prefs.getBool("scr_ring", false);
  config.screen_brewing  = prefs.getBool("scr_brew", false);
  strlcpy(config.coffee_name,    prefs.getString("cof_name", "").c_str(),    sizeof(config.coffee_name));
  strlcpy(config.coffee_img,     prefs.getString("cof_img", "").c_str(),     sizeof(config.coffee_img));
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
  prefs.putBool("scr_ring",       config.screen_ring);
  prefs.putBool("scr_brew",       config.screen_brewing);
  prefs.putString("cof_name",     config.coffee_name);
  prefs.putString("cof_img",      config.coffee_img);
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
<meta charset="UTF-8">
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

      <p class="section-label">Camera Screens</p>
      <div class="toggle-row">
        <span>Ring Cameras</span>
        <label class="toggle"><input type="checkbox" name="scr_ring" id="scr_ring"><span class="slider"></span></label>
      </div>

      <p class="section-label">Coffee Screen</p>
      <div class="toggle-row">
        <span>Now Brewing</span>
        <label class="toggle"><input type="checkbox" name="scr_brewing" id="scr_brewing"><span class="slider"></span></label>
      </div>
      <label>Coffee Name</label>
      <input type="text" name="coffee_name" id="coffee_name" placeholder="Ethiopian Yirgacheffe">
      <label>Coffee Image URL</label>
      <input type="text" name="coffee_img" id="coffee_img" placeholder="https://roaster.com/bag.jpg" oninput="updateCoffeePreview()">
      <p class="hint">Direct link to a JPEG or PNG image of the coffee bag</p>
      <div id="coffeePreview" style="margin-top:10px;text-align:center;display:none;">
        <img id="coffeePreviewImg" style="max-width:200px;max-height:200px;border-radius:8px;border:1px solid #2a2d3a;" />
        <p id="coffeePreviewStatus" style="color:#555;font-size:0.78em;margin-top:4px;"></p>
      </div>
    </div>

    <button type="submit" class="btn">Save &amp; Reboot</button>
    <div class="msg" id="statusMsg"></div>
  </form>

  <a href="/update" class="btn btn-ota">Firmware Update (OTA)</a>

  <div class="card">
    <h2>WiFi Scan Log <span style="color:#555;font-weight:normal;font-size:0.85em;">&mdash; recorded on failed connection</span></h2>
    <div id="wifiLogContent"><p style="color:#555;font-size:0.85em;">Loading...</p></div>
  </div>

</div>

<script>
  function updateCoffeePreview() {
    var url = document.getElementById('coffee_img').value.trim();
    var preview = document.getElementById('coffeePreview');
    var img = document.getElementById('coffeePreviewImg');
    var status = document.getElementById('coffeePreviewStatus');
    if (!url) { preview.style.display='none'; return; }
    preview.style.display='block';
    status.textContent='Loading...';
    img.onload = function() { status.textContent=img.naturalWidth+'x'+img.naturalHeight+' — OK'; };
    img.onerror = function() { status.textContent='Failed to load image'; };
    img.src = url;
  }

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
    document.getElementById('scr_ring').checked = c.scr_ring;
    document.getElementById('scr_brewing').checked = c.scr_brewing;
    document.getElementById('coffee_name').value = c.coffee_name || '';
    document.getElementById('coffee_img').value = c.coffee_img || '';
    updateCoffeePreview();
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
      scr_ring: document.getElementById('scr_ring').checked,
      scr_brewing: document.getElementById('scr_brewing').checked,
      coffee_name: document.getElementById('coffee_name').value,
      coffee_img: document.getElementById('coffee_img').value,
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

  // Load WiFi scan log
  fetch('/api/wifilog').then(r=>r.json()).then(d=>{
    const el = document.getElementById('wifiLogContent');
    if (!d.count) {
      el.innerHTML = '<p style="color:#555;font-size:0.85em;">No scan log yet &mdash; recorded automatically when WiFi connection fails.</p>';
      return;
    }
    let html = '<p style="color:#888;font-size:0.78em;margin-bottom:12px;">Target: <span style="color:#fbbf24;">' + d.target + '</span> &mdash; ' + d.count + ' networks saved</p>';
    html += '<table style="width:100%;border-collapse:collapse;font-size:0.82em;">';
    html += '<tr style="color:#555;border-bottom:1px solid #2a2d3a;"><th style="text-align:left;padding:4px 4px 4px 0;">SSID</th><th style="text-align:left;padding:4px 8px;">BSSID</th><th style="text-align:right;padding:4px 0;">dBm</th></tr>';
    d.networks.forEach(n=>{
      const isTarget = n.ssid === d.target;
      const rc = n.rssi >= -50 ? '#4ade80' : n.rssi >= -65 ? '#fbbf24' : n.rssi >= -75 ? '#f97316' : '#ef4444';
      const ssidColor = isTarget ? '#4ade80' : '#e0e0e0';
      const rowBg = isTarget ? 'background:#162316;' : '';
      html += '<tr style="border-bottom:1px solid #1a1d27;' + rowBg + '">';
      html += '<td style="padding:6px 4px 6px 0;color:' + ssidColor + ';">' + (n.ssid || '<em style="color:#444">(hidden)</em>') + (isTarget ? ' &#10004;' : '') + '</td>';
      html += '<td style="padding:6px 8px;color:#668;font-family:monospace;font-size:0.9em;">' + n.bssid + '</td>';
      html += '<td style="text-align:right;padding:6px 0;color:' + rc + ';font-weight:bold;">' + n.rssi + '</td>';
      html += '</tr>';
    });
    html += '</table>';
    el.innerHTML = html;
  }).catch(()=>{
    document.getElementById('wifiLogContent').innerHTML = '<p style="color:#555;font-size:0.85em;">Could not load WiFi log.</p>';
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
    doc["scr_ring"]     = config.screen_ring;
    doc["scr_brewing"]  = config.screen_brewing;
    doc["coffee_name"]  = config.coffee_name;
    doc["coffee_img"]   = config.coffee_img;
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
      config.screen_ring     = obj["scr_ring"]      | false;
      config.screen_brewing  = obj["scr_brewing"]   | false;
      strlcpy(config.coffee_name,     obj["coffee_name"] | "",     sizeof(config.coffee_name));
      strlcpy(config.coffee_img,      obj["coffee_img"] | "",      sizeof(config.coffee_img));

      saveConfig();
      request->send(200, "application/json", "{\"status\":\"ok\"}");

      // Reboot after a short delay so the response can be sent
      delay(500);
      ESP.restart();
    }
  );
  server.addHandler(handler);

  // GET saved WiFi scan log from NVS
  server.on("/api/wifilog", HTTP_GET, [](AsyncWebServerRequest *request) {
    prefs.begin("wifilog", true);
    int cnt = prefs.getInt("wl_cnt", 0);
    JsonDocument doc;
    doc["count"]  = cnt;
    doc["target"] = config.wifi_ssid;
    JsonArray nets = doc["networks"].to<JsonArray>();
    for (int i = 0; i < cnt; i++) {
      char keyS[10], keyB[10], keyR[10];
      snprintf(keyS, sizeof(keyS), "wl%dssid", i);
      snprintf(keyB, sizeof(keyB), "wl%dbssid", i);
      snprintf(keyR, sizeof(keyR), "wl%drssi", i);
      JsonObject net = nets.add<JsonObject>();
      net["ssid"]  = prefs.getString(keyS, "");
      net["bssid"] = prefs.getString(keyB, "");
      net["rssi"]  = prefs.getInt(keyR, 0);
    }
    prefs.end();
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // ElegantOTA handles /update endpoint automatically
  ElegantOTA.begin(&server);

  server.begin();
  Serial.println("Web server started on port 80");
}

void drawWifiScan();  // forward declaration

// ============================================================
// WIFI CONNECTION
// ============================================================
bool connectWiFi() {
  if (strlen(config.wifi_ssid) == 0) return false;

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 70);
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
    tft.setCursor(10, 55);
    tft.setTextSize(2);
    tft.print("WiFi Connected");
    tft.setCursor(10, 85);
    tft.setTextSize(1);
    tft.printf("IP: %s", WiFi.localIP().toString().c_str());
    tft.setCursor(10, 100);
    tft.print("Config: http://");
    tft.print(WiFi.localIP().toString());
    delay(3000);
    return true;
  }

  Serial.println("\nWiFi connection failed. Scanning networks...");
  drawWifiScan();
  return false;
}

// Show all visible WiFi networks with slow scroll — called on connection failure
void drawWifiScan() {
  // Scanning splash
  sprite.fillSprite(CLR_BG);
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("WIFI SCAN", 8, 8);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Scanning for networks...", 8, 32);
  sprite.pushSprite(0, 0);

  // Disconnect cleanly so the radio is free to scan
  WiFi.disconnect(true);
  delay(500);

  int n = WiFi.scanNetworks();
  Serial.printf("WiFi scan: %d networks found\n", n);

  if (n <= 0) {
    sprite.fillSprite(CLR_BG);
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    sprite.drawString("No networks found", 8, 65);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Device may be out of range", 8, 92);
    sprite.pushSprite(0, 0);
    delay(15000);
    return;
  }

  // Sort indices by RSSI descending (strongest first)
  const int MAX_NETS = 25;
  int cnt = min(n, MAX_NETS);
  int order[MAX_NETS];
  for (int i = 0; i < cnt; i++) order[i] = i;
  for (int i = 0; i < cnt - 1; i++)
    for (int j = 0; j < cnt - i - 1; j++)
      if (WiFi.RSSI(order[j]) < WiFi.RSSI(order[j + 1])) {
        int t = order[j]; order[j] = order[j + 1]; order[j + 1] = t;
      }

  // Save top 7 to NVS so it can be reviewed later
  prefs.begin("wifilog", false);
  int saveCount = min(cnt, 7);
  prefs.putInt("wl_cnt", saveCount);
  for (int i = 0; i < saveCount; i++) {
    int idx = order[i];
    char keyS[10], keyB[10], keyR[10];
    snprintf(keyS, sizeof(keyS), "wl%dssid", i);
    snprintf(keyB, sizeof(keyB), "wl%dbssid", i);
    snprintf(keyR, sizeof(keyR), "wl%drssi", i);
    prefs.putString(keyS, WiFi.SSID(idx));
    uint8_t b[6];
    memcpy(b, WiFi.BSSID(idx), 6);
    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             b[0], b[1], b[2], b[3], b[4], b[5]);
    prefs.putString(keyB, bssidStr);
    prefs.putInt(keyR, WiFi.RSSI(idx));
  }
  prefs.end();
  Serial.printf("WiFi scan log saved (%d networks)\n", saveCount);

  // Log everything to Serial
  for (int i = 0; i < cnt; i++) {
    int idx = order[i];
    uint8_t* b = WiFi.BSSID(idx);
    Serial.printf("  [%2d] %-28s  %02X:%02X:%02X:%02X:%02X:%02X  %d dBm  ch%d\n",
                  i + 1, WiFi.SSID(idx).c_str(),
                  b[0], b[1], b[2], b[3], b[4], b[5],
                  WiFi.RSSI(idx), WiFi.channel(idx));
  }

  const int VISIBLE  = 5;     // rows shown at once
  const int ROW_H    = 24;    // px per network entry
  const int HDR_H    = 44;    // px reserved for header
  const int PAUSE_MS = 2500;  // ms between scroll steps

  unsigned long loopStart = millis();
  int startIdx = 0;

  // Scroll continuously for 90 seconds then fall through to AP mode
  while (millis() - loopStart < 90000) {
    sprite.fillSprite(CLR_BG);

    // Header
    sprite.setTextColor(CLR_PRIMARY);
    sprite.setTextSize(2);
    sprite.drawString("WIFI SCAN", 8, 8);

    sprite.setTextColor(CLR_ACCENT);
    sprite.setTextSize(1);
    sprite.drawString(String("Target: ") + config.wifi_ssid, 8, 30);

    // Position counter top-right
    char countBuf[12];
    snprintf(countBuf, sizeof(countBuf), "%d / %d", startIdx + 1, cnt);
    int cw = sprite.textWidth(countBuf);
    sprite.setTextColor(CLR_DIM);
    sprite.drawString(countBuf, SCREEN_WIDTH - cw - 8, 30);

    sprite.drawFastHLine(0, HDR_H - 2, SCREEN_WIDTH, CLR_DIM);

    // Network rows — wrap around so it loops continuously
    for (int i = 0; i < VISIBLE; i++) {
      int ni  = (startIdx + i) % cnt;
      int idx = order[ni];
      bool isTarget = (WiFi.SSID(idx) == String(config.wifi_ssid));
      int y = HDR_H + i * ROW_H;

      // Row 1: SSID + RSSI
      String ssid = WiFi.SSID(idx);
      if (ssid.length() == 0) ssid = "(hidden)";
      if (ssid.length() > 22) ssid = ssid.substring(0, 21) + "~";
      char row1[52];
      snprintf(row1, sizeof(row1), "%-22s %d dBm", ssid.c_str(), WiFi.RSSI(idx));
      sprite.setTextColor(isTarget ? CLR_GREEN : CLR_TEXT);
      sprite.setTextSize(1);
      sprite.drawString(row1, 8, y);

      // Row 2: BSSID + channel
      uint8_t bssid[6];
      memcpy(bssid, WiFi.BSSID(idx), 6);
      char row2[48];
      snprintf(row2, sizeof(row2), "%02X:%02X:%02X:%02X:%02X:%02X  ch%d",
               bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
               WiFi.channel(idx));
      sprite.setTextColor(isTarget ? CLR_PRIMARY : CLR_DIM);
      sprite.drawString(row2, 8, y + 12);
    }

    sprite.pushSprite(0, 0);
    delay(PAUSE_MS);
    startIdx = (startIdx + 1) % cnt;
  }
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
  tft.setCursor(10, 12);
  tft.print("Setup Mode");
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 48);
  tft.print("Connect to WiFi:");
  tft.setCursor(10, 62);
  tft.setTextColor(TFT_YELLOW);
  tft.print("InfoDisplayHub");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 80);
  tft.print("Password: configure");
  tft.setCursor(10, 108);
  tft.print("Then browse to:");
  tft.setCursor(10, 122);
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

// Ring camera snapshot data
struct RingCamData {
  uint8_t *jpegBuf;      // PSRAM-allocated JPEG buffer
  size_t   jpegLen;      // actual JPEG byte count
  int      currentCam;   // index into RING_CAM_ENTITIES[]
  bool     valid;        // true if jpegBuf has valid data
} ringCamData = { nullptr, 0, 0, false };

unsigned long lastRingCamFetch = 0;
const unsigned long RING_CAM_INTERVAL = 60000;  // 60s refresh when paused on screen
bool ringScreenWasActive = false;  // tracks transitions to Ring screen

// Now Brewing coffee image data
struct BrewingData {
  uint8_t *imgBuf;
  size_t   imgLen;
  uint16_t imgW, imgH;     // decoded dimensions (after scaling)
  uint8_t  scale;           // scale factor used (JPEG: 1/2/4/8, PNG: computed)
  bool     valid;
  bool     isPng;           // true=PNG, false=JPEG
} brewingData = { nullptr, 0, 0, 0, 1, false, false };

PNG png;  // PNGdec instance for coffee image

unsigned long lastBrewingFetch = 0;
const unsigned long BREWING_INTERVAL = 600000;  // 10 min refresh
bool brewingScreenWasActive = false;
char lastBrewingUrl[256] = "";  // detect URL changes

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
void computeSumpTimeAgo(const char* isoTimestamp);  // forward declaration

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

// --- Ring camera snapshot fetch ---
bool fetchRingSnapshot() {
  if (strlen(config.ha_url) == 0 || strlen(config.ha_token) == 0) {
    Serial.println("Ring: No HA URL or token configured");
    return false;
  }

  // Allocate buffer on first call (try PSRAM first, fall back to heap)
  if (ringCamData.jpegBuf == nullptr) {
    ringCamData.jpegBuf = (uint8_t*)ps_malloc(65536);
    if (ringCamData.jpegBuf == nullptr) {
      Serial.println("Ring: PSRAM alloc failed, trying heap...");
      ringCamData.jpegBuf = (uint8_t*)malloc(65536);
    }
    if (ringCamData.jpegBuf == nullptr) {
      Serial.println("Ring: Buffer alloc failed!");
      return false;
    }
    Serial.println("Ring: Buffer allocated OK");
  }

  HTTPClient http;
  String url = String(config.ha_url) + "/api/camera_proxy/" + RING_CAM_ENTITIES[ringCamData.currentCam];
  Serial.printf("Ring: Fetching %s\n", url.c_str());
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + config.ha_token);
  http.setTimeout(10000);

  int code = http.GET();
  bool ok = false;

  if (code == 200) {
    int len = http.getSize();
    Serial.printf("Ring: HTTP 200, Content-Length: %d\n", len);

    // Use getString() to get the full response, then memcpy
    // This is simpler and more reliable than manual stream reading
    WiFiClient *stream = http.getStreamPtr();
    size_t bytesRead = 0;
    size_t maxLen = 65536;
    size_t toRead = (len > 0 && (size_t)len <= maxLen) ? (size_t)len : maxLen;

    unsigned long start = millis();
    while (bytesRead < toRead && millis() - start < 10000) {
      if (stream->available()) {
        size_t remain = toRead - bytesRead;
        int avail = stream->available();
        int chunk = (avail < (int)remain) ? avail : (int)remain;
        int got = stream->readBytes(ringCamData.jpegBuf + bytesRead, chunk);
        bytesRead += got;
      } else if (!stream->connected()) {
        break;  // server closed connection, we have what we have
      } else {
        delay(1);  // yield to avoid tight loop while waiting for data
      }
    }

    if (bytesRead > 0) {
      ringCamData.jpegLen = bytesRead;
      ringCamData.valid = true;
      ok = true;
      Serial.printf("Ring: Got %s (%d bytes in %lums)\n",
        RING_CAM_NAMES[ringCamData.currentCam], bytesRead, millis() - start);
    } else {
      Serial.println("Ring: No bytes received");
    }
  } else {
    Serial.printf("Ring: HTTP %d for %s\n", code, RING_CAM_ENTITIES[ringCamData.currentCam]);
  }

  http.end();
  return ok;
}

// --- Now Brewing image fetch ---
bool fetchBrewingImage() {
  if (strlen(config.coffee_img) == 0) return false;

  // Allocate buffer on first call (384KB for large PNG images, PSRAM only)
  if (brewingData.imgBuf == nullptr) {
    brewingData.imgBuf = (uint8_t*)ps_malloc(393216);
    if (brewingData.imgBuf == nullptr) {
      Serial.println("Brew: PSRAM alloc failed for 384KB!");
      brewingData.imgBuf = (uint8_t*)ps_malloc(131072);  // try 128KB fallback
    }
    if (brewingData.imgBuf == nullptr) {
      Serial.println("Brew: Buffer alloc failed!");
      return false;
    }
  }

  HTTPClient http;
  Serial.printf("Brew: Fetching %s\n", config.coffee_img);

  // Support HTTPS URLs (skip certificate verification for simplicity)
  WiFiClientSecure *secureClient = nullptr;
  if (strncmp(config.coffee_img, "https", 5) == 0) {
    secureClient = new WiFiClientSecure();
    secureClient->setInsecure();  // skip cert verification
    http.begin(*secureClient, config.coffee_img);
  } else {
    http.begin(config.coffee_img);
  }
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "ESP32-InfoDisplay/1.0");

  int code = http.GET();
  bool ok = false;

  if (code == 200) {
    int len = http.getSize();
    Serial.printf("Brew: HTTP 200, Content-Length: %d\n", len);

    WiFiClient *stream = http.getStreamPtr();
    size_t bytesRead = 0;
    size_t maxLen = 393216;  // 384KB
    size_t toRead = (len > 0 && (size_t)len <= maxLen) ? (size_t)len : maxLen;

    unsigned long start = millis();
    while (bytesRead < toRead && millis() - start < 15000) {
      if (stream->available()) {
        size_t remain = toRead - bytesRead;
        int avail = stream->available();
        int chunk = (avail < (int)remain) ? avail : (int)remain;
        int got = stream->readBytes(brewingData.imgBuf + bytesRead, chunk);
        bytesRead += got;
      } else if (!stream->connected()) {
        break;
      } else {
        delay(1);
      }
    }

    if (bytesRead > 0) {
      brewingData.imgLen = bytesRead;

      // Detect format by magic bytes (PNG: 0x89504E47, JPEG: 0xFFD8)
      bool isPng = (bytesRead >= 4 && brewingData.imgBuf[0] == 0x89 &&
                    brewingData.imgBuf[1] == 0x50 && brewingData.imgBuf[2] == 0x4E &&
                    brewingData.imgBuf[3] == 0x47);
      brewingData.isPng = isPng;

      uint16_t rawW = 0, rawH = 0;
      if (isPng) {
        // PNG: read dimensions from IHDR chunk (bytes 16-23)
        if (bytesRead >= 24) {
          rawW = (brewingData.imgBuf[16] << 24) | (brewingData.imgBuf[17] << 16) |
                 (brewingData.imgBuf[18] << 8) | brewingData.imgBuf[19];
          rawH = (brewingData.imgBuf[20] << 24) | (brewingData.imgBuf[21] << 16) |
                 (brewingData.imgBuf[22] << 8) | brewingData.imgBuf[23];
        }
        Serial.printf("Brew: PNG %dx%d (%d bytes)\n", rawW, rawH, bytesRead);
      } else {
        TJpgDec.getJpgSize(&rawW, &rawH, brewingData.imgBuf, brewingData.imgLen);
        Serial.printf("Brew: JPEG %dx%d (%d bytes)\n", rawW, rawH, bytesRead);
      }

      // Pick scale to fit in 150x170 area
      if (rawW <= 150 && rawH <= 170) {
        brewingData.scale = 1;
      } else if (rawW <= 300 && rawH <= 340) {
        brewingData.scale = 2;
      } else if (rawW <= 600 && rawH <= 680) {
        brewingData.scale = 4;
      } else {
        brewingData.scale = 8;
      }
      brewingData.imgW = rawW / brewingData.scale;
      brewingData.imgH = rawH / brewingData.scale;
      brewingData.valid = true;
      ok = true;
      Serial.printf("Brew: Scale 1/%d -> %dx%d (%s)\n",
        brewingData.scale, brewingData.imgW, brewingData.imgH, isPng ? "PNG" : "JPEG");
    } else {
      Serial.println("Brew: No bytes received");
    }
  } else {
    Serial.printf("Brew: HTTP %d\n", code);
  }

  http.end();
  if (secureClient) delete secureClient;
  if (ok) strlcpy(lastBrewingUrl, config.coffee_img, sizeof(lastBrewingUrl));
  return ok;
}

// ============================================================
// DISPLAY SCREENS
// ============================================================

// Global offset for JPEG decode callback positioning
int16_t jpgOffsetX = 0, jpgOffsetY = 0;

// TJpg_Decoder callback: render decoded JPEG blocks into sprite
// Uses jpgOffsetX/Y globals for positioning (set before calling drawJpg)
bool jpgDrawCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  int16_t ax = x + jpgOffsetX;
  int16_t ay = y + jpgOffsetY;
  if (ay + (int16_t)h <= 0 || ay >= SCREEN_HEIGHT) return true;
  if (ax + (int16_t)w <= 0 || ax >= SCREEN_WIDTH) return true;

  for (int16_t row = 0; row < (int16_t)h; row++) {
    int16_t sy = ay + row;
    if (sy < 0 || sy >= SCREEN_HEIGHT) continue;
    for (int16_t col = 0; col < (int16_t)w; col++) {
      int16_t sx = ax + col;
      if (sx >= 0 && sx < SCREEN_WIDTH) {
        sprite.drawPixel(sx, sy, bitmap[row * w + col]);
      }
    }
  }
  return true;
}

// PNGdec callback: render decoded PNG lines into sprite
// Uses jpgOffsetX/Y globals and brewingData.scale for positioning/scaling
// Static buffer sized for large source PNGs (scaled down during rendering)
static uint16_t pngLineBuffer[1024];  // supports source PNGs up to 1024px wide

int pngDrawCallback(PNGDRAW *pDraw) {
  if (pDraw->iWidth > 1024) return 1;  // skip lines wider than our buffer
  int scale = brewingData.scale;
  if (pDraw->y % scale != 0) return 1;  // skip rows for scaling

  int16_t sy = (pDraw->y / scale) + jpgOffsetY;
  if (sy < 0 || sy >= SCREEN_HEIGHT) return 1;

  // Decode the PNG line to RGB565
  png.getLineAsRGB565(pDraw, pngLineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);

  // Draw scaled pixels
  int srcW = pDraw->iWidth;
  for (int srcX = 0; srcX < srcW; srcX += scale) {
    int16_t sx = (srcX / scale) + jpgOffsetX;
    if (sx >= 0 && sx < SCREEN_WIDTH) {
      sprite.drawPixel(sx, sy, pngLineBuffer[srcX]);
    }
  }
  return 1;  // continue decoding
}

// (Color palette defined at top of file)

void drawScreenClock() {
  sprite.fillSprite(CLR_BG);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    int tw = sprite.textWidth("No time sync");
    sprite.drawString("No time sync", (SCREEN_WIDTH - tw) / 2, 75);
    sprite.pushSprite(0, 0);
    return;
  }

  // Time — size 8 (48px wide × 64px tall per char), pseudo-bold
  char timeBuf[10];
  strftime(timeBuf, sizeof(timeBuf), "%I:%M", &timeinfo);
  char *timeStr = timeBuf;
  if (timeStr[0] == '0') timeStr++;

  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(8);
  int tw = sprite.textWidth(timeStr);
  int timeX = (SCREEN_WIDTH - tw) / 2;
  sprite.drawString(timeStr, timeX,     10);
  sprite.drawString(timeStr, timeX + 1, 10);  // pseudo-bold

  // Divider
  sprite.drawFastHLine(20, 84, SCREEN_WIDTH - 40, CLR_DIM);

  // :SS and AM/PM centered on one row below time
  char secBuf[4];
  strftime(secBuf, sizeof(secBuf), ":%S", &timeinfo);
  char ampm[4];
  strftime(ampm, sizeof(ampm), "%p", &timeinfo);
  sprite.setTextSize(3);
  int secW  = sprite.textWidth(secBuf);
  int ampmW = sprite.textWidth(ampm);
  int rowX  = (SCREEN_WIDTH - secW - 14 - ampmW) / 2;
  sprite.setTextColor(CLR_PRIMARY);
  sprite.drawString(secBuf, rowX, 93);
  sprite.setTextColor(CLR_DIM);
  sprite.drawString(ampm, rowX + secW + 14, 93);

  // Divider + date
  sprite.drawFastHLine(20, 126, SCREEN_WIDTH - 40, CLR_DIM);

  char dateBuf[48];
  strftime(dateBuf, sizeof(dateBuf), "%A,  %B %d  %Y", &timeinfo);
  sprite.setTextColor(CLR_ACCENT);
  sprite.setTextSize(2);
  tw = sprite.textWidth(dateBuf);
  if (tw > SCREEN_WIDTH - 20) {
    sprite.setTextSize(1);
    tw = sprite.textWidth(dateBuf);
  }
  sprite.drawString(dateBuf, (SCREEN_WIDTH - tw) / 2, 137);

  sprite.pushSprite(0, 0);
}

// Draw a simple weather icon centered at (cx, cy), radius r
// icon = OWM icon code e.g. "01d", "10n"
void drawWeatherIcon(int cx, int cy, int r, const char* icon) {
  char c0 = icon[0], c1 = icon[1];
  const uint16_t CLR_CLOUD_C = 0xC618; // light gray — visible on dark background

  // Fluffy cloud: 3 overlapping bumps + filled base, cr = approximate half-width
  auto drawCloud = [&](int x, int y, int cr) {
    int topR  = cr * 10 / 16;
    int sideR = cr *  7 / 16;
    sprite.fillCircle(x,           y - cr/4,  topR,  CLR_CLOUD_C);
    sprite.fillCircle(x - cr*5/8,  y + cr/8,  sideR, CLR_CLOUD_C);
    sprite.fillCircle(x + cr*5/8,  y + cr/8,  sideR, CLR_CLOUD_C);
    sprite.fillRect(x - cr, y + cr/8 - sideR/2, cr * 2, sideR + cr/4 + 2, CLR_CLOUD_C);
  };

  if (c0 == '0' && c1 == '1') {
    // Clear — large sun filling the icon area
    int sr = r * 9 / 16;
    sprite.fillCircle(cx, cy, sr, CLR_ACCENT);
    for (int a = 0; a < 8; a++) {
      float rad = a * 3.14159f / 4.0f;
      float ca = cosf(rad), sa = sinf(rad);
      // 3px-wide rays
      for (int w = -1; w <= 1; w++) {
        float pa = rad + 1.5708f;
        int ox = (int)(w * cosf(pa)), oy = (int)(w * sinf(pa));
        sprite.drawLine(cx + ox + (int)((sr + 5) * ca), cy + oy + (int)((sr + 5) * sa),
                        cx + ox + (int)((sr + r/2) * ca), cy + oy + (int)((sr + r/2) * sa),
                        CLR_ACCENT);
      }
    }

  } else if (c0 == '0' && c1 == '2') {
    // Few clouds — small sun peeking behind cloud
    int sr = r / 4;
    sprite.fillCircle(cx - r*3/8, cy - r/5, sr, CLR_ACCENT);
    for (int a = 0; a < 8; a++) {
      float rad = a * 3.14159f / 4.0f;
      sprite.drawLine(cx - r*3/8 + (int)((sr + 2) * cosf(rad)),
                      cy - r/5   + (int)((sr + 2) * sinf(rad)),
                      cx - r*3/8 + (int)((sr + 5) * cosf(rad)),
                      cy - r/5   + (int)((sr + 5) * sinf(rad)), CLR_ACCENT);
    }
    drawCloud(cx + r/8, cy + r/8, r * 5/8);

  } else if ((c0 == '0' && (c1 == '3' || c1 == '4')) || (c0 == '5' && c1 == '0')) {
    // Cloudy / mist — full-size cloud
    drawCloud(cx, cy, r * 11/16);

  } else if (c0 == '0' && c1 == '9') {
    // Shower rain — cloud + 5 angled drops
    drawCloud(cx, cy - r/5, r * 9/16);
    for (int i = -2; i <= 2; i++) {
      int rx = cx + i * (r / 4);
      sprite.drawLine(rx,     cy + r*3/8, rx - 3, cy + r*11/16, CLR_WATER);
      sprite.drawLine(rx + 1, cy + r*3/8, rx - 2, cy + r*11/16, CLR_WATER);
    }

  } else if (c0 == '1' && c1 == '0') {
    // Rain — cloud + 5 longer drops
    drawCloud(cx, cy - r/5, r * 9/16);
    for (int i = -2; i <= 2; i++) {
      int rx = cx + i * (r / 4);
      sprite.drawLine(rx,     cy + r*3/8, rx - 5, cy + r*13/16, CLR_WATER);
      sprite.drawLine(rx + 1, cy + r*3/8, rx - 4, cy + r*13/16, CLR_WATER);
    }

  } else if (c0 == '1' && c1 == '1') {
    // Thunderstorm — cloud + bold lightning bolt
    drawCloud(cx, cy - r/4, r * 9/16);
    int bx = cx, by = cy + r/8;
    sprite.fillTriangle(bx + r/5,  by,          bx - r/8,  by + r*6/16, bx + r/12, by + r*6/16, CLR_ACCENT);
    sprite.fillTriangle(bx + r/12, by + r*4/16, bx - r/5,  by + r*11/16, bx + r/8, by + r*11/16, CLR_ACCENT);

  } else if (c0 == '1' && c1 == '3') {
    // Snow — cloud + 3 snowflake asterisks
    drawCloud(cx, cy - r/5, r * 9/16);
    for (int i = -1; i <= 1; i++) {
      int sx = cx + i * (r * 3/8), sy = cy + r * 9/16;
      int sr = r / 5;
      sprite.drawFastHLine(sx - sr, sy, sr*2+1, CLR_TEXT);
      sprite.drawFastVLine(sx, sy - sr, sr*2+1, CLR_TEXT);
      sprite.drawLine(sx - sr*3/4, sy - sr*3/4, sx + sr*3/4, sy + sr*3/4, CLR_TEXT);
      sprite.drawLine(sx + sr*3/4, sy - sr*3/4, sx - sr*3/4, sy + sr*3/4, CLR_TEXT);
    }

  } else {
    sprite.drawCircle(cx, cy, r / 2, CLR_DIM);
  }
}

void drawScreenWeather() {
  sprite.fillSprite(CLR_BG);

  // Left panel: labels and details
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("WEATHER", 12, 10);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString(config.weather_city, 12, 30);

  // Vertical divider
  sprite.drawFastVLine(158, 8, 155, CLR_DIM);

  if (!weatherData.valid) {
    sprite.setTextColor(TFT_RED);
    sprite.setTextSize(2);
    sprite.drawString("No data", 12, 70);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Check API key &", 12, 98);
    sprite.drawString("web config", 12, 110);
    sprite.pushSprite(0, 0);
    return;
  }

  sprite.drawFastHLine(12, 42, 140, CLR_DIM);

  // Description
  char desc[64];
  strlcpy(desc, weatherData.description, sizeof(desc));
  if (desc[0] >= 'a' && desc[0] <= 'z') desc[0] -= 32;
  sprite.setTextColor(CLR_ACCENT);
  sprite.setTextSize(1);
  sprite.drawString(desc, 12, 52);

  // Feels like
  char tempBuf[12];
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Feels like", 12, 82);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  snprintf(tempBuf, sizeof(tempBuf), "%.0fF", weatherData.feels_like);
  sprite.drawString(tempBuf, 12, 94);

  // Humidity
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Humidity", 12, 126);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  snprintf(tempBuf, sizeof(tempBuf), "%d%%", weatherData.humidity);
  sprite.drawString(tempBuf, 12, 138);

  // Right panel: temperature + icon
  int rightCx = 158 + (SCREEN_WIDTH - 158) / 2;  // ~239
  snprintf(tempBuf, sizeof(tempBuf), "%.0f", weatherData.temp);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(5);
  int tw = sprite.textWidth(tempBuf);
  sprite.drawString(tempBuf, rightCx - tw / 2, 18);

  // Degree + F
  sprite.setTextSize(2);
  sprite.setTextColor(CLR_DIM);
  sprite.drawString("o", rightCx + tw / 2, 16);
  sprite.setTextColor(CLR_TEXT);
  sprite.drawString("F", rightCx + tw / 2 + 14, 20);

  // Weather icon below temperature
  drawWeatherIcon(rightCx, 118, 38, weatherData.icon);

  sprite.pushSprite(0, 0);
}

void drawScreenSports() {
  sprite.fillSprite(CLR_BG);

  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("SCORES", 12, 10);
  sprite.drawFastHLine(12, 30, SCREEN_WIDTH - 24, CLR_DIM);

  // Placeholder
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Add a sports API in the code to show live scores", 12, 48);

  sprite.setTextColor(CLR_ACCENT);
  sprite.setTextSize(3);
  int tw = sprite.textWidth("GO PACK GO");
  sprite.drawString("GO PACK GO", (SCREEN_WIDTH - tw) / 2, 72);

  // Example layout
  sprite.drawFastHLine(12, 120, SCREEN_WIDTH - 24, CLR_DIM);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Example layout:", 12, 130);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  sprite.drawString("GB  24", 12, 145);
  sprite.setTextColor(CLR_DIM);
  sprite.drawString("CHI 17", 120, 145);
  sprite.setTextColor(CLR_GREEN);
  sprite.setTextSize(1);
  sprite.drawString("FINAL", 240, 150);

  sprite.pushSprite(0, 0);
}

// Draw a "no HA data" error block — shows whether it's a config or fetch issue
void drawHAError(int x, int y) {
  sprite.setTextColor(TFT_RED);
  sprite.setTextSize(2);
  if (strlen(config.ha_url) == 0 || strlen(config.ha_token) == 0) {
    sprite.drawString("HA not configured", x, y);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Set HA URL + token", x, y + 20);
    sprite.drawString("at " + String(WiFi.localIP().toString()), x, y + 32);
  } else {
    sprite.drawString("No data", x, y);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Check token & entity ID", x, y + 20);
    sprite.drawString(config.ha_url, x, y + 32);
  }
}

// ============================================================
// HA SCREEN 1: BEDROOM HUMIDITY
// ============================================================
void drawScreenHumidity() {
  sprite.fillSprite(CLR_BG);

  // Header (left side)
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("BEDROOM", 12, 10);
  sprite.drawString("HUMIDITY", 12, 30);
  sprite.drawFastHLine(12, 50, 100, CLR_DIM);

  if (!humidityData.valid) {
    drawHAError(12, 72);
    sprite.pushSprite(0, 0);
    return;
  }

  float h = humidityData.humidity;

  uint16_t dropColor;
  const char* statusText;
  if (h < 30) {
    dropColor = CLR_ACCENT;
    statusText = "Too Dry";
  } else if (h <= 60) {
    dropColor = CLR_WATER;
    statusText = "Comfortable";
  } else {
    dropColor = 0x0410;
    statusText = "Too Humid";
  }

  // --- Water droplet (left side) ---
  int cx = 65;
  int bulbY = 105;
  int bulbR = 22;
  int tipY = 55;

  sprite.fillCircle(cx, bulbY, bulbR, dropColor);
  sprite.fillTriangle(cx - bulbR, bulbY, cx + bulbR, bulbY, cx, tipY, dropColor);

  int fillTop = bulbY + bulbR - (int)((h / 100.0f) * (bulbY + bulbR - tipY));
  float phase = millis() / 300.0f;

  for (int wy = fillTop; wy <= bulbY + bulbR; wy++) {
    int halfWidth;
    if (wy >= bulbY - bulbR && wy <= bulbY + bulbR) {
      float dy = (float)(wy - bulbY);
      halfWidth = (int)sqrtf(bulbR * bulbR - dy * dy);
    } else if (wy < bulbY && wy >= tipY) {
      float t = (float)(wy - tipY) / (float)(bulbY - tipY);
      halfWidth = (int)(t * bulbR);
    } else {
      continue;
    }
    if (wy == fillTop) {
      int x1 = cx - halfWidth + 1;
      int x2 = cx + halfWidth - 1;
      for (int px = x1; px <= x2; px++) {
        float waveAtPx = sinf(phase + px * 0.08f) * 3.0f;
        if (wy + (int)waveAtPx <= fillTop + 2) {
          sprite.drawPixel(px, wy + (int)waveAtPx, CLR_TEXT);
        }
      }
    }
  }

  sprite.drawCircle(cx, bulbY, bulbR, CLR_TEXT);
  sprite.drawLine(cx - bulbR, bulbY, cx, tipY, CLR_TEXT);
  sprite.drawLine(cx + bulbR, bulbY, cx, tipY, CLR_TEXT);
  sprite.fillCircle(cx - 8, bulbY - 10, 3, CLR_TEXT);

  // Vertical divider
  sprite.drawFastVLine(115, 45, 118, CLR_DIM);

  // --- Right side: humidity value ---
  int rightCx = 115 + (SCREEN_WIDTH - 115) / 2;  // ~218
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(5);
  char humBuf[10];
  snprintf(humBuf, sizeof(humBuf), "%.0f%%", h);
  int tw = sprite.textWidth(humBuf);
  sprite.drawString(humBuf, rightCx - tw / 2, 52);

  sprite.setTextColor(dropColor);
  sprite.setTextSize(2);
  tw = sprite.textWidth(statusText);
  sprite.drawString(statusText, rightCx - tw / 2, 118);

  sprite.pushSprite(0, 0);
}

// ============================================================
// HA SCREEN 2: ESPRESSO STATS
// ============================================================
void drawScreenEspresso() {
  sprite.fillSprite(CLR_BG);

  // Header (left panel)
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("ESPRESSO", 12, 10);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("La Marzocco Linea Micra", 12, 30);
  sprite.drawFastHLine(12, 42, 105, CLR_DIM);

  if (!espressoData.stateValid) {
    drawHAError(12, 72);
    sprite.pushSprite(0, 0);
    return;
  }

  // --- Coffee cup graphic (left side) ---
  int cupX = 18, cupY = 68;
  int cupW = 55, cupH = 45;

  sprite.fillRoundRect(cupX, cupY, cupW, cupH, 4, CLR_COFFEE);
  sprite.drawFastHLine(cupX, cupY, cupW, CLR_ACCENT);
  sprite.drawFastHLine(cupX, cupY + 1, cupW, CLR_ACCENT);

  for (int a = -40; a <= 40; a++) {
    float rad = a * 3.14159f / 180.0f;
    int hx = cupX + cupW + (int)(10 * cosf(rad));
    int hy = cupY + cupH / 2 + (int)(12 * sinf(rad));
    sprite.drawPixel(hx, hy, CLR_COFFEE);
    sprite.drawPixel(hx + 1, hy, CLR_COFFEE);
  }

  sprite.fillRoundRect(cupX - 8, cupY + cupH + 2, cupW + 16, 5, 3, CLR_DIM);
  sprite.fillRect(cupX + 3, cupY + 6, cupW - 6, cupH - 10, 0x4100);

  // --- Animated steam ---
  float t = millis() / 200.0f;
  for (int s = 0; s < 3; s++) {
    int baseX = cupX + 8 + s * 16;
    for (int y = cupY - 5; y > cupY - 22; y--) {
      float age = (float)(cupY - 5 - y) / 18.0f;
      float wave = sinf(t + y * 0.25f + s * 1.5f) * (2.0f + age * 3.0f);
      // Fade out as steam rises
      uint16_t steamColor = (age < 0.5f) ? CLR_DIM : 0x2104;
      sprite.drawPixel(baseX + (int)wave, y, steamColor);
      sprite.drawPixel(baseX + (int)wave + 1, y, steamColor);
    }
  }

  // Vertical divider
  sprite.drawFastVLine(120, 45, 118, CLR_DIM);

  // --- Right side: total coffees ---
  int rightX = 135;
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Total coffees", rightX, 52);

  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(4);
  char totalBuf[10];
  snprintf(totalBuf, sizeof(totalBuf), "%d", espressoData.totalCoffees);
  sprite.drawString(totalBuf, rightX, 64);

  // --- Yesterday's shots ---
  sprite.drawFastHLine(rightX, 110, SCREEN_WIDTH - rightX - 12, CLR_DIM);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Yesterday", rightX, 118);

  if (espressoData.historyValid) {
    sprite.setTextColor(CLR_ACCENT);
    sprite.setTextSize(3);
    char yBuf[16];
    snprintf(yBuf, sizeof(yBuf), "%d", espressoData.yesterdayShots);
    sprite.drawString(yBuf, rightX, 132);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    const char* shotLabel = (espressoData.yesterdayShots == 1) ? "shot" : "shots";
    sprite.drawString(shotLabel, rightX + sprite.textWidth(yBuf) * 3 + 6, 142);
  } else {
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("loading...", rightX, 135);
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
  sprite.drawString("BACKYARD", 12, 10);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Temperature", 12, 30);
  sprite.drawFastHLine(12, 42, 58, CLR_DIM);

  if (!backyardData.valid) {
    drawHAError(12, 72);
    sprite.pushSprite(0, 0);
    return;
  }

  float temp = backyardData.temperature;

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

  // --- Thermometer (left side), fits within 170px height ---
  int thX = 35;
  int thTop = 22;
  int thBot = 138;
  int tubeW = 8;
  int bulbR = 14;

  sprite.fillRect(thX - tubeW, thTop, tubeW * 2, thBot - thTop, CLR_DIM);
  sprite.fillCircle(thX, thBot, bulbR, CLR_DIM);

  float fillPct = constrain((temp + 20.0f) / 140.0f, 0.0f, 1.0f);
  int mercuryTop = thBot - (int)(fillPct * (thBot - thTop - 4));
  sprite.fillRect(thX - tubeW + 3, mercuryTop, tubeW * 2 - 6, thBot - mercuryTop, tempColor);
  sprite.fillCircle(thX, thBot, bulbR - 4, tempColor);

  sprite.drawRect(thX - tubeW, thTop, tubeW * 2, thBot - thTop, CLR_TEXT);
  sprite.drawCircle(thX, thBot, bulbR, CLR_TEXT);

  for (int i = 0; i <= 4; i++) {
    int markY = thTop + 4 + i * ((thBot - thTop - 8) / 4);
    sprite.drawFastHLine(thX + tubeW + 2, markY, 5, CLR_DIM);
  }

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("120", thX + tubeW + 10, thTop + 2);
  sprite.drawString("0", thX + tubeW + 10, thBot - 12);

  // Vertical divider
  sprite.drawFastVLine(78, 45, 118, CLR_DIM);

  // --- Large temperature value (right side) ---
  char tempBuf[10];
  snprintf(tempBuf, sizeof(tempBuf), "%.0f", temp);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(6);
  int tw = sprite.textWidth(tempBuf);
  sprite.drawString(tempBuf, 92, 38);

  // Degree + F
  sprite.setTextSize(2);
  sprite.setTextColor(CLR_DIM);
  sprite.drawString("o", 92 + tw + 2, 36);
  sprite.setTextColor(CLR_TEXT);
  sprite.drawString("F", 92 + tw + 16, 42);

  // Status
  sprite.drawFastHLine(85, 100, SCREEN_WIDTH - 97, CLR_DIM);
  sprite.setTextColor(tempColor);
  sprite.setTextSize(3);
  int rightCx = 78 + (SCREEN_WIDTH - 78) / 2;
  tw = sprite.textWidth(statusText);
  sprite.drawString(statusText, rightCx - tw / 2, 112);

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
  sprite.drawString("SUMP PUMP", 12, 10);
  sprite.drawFastHLine(12, 30, SCREEN_WIDTH - 24, CLR_DIM);

  if (!sumpData.stateValid) {
    drawHAError(12, 55);
    sprite.pushSprite(0, 0);
    return;
  }

  // --- Pump icon ---
  sprite.fillRect(12, 38, 6, 16, CLR_PRIMARY);
  sprite.fillRect(12, 38, 20, 5, CLR_PRIMARY);
  sprite.fillRoundRect(28, 40, 14, 12, 3, CLR_ACCENT);
  sprite.fillRect(42, 44, 10, 5, CLR_PRIMARY);

  // --- 24-hour run count ---
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Last 24h", 60, 36);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(3);
  char countBuf[10];
  snprintf(countBuf, sizeof(countBuf), "%d", sumpData.runsLast24h);
  sprite.drawString(countBuf, 60, 48);
  int cw = sprite.textWidth(countBuf);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString(sumpData.runsLast24h == 1 ? "run" : "runs", 60 + cw * 3 + 5, 57);

  // --- Last run time (right of divider) ---
  sprite.drawFastVLine(160, 32, 50, CLR_DIM);
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Last run", 170, 36);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  sprite.drawString(sumpData.lastRunTime, 170, 50);

  // --- 7-Day Bar Chart ---
  sprite.drawFastHLine(12, 88, SCREEN_WIDTH - 24, CLR_DIM);
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(1);
  sprite.drawString("7-Day History", 12, 93);

  if (!sumpData.historyValid) {
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Loading history...", 12, 130);
    sprite.pushSprite(0, 0);
    return;
  }

  int chartX = 12;
  int chartY = 104;
  int chartH = 50;
  int barW = 32;
  int gap = 8;

  int maxVal = 1;
  for (int i = 0; i < 7; i++) {
    if (sumpData.dailyRuns[i] > maxVal) maxVal = sumpData.dailyRuns[i];
  }

  sprite.drawFastHLine(chartX, chartY + chartH, 7 * (barW + gap), CLR_DIM);

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  for (int i = 0; i < 7; i++) {
    int dayIdx = 6 - i;
    int x = chartX + i * (barW + gap);
    int barH = 0;
    if (maxVal > 0) {
      barH = (sumpData.dailyRuns[dayIdx] * (chartH - 12)) / maxVal;
    }
    if (sumpData.dailyRuns[dayIdx] > 0 && barH < 4) barH = 4;

    uint16_t color = (dayIdx == 0) ? CLR_ACCENT : CLR_PRIMARY;
    if (barH > 0) {
      sprite.fillRect(x, chartY + chartH - barH, barW, barH, color);
    }

    time_t now_t = mktime(&timeinfo);
    time_t dayT = now_t - dayIdx * 86400;
    struct tm dayTm;
    localtime_r(&dayT, &dayTm);
    const char* dayNames[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
    sprite.setTextColor((dayIdx == 0) ? CLR_ACCENT : CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString(dayNames[dayTm.tm_wday], x + barW / 2 - 4, chartY + chartH + 3);

    if (sumpData.dailyRuns[dayIdx] > 0) {
      char cStr[4];
      snprintf(cStr, sizeof(cStr), "%d", sumpData.dailyRuns[dayIdx]);
      sprite.setTextColor(CLR_TEXT);
      sprite.setTextSize(1);
      sprite.drawString(cStr, x + barW / 2 - 3, chartY + chartH - barH - 10);
    }
  }

  sprite.pushSprite(0, 0);
}

// ============================================================
// NOW BREWING COFFEE SCREEN
// ============================================================
void drawScreenBrewing() {
  sprite.fillSprite(CLR_BG);

  // Right side: text content
  const int SPLIT_X = 155;  // divider position

  // "NOW BREWING" header
  sprite.setTextColor(CLR_ACCENT);
  sprite.setTextSize(2);
  sprite.drawString("NOW", SPLIT_X + 8, 12);
  sprite.drawString("BREWING", SPLIT_X + 8, 34);

  // Divider line
  sprite.drawFastVLine(SPLIT_X - 2, 8, SCREEN_HEIGHT - 16, CLR_DIM);

  // Coffee name (word-wrapped, large text)
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  if (strlen(config.coffee_name) > 0) {
    String name = String(config.coffee_name);
    int16_t lineY = 68;
    int16_t maxW = SCREEN_WIDTH - SPLIT_X - 12;
    while (name.length() > 0 && lineY < SCREEN_HEIGHT - 8) {
      int len = name.length();
      while (len > 0 && sprite.textWidth(name.substring(0, len).c_str()) > maxW) {
        int sp = name.lastIndexOf(' ', len - 1);
        if (sp > 0) {
          len = sp;
        } else {
          len--;
        }
      }
      if (len <= 0) len = 1;
      sprite.drawString(name.substring(0, len).c_str(), SPLIT_X + 8, lineY);
      name = name.substring(len);
      name.trim();
      lineY += 22;  // line height for size 2
    }
  } else {
    sprite.setTextColor(CLR_DIM);
    sprite.drawString("No coffee set", SPLIT_X + 8, 68);
  }

  // Left side: coffee bag image
  if (brewingData.valid && brewingData.imgBuf != nullptr) {
    // Center image in the left half (0 to SPLIT_X-4)
    int16_t areaW = SPLIT_X - 4;
    int16_t cx = (areaW - (int16_t)brewingData.imgW) / 2;
    int16_t cy = (SCREEN_HEIGHT - (int16_t)brewingData.imgH) / 2;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;

    jpgOffsetX = cx;
    jpgOffsetY = cy;

    if (brewingData.isPng) {
      int rc = png.openRAM(brewingData.imgBuf, brewingData.imgLen, pngDrawCallback);
      if (rc == PNG_SUCCESS) {
        png.decode(NULL, 0);
        png.close();
      }
    } else {
      TJpgDec.setJpgScale(brewingData.scale);
      TJpgDec.drawJpg(0, 0, brewingData.imgBuf, brewingData.imgLen);
    }
  } else if (strlen(config.coffee_img) > 0) {
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Loading", 40, 78);
    sprite.drawString("image...", 40, 92);
  }

  sprite.pushSprite(0, 0);
}

// ============================================================
// RING CAMERA SNAPSHOT SCREEN
// ============================================================
void drawScreenRingCam() {
  sprite.fillSprite(CLR_BG);

  if (!ringCamData.valid || ringCamData.jpegBuf == nullptr) {
    sprite.setTextColor(CLR_PRIMARY);
    sprite.setTextSize(2);
    sprite.drawString("RING CAMERAS", 12, 10);
    sprite.drawFastHLine(12, 30, 140, CLR_DIM);
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("Waiting for snapshot...", 12, 50);
    sprite.pushSprite(0, 0);
    return;
  }

  // Decode JPEG at 1/2 scale directly into sprite via callback
  // Set offsets: center 320x180 decoded image in 320x170 display (crop 5px top/bottom)
  jpgOffsetX = 0;
  jpgOffsetY = -5;
  TJpgDec.setJpgScale(2);
  TJpgDec.drawJpg(0, 0, ringCamData.jpegBuf, ringCamData.jpegLen);

  // Semi-transparent dark bar at bottom for camera name
  for (int y = SCREEN_HEIGHT - 22; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      uint16_t orig = sprite.readPixel(x, y);
      sprite.drawPixel(x, y, (orig >> 1) & 0x7BEF);  // 50% darken
    }
  }

  // Camera name centered
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(1);
  const char* name = RING_CAM_NAMES[ringCamData.currentCam];
  int16_t tw = sprite.textWidth(name);
  sprite.drawString(name, (SCREEN_WIDTH - tw) / 2, SCREEN_HEIGHT - 16);

  // Camera index indicator (e.g. "3/5")
  char idxBuf[8];
  snprintf(idxBuf, sizeof(idxBuf), "%d/%d", ringCamData.currentCam + 1, RING_CAM_COUNT);
  sprite.drawString(idxBuf, SCREEN_WIDTH - sprite.textWidth(idxBuf) - 4, SCREEN_HEIGHT - 16);

  sprite.pushSprite(0, 0);
}

// ============================================================
// NETWORK INFO SCREEN (both buttons held)
// ============================================================
void drawScreenIP() {
  sprite.fillSprite(CLR_BG);

  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("NETWORK", 12, 10);
  sprite.drawFastHLine(12, 30, SCREEN_WIDTH - 24, CLR_DIM);

  // Left column
  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("WiFi Network", 12, 42);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  sprite.drawString(config.wifi_ssid, 12, 53);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("IP Address", 12, 76);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(2);
  sprite.drawString(WiFi.localIP().toString(), 12, 87);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Signal", 12, 112);
  sprite.setTextColor(CLR_TEXT);
  sprite.setTextSize(1);
  char rssi[16];
  snprintf(rssi, sizeof(rssi), "%d dBm", WiFi.RSSI());
  sprite.drawString(rssi, 12, 122);

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Free heap: " + String(ESP.getFreeHeap()), 12, 130);

  // Right column
  sprite.drawFastVLine(170, 32, 130, CLR_DIM);
  String configUrl = "http://" + WiFi.localIP().toString();

  sprite.setTextColor(CLR_DIM);
  sprite.setTextSize(1);
  sprite.drawString("Web Config", 180, 42);
  sprite.setTextColor(CLR_GREEN);
  sprite.drawString(configUrl, 180, 54);

  sprite.setTextColor(CLR_DIM);
  sprite.drawString("OTA Update", 180, 84);
  sprite.setTextColor(CLR_GREEN);
  sprite.drawString(configUrl + "/update", 180, 96);

  sprite.pushSprite(0, 0);
}

// Show the saved WiFi scan log from NVS (recorded on last failed connection)
void drawWifiLogScreen() {
  prefs.begin("wifilog", true);
  int cnt = prefs.getInt("wl_cnt", 0);
  prefs.end();

  sprite.fillSprite(CLR_BG);
  sprite.setTextColor(CLR_PRIMARY);
  sprite.setTextSize(2);
  sprite.drawString("WIFI LOG", 12, 10);
  sprite.drawFastHLine(12, 30, SCREEN_WIDTH - 24, CLR_DIM);

  if (cnt == 0) {
    sprite.setTextColor(CLR_DIM);
    sprite.setTextSize(1);
    sprite.drawString("No scan log saved yet.", 12, 55);
    sprite.drawString("Log is recorded when WiFi", 12, 70);
    sprite.drawString("connection fails.", 12, 82);
    sprite.pushSprite(0, 0);
    delay(6000);
    return;
  }

  sprite.setTextColor(CLR_ACCENT);
  sprite.setTextSize(1);
  sprite.drawString(String("Target: ") + config.wifi_ssid, 12, 32);

  // Load and display all saved networks
  const int ROW_H = 20;
  const int HDR_H = 44;

  prefs.begin("wifilog", true);
  for (int i = 0; i < cnt; i++) {
    char keyS[10], keyB[10], keyR[10];
    snprintf(keyS, sizeof(keyS), "wl%dssid", i);
    snprintf(keyB, sizeof(keyB), "wl%dbssid", i);
    snprintf(keyR, sizeof(keyR), "wl%drssi", i);

    String ssid   = prefs.getString(keyS, "");
    String bssid  = prefs.getString(keyB, "");
    int    rssi   = prefs.getInt(keyR, 0);

    bool isTarget = (ssid == String(config.wifi_ssid));
    int y = HDR_H + i * ROW_H;

    // SSID + RSSI
    if (ssid.length() == 0) ssid = "(hidden)";
    if (ssid.length() > 22) ssid = ssid.substring(0, 21) + "~";
    char row1[52];
    snprintf(row1, sizeof(row1), "%-22s %d dBm", ssid.c_str(), rssi);
    sprite.setTextColor(isTarget ? CLR_GREEN : CLR_TEXT);
    sprite.setTextSize(1);
    sprite.drawString(row1, 12, y);

    // BSSID
    sprite.setTextColor(isTarget ? CLR_PRIMARY : CLR_DIM);
    sprite.drawString(bssid, 12, y + 10);
  }
  prefs.end();

  sprite.pushSprite(0, 0);
  delay(12000);  // show for 12 seconds
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
  { drawScreenRingCam,  &config.screen_ring },
  { drawScreenBrewing,  &config.screen_brewing },
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
  tft.setRotation(3);  // Landscape, USB-C on left
  tft.fillScreen(TFT_BLACK);

  // Create sprite (double-buffered drawing = no flicker)
  sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
  sprite.setTextWrap(false);

  // JPEG decoder for camera snapshots
  TJpgDec.setJpgScale(2);  // 1/2 scale: 640x360 -> 320x180
  TJpgDec.setCallback(jpgDrawCallback);

  // Buttons
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  // Load saved config
  loadConfig();

  // Connect WiFi or start AP mode
  if (strlen(config.wifi_ssid) > 0) {
    if (connectWiFi()) {
      setupTime();
      // Data fetches happen in loop() on first iteration — avoids blocking setup
      // and hanging on the "WiFi Connected" splash screen
    } else {
      startAPMode();  // WiFi failed — fall back to AP so user can reconfigure
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
    // Both buttons held: show network info then saved WiFi scan log
    if (btnLeft == LOW && btnRight == LOW) {
      drawScreenIP();
      delay(5000);
      drawWifiLogScreen();
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
    if (lastWeatherFetch == 0 || now - lastWeatherFetch >= WEATHER_INTERVAL) {
      fetchWeather();
      lastWeatherFetch = now;
    }
    if (lastHAStateFetch == 0 || now - lastHAStateFetch >= HA_STATE_INTERVAL) {
      fetchHAStates();
      lastHAStateFetch = now;
    }
    if (lastHAHistoryFetch == 0 || now - lastHAHistoryFetch >= HA_HISTORY_INTERVAL) {
      fetchEspressoHistory();
      fetchSumpHistory();
      lastHAHistoryFetch = now;
    }
    // Ring camera: advance + fetch on each transition to this screen
    bool ringIsActive = config.screen_ring &&
                        allScreens[currentScreen].draw == drawScreenRingCam;
    if (ringIsActive && !ringScreenWasActive) {
      // Just transitioned TO Ring screen — advance and fetch
      if (lastRingCamFetch != 0) {
        ringCamData.currentCam = (ringCamData.currentCam + 1) % RING_CAM_COUNT;
      }
      fetchRingSnapshot();
      lastRingCamFetch = now;
    } else if (ringIsActive && now - lastRingCamFetch >= RING_CAM_INTERVAL) {
      // Paused on Ring screen — periodic refresh of same camera
      fetchRingSnapshot();
      lastRingCamFetch = now;
    }
    ringScreenWasActive = ringIsActive;

    // Now Brewing: fetch on screen transition, URL change, or periodic refresh
    bool brewIsActive = config.screen_brewing &&
                        allScreens[currentScreen].draw == drawScreenBrewing;
    bool urlChanged = strcmp(config.coffee_img, lastBrewingUrl) != 0;
    if (brewIsActive && (!brewingScreenWasActive || urlChanged)) {
      fetchBrewingImage();
      lastBrewingFetch = now;
    } else if (brewIsActive && now - lastBrewingFetch >= BREWING_INTERVAL) {
      fetchBrewingImage();
      lastBrewingFetch = now;
    }
    brewingScreenWasActive = brewIsActive;
  }

  // --- Draw current screen ---
  allScreens[currentScreen].draw();

  // ElegantOTA processing
  ElegantOTA.loop();

  delay(100);  // ~10 fps, keeps things smooth without burning CPU
}
