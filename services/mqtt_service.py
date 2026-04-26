import paho.mqtt.client as mqtt

import state
from config import BROKER, PORT, RELAY_TOPICS, sensor_topics
from utils import safe_float, now_hms
from services.decisiontree_service import maybe_infer_and_update


def publish_relay(rid: int, relay_state_value: str):
    """
    Publish relay ON/OFF and update state.relay_state.
    PENTING: gunakan retain + qos supaya OFF tidak hilang ketika ESP32 reconnect.
    """
    if state.mqtt_client is None:
        return

    val = "ON" if str(relay_state_value).upper().strip() == "ON" else "OFF"
    topic = RELAY_TOPICS[rid]

    # qos=1 + retain=True -> lebih stabil untuk command
    state.mqtt_client.publish(topic, val, qos=1, retain=True)
    state.relay_state[rid] = val


def on_connect(client, userdata, flags, rc):
    print("[MQTT] Connected rc=", rc)

    # SUBSCRIBE SENSOR SAJA (hindari loop membaca command relay sendiri)
    for t in sensor_topics:
        client.subscribe(t, qos=1)


def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode(errors="ignore").strip()

    key = topic.split("/")[-1]

    # sensor update
    if key in state.sensor_data:
        val = safe_float(payload)
        if val is not None:
            state.sensor_data[key] = val
            state.sensor_timestamp[key] = now_hms()

            # infer + history + optional auto relay
            maybe_infer_and_update(publish_relay_fn=publish_relay)


def init_mqtt():
    # Paling kompatibel antar versi paho-mqtt
    client = mqtt.Client(client_id="SMARTFARM_FINAL", protocol=mqtt.MQTTv311)

    client.on_connect = on_connect
    client.on_message = on_message

    # keepalive lebih stabil
    client.connect(BROKER, PORT, 60)
    client.loop_start()

    state.mqtt_client = client
    print("[MQTT] loop started")
