from __future__ import annotations

from typing import TYPE_CHECKING

import paho.mqtt.client as mqtt

from smart_space.core.settings import Settings
from smart_space.mqtt.topics import build_mqtt_plan

if TYPE_CHECKING:
    from smart_space.dashboard.state import DashboardState


class MqttClientManager:
    def __init__(self, settings: Settings, state: DashboardState):
        self.settings = settings
        self.state = state
        self.clients: dict[str, mqtt.Client] = {}
        self.connected: dict[str, bool] = {}

    def start(self) -> None:
        for name, broker in build_mqtt_plan(self.settings).items():
            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"smart_space_dashboard_{name}")
            if broker.username:
                client.username_pw_set(broker.username, broker.password or None)
            client.on_connect = self._on_connect(name, broker.subscribe_topics)
            client.on_disconnect = self._on_disconnect(name)
            client.on_message = self._on_message
            self.clients[name] = client
            self.connected[name] = False
            client.connect_async(broker.host, broker.port, keepalive=30)
            client.loop_start()

    def stop(self) -> None:
        for client in self.clients.values():
            client.loop_stop()
            client.disconnect()
        self.connected = {name: False for name in self.connected}

    def is_connected(self, name: str) -> bool:
        return self.connected.get(name, False)

    def publish(self, name: str, topic: str, payload: str, qos: int = 1) -> None:
        client = self.clients.get(name)
        if not client or not self.connected.get(name, False):
            raise ValueError("mqtt_not_connected")
        info = client.publish(topic, payload, qos=qos)
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            raise ValueError("mqtt_publish_failed")

    def _on_connect(self, name: str, subscribe_topics: list[str]):
        def handler(client: mqtt.Client, _userdata, _flags, reason_code, _properties=None):
            reason_value = _reason_code_value(reason_code)
            self.connected[name] = reason_value == 0
            if self.state.system_state:
                self.state.system_state.set_mqtt_connection(name, self.connected[name])
            if reason_value == 0 and subscribe_topics:
                client.subscribe([(topic, 0) for topic in subscribe_topics])
            self.state.broadcast_patch({
                "sourceStatus": self.state.mqtt_source_status(),
                "debugEvent": {
                    "time": None,
                    "title": f"{name} MQTT connected" if reason_value == 0 else f"{name} MQTT connect failed",
                    "detail": {"reasonCode": reason_value, "subscribeTopics": subscribe_topics},
                },
            })
        return handler

    def _on_disconnect(self, name: str):
        def handler(_client: mqtt.Client, _userdata, _disconnect_flags, reason_code, _properties=None):
            reason_value = _reason_code_value(reason_code)
            self.connected[name] = False
            if self.state.system_state:
                self.state.system_state.set_mqtt_connection(name, False)
            self.state.broadcast_patch({
                "sourceStatus": self.state.mqtt_source_status(),
                "debugEvent": {
                    "time": None,
                    "title": f"{name} MQTT disconnected",
                    "detail": {"reasonCode": reason_value},
                },
            })
        return handler

    def _on_message(self, _client: mqtt.Client, _userdata, message: mqtt.MQTTMessage) -> None:
        self.state.handle_mqtt_message(message.topic, message.payload)


def _reason_code_value(reason_code) -> int:
    if hasattr(reason_code, "value"):
        return int(reason_code.value)
    try:
        return int(reason_code)
    except (TypeError, ValueError):
        return 0 if str(reason_code).lower() == "success" else -1
