# Info Display Hub — Claude Code Project Context

## What This Is
An Arduino project for the **Lilygo T-Display S3** (ESP32-S3 + 1.9" ST7789 170×320 IPS LCD). It's a USB-powered, WiFi-connected rotating info display that cycles through configurable screens showing clock, weather, sports scores, Home Assistant data, and more.

## Hardware
- **Board**: Lilygo T-Display S3 (ESP32-S3, 16MB flash, built-in 1.9" LCD, USB-C, 2 buttons)
- **Display**: ST7789 170×320 IPS color LCD (portrait orientation, USB-C at bottom)
- **Buttons**: GPIO 0 (left), GPIO 14 (right)
- **Display driver**: TFT_eSPI with `Setup206_LilyGo_T_Display_S3.h` config

## Framework & Libraries
- **Arduino IDE** (C++, targeting ESP32-S3)
- **TFT_eSPI** — display driver (configured for T-Display S3)
- **TFT_eSprite** — double-buffered drawing to eliminate flicker
- **ArduinoJson** — JSON parsing for API responses
- **AsyncTCP + ESPAsyncWebServer** — non-blocking web server for config portal
- **ElegantOTA** — web-based firmware updates at `/update`
- **Preferences** — persistent key-value config storage in NVMe flash
- **HTTPClient** — outbound API calls (weather, Home Assistant, etc.)

## Architecture

### Screen System
Each display screen is a `void drawScreenXxx()` function that draws into the `sprite` (TFT_eSprite) and calls `sprite.pushSprite(0, 0)` at the end. Screens are registered in the `allScreens[]` array with a pointer to their draw function and a pointer to their enable/disable bool in the config struct.

**To add a new screen:**
1. Create `void drawScreenXxx()` following the existing pattern
2. Add a `bool screen_xxx;` field to the `Config` struct
3. Add load/save lines in `loadConfig()` and `saveConfig()`
4. Add the screen to `allScreens[]`
5. Add a toggle in the `CONFIG_HTML` web interface
6. Add the JSON field handling in the GET/POST `/api/config` handlers

### Configuration System
All settings live in the `Config` struct and are persisted via the ESP32 `Preferences` library (NVMe flash, survives reboots and power cycles). The web UI at `/` reads config via `GET /api/config` (JSON) and writes via `POST /api/config` (JSON), then the device reboots.

### Data Fetching
API calls happen on intervals in the main `loop()`:
- Weather: every 5 minutes (`WEATHER_INTERVAL`)
- Home Assistant: every 30 seconds (`HA_INTERVAL`)
- Time: NTP auto-sync on boot

New data sources should follow this pattern: define a data struct, a fetch function, an interval constant, and a `lastXxxFetch` timestamp.

### Web Server Endpoints
| Endpoint | Method | Purpose |
|---|---|---|
| `/` | GET | Config page (embedded HTML) |
| `/api/config` | GET | Current config as JSON |
| `/api/config` | POST | Save config + reboot |
| `/update` | GET | ElegantOTA firmware upload page |

### Button Controls
- Left (GPIO 0): previous screen
- Right (GPIO 14): next screen  
- Both held: show network info screen (IP, RSSI, URLs)

## Display Constants
- Screen: 170×320 pixels (portrait)
- Color format: RGB565
- Color palette defined as `#define CLR_*` constants at top of draw functions section

## First Boot Behavior
If no WiFi SSID is configured, the device starts in AP mode:
- SSID: `InfoDisplayHub`, Password: `configure`
- Config portal at `http://192.168.4.1`

## Owner Context
- Location: Chicago (timezone: `CST6CDT,M3.2.0,M11.1.0`)
- Runs **Home Assistant Green** on local network
- Green Bay Packers fan (sports screen default)
- Has Firewalla + UniFi network infrastructure
- Weather default city: Chicago
- Uses OpenWeatherMap free tier for weather data

## Code Style
- Use `sprite` (TFT_eSprite) for all drawing, never write directly to `tft` after setup
- Keep screen draw functions self-contained
- Use the defined color palette (`CLR_BG`, `CLR_PRIMARY`, `CLR_ACCENT`, `CLR_DIM`, `CLR_TEXT`, `CLR_GREEN`)
- Center text manually using `sprite.textWidth()` when appropriate
- All strings that come from config should use `strlcpy()` with `sizeof()` for safety
- Debounce buttons in `loop()` with a 200ms threshold

## Future Enhancement Ideas
- NFL/Packers scores via ESPN API or sportsdata.io
- Multiple Home Assistant entities (cycle within the HA screen)
- Display brightness auto-dimming by time of day (backlight PWM)
- MQTT subscriber screen for smart home events
- Fantasy football scores (ESPN API)
- CTA train arrival times (Chicago Transit Authority API)
- Bitcoin price / Bitaxe hashrate display
- Animated screen transitions (slide/fade between screens)
- mDNS so device is reachable at `http://infodisplay.local`
