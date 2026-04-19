import paho.mqtt.client as mqtt
import json
import base64
import os
import time
from datetime import datetime
from ascon128 import ascon128_encrypt, ascon128_decrypt
import secrets

KEYFILE = "keys.json"

if not os.path.exists(KEYFILE):
    raise Exception("keys.json missing! Cannot start secure gateway.")

with open(KEYFILE, "r") as f:
    keydata = json.load(f)

K_DB = bytes.fromhex(keydata["K_DB"])

BROKER_IP = "10.183.112.4"
DEVICE_IDS = ["ESP32_H", "ESP32_L"]

THRESHOLD_HIGH = 40.0   # LHS too humid if > 40
THRESHOLD_LOW  = 30.0   # HHS too dry if < 30

OFFLINE_TIMEOUT = 30.0

K_device = bytearray([
    0x10,0x20,0x30,0x40,
    0x50,0x60,0x70,0x80,
    0x90,0xA0,0xB0,0xC0,
    0xD0,0xE0,0xF0,0x00
])

last_ctr = {"ESP32_H": 0, "ESP32_L": 0}
current_alert_state = "NONE"

last_seen = {dev: None for dev in DEVICE_IDS}
offline_state = {dev: False for dev in DEVICE_IDS}

LOG_DIR = "logs"
os.makedirs(LOG_DIR, exist_ok=True)

def encrypt_payload(plaintext: str) -> dict:
    """Encrypt small control messages (ALERT / RESET / OFFLINE / ROTATE_KEY) with K_device."""
    nonce = bytes([0] * 16) 
    ct_list = []
    ascon128_encrypt(ct_list, plaintext.encode(), bytes(K_device), nonce)
    return {
        "cipher": base64.b64encode(bytes(ct_list)).decode(),
        "nonce":  base64.b64encode(nonce).decode(),
        "ctr": 0
    }

def decrypt_wrapper(wrapper: dict) -> str | None:
    """Decrypt outer wrapper from ESP32 (status messages)."""
    try:
        cipher = base64.b64decode(wrapper["cipher"])
        nonce  = base64.b64decode(wrapper["nonce"])
    except Exception:
        print("[DROP] Invalid base64 wrapper")
        return None

    pt_list = []
    rc = ascon128_decrypt(pt_list, list(cipher), bytes(K_device), nonce)
    if rc < 0:
        print("[DROP] Decryption failed")
        return None

    try:
        return bytes(pt_list).decode()
    except Exception:
        print("[DROP] UTF-8 decode error")
        return None


def log_encrypted_sensor(device_id, humidity, temp, ctr_val):
    entry = json.dumps({
        "ts": datetime.utcnow().isoformat(),
        "id": device_id,
        "humidity": humidity,
        "temp": temp,
        "ctr": ctr_val
    })

    nonce = os.urandom(16)
    ct_list = []
    ascon128_encrypt(ct_list, entry.encode(), bytes(K_DB), nonce)

    line = json.dumps({
        "nonce":  base64.b64encode(nonce).decode(),
        "cipher": base64.b64encode(bytes(ct_list)).decode()
    })

    fname = os.path.join(LOG_DIR, datetime.utcnow().strftime("%Y-%m-%d") + ".log")
    with open(fname, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def send_encrypted_alert(msg: str):
    global current_alert_state
    wrapper = encrypt_payload(msg)
    print(f"[GATEWAY] Sending encrypted {msg}")
    client.publish("alert", json.dumps(wrapper))
    current_alert_state = msg


def rotate_key_if_needed():
    global K_device, last_key_rotation

    if time.time() - last_key_rotation < 10000:
        return

    print("\n***** ROTATING DEVICE KEY *****\n")

    new_key = secrets.token_bytes(16)
    K_device = bytearray(new_key)

    msg = json.dumps({
        "cmd": "ROTATE_KEY",
        "new_key": base64.b64encode(new_key).decode()
    })

    wrapper = encrypt_payload(msg)
    client.publish("key_update", json.dumps(wrapper))

    print("[SERVER] New key rotated & pushed to ESP32s.")
    last_key_rotation = time.time()


def process_sensor(device_id, humidity, temp, ctr_val):
    now = time.time()
    last_seen[device_id] = now

    if offline_state[device_id]:
        print(f"[INFO] {device_id} came back online -> sending RESET")
        offline_state[device_id] = False
        send_encrypted_alert("RESET")

    print(f"[OK] From {device_id}: H={humidity}, T={temp}, ctr={ctr_val}")
    log_encrypted_sensor(device_id, humidity, temp, ctr_val)

    for other in DEVICE_IDS:
        if other != device_id and offline_state[other]:
            print(f"[INFO] {other} is offline -> suppressing alerts for {device_id}")
            return

    if device_id == "ESP32_L":
        if humidity > THRESHOLD_HIGH:
            send_encrypted_alert("ALERT_HIGH")
            return

    if device_id == "ESP32_H":
        if humidity < THRESHOLD_LOW:
            send_encrypted_alert("ALERT_LOW")
            return


def on_message(client, userdata, msg):
    global current_alert_state

    if msg.topic == "ack":
        device = msg.payload.decode(errors="ignore")
        print(f"[ACK RECEIVED] from {device}")

        if any(offline_state.values()):
            print("[INFO] Ignoring ACK while offline state active")
            return

        send_encrypted_alert("RESET")
        return

    if msg.topic != "status":
        return

    try:
        wrapper = json.loads(msg.payload.decode())
    except Exception:
        print("[DROP] wrapper JSON error")
        return

    plaintext = decrypt_wrapper(wrapper)
    if plaintext is None:
        return

    try:
        obj = json.loads(plaintext)
    except Exception:
        print("[DROP] inner JSON invalid")
        return

    device_id = obj.get("id")
    humidity  = obj.get("humidity")
    temp      = obj.get("temp")
    ctr_val   = obj.get("ctr")

    if device_id not in DEVICE_IDS:
        print("[DROP] Unknown device ID")
        return

    if ctr_val is None or ctr_val <= last_ctr[device_id]:
        print("[DROP] replay detected")
        return

    last_ctr[device_id] = ctr_val
    process_sensor(device_id, humidity, temp, ctr_val)


client = mqtt.Client()
client.connect(BROKER_IP)
client.on_message = on_message

client.subscribe("status")
client.subscribe("ack")

print("SERVER GATEWAY RUNNING (offline detection, secure DB, key rotation)...")

last_key_rotation = time.time()
client.loop_start()

try:
    while True:
        rotate_key_if_needed()

        now = time.time()
        for dev in DEVICE_IDS:
            ts = last_seen.get(dev)
            if ts is None:
                continue

            if not offline_state[dev] and (now - ts) > OFFLINE_TIMEOUT:
                offline_state[dev] = True
                if dev == "ESP32_H":
                    print("[OFFLINE] ESP32_H missing -> OFFLINE_H")
                    send_encrypted_alert("OFFLINE_H")
                elif dev == "ESP32_L":
                    print("[OFFLINE] ESP32_L missing -> OFFLINE_L")
                    send_encrypted_alert("OFFLINE_L")

        time.sleep(1)

except KeyboardInterrupt:
    client.loop_stop()
    client.disconnect()
