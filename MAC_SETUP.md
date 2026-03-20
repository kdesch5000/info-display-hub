# Info Display Hub — Mac Setup Guide

## Current State
- GitHub repo: `git@github.com:kdesch5000/info-display-hub.git`
- Branch: `main`
- Latest commit: `14ae29d` — "Add 4 dedicated Home Assistant display screens"
- **Compile error**: The sketch has a compile error in Arduino IDE that needs to be fixed

## Hardware
- **Board**: Lilygo T-Display S3 (ESP32-S3, 16MB flash, built-in 1.9" ST7789 170x320 LCD, USB-C, 2 buttons)
- **Buttons**: GPIO 0 (left), GPIO 14 (right)
- **Display driver**: TFT_eSPI with `Setup206_LilyGo_T_Display_S3.h`

## Arduino IDE Setup (Already Done)
1. Arduino IDE 2.x installed on Mac
2. ESP32 board support installed via Board Manager
3. Libraries installed:
   - TFT_eSPI (configured: `Setup206_LilyGo_T_Display_S3.h` uncommented in `User_Setup_Select.h`)
   - ArduinoJson
   - ElegantOTA
   - AsyncTCP (from GitHub ZIP)
   - ESPAsyncWebServer (from GitHub ZIP)

## Arduino IDE Board Settings
- **Board**: ESP32S3 Dev Module (or LilyGo T-Display-S3)
- **USB CDC On Boot**: Enabled
- **Flash Size**: 16MB (128Mb)
- **Partition Scheme**: Default 4MB with spiffs
- **Port**: `/dev/cu.usbmodem...` (appears after plugging in device)

## TFT_eSPI Configuration
File location on Mac:
```
~/Documents/Arduino/libraries/TFT_eSPI/User_Setup_Select.h
```
- `#include <User_Setup.h>` — COMMENTED OUT
- `#include <User_Setups/Setup206_LilyGo_T_Display_S3.h>` — UNCOMMENTED

## Clone and Build
```bash
git clone git@github.com:kdesch5000/info-display-hub.git
cd info-display-hub
# Open info_display_hub.ino in Arduino IDE
# Fix compile error, then Upload
```

## TODO: Fix Compile Error
The sketch failed to compile in Arduino IDE on Mac. The error output needs to be captured and the code fixed. Common causes:
- `#include <math.h>` — may need `<cmath>` or the functions may need different signatures on ESP32
- `sinf()`/`cosf()`/`sqrtf()` — should be available with `<math.h>` on ESP32 but verify
- `constrain()` macro — built into Arduino framework, should work
- `AsyncCallbackJsonWebHandler` — requires both AsyncTCP and ESPAsyncWebServer plus ArduinoJson
- Library version conflicts between ZIP-installed and Library Manager versions

**Next step**: Copy the compile error output from Arduino IDE and fix the code.

## Flashing Instructions
1. Plug T-Display S3 into Mac via USB-C
2. If no port appears: hold **Boot** button while plugging in, release after 1 second
3. Tools → Port → select `/dev/cu.usbmodem...`
4. Click Upload (→ button)
5. Wait for "Hard resetting via RTS pin..." message

## First Boot After Flash
1. Device shows "Setup Mode" screen
2. Connect phone/laptop to WiFi: **InfoDisplayHub** (password: `configure`)
3. Browse to **http://192.168.4.1**
4. Configure:
   - WiFi SSID + password (your home network)
   - OpenWeatherMap API key (free at openweathermap.org/api)
   - HA URL: `http://home.mf:8123`
   - HA Token: (create at HA profile → Security → Long-Lived Access Tokens)
   - Enable desired screens (Clock, Weather, Humidity, Espresso, Backyard Temp, Sump Pump)
5. Click **Save & Reboot**

## Home Assistant Entities Used
| Screen | Entity ID |
|---|---|
| Bedroom Humidity | `sensor.my_humidifier_humidity` |
| Espresso Stats | `sensor.kd_micra_total_coffees_made` |
| Backyard Temp | `sensor.backyard_temperature_sensor_temperature` |
| Sump Pump | `sensor.pumpspy_battery_backup_main_last_cycle` |

## OTA Updates (After First Flash)
Once running on WiFi, no more USB needed:
1. Arduino IDE: **Sketch → Export Compiled Binary**
2. Browse to `http://<device-ip>/update`
3. Upload the `.bin` file

## Architecture Notes
- Single file: `info_display_hub.ino`
- All drawing via `TFT_eSprite` (double-buffered, no flicker)
- Config stored in ESP32 Preferences (NVMe flash, survives reboots)
- Web config portal at `/`, API at `/api/config` (GET/POST JSON)
- Screen system: `allScreens[]` array with function pointers + enable bools
- HA state fetches every 30s, history API every 10 min
- Animations use `millis()` for phase (humidity waves, espresso steam)
