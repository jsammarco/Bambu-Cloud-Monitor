import paho.mqtt.client as mqtt
import ssl
import json
import os
import time
import threading

def load_env_file(path=".env"):
    if not os.path.exists(path):
        return

    with open(path, "r", encoding="utf-8") as env_file:
        for raw_line in env_file:
            line = raw_line.strip()

            if not line or line.startswith("#") or "=" not in line:
                continue

            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip().strip('"').strip("'")

            os.environ.setdefault(key, value)


load_env_file()


def require_env(name):
    value = os.getenv(name)
    if value:
        return value

    raise RuntimeError(
        f"Missing required environment variable: {name}. "
        "Copy sample.env to .env and fill in your printer details."
    )


PRINTER_IP = require_env("PRINTER_IP")
SERIAL = require_env("SERIAL")
ACCESS_CODE = require_env("ACCESS_CODE")

TOPIC = f"device/{SERIAL}/report"

state = {
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
    "file": None
}

def request_full_status(client):
    payload = {
        "pushing": {
            "sequence_id": "0",
            "command": "pushall"
        }
    }

    client.publish(
        f"device/{SERIAL}/request",
        json.dumps(payload)
    )

def clear():
    os.system("cls" if os.name == "nt" else "clear")

def format_time(minutes):
    if minutes is None:
        return "?"

    h = minutes // 60
    m = minutes % 60

    return f"{h}h {m}m"

def draw_dashboard():
    clear()

    progress = state["progress"] or 0
    bar_len = 40
    filled = int(progress / 100 * bar_len)
    bar = "#" * filled + "-" * (bar_len - filled)

    print("BAMBU PRINTER DASHBOARD")
    print("=" * 50)

    print(f"Printer State : {state['state']}")
    print(f"File          : {state['file']}")
    print(f"Progress      : [{bar}] {progress}%")

    print(f"Layer         : {state['layer']}/{state['total_layers']}")
    print(f"Remaining     : {format_time(state['remaining'])}")

    print()
    print("Temperatures")
    print(f"Nozzle        : {state['nozzle_temp']} °C")
    print(f"Bed           : {state['bed_temp']} °C")

    print()
    print("Machine")
    print(f"Speed Level   : {state['speed']}")
    print(f"Cooling Fan   : {state['fan']}")
    print(f"Aux Fan 1     : {state['fan_aux1']}")
    print(f"Aux Fan 2     : {state['fan_aux2']}")
    print(f"WiFi Signal   : {state['wifi']}")

    print()
    print("Last update:", time.strftime("%H:%M:%S"))
    print("=" * 50)


def refresh_loop(client):
    while True:
        time.sleep(5)
        request_full_status(client)


def on_connect(client, userdata, flags, rc):
    print("Connected to printer MQTT")

    client.subscribe(TOPIC)

    time.sleep(1)

    request_full_status(client)

    threading.Thread(target=refresh_loop, args=(client,), daemon=True).start()


def on_message(client, userdata, msg):
    global state

    try:
        data = json.loads(msg.payload.decode())

        if "print" in data:
            p = data["print"]

            # Progress
            if "mc_percent" in p:
                state["progress"] = p["mc_percent"]

            if "layer_num" in p:
                state["layer"] = p["layer_num"]

            if "total_layer_num" in p:
                state["total_layers"] = p["total_layer_num"]

            if "mc_remaining_time" in p:
                state["remaining"] = p["mc_remaining_time"]

            if "gcode_state" in p:
                state["state"] = p["gcode_state"]

            if "spd_lvl" in p:
                state["speed"] = p["spd_lvl"]

            # temperatures
            if "nozzle_temper" in p:
                state["nozzle_temp"] = round(p["nozzle_temper"],1)

            if "bed_temper" in p:
                state["bed_temp"] = round(p["bed_temper"],1)

            # fans
            if "cooling_fan_speed" in p:
                state["fan"] = p["cooling_fan_speed"]

            if "big_fan1_speed" in p:
                state["fan_aux1"] = p["big_fan1_speed"]

            if "big_fan2_speed" in p:
                state["fan_aux2"] = p["big_fan2_speed"]

            # wifi
            if "wifi_signal" in p:
                state["wifi"] = p["wifi_signal"]

            # file name
            if "gcode_file" in p:
                state["file"] = p["gcode_file"]

        draw_dashboard()

    except Exception as e:
        print("Parse error:", e)


client = mqtt.Client(client_id=SERIAL)

client.username_pw_set("bblp", ACCESS_CODE)

client.tls_set(cert_reqs=ssl.CERT_NONE)
client.tls_insecure_set(True)

client.on_connect = on_connect
client.on_message = on_message

client.connect(PRINTER_IP, 8883, 60)

draw_dashboard()

client.loop_forever()
