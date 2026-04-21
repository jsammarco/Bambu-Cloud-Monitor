# Bambu Cloud Monitor

Simple terminal dashboard for monitoring a Bambu Lab printer over MQTT on your local network.

This project connects directly to the printer, requests a full status snapshot, and keeps refreshing the dashboard with live print telemetry such as progress, layer counts, temperatures, fan speeds, WiFi signal, and the current file name.

## Features

- Live terminal dashboard
- Print progress bar and remaining time
- Nozzle and bed temperature monitoring
- Fan speed and WiFi signal display
- Automatic refresh requests every 5 seconds
- Local configuration through `.env`

## Requirements

- Python 3
- `paho-mqtt`
- A Bambu Lab printer reachable on your local network
- Your printer IP address, serial number, and access code

## Setup

1. Install the dependency:

```bash
pip install paho-mqtt
```

2. Copy the sample config:

```bash
copy sample.env .env
```

3. Edit `.env` and fill in your printer details:

```env
PRINTER_IP=192.168.50.162
SERIAL=YOUR_PRINTER_SERIAL
ACCESS_CODE=YOUR_ACCESS_CODE
```

## Usage

Run the monitor with:

```bash
python monitor.py
```

The script will:

- connect to the printer over TLS on port `8883`
- subscribe to `device/<serial>/report`
- publish a `pushall` request to `device/<serial>/request`
- redraw the dashboard whenever new status data arrives

## Environment Variables

The project reads configuration from `.env` or your process environment:

- `PRINTER_IP`: local IP address of the printer
- `SERIAL`: printer serial number used in MQTT topics
- `ACCESS_CODE`: printer access code used for MQTT authentication

If any of these are missing, `monitor.py` raises a startup error explaining how to create `.env`.

## Project Files

- [`monitor.py`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\monitor.py): the main MQTT dashboard script
- [`sample.env`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\sample.env): example environment file
- [`Images/Screenshot.JPG`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\Images\Screenshot.JPG): example dashboard screenshot

## Research Notes

This monitor was informed by the Bambu Lab protocol research documented in [`coelacant1/Bambu-Lab-Cloud-API`](https://github.com/coelacant1/Bambu-Lab-Cloud-API).

In particular, this project follows the MQTT topic pattern and status request behavior documented in:

- [`README.md`](https://github.com/coelacant1/Bambu-Lab-Cloud-API/blob/main/README.md)
- [`API_INDEX.md`](https://github.com/coelacant1/Bambu-Lab-Cloud-API/blob/main/API_INDEX.md)
- [`API_MQTT.md`](https://github.com/coelacant1/Bambu-Lab-Cloud-API/blob/main/API_MQTT.md)

Relevant details from that research include:

- Bambu printers use MQTT over TLS on port `8883`
- printer updates are exposed on `device/<device_serial>/report`
- commands are published to `device/<device_serial>/request`
- the `pushall` request returns a full status payload
- common print fields include `mc_percent`, `layer_num`, `total_layer_num`, `mc_remaining_time`, `gcode_state`, `nozzle_temper`, `bed_temper`, and `wifi_signal`

This repository does not bundle or reimplement the upstream library. It uses the documented MQTT behavior to build a small local monitoring dashboard.

## Disclaimer

This project is not affiliated with or endorsed by Bambu Lab.

Use it at your own risk and only with hardware and credentials you control.

## License

See [`LICENSE`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\LICENSE).
