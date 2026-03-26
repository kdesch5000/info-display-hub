# Info Display Hub

**A pocket-sized, WiFi-connected info display that turns a $15 microcontroller into a rotating dashboard for your home.** It cycles through configurable screens showing the time, weather, Home Assistant sensor data, security camera snapshots, espresso stats, and more — all on a vibrant 1.9" color LCD.

Built on the Lilygo T-Display S3 (ESP32-S3), the entire project is a single Arduino sketch with a built-in web config portal and over-the-air firmware updates. Plug it into any USB-C charger, configure it from your phone, and forget about it.

## Why This Exists

Most smart home dashboards require a tablet or a Raspberry Pi. This project proves you can build a useful, always-on display for under $20 in hardware with zero ongoing maintenance. It's designed to sit on a desk, kitchen counter, or nightstand and surface the information you actually glance at throughout the day.

## What It Can Do Today

**9 screens, all optional and toggleable from the web UI:**

| Screen | Source | What It Shows |
|---|---|---|
| Clock | NTP | Time, date, day of week |
| Weather | OpenWeatherMap | Temp, feels-like, humidity, conditions |
| Sports | (placeholder) | Ready for your preferred sports API |
| Bedroom Humidity | Home Assistant | Animated droplet, color-coded humidity % |
| Espresso Stats | Home Assistant | Total coffees + yesterday's shot count |
| Backyard Temp | Home Assistant | Thermometer graphic, color-coded reading |
| Sump Pump | Home Assistant | 24h run count, last cycle time, 7-day chart |
| Ring Cameras | Home Assistant | Cycles through 5 camera snapshots |
| Now Brewing | Any URL | Coffee name + roaster bag image (JPEG/PNG) |

**Other features:**
- Web-based configuration portal — no code changes needed for WiFi, API keys, or screen toggles
- OTA firmware updates via browser — flash once over USB, update forever over WiFi
- Button controls for manual screen navigation
- Network info overlay (IP, signal strength, config URLs)
- `/api/debug` and `/api/log` endpoints for remote diagnostics
- AP mode fallback for first-time setup

## What You Could Build With It

The screen system is modular — each screen is a self-contained draw function. Adding a new screen is ~30 lines of code and a config toggle. Some ideas:

- **NFL / Packers scores** — ESPN API or sportsdata.io, show live scores and next game countdown
- **CTA train arrivals** — Chicago Transit Authority API for real-time "L" train ETAs
- **Bitcoin / crypto ticker** — price, 24h change, maybe a sparkline chart
- **Bitaxe hashrate** — monitor your Bitcoin miner's performance
- **Fantasy football** — ESPN Fantasy API for matchup scores during the season
- **MQTT subscriber** — display any smart home event pushed via MQTT
- **Package tracking** — show delivery status from 17track or AfterShip APIs
- **Calendar** — next 3 events from a Google Calendar or CalDAV feed
- **Air quality / pollen** — AirNow or Ambee API for outdoor conditions
- **3D printer status** — OctoPrint API for print progress and temps
- **Pi-hole stats** — DNS queries blocked today, top blocked domains
- **Spotify now playing** — current track, artist, album art
- **Animated transitions** — slide or fade effects between screen rotations
- **Auto-dimming** — PWM backlight control based on time of day
- **mDNS** — reach the device at `http://infodisplay.local` instead of an IP

The architecture is intentionally simple. No frameworks, no build systems beyond Arduino — just a `.ino` file, a sprite buffer, and HTTP calls.

## Hardware

- **Lilygo T-Display S3** (~$15–20) — ESP32-S3 with built-in 1.9" 170×320 IPS LCD
- **USB-C cable** for initial flash and power
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

### Remote Diagnostics
- `http://<device-ip>/api/debug` — JSON system status (memory, screen state, data sources, WiFi)
- `http://<device-ip>/api/log` — recent log messages (last 40 entries)

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

## Adding a New Screen

1. Create a `void drawScreenXxx()` function — draw into `sprite`, call `sprite.pushSprite(0, 0)` at the end
2. Add a `bool screen_xxx;` field to the `Config` struct
3. Add load/save lines in `loadConfig()` and `saveConfig()`
4. Add the screen to the `allScreens[]` array
5. Add a toggle in the `CONFIG_HTML` web interface
6. Add JSON handling in the GET/POST `/api/config` handlers

## Architecture

- **Single-file Arduino sketch** — everything in one `.ino` for simplicity
- **TFT_eSprite double buffering** — all drawing goes to an off-screen sprite, pushed in one call (no flicker)
- **AsyncWebServer** — non-blocking web server runs alongside the display loop
- **Preferences (NVMe flash)** — config persists across reboots and power cycles
- **Interval-based data fetching** — weather every 5 min, HA states every 30s, history every 10 min
- **Graceful memory handling** — image buffers try PSRAM first, fall back to heap with size-appropriate limits
