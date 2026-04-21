# ESP32 Bridge

This sketch is the network coprocessor for the Flipper-side `Bambu Fleet` app.

## Responsibilities

- connect to Wi-Fi
- store Wi-Fi credentials and bearer token in flash
- call the Bambu cloud API over HTTPS
- emit compact UART protocol lines back to the Flipper

## Current Commands

- `PING`
- `WIFI_SCAN`
- `WIFI_CONNECT|ssid|password`
- `WIFI_STATUS`
- `BAMBU_SET_TOKEN|bearer-token`
- `BAMBU_DISCOVER`
- `BAMBU_STATUS|serial`

## Arduino Libraries

This scaffold expects:

- `WiFi`
- `HTTPClient`
- `WiFiClientSecure`
- `Preferences`
- `ArduinoJson`

## Wiring

Default UART wiring matches the `FlipperWalkPrint` approach:

- ESP32 `GPIO1/TX0` -> Flipper `PB7`
- ESP32 `GPIO3/RX0` -> Flipper `PB6`
- `GND` -> `GND`

Baud rate:

- `115200`
- `8N1`

## Notes

- The current sketch fetches cloud printer metadata and cloud print-status summaries.
- It does not yet implement the local MQTT/IP matching logic from the Python discovery path.
- That is the main feature needed to fully mirror `query.py --resolve-ip`.
