# Bambu Cloud Monitor & BambuFleet Flipper App

Tools for monitoring Bambu Lab printers from either a desktop terminal or a Flipper Zero + ESP32 bridge.

You only need a bearer token to discover your printers and view the printer settings needed by the desktop and Flipper workflows. The desktop tools use `query.py`, `monitor.py`, and `monitor_all.py` to retrieve printers from the token, resolve local IPs, cache the result in `found_printers.json`, and display single-printer or fleet dashboards over local MQTT. The Flipper workflow uses the app in [`FlipperApp`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp) with an ESP32 bridge to connect to Wi‑Fi, load a token and cached printer list from SD, browse printers on-device, and request local MQTT status for a selected printer from the handheld UI.

`monitor.py` remains the single-printer desktop option when you already know one printer's IP, serial, and access code.

![Bambu Cloud Monitor All Printers Dashboard](https://raw.githubusercontent.com/jsammarco/Bambu-Cloud-Monitor/refs/heads/main/Images/MonitorAll%20Screenshot.JPG)

![Bambu Cloud Monitor Dashboard](https://raw.githubusercontent.com/jsammarco/Bambu-Cloud-Monitor/refs/heads/main/Images/Screenshot.JPG)

| Flipper App | Flipper App |
| --- | --- |
| ![Bambu Fleet Flipper Main Menu](https://raw.githubusercontent.com/jsammarco/Bambu-Cloud-Monitor/refs/heads/main/Images/FlipperMainMenu.png) | ![Bambu Fleet Flipper Printer List](https://raw.githubusercontent.com/jsammarco/Bambu-Cloud-Monitor/refs/heads/main/Images/FlipperPrinterList.png) |
| ![Bambu Fleet Flipper Printer Details](https://raw.githubusercontent.com/jsammarco/Bambu-Cloud-Monitor/refs/heads/main/Images/FlipperPrinterDetails.png) | ![Bambu Fleet Flipper Printer Details Alternate](https://raw.githubusercontent.com/jsammarco/Bambu-Cloud-Monitor/refs/heads/main/Images/FlipperPrinterDetails2.png) |

## Features

- Single-printer terminal dashboard with live MQTT telemetry
- Multi-printer terminal dashboard with cached discovery
- Bearer-token helper scripts for login and printer lookup
- Local IP resolution by scanning `8883` and testing MQTT auth
- Flipper Zero external app with ESP32 bridge support
- SD-card token and printer-cache loading for the Flipper workflow
- Local `.env` configuration for the single-printer desktop monitor

## Requirements

- Python 3
- `requests`
- `paho-mqtt`
- A Bambu Lab printer reachable on your local network
- For Flipper mode: Flipper Zero, supported ESP32 Wi-Fi module, and a Flipper firmware checkout for building the `.fap`

## Install

```bash
pip install -r requirements.txt
```

## Desktop Setup

For the single-printer monitor:

1. Copy [`sample.env`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\sample.env) to `.env`
2. Fill in:

```env
PRINTER_IP=192.168.50.162
SERIAL=YOUR_PRINTER_SERIAL
ACCESS_CODE=YOUR_ACCESS_CODE
```

## Get Token And Printers

### 1. Get a bearer token

```bash
python login.py
```

### 2. Query printers on the account

```bash
python query.py [BEARER_TOKEN]
```

This returns printer metadata including:

- name
- serial number
- access code
- model
- online state
- cloud print status

### 3. Resolve local printer IPs

```bash
python query.py [BEARER_TOKEN] --resolve-ip
```

Useful variants:

- `python query.py [BEARER_TOKEN] --resolve-ip --subnet 192.168.50.0/24`
- `python query.py [BEARER_TOKEN] --resolve-ip --debug-ip --subnet 192.168.50.0/24`

When successful, the script prints `Local IP` for each matched printer.

## Desktop Usage

### Single printer

```bash
python monitor.py
```

This connects to the printer over TLS MQTT on port `8883`, subscribes to `device/<serial>/report`, sends `pushall` to `device/<serial>/request`, and redraws the terminal dashboard when new telemetry arrives.

### All printers

First run with discovery:

```bash
python monitor_all.py [BEARER_TOKEN] --rediscover --subnet 192.168.50.0/24
```

Later runs can reuse the cache:

```bash
python monitor_all.py [BEARER_TOKEN]
```

Useful flags:

- `--rediscover`
- `--subnet 192.168.50.0/24`
- `--debug-ip`
- `--cache-file custom_found_printers.json`

`monitor_all.py` stores discovered printers in `found_printers.json`. That file is intentionally local-only and ignored by git.

## Flipper Zero Workflow

The Flipper companion app lives in [`FlipperApp`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp).

Current design:

- Flipper handles the UI
- ESP32 handles Wi‑Fi and local MQTT
- Flipper and ESP32 communicate over UART at `115200`

Recommended asset paths on the Flipper SD card:

- `/ext/apps_assets/bambu_fleet/bambu_token.txt`
- `/ext/apps_assets/bambu_fleet/found_printers.json`

Current practical flow:

1. Build and install the `.fap`
2. Flash the ESP32 sketch
3. Copy `bambu_token.txt` and `found_printers.json` to `/ext/apps_assets/bambu_fleet`
4. Connect Wi‑Fi from the Flipper app
5. Load printers from SD and open a printer detail page

Important note:

- The Flipper/ESP32 work is still in active iteration
- local MQTT status is partially working, but reliability varies by printer and timing
- the desktop Python tools are the most reliable monitoring path today

See [`FlipperApp/README.md`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp\README.md) for the current Flipper-specific details.

## Project Files

- [`login.py`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\login.py): gets a bearer token
- [`query.py`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\query.py): lists printers and can resolve local IPs
- [`monitor.py`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\monitor.py): single-printer desktop dashboard
- [`monitor_all.py`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\monitor_all.py): fleet discovery and monitoring dashboard
- [`requirements.txt`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\requirements.txt): Python dependencies
- [`sample.env`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\sample.env): example environment file
- [`FlipperApp`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\FlipperApp): Flipper Zero app and ESP32 bridge

## Research Notes

This project was informed by the protocol research in [`coelacant1/Bambu-Lab-Cloud-API`](https://github.com/coelacant1/Bambu-Lab-Cloud-API), especially:

- [`README.md`](https://github.com/coelacant1/Bambu-Lab-Cloud-API/blob/main/README.md)
- [`API_INDEX.md`](https://github.com/coelacant1/Bambu-Lab-Cloud-API/blob/main/API_INDEX.md)
- [`API_MQTT.md`](https://github.com/coelacant1/Bambu-Lab-Cloud-API/blob/main/API_MQTT.md)

Relevant pieces used here:

- MQTT over TLS on port `8883`
- `device/<serial>/report`
- `device/<serial>/request`
- `pushall`
- status fields such as `mc_percent`, `layer_num`, `total_layer_num`, `mc_remaining_time`, `gcode_state`, `nozzle_temper`, `bed_temper`, and `wifi_signal`

## Disclaimer

This project is not affiliated with or endorsed by Bambu Lab.

Use it only with hardware, networks, and credentials you control.

## License

See [`LICENSE`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\LICENSE).
