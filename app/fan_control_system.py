import math
import time
import threading
from dataclasses import dataclass

# =========================================================
# CONFIG
# =========================================================
FILTER_DEV = "/dev/pwm_filter"

CONTROL_HZ = 20
CONTROL_DT = 1.0 / CONTROL_HZ

DECISION_HZ = 10
DECISION_DT = 1.0 / DECISION_HZ

PRESENCE_TIMEOUT = 300.0
AIR_ALPHA = 0.2
DUTY_STEP = 3

CONTROLLER_OFFLINE_TIMEOUT = 10.0
CONTROLLER_ONLINE_CONFIRM = 2.0
MODE_SWITCH_COOLDOWN = 3.0

# =========================================================
# HYSTERESIS THRESHOLDS
# =========================================================
# feels_like_level: 0~10
FEELS_UP_THRESHOLDS =   [23.0, 24.0, 25.0, 26.0, 27.0, 28.0, 29.0, 30.0, 31.0, 32.0]
FEELS_DOWN_THRESHOLDS = [22.5, 23.5, 24.5, 25.5, 26.5, 27.5, 28.5, 29.5, 30.5, 31.5]

# air_quality_level: 0~10
AIR_UP_THRESHOLDS =   [100.0, 180.0, 260.0, 340.0, 420.0, 500.0, 580.0, 660.0, 740.0, 800.0]
AIR_DOWN_THRESHOLDS = [ 90.0, 170.0, 250.0, 330.0, 410.0, 490.0, 570.0, 650.0, 730.0, 790.0]

# =========================================================
# HELPERS
# =========================================================
def clamp(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, int(v)))


def update_level_with_hysteresis(value, current_level, up_thresholds, down_thresholds):
    level = current_level

    while level < len(up_thresholds) and value >= up_thresholds[level]:
        level += 1

    while level > 0 and value < down_thresholds[level - 1]:
        level -= 1

    return level


def fan_level_to_duty(level: int) -> int:
    table = {
        0: 0,
        1: 20,
        2: 28,
        3: 36,
        4: 44,
        5: 52,
        6: 60,
        7: 70,
        8: 80,
        9: 90,
        10: 100,
    }
    return table[clamp(level, 0, 10)]


def smooth_duty(current_duty: int, target_duty: int) -> int:
    if target_duty > current_duty:
        return min(current_duty + DUTY_STEP, target_duty)
    if target_duty < current_duty:
        return max(current_duty - DUTY_STEP, target_duty)
    return target_duty


def comfort_label(level: int) -> str:
    labels = {
        0: "非常不舒適",
        1: "不佳",
        2: "普通",
        3: "良好",
        4: "非常舒適",
    }
    return labels.get(int(level), "普通")


def feels_like_temperature(temp_c: float, humidity_percent: float) -> float:
    return temp_c + 0.1 * (humidity_percent - 50.0)


def thermal_comfort_score(feels_like: float) -> int:
    if feels_like < 8 or feels_like > 33:
        return 0
    elif feels_like < 12 or feels_like > 30:
        return 1
    elif feels_like < 18 or feels_like > 27:
        return 2
    elif feels_like < 22 or feels_like > 25:
        return 3
    else:
        return 4


def air_comfort_score(air_value: float) -> int:
    if air_value < 100:
        return 4
    elif air_value < 200:
        return 3
    elif air_value < 400:
        return 2
    elif air_value < 600:
        return 1
    else:
        return 0


# =========================================================
# STATE
# =========================================================
@dataclass
class FanState:
    temperature_c: float = 25.0
    humidity_percent: float = 50.0
    air_quality_raw: float = 40.0
    air_quality_filtered: float = 40.0

    last_pir_time: float = 0.0
    occupancy: bool = True

    power_state: str = "ON"
    control_mode: str = "SCENE"
    mode_reason: str = "boot"

    manual_fan_level: int = 0
    scene_id: str = ""
    scene_fan_level: int | None = None
    scene_expires_at: float = 0.0

    controller_online: bool = False
    last_controller_seen: float = 0.0
    controller_online_since: float = 0.0
    last_mode_change_time: float = 0.0

    current_fan_level: int = 0
    current_duty: int = 0
    target_duty: int = 0

    # 保留原本欄位名稱，避免 status payload 大改
    temperature_level: int = 0
    humidity_level: int = 0
    comfort_level: int = 0
    air_stage: int = 0
    air_quality_level: int = 0
    winner: str = "none"

    comfort_display_level: int = 2
    feels_like_c: float = 24.0

    control_loop_hz: float = 0.0


# =========================================================
# SYSTEM
# =========================================================
class SmartFanSystem:
    def __init__(self, filter_dev: str = FILTER_DEV):
        self.filter_dev = filter_dev
        now = time.perf_counter()
        self.state = FanState(
            last_pir_time=now,
            last_mode_change_time=now - MODE_SWITCH_COOLDOWN,
        )
        self._pwm_dev = None
        self._lock = threading.RLock()

    def update_sensor(self, temperature_c=None, humidity_percent=None, air_quality_index=None):
        with self._lock:
            if temperature_c is not None:
                self.state.temperature_c = float(temperature_c)

            if humidity_percent is not None:
                self.state.humidity_percent = float(humidity_percent)

            if air_quality_index is not None:
                raw = float(air_quality_index)
                self.state.air_quality_raw = raw
                self.state.air_quality_filtered = (
                    (1.0 - AIR_ALPHA) * self.state.air_quality_filtered
                    + AIR_ALPHA * raw
                )

    def on_pir_event(self):
        with self._lock:
            self.state.last_pir_time = time.perf_counter()
            self.state.occupancy = True

    def _set_control_mode(self, mode: str, reason: str):
        mode = str(mode).upper()
        if mode not in ("MANUAL", "SCENE", "AUTO"):
            return False

        if self.state.control_mode != mode:
            self.state.control_mode = mode
            self.state.last_mode_change_time = time.perf_counter()

        self.state.mode_reason = str(reason)
        return True

    def _mode_switch_ready(self, now: float) -> bool:
        return (now - self.state.last_mode_change_time) >= MODE_SWITCH_COOLDOWN

    def _mark_controller_seen(self, now: float | None = None):
        now = time.perf_counter() if now is None else now
        s = self.state
        s.last_controller_seen = now

        if not s.controller_online and s.controller_online_since <= 0:
            s.controller_online_since = now

    def _update_connection_monitor(self, now: float):
        s = self.state

        if s.last_controller_seen <= 0:
            s.controller_online = False
            s.controller_online_since = 0.0
            return

        if (now - s.last_controller_seen) > CONTROLLER_OFFLINE_TIMEOUT:
            s.controller_online = False
            s.controller_online_since = 0.0
            return

        if s.controller_online:
            return

        if s.controller_online_since > 0 and (now - s.controller_online_since) >= CONTROLLER_ONLINE_CONFIRM:
            s.controller_online = True

    def _scene_valid_unlocked(self, now: float | None = None) -> bool:
        now = time.perf_counter() if now is None else now
        s = self.state
        return (
            s.controller_online
            and s.scene_fan_level is not None
            and now < s.scene_expires_at
        )

    def set_mode(self, mode: str):
        with self._lock:
            mode = str(mode).upper()
            if mode == "OFF":
                self.set_power("OFF")
                return True

            if mode in ("MANUAL", "SCENE", "AUTO"):
                return self._set_control_mode(mode, "mqtt_set_mode")

            return False

    def set_power(self, power_state: str):
        with self._lock:
            power_state = str(power_state).upper()
            if power_state not in ("ON", "OFF"):
                return False

            self.state.power_state = power_state
            self.state.mode_reason = "power_on" if power_state == "ON" else "power_off"
            return True

    def set_manual_fan_level(self, level: int):
        with self._lock:
            self.state.manual_fan_level = clamp(level, 0, 10)
            self._set_control_mode("MANUAL", "mqtt_set_fan_level")

    def change_manual_fan_level(self, delta: int):
        with self._lock:
            self.state.manual_fan_level = clamp(self.state.manual_fan_level + delta, 0, 10)
            self._set_control_mode("MANUAL", "button_manual")

    def toggle_power(self):
        with self._lock:
            if self.state.power_state == "OFF":
                self.set_power("ON")
            else:
                self.set_power("OFF")

    def set_auto_mode(self):
        with self._lock:
            self._set_control_mode("AUTO", "set_auto_mode")

    def set_scene(self, fan_level: int, ttl_sec: float, scene_id: str = ""):
        with self._lock:
            now = time.perf_counter()
            ttl_sec = max(0.0, float(ttl_sec))

            self.state.scene_id = str(scene_id or "")
            self.state.scene_fan_level = clamp(fan_level, 0, 10)
            self.state.scene_expires_at = now + ttl_sec
            self._mark_controller_seen(now)

            if (
                self.state.control_mode == "AUTO"
                and self.state.controller_online
                and self._scene_valid_unlocked(now)
                and self._mode_switch_ready(now)
            ):
                self._set_control_mode("SCENE", "scene_restored")
            elif self.state.control_mode != "MANUAL":
                self.state.mode_reason = "scene_updated"

    def clear_scene(self):
        with self._lock:
            self.state.scene_id = ""
            self.state.scene_fan_level = None
            self.state.scene_expires_at = 0.0
            self.state.controller_online = False
            self.state.controller_online_since = 0.0

            if self.state.control_mode == "SCENE":
                self._set_control_mode("AUTO", "scene_cleared")

    def on_controller_heartbeat(self):
        with self._lock:
            self._mark_controller_seen()

    def handle_button_event(self, key: str, press_type: str = "short"):
        with self._lock:
            key = str(key).upper()
            press_type = str(press_type).lower()

            if key == "POWER":
                if press_type == "short":
                    if self.state.power_state != "OFF":
                        self._set_control_mode("SCENE", "button_power_short")

                elif press_type == "long":
                    self.toggle_power()

            elif key == "UP" and press_type in ("short", "long"):
                self.state.manual_fan_level = clamp(self.state.manual_fan_level + 1, 0, 10)
                self._set_control_mode("MANUAL", "button_up")

            elif key == "DOWN" and press_type in ("short", "long"):
                self.state.manual_fan_level = clamp(self.state.manual_fan_level - 1, 0, 10)
                self._set_control_mode("MANUAL", "button_down")

    def _run_auto_decision(self, s: FanState):
        # 溫度 + 濕度 → 體感溫度 → 風級
        # 暫時沿用 temperature_level 欄位存體感溫度需求，避免大改 status payload
        s.temperature_level = update_level_with_hysteresis(
            s.feels_like_c,
            s.temperature_level,
            FEELS_UP_THRESHOLDS,
            FEELS_DOWN_THRESHOLDS,
        )

        # 濕度已經併入體感溫度，不再單獨參與風級控制
        s.humidity_level = 0

        # MQ135 空氣品質 → 風級
        s.air_stage = update_level_with_hysteresis(
            s.air_quality_filtered,
            s.air_stage,
            AIR_UP_THRESHOLDS,
            AIR_DOWN_THRESHOLDS,
        )

        s.air_quality_level = s.air_stage
        s.comfort_level = s.temperature_level

        if s.comfort_level >= s.air_quality_level:
            s.winner = "feels_like"
        else:
            s.winner = "air_quality"

        s.current_fan_level = max(s.comfort_level, s.air_quality_level)

        # 有人且需求為 0 時，保留低速 baseline
        if s.occupancy and s.current_fan_level == 0:
            s.current_fan_level = 1
            s.winner = "baseline"

        # 無人時強制關閉
        if not s.occupancy:
            s.current_fan_level = 0
            s.winner = "occupancy_gate"

    def decision_tick(self):
        with self._lock:
            now = time.perf_counter()
            s = self.state

            s.occupancy = (now - s.last_pir_time) < PRESENCE_TIMEOUT

            s.feels_like_c = feels_like_temperature(
                s.temperature_c,
                s.humidity_percent
            )

            thermal_score = thermal_comfort_score(s.feels_like_c)
            air_score = air_comfort_score(s.air_quality_filtered)
            s.comfort_display_level = round((thermal_score + air_score) / 2)

            self._update_connection_monitor(now)
            scene_valid = self._scene_valid_unlocked(now)

            if s.power_state == "ON" and self._mode_switch_ready(now):
                if s.control_mode == "SCENE" and not scene_valid:
                    self._set_control_mode("AUTO", "scene_unavailable")
                elif s.control_mode == "AUTO" and scene_valid:
                    self._set_control_mode("SCENE", "scene_restored")

            if s.power_state == "OFF":
                s.current_fan_level = 0
                s.winner = "off"
                s.temperature_level = 0
                s.humidity_level = 0
                s.air_stage = 0
                s.comfort_level = 0
                s.air_quality_level = 0

            elif s.control_mode == "MANUAL":
                s.current_fan_level = s.manual_fan_level
                s.winner = "manual"
                s.temperature_level = 0
                s.humidity_level = 0
                s.air_stage = 0
                s.comfort_level = 0
                s.air_quality_level = 0

            elif s.control_mode == "SCENE":
                s.current_fan_level = clamp(s.scene_fan_level or 0, 0, 10)
                s.winner = "scene"
                s.temperature_level = 0
                s.humidity_level = 0
                s.air_stage = 0
                s.comfort_level = 0
                s.air_quality_level = 0

            elif s.control_mode == "AUTO":
                self._run_auto_decision(s)

    def control_tick(self, dt: float):
        with self._lock:
            s = self.state

            if s.power_state == "OFF":
                s.target_duty = 0
            else:
                s.target_duty = fan_level_to_duty(s.current_fan_level)

            s.current_duty = smooth_duty(s.current_duty, s.target_duty)

            if self._pwm_dev is None:
                try:
                    self._pwm_dev = open(self.filter_dev, "wb", buffering=0)
                except Exception as e:
                    print(f"[ERROR] PWM Driver Open Fail: {e}")
                    return

            self._pwm_dev.write(f"{int(s.current_duty)}".encode())

            if dt > 0:
                s.control_loop_hz = 1.0 / dt

    def get_status_payload(self) -> dict:
        with self._lock:
            s = self.state
            now = time.perf_counter()
            scene_valid = self._scene_valid_unlocked(now)
            expires_in = max(0.0, s.scene_expires_at - now)
            last_seen_ago = None
            if s.last_controller_seen > 0:
                last_seen_ago = max(0.0, now - s.last_controller_seen)

            power_mode = "off" if s.power_state == "OFF" else s.control_mode.lower()

            return {
                "device_id": "rpi_env02",
                "timestamp": int(time.time()),
                "status": {
                    "fan_level": s.current_fan_level,
                    "duty": s.current_duty,
                    "power_mode": power_mode,
                    "mode": s.control_mode,
                    "power_state": s.power_state,
                    "control_mode": s.control_mode,
                    "mode_reason": s.mode_reason,
                    "occupancy": s.occupancy,
                    "scene": {
                        "valid": scene_valid,
                        "scene_id": s.scene_id,
                        "fan_level": s.scene_fan_level,
                        "expires_in_sec": round(expires_in, 1),
                    },
                    "connection": {
                        "controller_online": s.controller_online,
                        "last_seen_ago_sec": (
                            None if last_seen_ago is None else round(last_seen_ago, 1)
                        ),
                        "mode_switch_cooldown_sec": round(
                            max(0.0, MODE_SWITCH_COOLDOWN - (now - s.last_mode_change_time)),
                            1,
                        ),
                    },
                    "comfort": {
                        "level": s.comfort_display_level,
                        "label": comfort_label(s.comfort_display_level),
                    },
                    "decision": {
                        "temperature_level": s.temperature_level,
                        "humidity_level": s.humidity_level,
                        "comfort_level": s.comfort_level,
                        "air_quality_level": s.air_quality_level,
                        "winner": s.winner,
                        "fan_level": s.current_fan_level,
                        "feels_like_c": round(s.feels_like_c, 1),
                    },
                    "error_code": 0,
                },
                "environment": {
                    "temperature_c": s.temperature_c,
                    "humidity_percent": s.humidity_percent,
                    "air_quality_index": round(s.air_quality_filtered, 1),
                },
                "health": {
                    "control_loop_hz": round(s.control_loop_hz, 2),
                },
            }

    def shutdown(self):
        with self._lock:
            try:
                if self._pwm_dev is not None:
                    self._pwm_dev.write(b"0")
                    self._pwm_dev.close()
                    self._pwm_dev = None
            except Exception:
                pass
