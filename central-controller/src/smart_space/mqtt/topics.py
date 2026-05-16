from dataclasses import dataclass, field

from smart_space.core.settings import Settings


@dataclass
class BrokerPlan:
    name: str
    host: str
    port: int
    username: str = ""
    password: str = ""
    subscribe_topics: list[str] = field(default_factory=list)


class Topics:
    def __init__(self, settings: Settings):
        self.settings = settings
        base = f"{settings.smartlight_topic_root}/{settings.smartlight_node_id}"
        root = settings.integration_topic_root
        self.base_topic = base
        self.smartlight_cmd = f"{base}/cmd"
        self.controller_event = f"{root}/controller/{settings.controller_node_id}/event"
        self.led_sub_topics = [
            f"{base}/status",
            f"{base}/ack",
            f"{base}/availability",
            f"{base}/event",
        ]
        self.wearable_status = f"{root}/wearable/{settings.wearable_node_id}/status"
        self.wearable_availability = f"{root}/wearable/{settings.wearable_node_id}/availability"
        self.env_status = f"{root}/env/{settings.env_sensor_node_id}/status"
        self.env_event = f"{root}/env/{settings.env_sensor_node_id}/event"
        self.env_availability = f"{root}/env/{settings.env_sensor_node_id}/availability"
        self.fan_status = f"{root}/env/{settings.fan_node_id}/status"
        self.fan_ack = f"{root}/env/{settings.fan_node_id}/ack"
        self.fan_availability = f"{root}/env/{settings.fan_node_id}/availability"
        self.fan_cmd = f"{root}/env/{settings.fan_node_id}/command"
        self.light_integration_status = f"{root}/light/{settings.light_integration_node_id}/status"
        self.light_integration_ack = f"{root}/light/{settings.light_integration_node_id}/ack"
        self.light_integration_availability = f"{root}/light/{settings.light_integration_node_id}/availability"
        self.light_integration_sub_topics = [
            self.light_integration_status,
            self.light_integration_ack,
            self.light_integration_availability,
        ]
        self.wearable_sub_topics = [self.wearable_status, self.wearable_availability]
        self.env_sub_topics = [
            self.env_status,
            self.env_event,
            self.env_availability,
            self.fan_status,
            self.fan_ack,
            self.fan_availability,
        ]

    def integration_parts(self, topic: str) -> dict | None:
        parts = str(topic or "").split("/")
        root_parts = self.settings.integration_topic_root.split("/")
        if len(parts) != len(root_parts) + 3:
            return None
        if parts[: len(root_parts)] != root_parts:
            return None
        return {"domain": parts[-3], "node_id": parts[-2], "type": parts[-1]}

    def extra_sub_topics(self) -> list[str]:
        return [item.strip() for item in self.settings.dashboard_extra_sub_topics.split(",") if item.strip()]


def _same_broker(host_a: str, port_a: int, host_b: str, port_b: int) -> bool:
    return host_a == host_b and int(port_a) == int(port_b)


def _append_unique(items: list[str], next_items: list[str]) -> None:
    for item in next_items:
        if item and item not in items:
            items.append(item)


def build_mqtt_plan(settings: Settings) -> dict[str, BrokerPlan]:
    topics = Topics(settings)
    plan: dict[str, BrokerPlan] = {
        "light": BrokerPlan(
            name="light",
            host=settings.mqtt_host,
            port=settings.mqtt_port,
            username=settings.mqtt_username,
            password=settings.mqtt_password,
            subscribe_topics=[
                *topics.led_sub_topics,
                *topics.light_integration_sub_topics,
                *topics.extra_sub_topics(),
            ],
        )
    }

    if settings.env_mqtt_enabled:
        if _same_broker(settings.env_mqtt_host, settings.env_mqtt_port, settings.mqtt_host, settings.mqtt_port):
            _append_unique(plan["light"].subscribe_topics, topics.env_sub_topics)
        else:
            plan["env"] = BrokerPlan(
                name="env",
                host=settings.env_mqtt_host,
                port=settings.env_mqtt_port,
                username=settings.env_mqtt_username or settings.mqtt_username,
                password=settings.env_mqtt_password or settings.mqtt_password,
                subscribe_topics=[*topics.env_sub_topics],
            )

    if settings.wearable_mqtt_enabled:
        if _same_broker(settings.wearable_mqtt_host, settings.wearable_mqtt_port, settings.mqtt_host, settings.mqtt_port):
            _append_unique(plan["light"].subscribe_topics, topics.wearable_sub_topics)
        elif "env" in plan and _same_broker(
            settings.wearable_mqtt_host,
            settings.wearable_mqtt_port,
            settings.env_mqtt_host,
            settings.env_mqtt_port,
        ):
            _append_unique(plan["env"].subscribe_topics, topics.wearable_sub_topics)
        else:
            plan["wearable"] = BrokerPlan(
                name="wearable",
                host=settings.wearable_mqtt_host,
                port=settings.wearable_mqtt_port,
                username=settings.wearable_mqtt_username or settings.mqtt_username,
                password=settings.wearable_mqtt_password or settings.mqtt_password,
                subscribe_topics=[*topics.wearable_sub_topics],
            )

    return plan
