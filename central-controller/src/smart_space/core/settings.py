from pathlib import Path

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
        populate_by_name=True,
    )

    port: int = Field(default=3000, validation_alias="PORT")
    dashboard_dir: Path = Field(default=Path("src/smart_space/web/dashboard"), validation_alias="DASHBOARD_DIR")

    mqtt_host: str = Field(default="localhost", validation_alias="MQTT_HOST")
    mqtt_port: int = Field(default=1883, validation_alias="MQTT_PORT")
    mqtt_username: str = Field(default="", validation_alias="MQTT_USERNAME")
    mqtt_password: str = Field(default="", validation_alias="MQTT_PASSWORD")
    smartlight_node_id: str = Field(default="bedroom01", validation_alias="SMARTLIGHT_NODE_ID")
    smartlight_topic_root: str = Field(default="smartlight", validation_alias="SMARTLIGHT_TOPIC_ROOT")

    integration_topic_root: str = Field(default="integration/smart/v1", validation_alias="INTEGRATION_TOPIC_ROOT")
    controller_node_id: str = Field(default="smart_space_master", validation_alias="CONTROLLER_NODE_ID")
    wearable_node_id: str = Field(default="pico_wearable01", validation_alias="WEARABLE_NODE_ID")
    env_sensor_node_id: str = Field(default="rpi_env01", validation_alias="ENV_SENSOR_NODE_ID")
    fan_node_id: str = Field(default="rpi_env02", validation_alias="FAN_NODE_ID")
    light_integration_node_id: str = Field(default="rpi_light01", validation_alias="LIGHT_INTEGRATION_NODE_ID")

    wearable_mqtt_enabled: bool = Field(default=True, validation_alias="WEARABLE_MQTT_ENABLED")
    wearable_mqtt_host: str = Field(default="localhost", validation_alias="WEARABLE_MQTT_HOST")
    wearable_mqtt_port: int = Field(default=1883, validation_alias="WEARABLE_MQTT_PORT")
    wearable_mqtt_username: str = Field(default="", validation_alias="WEARABLE_MQTT_USERNAME")
    wearable_mqtt_password: str = Field(default="", validation_alias="WEARABLE_MQTT_PASSWORD")

    env_mqtt_enabled: bool = Field(default=True, validation_alias="ENV_MQTT_ENABLED")
    env_mqtt_host: str = Field(default="pi5203.local", validation_alias="ENV_MQTT_HOST")
    env_mqtt_port: int = Field(default=1883, validation_alias="ENV_MQTT_PORT")
    env_mqtt_username: str = Field(default="", validation_alias="ENV_MQTT_USERNAME")
    env_mqtt_password: str = Field(default="", validation_alias="ENV_MQTT_PASSWORD")

    dashboard_extra_sub_topics: str = Field(default="", validation_alias="DASHBOARD_EXTRA_SUB_TOPICS")
    dashboard_module_stale_ms: int = Field(default=15000, validation_alias="DASHBOARD_MODULE_STALE_MS")
    smartlight_status_stale_ms: int = Field(default=15000, validation_alias="SMARTLIGHT_STATUS_STALE_MS")
    smartlight_legacy_light_action_min_ms: int = Field(default=1000, validation_alias="SMARTLIGHT_LEGACY_LIGHT_ACTION_MIN_MS")
    smart_mode_enabled: bool = Field(default=True, validation_alias="SMART_MODE_ENABLED")
    auto_activity_led_enabled: bool = Field(default=False, validation_alias="AUTO_ACTIVITY_LED_ENABLED")
    auto_hr_led_enabled: bool = Field(default=False, validation_alias="AUTO_HR_LED_ENABLED")
    google_api_key: str = Field(default="", validation_alias="GOOGLE_API_KEY")

    gpio_button_enabled: bool = Field(default=True, validation_alias="GPIO_BUTTON_ENABLED")
    gpio_button_pin: int = Field(default=17, validation_alias="GPIO_BUTTON_PIN")
    gpio_button_bounce_seconds: float = Field(default=0.1, validation_alias="GPIO_BUTTON_BOUNCE_SECONDS")

    scene_awareness_enabled: bool = Field(default=True, validation_alias="SCENE_AWARENESS_ENABLED")
    scene_awareness_model: str = Field(default="gemma-4-31b-it", validation_alias="SCENE_AWARENESS_MODEL")
    scene_awareness_capture_interval_seconds: float = Field(default=5.0, validation_alias="SCENE_AWARENESS_CAPTURE_INTERVAL_SECONDS")
    scene_awareness_infer_interval_seconds: float = Field(default=30.0, validation_alias="SCENE_AWARENESS_INFER_INTERVAL_SECONDS")
    scene_awareness_prompt_path: Path = Field(
        default=Path("src/smart_space/config/scene_awareness_system_prompt.txt"),
        validation_alias="SCENE_AWARENESS_PROMPT_PATH",
    )
    scene_awareness_env_fallback_path: Path = Field(
        default=Path("drafts/sence_aware/.env"),
        validation_alias="SCENE_AWARENESS_ENV_FALLBACK_PATH",
    )
    scene_awareness_log_path: Path = Field(
        default=Path(".runtime/scene_awareness.log"),
        validation_alias="SCENE_AWARENESS_LOG_PATH",
    )

    music_enabled: bool = Field(default=True, validation_alias="MUSIC_ENABLED")
    music_dir: Path = Field(default=Path("music"), validation_alias="MUSIC_DIR")
    music_default_volume: int = Field(default=50, validation_alias="MUSIC_DEFAULT_VOLUME")

    camera_enabled: bool = Field(default=True, validation_alias="CAMERA_ENABLED")
    camera_stream_path: str = Field(default="space", validation_alias="CAMERA_STREAM_PATH")
    camera_snapshot_interval_ms: int = Field(default=2000, validation_alias="CAMERA_SNAPSHOT_INTERVAL_MS")
    camera_rtsp_port: int = Field(default=8554, validation_alias="CAMERA_RTSP_PORT")
    camera_hls_port: int = Field(default=8888, validation_alias="CAMERA_HLS_PORT")
    camera_webrtc_port: int = Field(default=8889, validation_alias="CAMERA_WEBRTC_PORT")
    camera_public_host: str = Field(default="", validation_alias="CAMERA_PUBLIC_HOST")
    camera_width: int = Field(default=960, validation_alias="CAMERA_WIDTH")
    camera_height: int = Field(default=720, validation_alias="CAMERA_HEIGHT")
    camera_fps: int = Field(default=24, validation_alias="CAMERA_FPS")
    camera_bitrate: int = Field(default=2000000, validation_alias="CAMERA_BITRATE")
    camera_mediamtx_bin: Path = Field(default=Path("mediamtx/mediamtx"), validation_alias="CAMERA_MEDIAMTX_BIN")
    camera_mediamtx_config: Path = Field(default=Path("mediamtx/mediamtx.yml"), validation_alias="CAMERA_MEDIAMTX_CONFIG")

    led_profile_default_path: Path = Field(
        default=Path("src/smart_space/config/led_profiles.default.json"),
        validation_alias="LED_PROFILE_DEFAULT_PATH",
    )
    led_profile_user_path: Path = Field(
        default=Path("src/smart_space/data/led_profiles.user.json"),
        validation_alias="LED_PROFILE_USER_PATH",
    )
