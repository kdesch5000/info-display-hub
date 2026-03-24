# Info Display Hub — Lilygo T-Display S3

A WiFi-connected rotating info display with OTA updates and web-based configuration.

## Screens
- **Clock** — NTP-synced time, date, day of week
- **Weather** — Temperature, feels-like, humidity, conditions (OpenWeatherMap)
- **Sports** — Placeholder ready for your preferred sports API
- **Bedroom Humidity** — Animated water droplet with humidity %, color-coded status (Home Assistant)
- **Espresso Stats** — Coffee cup graphic with total count + yesterday's shots (Home Assistant + History API)
- **Backyard Temperature** — Thermometer graphic with color-coded temp reading (Home Assistant)
- **Sump Pump Monitor** — 24h run count, time since last run, 7-day bar chart (Home Assistant + History API)
- **Ring Cameras** — Cycles through 5 Ring camera snapshots via HA camera_proxy (Home Assistant)
- **Now Brewing** — Coffee name + bag image from roaster website (JPEG/PNG, HTTPS supported)

## Hardware
- Lilygo T-Display S3 (~$15–20)
- USB-C cable
- That's it.

## Arduino IDE Setup

### 1. Install ESP32 Board Support
- Open **File → Preferences**
- In "Additional Board Manager URLs", add:
  ```
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  ```
- Go to **Tools → Board → Board Manager**, search "esp32", install **esp32 by Espressif Systems**

### 2. Select Board
- **Tools → Board** → select **LilyGo T-Display-S3** (or "ESP32S3 Dev Module")
- **Tools → USB CDC On Boot** → Enabled
- **Tools → Flash Size** → 16MB (128Mb)
- **Tools → Partition Scheme** → Default 4MB with spiffs (or Huge APP if available)
- **Tools → Port** → select the USB serial port

### 3. Install Libraries
Install these via **Sketch → Include Library → Manage Libraries**:
- **TFT_eSPI** by Bodmer
- **TJpg_Decoder** by Bodmer
- **PNGdec** by Larry Bank
- **ArduinoJson** by Benoit Blanchon
- **ElegantOTA** by Ayush Sharma

Install these from GitHub (download ZIP, then Sketch → Include Library → Add .ZIP Library):
- **AsyncTCP**: https://github.com/me-no-dev/AsyncTCP
- **ESPAsyncWebServer**: https://github.com/me-no-dev/ESPAsyncWebServer

### 4. Configure TFT_eSPI for T-Display S3
The TFT_eSPI library needs to know your board. Edit the file:
```
Arduino/libraries/TFT_eSPI/User_Setup_Select.h
```
- Comment out: `#include <User_Setup.h>`
- Uncomment: `#include <User_Setups/Setup206_LilyGo_T_Display_S3.h>`

### 5. Flash
- Click **Upload** in Arduino IDE
- This is the only time you need USB — all future updates go over WiFi

## First Boot

1. The board creates a WiFi access point:
   - **SSID:** `InfoDisplayHub`
   - **Password:** `configure`
2. Connect to that network from your phone/laptop
3. Browse to `http://192.168.4.1`
4. Enter your WiFi credentials, API keys, and preferences
5. Click **Save & Reboot**
6. The board connects to your WiFi and starts displaying

## Daily Use

### Button Controls
| Action | What it does |
|---|---|
| Left button | Previous screen |
| Right button | Next screen |
| Both buttons held | Show network info (IP, signal, URLs) |

### Web Configuration
Browse to `http://<device-ip>/` to change any setting. The IP is shown on the network info screen.

### OTA Firmware Updates
1. In Arduino IDE, compile your updated sketch: **Sketch → Export Compiled Binary**
2. Browse to `http://<device-ip>/update`
3. Upload the `.bin` file from your sketch's `build` folder
4. Device reboots with new firmware

## API Keys & Services

### OpenWeatherMap (free)
1. Sign up at https://openweathermap.org/api
2. Create an API key (free tier = 1,000 calls/day, plenty for 5-min intervals)
3. Enter the key in the web config

### Home Assistant
Required for the Humidity, Espresso, Backyard Temp, Sump Pump, and Ring Cameras screens.

1. In HA, go to your profile → Long-Lived Access Tokens → Create Token
2. Enter the token and your HA URL in the web config
3. Entity IDs are hardcoded for each screen (see `#define HA_ENTITY_*` in the source)

#### HA Entities Used
| Screen | Entity ID | Data |
|---|---|---|
| Bedroom Humidity | `sensor.my_humidifier_humidity` | Current state (every 30s) |
| Espresso Stats | `sensor.kd_micra_total_coffees_made` | Current state + History API (every 10 min) |
| Backyard Temp | `sensor.backyard_temperature_sensor_temperature` | Current state (every 30s) |
| Sump Pump | `sensor.pumpspy_battery_backup_main_last_cycle` | Current state + History API (every 10 min) |
| Ring Cameras | `camera.*_snapshot` (5 cameras) | Camera proxy JPEG snapshot (every 30s, cycles cameras) |

## Customization Ideas

- **Add more screens**: Create a new `drawScreenXxx()` function, add it to the `allScreens[]` array
- **Change HA entities**: Edit the `#define HA_ENTITY_*` constants at the top of the sketch
- **Sports API**: ESPN's hidden API, The Score, or sportsdata.io — fetch JSON, parse, display
- **Display brightness**: Use `ledcWrite()` to control the backlight pin based on time of day
- **MQTT**: Subscribe to topics and display messages from your smart home
- **Packers schedule**: Pull the NFL schedule API and show next game time
