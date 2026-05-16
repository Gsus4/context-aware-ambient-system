from pathlib import Path
import json

from smart_space.core.settings import Settings
from smart_space.mqtt.topics import build_mqtt_plan
from smart_space.services.gpio_button import GpioButtonService


class FakeButton:
    def __init__(self, pin, *, pull_up, bounce_time):
        self.pin = pin
        self.pull_up = pull_up
        self.bounce_time = bounce_time
        self.when_pressed = None
        self.closed = False

    def close(self):
        self.closed = True


class FakeMqttClient:
    def __init__(self):
        self.connected = False
        self.loop_started = False
        self.published = []

    def username_pw_set(self, username, password=None):
        return None

    def connect(self, host, port, keepalive=30):
        self.connected = True
        self.host = host
        self.port = port
        self.keepalive = keepalive
        return 0

    def loop_start(self):
        self.loop_started = True

    def publish(self, topic, payload, qos=1):
        self.published.append((topic, json.loads(payload), qos))

        class Info:
            rc = 0

        return Info()

    def loop_stop(self):
        self.loop_started = False

    def disconnect(self):
        self.connected = False


def make_settings(tmp_path, **overrides):
    values = {
        "port": 0,
        "mqtt_host": "localhost",
        "wearable_mqtt_host": "localhost",
        "env_mqtt_host": "env-broker.local",
        "dashboard_dir": Path("src/smart_space/web/dashboard"),
        "led_profile_default_path": Path("src/smart_space/config/led_profiles.default.json"),
        "led_profile_user_path": tmp_path / "led_profiles.user.json",
    }
    values.update(overrides)
    return Settings(**values)


def test_gpio_button_service_stays_disabled_when_button_library_unavailable(tmp_path):
    settings = make_settings(tmp_path, gpio_button_enabled=True)
    service = GpioButtonService(settings, button_factory=lambda: None)

    status = service.start()

    assert status["enabled"] is False
    assert status["running"] is False
    assert status["reason"] == "gpio_button_unavailable"


def test_gpio_button_press_publishes_controller_event_to_local_broker(tmp_path):
    settings = make_settings(tmp_path, gpio_button_enabled=True, gpio_button_pin=17, gpio_button_bounce_seconds=0.1)
    created = []
    mqtt = FakeMqttClient()

    def factory():
        def make_button(pin, *, pull_up, bounce_time):
            button = FakeButton(pin, pull_up=pull_up, bounce_time=bounce_time)
            created.append(button)
            return button

        return make_button

    service = GpioButtonService(settings, button_factory=factory, mqtt_client_factory=lambda: mqtt)

    status = service.start()
    created[0].when_pressed()

    assert status["enabled"] is True
    assert created[0].pin == 17
    assert created[0].pull_up is True
    assert created[0].bounce_time == 0.1
    assert mqtt.connected is True
    assert mqtt.published[0][0] == "integration/smart/v1/controller/smart_space_master/event"
    assert mqtt.published[0][1]["event"]["eventType"] == "gpio_button_pressed"
    assert mqtt.published[0][1]["event"]["pin"] == 17
    assert mqtt.published[0][1]["event"]["targetScene"] == "relax"
    assert mqtt.published[0][2] == 1
    service.stop()
    assert created[0].closed is True
    assert mqtt.connected is False


def test_gpio_button_press_publishes_event_for_each_press(tmp_path):
    settings = make_settings(tmp_path, gpio_button_enabled=True)
    mqtt = FakeMqttClient()

    def factory():
        def make_button(pin, *, pull_up, bounce_time):
            return FakeButton(pin, pull_up=pull_up, bounce_time=bounce_time)

        return make_button

    service = GpioButtonService(settings, button_factory=factory, mqtt_client_factory=lambda: mqtt)

    service.start()
    service._button.when_pressed()
    service._button.when_pressed()

    assert len(mqtt.published) == 2


def test_gpio_button_press_callback_runs_even_when_mqtt_unavailable(tmp_path):
    settings = make_settings(tmp_path, gpio_button_enabled=True)
    handled = []

    def factory():
        def make_button(pin, *, pull_up, bounce_time):
            return FakeButton(pin, pull_up=pull_up, bounce_time=bounce_time)

        return make_button

    service = GpioButtonService(
        settings,
        button_factory=factory,
        mqtt_client_factory=lambda: None,
        on_pressed=lambda event: handled.append(event),
    )

    service.start()
    service._button.when_pressed()

    assert handled[0]["eventType"] == "gpio_button_pressed"
    assert handled[0]["targetScene"] == "relax"
    assert service.status()["lastError"] == "mqtt_not_connected"


def test_dashboard_light_client_does_not_subscribe_gpio_button_event(tmp_path):
    settings = make_settings(tmp_path, gpio_button_enabled=True)
    plan = build_mqtt_plan(settings)

    assert "integration/smart/v1/controller/smart_space_master/event" not in plan["light"].subscribe_topics
