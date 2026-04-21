#!/usr/bin/env python3
"""
Query Bambu Lab Printer Information
====================================

Query and display comprehensive printer information from the Cloud API.
"""

import sys
import os
import json
import ipaddress
import re
import subprocess
import socket
import ssl
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

import paho.mqtt.client as mqtt

# Add parent directory to path for bambulab import
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from bambulab import BambuClient, Device
from bambulab.client import BambuAPIError


MQTT_PORT = 8883


def display_device_info(device: Device, resolved_ip=None):
    """Display device information in formatted output"""
    print(f"\n{'='*80}")
    print(f"Device: {device.name}")
    print(f"{'='*80}")
    print(f"  Serial Number:  {device.dev_id}")
    print(f"  Model:          {device.dev_product_name} ({device.dev_model_name})")
    print(f"  Structure:      {device.dev_structure}")
    print(f"  Nozzle Size:    {device.nozzle_diameter}mm")
    print(f"  Access Code:    {device.dev_access_code}")
    print(f"  Online:         {'Yes' if device.online else 'No'}")
    print(f"  Print Status:   {device.print_status}")
    print(f"  Local IP:       {resolved_ip or 'Not resolved'}")


def get_local_ipv4_addresses():
    addresses = set()

    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, family=socket.AF_INET):
            ip = info[4][0]
            if not ip.startswith("127.") and not ip.startswith("169.254."):
                addresses.add(ip)
    except socket.gaierror:
        pass

    for target in ("8.8.8.8", "1.1.1.1"):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.connect((target, 80))
                ip = sock.getsockname()[0]
                if not ip.startswith("127.") and not ip.startswith("169.254."):
                    addresses.add(ip)
        except OSError:
            continue

    return sorted(addresses)


def build_scan_targets(subnet=None):
    arp_ips = get_arp_ipv4_addresses()

    if subnet:
        network = ipaddress.ip_network(subnet, strict=False)
        subnet_hosts = [str(host) for host in network.hosts()]
        ordered = [ip for ip in arp_ips if ip in subnet_hosts]
        ordered.extend(ip for ip in subnet_hosts if ip not in ordered)
        return ordered

    targets = []
    seen = set()

    for ip in arp_ips:
        seen.add(ip)
        targets.append(ip)

    for ip in get_local_ipv4_addresses():
        network = ipaddress.ip_network(f"{ip}/24", strict=False)
        for host in network.hosts():
            host_ip = str(host)
            if host_ip == ip or host_ip in seen:
                continue
            seen.add(host_ip)
            targets.append(host_ip)

    return targets


def get_arp_ipv4_addresses():
    try:
        result = subprocess.run(
            ["arp", "-a"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return []

    if result.returncode != 0:
        return []

    ipv4_pattern = re.compile(r"\b(\d{1,3}(?:\.\d{1,3}){3})\b")
    addresses = []
    seen = set()
    for line in result.stdout.splitlines():
        match = ipv4_pattern.search(line)
        if not match:
            continue
        ip = match.group(1)
        if (
            ip.startswith("127.")
            or ip.startswith("169.254.")
            or ip.endswith(".255")
            or ip.startswith("224.")
            or ip.startswith("239.")
            or ip == "255.255.255.255"
        ):
            continue
        if ip not in seen:
            seen.add(ip)
            addresses.append(ip)

    return addresses


def has_open_port(ip, port=MQTT_PORT, timeout=0.75):
    try:
        with socket.create_connection((ip, port), timeout=timeout):
            return True
    except OSError:
        return False


def mqtt_auth_succeeds(ip, serial, access_code, timeout=4.0):
    state = {"connected": False, "done": False}

    def on_connect(client, userdata, flags, rc):
        state["connected"] = (rc == 0)
        state["done"] = True

    def on_disconnect(client, userdata, rc):
        state["done"] = True

    client = mqtt.Client(client_id=serial)
    client.username_pw_set("bblp", access_code)
    client.tls_set(cert_reqs=ssl.CERT_NONE)
    client.tls_insecure_set(True)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect

    try:
        client.connect(ip, MQTT_PORT, 10)
        client.loop_start()

        end_time = time.time() + timeout
        while time.time() < end_time:
            if state["done"]:
                break
            time.sleep(0.05)
    except Exception:
        state["connected"] = False
    finally:
        try:
            client.loop_stop()
        except Exception:
            pass
        try:
            client.disconnect()
        except Exception:
            pass

    return state["connected"]


def discover_mqtt_candidates(targets, debug=False):
    if debug:
        print(f"Scanning {len(targets)} host(s) for port {MQTT_PORT}...")

    candidates = []
    with ThreadPoolExecutor(max_workers=64) as executor:
        future_map = {
            executor.submit(has_open_port, ip): ip
            for ip in targets
        }
        for future in as_completed(future_map):
            ip = future_map[future]
            try:
                if future.result():
                    candidates.append(ip)
            except Exception:
                continue

    candidates = sorted(candidates, key=lambda value: tuple(int(part) for part in value.split(".")))
    if debug:
        if candidates:
            print(f"MQTT candidate host(s): {', '.join(candidates)}")
        else:
            print("No hosts responded on port 8883.")
    return candidates


def resolve_device_ip(device, candidates, debug=False):
    if not device.dev_access_code:
        if debug:
            print(f"  Skipping {device.name}: no access code available.")
        return None

    if not candidates:
        return None

    if debug:
        print(f"  Trying {len(candidates)} candidate host(s) for {device.name}...")

    for ip in candidates:
        if debug:
            print(f"    Testing {ip}...")
        if mqtt_auth_succeeds(ip, device.dev_id, device.dev_access_code):
            if debug:
                print(f"    Matched {device.name} -> {ip}")
            return ip
        if debug:
            print(f"    No match at {ip}")

    if debug:
        print(f"  No local IP match found for {device.name}.")
    return None


def resolve_device_ips(devices, subnet=None, debug=False):
    targets = build_scan_targets(subnet=subnet)
    if not targets:
        return {}

    candidates = discover_mqtt_candidates(targets, debug=debug)
    resolved = {}
    for device in devices:
        print(f"Resolving local IP for {device.name} ({device.dev_id})...")
        resolved[device.dev_id] = resolve_device_ip(device, candidates, debug=debug)
    return resolved


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print("Usage: python query.py <access_token> [options]")
        print()
        print("Arguments:")
        print("  access_token:  Your Bambu Lab access token")
        print()
        print("Options:")
        print("  --devices      Show device list (default)")
        print("  --status       Show print status")
        print("  --profile      Show user profile")
        print("  --projects     Show projects")
        print("  --firmware     Show firmware info")
        print("  --resolve-ip   Best-effort scan for local printer IPs")
        print("  --debug-ip     Print IP discovery progress")
        print("  --subnet CIDR  Limit IP scan to a subnet such as 192.168.1.0/24")
        print("  --json         Output in JSON format")
        print("  --device <id>  Filter by device ID")
        print()
        print("Examples:")
        print("  python query.py AADBD2wZe_token...")
        print("  python query.py AADBD2wZe_token... --status")
        print("  python query.py AADBD2wZe_token... --device 01S00A000000000")
        print("  python query.py AADBD2wZe_token... --resolve-ip")
        print("  python query.py AADBD2wZe_token... --resolve-ip --subnet 192.168.50.0/24")
        sys.exit(1)
    
    access_token = sys.argv[1]
    args = sys.argv[2:]
    
    # Parse options
    show_json = '--json' in args
    show_status = '--status' in args
    show_profile = '--profile' in args
    show_projects = '--projects' in args
    show_firmware = '--firmware' in args
    resolve_ip = '--resolve-ip' in args
    debug_ip = '--debug-ip' in args
    
    device_filter = None
    if '--device' in args:
        idx = args.index('--device')
        if idx + 1 < len(args):
            device_filter = args[idx + 1]

    subnet = None
    if '--subnet' in args:
        idx = args.index('--subnet')
        if idx + 1 < len(args):
            subnet = args[idx + 1]
    
    # Default to showing devices
    show_devices = not any([show_status, show_profile, show_projects, show_firmware])
    
    # Create API client
    try:
        client = BambuClient(access_token)
    except Exception as e:
        print(f"Error: Failed to create API client: {e}")
        sys.exit(1)
    
    print("=" * 80)
    print("Bambu Lab Printer Information Query")
    print("=" * 80)
    print()

    resolved_ips = {}
    
    # Query devices
    if show_devices or device_filter:
        try:
            print("Fetching device list...")
            devices_data = client.get_devices()
            devices = [Device.from_dict(d) for d in devices_data]
            
            if device_filter:
                devices = [d for d in devices if d.dev_id == device_filter]

            if resolve_ip and devices:
                resolved_ips = resolve_device_ips(devices, subnet=subnet, debug=debug_ip)
            
            if show_json:
                payload = []
                for device in devices:
                    device_data = device.to_dict()
                    if resolve_ip:
                        device_data["local_ip"] = resolved_ips.get(device.dev_id)
                    payload.append(device_data)
                print(json.dumps(payload, indent=2))
            else:
                print(f"\nFound {len(devices)} device(s):")
                for device in devices:
                    display_device_info(device, resolved_ip=resolved_ips.get(device.dev_id))
        
        except BambuAPIError as e:
            print(f"Error fetching devices: {e}")
            sys.exit(1)
    
    # Query print status
    if show_status:
        try:
            print("\nFetching print status...")
            status = client.get_print_status(force=True)
            
            if show_json:
                print(json.dumps(status, indent=2))
            else:
                devices = status.get('devices', [])
                print(f"\nPrint status for {len(devices)} device(s):")
                for device_status in devices:
                    if device_filter and device_status.get('dev_id') != device_filter:
                        continue
                    
                    print(f"\n{'='*80}")
                    print(f"{device_status.get('name', 'Unknown')} ({device_status.get('dev_id')})")
                    print(f"{'='*80}")
                    print(f"  Status: {device_status.get('print_status', 'Unknown')}")
                    
                    if 'print' in device_status:
                        print_data = device_status['print']
                        
                        # Progress
                        if 'mc_percent' in print_data:
                            print(f"  Progress: {print_data['mc_percent']}%")
                        if 'layer_num' in print_data and 'total_layer_num' in print_data:
                            print(f"  Layer: {print_data['layer_num']}/{print_data['total_layer_num']}")
                        if 'mc_remaining_time' in print_data:
                            remaining = print_data['mc_remaining_time']
                            hours = remaining // 60
                            minutes = remaining % 60
                            print(f"  Time Remaining: {hours}h {minutes}m")
                        
                        # Temperatures
                        print(f"\n  Temperatures:")
                        if 'nozzle_temper' in print_data:
                            target = print_data.get('nozzle_target_temper', 0)
                            print(f"    Nozzle: {print_data['nozzle_temper']}C -> {target}C")
                        if 'bed_temper' in print_data:
                            target = print_data.get('bed_target_temper', 0)
                            print(f"    Bed: {print_data['bed_temper']}C -> {target}C")
                        if 'chamber_temper' in print_data:
                            print(f"    Chamber: {print_data['chamber_temper']}C")
                        
                        # Fans
                        if any(k in print_data for k in ['cooling_fan_speed', 'aux_part_fan', 'chamber_fan']):
                            print(f"\n  Fans:")
                            if 'cooling_fan_speed' in print_data:
                                print(f"    Cooling: {print_data['cooling_fan_speed']}%")
                            if 'aux_part_fan' in print_data:
                                print(f"    Aux: {print_data['aux_part_fan']}%")
                            if 'chamber_fan' in print_data:
                                print(f"    Chamber: {print_data['chamber_fan']}%")
                        
                        # File info
                        if 'gcode_file' in print_data:
                            print(f"\n  File: {print_data['gcode_file']}")
                        if 'gcode_state' in print_data:
                            print(f"  G-code State: {print_data['gcode_state']}")
        
        except BambuAPIError as e:
            print(f"Error fetching status: {e}")
            sys.exit(1)
    
    # Query user profile
    if show_profile:
        try:
            print("\nFetching user profile...")
            profile = client.get_user_profile()
            
            if show_json:
                print(json.dumps(profile, indent=2))
            else:
                print(f"\nUser Profile:")
                print(f"  UID:     {profile.get('uid', 'N/A')}")
                print(f"  Name:    {profile.get('name', 'N/A')}")
                print(f"  Account: {profile.get('account', 'N/A')}")
                if 'productModels' in profile:
                    print(f"  Printers: {', '.join(profile['productModels'])}")
        
        except BambuAPIError as e:
            print(f"Error fetching profile: {e}")
            sys.exit(1)
    
    # Query projects
    if show_projects:
        try:
            print("\nFetching projects...")
            projects = client.get_projects()
            
            if show_json:
                print(json.dumps(projects, indent=2))
            else:
                print(f"\nFound {len(projects)} project(s):")
                for project in projects:
                    print(f"\n  {project.get('name', 'Unnamed')}")
                    print(f"    ID: {project.get('id', 'N/A')}")
                    if 'created' in project:
                        print(f"    Created: {project['created']}")
        
        except BambuAPIError as e:
            print(f"Error fetching projects: {e}")
            sys.exit(1)
    
    # Query firmware
    if show_firmware and device_filter:
        try:
            print(f"\nFetching firmware info for {device_filter}...")
            firmware = client.get_device_version(device_filter)
            
            if show_json:
                print(json.dumps(firmware, indent=2))
            else:
                print(f"\nFirmware Information:")
                if 'devices' in firmware:
                    for device in firmware['devices']:
                        if device.get('dev_id') == device_filter:
                            fw = device.get('firmware', {})
                            print(f"  Current:  {fw.get('current_version', 'N/A')}")
                            print(f"  Available: {fw.get('available_version', 'N/A')}")
                            print(f"  Force Update: {fw.get('force_update', False)}")
        
        except BambuAPIError as e:
            print(f"Error fetching firmware: {e}")
            sys.exit(1)
    
    print()
    print("=" * 80)


if __name__ == '__main__':
    main()
