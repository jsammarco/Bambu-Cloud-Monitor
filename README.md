# Bambu Cloud Monitor

Simple terminal dashboard for monitoring a Bambu Lab printer over MQTT on your local network.

This project connects directly to the printer, requests a full status snapshot, and keeps refreshing the dashboard with live print telemetry such as progress, layer counts, temperatures, fan speeds, WiFi signal, and the current file name.

![Bambu Cloud Monitor Dashboard](https://raw.githubusercontent.com/jsammarco/Bambu-Cloud-Monitor/refs/heads/main/Images/Screenshot.JPG)

It also includes a multi-printer mode that can discover every printer on your account, cache the results, and show all active jobs in one console dashboard.

## Features

- Live terminal dashboard
- Print progress bar and remaining time
- Nozzle and bed temperature monitoring
- Fan speed and WiFi signal display
- Automatic refresh requests every 5 seconds
- Helper scripts to retrieve a bearer token and list printer metadata
- Multi-printer dashboard with cached discovery
- Local configuration through `.env`

## Requirements

- Python 3
- `paho-mqtt`
- A Bambu Lab printer reachable on your local network
- Your printer IP address, serial number, and access code

## Setup

1. Install the dependencies:

```bash
pip install -r requirements.txt
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

## Get Your Token And Printer Details

If you do not already have the values needed for `.env`, use the helper scripts in this repository.

### 1. Get a bearer token

Run:

```bash
python login.py
```

`login.py` authenticates with Bambu Lab and obtains a bearer token. It can also save the token locally, depending on how your local `bambulab` library is configured.

### 2. Query your printers

Run:

```bash
python query.py [BEARER_TOKEN]
```

This queries the Bambu Cloud API and lists the printers on your account. The current script outputs:

- printer name
- serial number
- model information
- access code
- online state
- print status

The two values most useful for `monitor.py` are:

- `Serial Number` -> `SERIAL`
- `Access Code` -> `ACCESS_CODE`

### 3. Get the printer IP

`query.py` now includes a best-effort local IP discovery mode. It scans your local subnet for hosts with port `8883` open, then tries the same local MQTT authentication used by `monitor.py` to match each printer's access code.

Run:

```bash
python query.py [BEARER_TOKEN] --resolve-ip
```

If you want to limit the scan to a known subnet, use:

```bash
python query.py [BEARER_TOKEN] --resolve-ip --subnet 192.168.50.0/24
```

For step-by-step discovery logs, add `--debug-ip`:

```bash
python query.py [BEARER_TOKEN] --resolve-ip --debug-ip --subnet 192.168.50.0/24
```

When discovery succeeds, `query.py` will print a `Local IP` line for each device.

Example output:

```text
================================================================================
Bambu Lab Printer Information Query
================================================================================

Fetching device list...
Scanning 254 host(s) for port 8883...
MQTT candidate host(s): 192.168.50.27, 192.168.50.162, 192.168.50.165
Resolving local IP for Example Printer (01XXXXXXXXXXXXX)...
  Trying 3 candidate host(s) for Example Printer...
    Testing 192.168.50.27...
    No match at 192.168.50.27
    Testing 192.168.50.162...
    Matched Example Printer -> 192.168.50.162

Found 1 device(s):

================================================================================
Device: Example Printer
================================================================================
  Serial Number:  01XXXXXXXXXXXXX
  Model:          P1S (C12)
  Structure:      CoreXY
  Nozzle Size:    0.4mm
  Access Code:    ********
  Online:         Yes
  Print Status:   RUNNING
  Local IP:       192.168.50.162
================================================================================
```

This is still best-effort. If the printer is on another VLAN, asleep, blocked by firewall rules, or your network is not a `/24`, auto-discovery may miss it.

Common ways to find it:

- check your router or DHCP client list
- check the printer's network settings from the device itself
- use your router app or web interface to match the printer by hostname or MAC address

Once you have the IP, place it in:

```env
PRINTER_IP=192.168.50.162
```

Then combine the IP with the serial number and access code returned by `query.py`.

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

## Monitor All Printers

Use `monitor_all.py` to discover all printers tied to your bearer token, cache the results, and open one combined dashboard for every resolved device.

First run with rediscovery:

```bash
python monitor_all.py [BEARER_TOKEN] --rediscover --subnet 192.168.50.0/24
```

Later runs can reuse the cached discovery file:

```bash
python monitor_all.py [BEARER_TOKEN]
```

To force a refresh of `found_printers.json`, pass `--rediscover` again.

Useful flags:

- `--rediscover`: rebuild the printer cache
- `--subnet 192.168.50.0/24`: narrow the discovery scan
- `--debug-ip`: print detailed discovery logs instead of progress bars
- `--cache-file custom_found_printers.json`: use a different cache file

`monitor_all.py` stores discovery results in `found_printers.json` so restarting the app does not require a fresh scan every time.

![Bambu Cloud Monitor All Printers Dashboard](https://raw.githubusercontent.com/jsammarco/Bambu-Cloud-Monitor/refs/heads/main/Images/MonitorAll%20Screenshot.JPG)

## Environment Variables

The project reads configuration from `.env` or your process environment:

- `PRINTER_IP`: local IP address of the printer
- `SERIAL`: printer serial number used in MQTT topics
- `ACCESS_CODE`: printer access code used for MQTT authentication

If any of these are missing, `monitor.py` raises a startup error explaining how to create `.env`.

## Project Files

- [`login.py`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\login.py): gets a bearer token through the Bambu authentication flow
- [`monitor.py`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\monitor.py): the main MQTT dashboard script
- [`monitor_all.py`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\monitor_all.py): discovers, caches, and monitors all printers in one dashboard
- [`query.py`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\query.py): lists printers, shows serial numbers and access codes, and can attempt local IP discovery
- [`requirements.txt`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\requirements.txt): Python dependencies for the project
- [`sample.env`](C:\Users\Joe\Projects\Bambu-Cloud-Monitor\sample.env): example environment file
- [`Images/MonitorAll Screenshot.JPG`](<C:\Users\Joe\Projects\Bambu-Cloud-Monitor\Images\MonitorAll Screenshot.JPG>): multi-printer dashboard screenshot
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
