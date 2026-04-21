#!/usr/bin/env python3
"""
Monitor all discovered Bambu printers from one console dashboard.
"""

import argparse
import json
import os
import ssl
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import paho.mqtt.client as mqtt

# Add parent directory to path for bambulab import
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from bambulab import BambuClient, Device
from bambulab.client import BambuAPIError
from query import build_scan_targets, has_open_port, mqtt_auth_succeeds


MQTT_PORT = 8883
CACHE_PATH = Path("found_printers.json")
DISCOVERY_TIMEOUT = 0.75
REFRESH_SECONDS = 5
DASHBOARD_REFRESH_SECONDS = 1


PRINT_STATE_KEYS = {
    "mc_percent": "progress",
    "layer_num": "layer",
    "total_layer_num": "total_layers",
    "mc_remaining_time": "remaining",
    "gcode_state": "state",
    "spd_lvl": "speed",
    "cooling_fan_speed": "fan",
    "big_fan1_speed": "fan_aux1",
    "big_fan2_speed": "fan_aux2",
    "wifi_signal": "wifi",
    "gcode_file": "file",
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Monitor all Bambu printers from one dashboard."
    )
    parser.add_argument("access_token", help="Bambu bearer token")
    parser.add_argument(
        "--rediscover",
        action="store_true",
        help="Refresh found_printers.json instead of using the cached printer list",
    )
    parser.add_argument(
        "--debug-ip",
        action="store_true",
        help="Print detailed discovery logs instead of progress bars",
    )
    parser.add_argument(
        "--subnet",
        help="Limit discovery to a subnet such as 192.168.50.0/24",
    )
    parser.add_argument(
        "--cache-file",
        default=str(CACHE_PATH),
        help="Path to the discovered printer cache file",
    )
    return parser.parse_args()


def clear():
    os.system("cls" if os.name == "nt" else "clear")


def format_time(minutes):
    if minutes in (None, ""):
        return "?"

    try:
        minutes = int(minutes)
    except (TypeError, ValueError):
        return str(minutes)

    hours = minutes // 60
    mins = minutes % 60
    return f"{hours}h {mins}m"


def make_progress_bar(current, total, width=28):
    if total <= 0:
        return "[" + ("-" * width) + "]"

    ratio = min(max(current / total, 0), 1)
    filled = int(width * ratio)
    return "[" + ("#" * filled) + ("-" * (width - filled)) + "]"


def print_progress(prefix, current, total):
    bar = make_progress_bar(current, total)
    message = f"\r{prefix} {bar} {current}/{total}"
    print(message, end="", flush=True)
    if current >= total:
        print()


def fetch_devices(access_token):
    client = BambuClient(access_token)
    devices_data = client.get_devices()
    return [Device.from_dict(device) for device in devices_data]


def scan_mqtt_candidates(targets, debug=False):
    if debug:
        print(f"Scanning {len(targets)} host(s) for port {MQTT_PORT}...")

    candidates = []
    completed = 0
    with ThreadPoolExecutor(max_workers=64) as executor:
        future_map = {
            executor.submit(has_open_port, ip, MQTT_PORT, DISCOVERY_TIMEOUT): ip
            for ip in targets
        }
        for future in as_completed(future_map):
            ip = future_map[future]
            completed += 1
            try:
                if future.result():
                    candidates.append(ip)
            except Exception:
                pass

            if debug:
                continue
            print_progress("Scanning subnet", completed, len(targets))

    candidates = sorted(candidates, key=lambda value: tuple(int(part) for part in value.split(".")))
    if debug:
        if candidates:
            print(f"MQTT candidate host(s): {', '.join(candidates)}")
        else:
            print("No hosts responded on port 8883.")
    else:
        print(f"Found {len(candidates)} MQTT candidate host(s).")
    return candidates


def resolve_device_ips(devices, candidates, debug=False):
    resolved = {}
    total = len(devices)

    for index, device in enumerate(devices, start=1):
        if debug:
            print(f"Resolving local IP for {device.name} ({device.dev_id})...")
            print(f"  Trying {len(candidates)} candidate host(s) for {device.name}...")

        resolved_ip = None
        for ip in candidates:
            if debug:
                print(f"    Testing {ip}...")
            if mqtt_auth_succeeds(ip, device.dev_id, device.dev_access_code):
                resolved_ip = ip
                if debug:
                    print(f"    Matched {device.name} -> {ip}")
                break
            if debug:
                print(f"    No match at {ip}")

        resolved[device.dev_id] = resolved_ip
        if not debug:
            print_progress("Resolving printers", index, total)

    return resolved


def serialize_device(device, resolved_ip):
    return {
        "name": device.name,
        "serial": device.dev_id,
        "model": device.dev_product_name,
        "model_code": device.dev_model_name,
        "structure": device.dev_structure,
        "nozzle_diameter": device.nozzle_diameter,
        "access_code": device.dev_access_code,
        "online": device.online,
        "print_status": device.print_status,
        "ip": resolved_ip,
    }


def discover_printers(access_token, subnet=None, debug=False):
    print("Fetching device list...")
    devices = fetch_devices(access_token)
    targets = build_scan_targets(subnet=subnet)
    if not targets:
        raise RuntimeError("No local scan targets found for IP discovery.")

    candidates = scan_mqtt_candidates(targets, debug=debug)
    resolved_ips = resolve_device_ips(devices, candidates, debug=debug)

    discovered = []
    for device in devices:
        discovered.append(serialize_device(device, resolved_ips.get(device.dev_id)))
    return discovered


def save_cache(cache_path, printers):
    payload = {
        "saved_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "printers": printers,
    }
    Path(cache_path).write_text(json.dumps(payload, indent=2), encoding="utf-8")


def load_cache(cache_path):
    cache_file = Path(cache_path)
    if not cache_file.exists():
        return None

    data = json.loads(cache_file.read_text(encoding="utf-8"))
    return data.get("printers", [])


def make_printer_state(printer):
    return {
        "name": printer["name"],
        "serial": printer["serial"],
        "model": printer["model"],
        "ip": printer.get("ip"),
        "access_code": printer["access_code"],
        "online": printer.get("online"),
        "cloud_status": printer.get("print_status"),
        "connected": False,
        "last_update": None,
        "progress": None,
        "layer": None,
        "total_layers": None,
        "remaining": None,
        "nozzle_temp": None,
        "bed_temp": None,
        "speed": None,
        "state": None,
        "wifi": None,
        "fan": None,
        "fan_aux1": None,
        "fan_aux2": None,
        "file": None,
        "error": None,
    }


def request_full_status(client, serial):
    payload = {
        "pushing": {
            "sequence_id": "0",
            "command": "pushall",
        }
    }
    client.publish(f"device/{serial}/request", json.dumps(payload))


class PrinterMonitor:
    def __init__(self, printer_state, lock):
        self.state = printer_state
        self.lock = lock
        self.client = mqtt.Client(client_id=printer_state["serial"])
        self.client.username_pw_set("bblp", printer_state["access_code"])
        self.client.tls_set(cert_reqs=ssl.CERT_NONE)
        self.client.tls_insecure_set(True)
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message
        self.topic = f"device/{printer_state['serial']}/report"
        self._refresh_thread = None

    def connect(self):
        if not self.state["ip"]:
            with self.lock:
                self.state["error"] = "Missing local IP"
            return

        try:
            self.client.connect(self.state["ip"], MQTT_PORT, 60)
            self.client.loop_start()
        except Exception as exc:
            with self.lock:
                self.state["connected"] = False
                self.state["error"] = str(exc)

    def on_connect(self, client, userdata, flags, rc):
        with self.lock:
            self.state["connected"] = (rc == 0)
            self.state["error"] = None if rc == 0 else f"MQTT rc={rc}"

        if rc != 0:
            return

        client.subscribe(self.topic)
        request_full_status(client, self.state["serial"])

        if self._refresh_thread is None:
            self._refresh_thread = threading.Thread(target=self.refresh_loop, daemon=True)
            self._refresh_thread.start()

    def on_disconnect(self, client, userdata, rc):
        with self.lock:
            self.state["connected"] = False
            if rc != 0:
                self.state["error"] = f"Disconnected rc={rc}"

    def on_message(self, client, userdata, msg):
        try:
            data = json.loads(msg.payload.decode())
        except Exception as exc:
            with self.lock:
                self.state["error"] = f"Parse error: {exc}"
            return

        if "print" not in data:
            return

        payload = data["print"]
        with self.lock:
            for source_key, target_key in PRINT_STATE_KEYS.items():
                if source_key in payload:
                    self.state[target_key] = payload[source_key]

            if "nozzle_temper" in payload:
                self.state["nozzle_temp"] = round(payload["nozzle_temper"], 1)
            if "bed_temper" in payload:
                self.state["bed_temp"] = round(payload["bed_temper"], 1)

            self.state["last_update"] = time.strftime("%H:%M:%S")
            self.state["error"] = None

    def refresh_loop(self):
        while True:
            time.sleep(REFRESH_SECONDS)
            try:
                request_full_status(self.client, self.state["serial"])
            except Exception:
                with self.lock:
                    self.state["error"] = "Refresh failed"

    def stop(self):
        try:
            self.client.loop_stop()
        except Exception:
            pass
        try:
            self.client.disconnect()
        except Exception:
            pass


def dashboard_line(printer):
    name = printer["name"][:24].ljust(24)
    state = str(printer["state"] or printer["cloud_status"] or "?")[:10].ljust(10)
    progress = f"{printer['progress'] or 0:>3}%"
    layer = f"{printer['layer'] or '?':>4}/{printer['total_layers'] or '?':<4}"
    remaining = f"{format_time(printer['remaining']):<8}"
    nozzle = f"{printer['nozzle_temp'] if printer['nozzle_temp'] is not None else '?':>5}"
    bed = f"{printer['bed_temp'] if printer['bed_temp'] is not None else '?':>5}"
    ip = (printer["ip"] or "unresolved").ljust(15)
    status = "UP" if printer["connected"] else "DOWN"
    current_file = str(printer["file"] or "-")[:28].ljust(28)
    return (
        f"{name} {status:<4} {state} {progress} "
        f"{layer} {remaining} N:{nozzle} B:{bed} {ip} {current_file}"
    )


def draw_dashboard(printers, cache_path):
    clear()
    print("BAMBU CLOUD MONITOR - ALL PRINTERS")
    print("=" * 120)
    print(f"Cache file: {cache_path}")
    print(f"Updated: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 120)
    print("Name                     Conn State      Job% Layer      Remain   Temps          IP              File")
    print("-" * 120)

    for printer in printers:
        print(dashboard_line(printer))
        if printer["error"]:
            print(f"  Error: {printer['error']}")
        elif printer["wifi"] or printer["speed"] or printer["fan"] is not None:
            print(
                f"  WiFi: {printer['wifi'] or '?'}  "
                f"Speed: {printer['speed'] or '?'}  "
                f"Fans: main={printer['fan'] or '?'} aux1={printer['fan_aux1'] or '?'} aux2={printer['fan_aux2'] or '?'}"
            )

    print("-" * 120)
    print("Press Ctrl+C to stop.")


def main():
    args = parse_args()
    cache_path = args.cache_file

    try:
        if args.rediscover:
            printers = discover_printers(args.access_token, subnet=args.subnet, debug=args.debug_ip)
            save_cache(cache_path, printers)
        else:
            printers = load_cache(cache_path)
            if printers:
                print(f"Loaded {len(printers)} printer(s) from {cache_path}.")
            else:
                printers = discover_printers(args.access_token, subnet=args.subnet, debug=args.debug_ip)
                save_cache(cache_path, printers)

        if not any(printer.get("ip") for printer in printers):
            raise RuntimeError("No printers with resolved IP addresses were found.")

        lock = threading.Lock()
        printer_states = [make_printer_state(printer) for printer in printers]
        monitors = [PrinterMonitor(printer_state, lock) for printer_state in printer_states]

        for monitor in monitors:
            monitor.connect()

        while True:
            with lock:
                snapshot = [dict(printer_state) for printer_state in printer_states]
            draw_dashboard(snapshot, cache_path)
            time.sleep(DASHBOARD_REFRESH_SECONDS)

    except KeyboardInterrupt:
        print("\nStopping monitor...")
    except BambuAPIError as exc:
        print(f"API error: {exc}")
        sys.exit(1)
    except Exception as exc:
        print(f"Error: {exc}")
        sys.exit(1)
    finally:
        if "monitors" in locals():
            for monitor in monitors:
                monitor.stop()


if __name__ == "__main__":
    main()
