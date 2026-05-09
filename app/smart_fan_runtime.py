import time
import json
import threading
import serial
import paho.mqtt.client as mqtt

from fan_control_system import SmartFanSystem

# =========================================================
# CONFIG
# =========================================================
UART_PORT = "/dev/ttyAMA0"
UART_BAUD = 115200

MQTT_BROKER = "localhost"
MQTT_PORT = 1883

NODE_ID = "rpi_env02"

TOPIC_BASE = f"integration/smart/v1/env/{NODE_ID}"

TOPIC_CMD = f"{TOPIC_BASE}/command"
TOPIC_STAT = f"{TOPIC_BASE}/status"
TOPIC_ACK = f"{TOPIC_BASE}/ack"
TOPIC_AVAILABILITY = f"{TOPIC_BASE}/availability"
TOPIC_CONTROLLER_HEARTBEAT = f"{TOPIC_BASE}/controller/heartbeat"

# 感測來源（rpi_env01）
SENSOR_STATUS_TOPIC = "integration/smart/v1/env/rpi_env01/status"
SENSOR_EVENT_TOPIC = "integration/smart/v1/env/rpi_env01/event"

CONTROL_HZ = 20
CONTROL_DT = 1.0 / CONTROL_HZ

DECISION_HZ = 10
DECISION_DT = 1.0 / DECISION_HZ

STATUS_HZ = 1
STATUS_DT = 1.0 / STATUS_HZ

system = SmartFanSystem()

# =========================================================
# MQTT HELPERS
# =========================================================
def now_ts():
    return int(time.time())


def publish_ack(client, request_id, ok=True, message="ok"):
    payload = {
        "type": "ack",
        "node_id": NODE_ID,
        "request_id": request_id,
        "ok": bool(ok),
        "message": str(message),
        "ts": now_ts(),
    }
    client.publish(TOPIC_ACK, json.dumps(payload))


def build_status_payload():
    old = system.get_status_payload()
    st = old["status"]
    env = old["environment"]

    return {
        "type": "status",
        "node_id": NODE_ID,
        "ts": now_ts(),
        "status": {
            "power_state": st["power_state"],
            "control_mode": st["control_mode"],
            "mode": st["mode"],
            "mode_reason": st["mode_reason"],

            "temperature_c": env["temperature_c"],
            "humidity_percent": env["humidity_percent"],
            "air_quality_index": env["air_quality_index"],

            "fan_level": st["fan_level"],
            "duty": st["duty"],
            "power_mode": st["power_mode"],
            "occupancy": st["occupancy"],
            "scene": st["scene"],
            "connection": st["connection"],

            "comfort": st["comfort"],
            "decision": st["decision"],
            "error_code": st["error_code"],
        },
    }


def build_availability_payload(online=True):
    return {
        "type": "availability",
        "node_id": NODE_ID,
        "ts": now_ts(),
        "availability": {
            "online": bool(online)
        }
    }

# =========================================================
# MQTT WORKER
# =========================================================
def mqtt_worker():
    client = mqtt.Client()

    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print("[MQTT] Connected")

            # 訂閱主控指令
            client.subscribe(TOPIC_CMD)
            client.subscribe(TOPIC_CONTROLLER_HEARTBEAT)

            # 訂閱感測資料（rpi_env01）
            client.subscribe(SENSOR_STATUS_TOPIC)
            client.subscribe(SENSOR_EVENT_TOPIC)

            client.publish(TOPIC_AVAILABILITY, json.dumps(build_availability_payload(True)))
        else:
            print(f"[MQTT] Connect failed: {rc}")

    def on_disconnect(client, userdata, rc):
        print(f"[MQTT] Disconnected: {rc}")

    def on_message(client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())

            # =========================
            # COMMAND
            # =========================
            if msg.topic == TOPIC_CMD:
                command = payload.get("command")
                params = payload.get("params", {})
                request_id = payload.get("request_id", "")

                try:
                    if command == "set_fan_level":
                        fan_level = int(params.get("fan_level"))
                        system.set_manual_fan_level(fan_level)
                        publish_ack(client, request_id, True, "ok")

                    elif command == "set_power":
                        power_state = str(params.get("power_state", params.get("power", ""))).upper()

                        if not system.set_power(power_state):
                            publish_ack(client, request_id, False, "invalid power_state")
                            return

                        publish_ack(client, request_id, True, "ok")

                    elif command == "set_mode":
                        mode = str(params.get("control_mode", params.get("mode", ""))).upper()

                        if mode not in ("MANUAL", "SCENE", "AUTO", "OFF"):
                            publish_ack(client, request_id, False, "invalid control_mode")
                            return

                        if not system.set_mode(mode):
                            publish_ack(client, request_id, False, "invalid control_mode")
                            return

                        publish_ack(client, request_id, True, "ok")

                    elif command == "set_scene":
                        fan_level = int(params.get("fan_level"))
                        ttl_sec = float(params.get("ttl_sec"))
                        scene_id = str(params.get("scene_id", ""))

                        system.set_scene(fan_level, ttl_sec, scene_id)
                        publish_ack(client, request_id, True, "ok")

                    elif command == "clear_scene":
                        system.clear_scene()
                        publish_ack(client, request_id, True, "ok")

                    else:
                        publish_ack(client, request_id, False, f"unsupported command: {command}")

                except Exception as e:
                    publish_ack(client, request_id, False, str(e))

            # =========================
            # CONTROLLER HEARTBEAT
            # =========================
            elif msg.topic == TOPIC_CONTROLLER_HEARTBEAT:
                system.on_controller_heartbeat()

            # =========================
            # SENSOR STATUS (rpi_env01)
            # =========================
            elif msg.topic == SENSOR_STATUS_TOPIC:
                status = payload.get("status", {})

                system.update_sensor(
                    temperature_c=status.get("temperature_c"),
                    humidity_percent=status.get("humidity_percent"),
                    air_quality_index=status.get("air_raw"),
                )

                if status.get("occupancy") is True:
                    system.on_pir_event()

            # =========================
            # SENSOR EVENT (rpi_env01)
            # =========================
            elif msg.topic == SENSOR_EVENT_TOPIC:
                event = payload.get("event", {})

                if (
                    event.get("category") == "occupancy"
                    and event.get("action") == "changed"
                    and event.get("value") is True
                ):
                    system.on_pir_event()

        except Exception as e:
            print(f"[MQTT] on_message error: {e}")

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    while True:
        try:
            client.connect(MQTT_BROKER, MQTT_PORT, 60)
            client.loop_start()

            while True:
                time.sleep(STATUS_DT)
                client.publish(TOPIC_STAT, json.dumps(build_status_payload()))

        except Exception as e:
            print(f"[MQTT] Worker reconnecting: {e}")
            try:
                client.loop_stop()
            except Exception:
                pass
            time.sleep(2)

# =========================================================
# UART WORKER
# =========================================================
def build_uart_mode_payload():
    st = system.get_status_payload()["status"]
    return {
        "type": "mode",
        "power_state": st["power_state"],
        "control_mode": st["control_mode"],
    }


def uart_worker():
    while True:
        try:
            ser = serial.Serial(UART_PORT, UART_BAUD, timeout=1)
            print(f"[UART] Opened {UART_PORT}")
            next_mode_send = time.perf_counter()

            while True:
                now = time.perf_counter()

                if now >= next_mode_send:
                    ser.write((
                        json.dumps(build_uart_mode_payload(), separators=(",", ":")) + "\n"
                    ).encode())
                    next_mode_send = now + STATUS_DT

                raw = ser.readline().decode(errors="ignore").strip()
                if not raw or not raw.startswith("{"):
                    continue

                try:
                    data = json.loads(raw)

                    if data.get("event") == "button":
                        key = data.get("key", "")
                        press_type = data.get("type", "short")

                        if key == "SYSTEM" and press_type == "pico_ready":
                            continue

                        system.handle_button_event(key, press_type)

                except Exception as e:
                    print(f"[UART] parse error: {e}")

        except Exception as e:
            print(f"[UART] reconnecting: {e}")
            time.sleep(1)

# =========================================================
# MAIN LOOP
# =========================================================
def main_loop():
    next_control = time.perf_counter()
    next_decision = time.perf_counter()
    prev_control = next_control

    print("[SYSTEM] SmartFan runtime started")

    while True:
        now = time.perf_counter()

        if now >= next_decision:
            system.decision_tick()
            next_decision += DECISION_DT

        if now >= next_control:
            dt = now - prev_control
            prev_control = now
            system.control_tick(dt)
            next_control += CONTROL_DT

        time.sleep(0.001)

# =========================================================
# ENTRY
# =========================================================
if __name__ == "__main__":
    threading.Thread(target=mqtt_worker, daemon=True).start()
    threading.Thread(target=uart_worker, daemon=True).start()
    main_loop()
