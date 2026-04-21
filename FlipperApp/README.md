# FlipperApp

Scaffold for a Flipper Zero app plus ESP32 bridge that can monitor Bambu printers from the Flipper using the same broad flow as the desktop Python tooling in this repository.

## Goal

This app is designed around the same split proven by `FlipperWalkPrint`:

- Flipper Zero handles the UI, menus, list views, and status pages
- ESP32 handles Wi-Fi, HTTPS calls to the Bambu cloud, and printer discovery/status work
- Flipper and ESP32 talk over UART at `115200 8N1`

## Current Scaffold

The Flipper side includes:

- `application.fam`
- a single-view menu-driven app
- Wi-Fi results screen
- printer list screen
- printer detail screen
- UART bridge transport parser for `WIFI`, `PRINTER`, and `STATUS` protocol lines

The ESP32 side includes:

- Wi-Fi scan
- Wi-Fi connect
- bearer token storage
- Bambu device discovery via `/v1/iot-service/api/user/bind`
- Bambu status fetch via `/v1/iot-service/api/user/print?force=true`

## Important Limitation

This is a strong starting scaffold, not a finished production app.

What is already practical:

- bridge ping / readiness
- Wi-Fi scanning
- Wi-Fi connect and saved credentials
- saved bearer token
- cloud-backed printer list
- cloud-backed status summary pages

What still needs finishing for parity with the Python tools:

- local subnet `8883` scan on ESP32
- local MQTT auth matching to resolve printer IPs from access codes
- richer printer detail fields
- settings UI for entering token and Wi-Fi credentials directly on Flipper
- persistence of discovered printers on the bridge side

## UART Protocol

Commands from Flipper:

- `PING`
- `WIFI_SCAN`
- `WIFI_CONNECT|ssid|password`
- `WIFI_STATUS`
- `BAMBU_SET_TOKEN|bearer-token`
- `BAMBU_DISCOVER`
- `BAMBU_STATUS|serial`

Responses from ESP32:

- `OK|...`
- `ERR|...`
- `WIFI|ssid|rssi|open|secure`
- `PRINTER|serial|name|model|ip|access_code|online|cloud_status`
- `STATUS|serial|state|progress|layer|total_layers|remaining|nozzle|bed|wifi|file`

## Files

- [`application.fam`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\application.fam)
- [`bambu_monitor_app.c`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\bambu_monitor_app.c)
- [`app_ui.c`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\app_ui.c)
- [`bambu_transport_live.c`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\bambu_transport_live.c)
- [`esp32_bridge/bambu_monitor_bridge.ino`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\esp32_bridge\bambu_monitor_bridge.ino)

## Suggested Next Steps

1. Build the Flipper app against your firmware tree and fix any SDK-specific compile issues.
2. Flash the ESP32 bridge and verify `PING`, `WIFI_SCAN`, and `BAMBU_DISCOVER`.
3. Add local `8883` scan and MQTT-based printer/IP matching on the ESP32.
4. Add Flipper-side settings input for token and Wi-Fi credentials.
