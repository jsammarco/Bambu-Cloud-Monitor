# FlipperApp

Flipper Zero companion app and ESP32 bridge for viewing Bambu printer status from the Flipper.

## Current Status

This project is functional as a development build, not a finished production app.

What works today:

- Flipper menu-driven UI
- Wi‑Fi scan and connect through the ESP32 bridge
- saved Wi‑Fi reconnect
- token loading from SD
- printer cache loading from SD
- printer list and printer detail pages
- UART bridge between Flipper and ESP32
- local MQTT status requests from the ESP32 using printer `serial`, `ip`, and `access_code`
- Flipper LED activity indicator during bridge traffic

What is still in active tuning:

- MQTT reliability across all printers
- how quickly sparse status fields populate
- timing and retry behavior on the ESP32 bridge
- cloud discovery from the ESP32

At the moment, the most reliable way to use the Flipper app is:

1. generate `found_printers.json` on the desktop with the Python tools
2. copy that cache to the Flipper SD card
3. use the Flipper app for local MQTT status refreshes

## Layout

- Flipper handles the UI and rendering
- ESP32 handles Wi‑Fi and local MQTT
- UART link runs at `115200 8N1`

## SD Card Paths

Default asset directory:

- `/ext/apps_assets/bambu_fleet`

Expected files:

- `/ext/apps_assets/bambu_fleet/bambu_token.txt`
- `/ext/apps_assets/bambu_fleet/found_printers.json`

The app also stores:

- `/ext/apps_assets/bambu_fleet/bambu_fleet.log`
- `/ext/apps_assets/bambu_fleet/bambu_fleet.cfg`

## Build The Flipper App

Use [`build.ps1`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\build.ps1):

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The script will:

- find a Flipper firmware checkout
- mirror this app into `applications_user/bambu_fleet`
- build the external app
- copy the final artifact to [`dist\bambu_fleet.fap`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\dist\bambu_fleet.fap)

## Flash The ESP32 Bridge

ESP32 sketch:

- [`esp32_bridge/bambu_monitor_bridge/bambu_monitor_bridge.ino`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\esp32_bridge\bambu_monitor_bridge\bambu_monitor_bridge.ino)

Important note:

- the ESP32 sketch is required for current status behavior
- changes to MQTT handling usually require reflashing the ESP32, not just reinstalling the `.fap`

## App Flow

Typical flow on-device:

1. `Ping Bridge`
2. `Connect Last WiFi` or `Connect Selected WiFi`
3. `Load Token from SD`
4. `Load Printers from SD`
5. `Printer List`
6. open a printer detail page
7. use `OK` to refresh, `Up/Down` to scroll, `Left/Right` to move between printers

## UART Protocol

Commands sent from Flipper:

- `PING`
- `WIFI_SCAN`
- `WIFI_CONNECT|ssid|password`
- `WIFI_RECONNECT`
- `WIFI_STATUS`
- `BAMBU_SET_TOKEN|bearer-token`
- `BAMBU_TEST_PROFILE`
- `BAMBU_DISCOVER`
- `BAMBU_STATUS|serial|ip|access_code`

Responses from ESP32:

- `OK|...`
- `ERR|...`
- `WIFI|ssid|rssi|open|secure`
- `PRINTER|serial|name|model|ip|access_code|online|cloud_status`
- `STATUS|serial|state|progress|layer|total_layers|remaining|nozzle|bed|wifi|file|speed|fan|fan_aux1|fan_aux2`

Unknown fields may be returned as `?` when the printer has not reported them yet.

## Logging

The Flipper app writes logs to:

- `/ext/apps_assets/bambu_fleet/bambu_fleet.log`

That log is the best first place to check for:

- token load failures
- Wi‑Fi reconnect failures
- cache load issues
- MQTT timeouts
- refresh results per printer

## Files

- [`application.fam`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\application.fam)
- [`app_ui.c`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\app_ui.c)
- [`bambu_monitor_app.c`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\bambu_monitor_app.c)
- [`bambu_monitor_app.h`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\bambu_monitor_app.h)
- [`bambu_monitor_config.h`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\bambu_monitor_config.h)
- [`bambu_transport.h`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\bambu_transport.h)
- [`bambu_transport_live.c`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\bambu_transport_live.c)
- [`build.ps1`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\build.ps1)
- [`esp32_bridge/bambu_monitor_bridge/bambu_monitor_bridge.ino`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\esp32_bridge\bambu_monitor_bridge\bambu_monitor_bridge.ino)

## Honest Limitation

The Flipper app is useful now, but the desktop Python tools in the repo are still the most dependable path for full monitoring and discovery.

The Flipper/ESP32 side is best treated as an actively improving companion client rather than the canonical monitor today.
