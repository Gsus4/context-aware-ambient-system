'use strict';

/*
  Dashboard MQTT Bridge
  ------------------------------------------------------------
  執行位置：Windows 電腦或 Raspberry Pi
  作用：
  1. 提供 Dashboard 靜態網頁 http://localhost:3000
  2. 提供 WebSocket /ws/dashboard 給前端接收即時狀態
  3. 訂閱 LED 舊版 SmartLight topic 與 Smart Platform v2 模組 topic，轉成 Dashboard 可用格式
  4. 接收 Dashboard 操作，依模組分別轉成 LED / 風扇 MQTT command payload

  設計原則：
  - 使用者事件紀錄只顯示整理後的操作結果。
  - MQTT 原始 topic / payload 只進 Debug Log，不直接顯示在事件紀錄。
  - status 只負責同步畫面，不加入使用者事件紀錄。
*/

const http = require('http');
const fs = require('fs');
const path = require('path');
const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');

const PORT = Number(process.env.PORT || 3000);
const MQTT_HOST = process.env.MQTT_HOST || process.env.MQTT_BROKER_HOST || '127.0.0.1';
const MQTT_PORT = Number(process.env.MQTT_PORT || process.env.MQTT_BROKER_PORT || 1883);
const MQTT_USERNAME = process.env.MQTT_USERNAME || '';
const MQTT_PASSWORD = process.env.MQTT_PASSWORD || '';
const NODE_ID = process.env.SMARTLIGHT_NODE_ID || 'bedroom01';
const TOPIC_ROOT = process.env.SMARTLIGHT_TOPIC_ROOT || 'smartlight';
const ROOT = path.join(__dirname, '..');
const LED_PROFILE_DEFAULT_PATH = path.join(__dirname, 'config', 'led_profiles.default.json');
const LED_PROFILE_USER_PATH = path.join(__dirname, 'data', 'led_profiles.user.json');

const BASE_TOPIC = `${TOPIC_ROOT}/${NODE_ID}`;
const CMD_TOPIC = `${BASE_TOPIC}/cmd`;
const LED_SUB_TOPICS = [
  `${BASE_TOPIC}/status`,
  `${BASE_TOPIC}/ack`,
  `${BASE_TOPIC}/availability`,
  `${BASE_TOPIC}/event`
];
const EXTRA_SUB_TOPICS = (process.env.DASHBOARD_EXTRA_SUB_TOPICS || '').split(',').map(item => item.trim()).filter(Boolean);

// Smart Platform v2 / Integration topics
const INTEGRATION_ROOT = process.env.INTEGRATION_TOPIC_ROOT || 'integration/smart/v1';
const WEARABLE_NODE_ID = process.env.WEARABLE_NODE_ID || 'pico_wearable01';
const ENV_SENSOR_NODE_ID = process.env.ENV_SENSOR_NODE_ID || 'rpi_env01';
const FAN_NODE_ID = process.env.FAN_NODE_ID || 'rpi_env02';
const LIGHT_INTEGRATION_NODE_ID = process.env.LIGHT_INTEGRATION_NODE_ID || 'rpi_light01';

// 多 Broker 實機設定：燈光、心律/血氧、環境監測分別可使用不同 MQTT Broker IP。
const WEARABLE_MQTT_ENABLED = String(process.env.WEARABLE_MQTT_ENABLED || 'true').toLowerCase() !== 'false';
const WEARABLE_MQTT_HOST = process.env.WEARABLE_MQTT_HOST || '127.0.0.1';
const WEARABLE_MQTT_PORT = Number(process.env.WEARABLE_MQTT_PORT || MQTT_PORT || 1883);
const WEARABLE_MQTT_USERNAME = process.env.WEARABLE_MQTT_USERNAME || MQTT_USERNAME || '';
const WEARABLE_MQTT_PASSWORD = process.env.WEARABLE_MQTT_PASSWORD || MQTT_PASSWORD || '';

const ENV_MQTT_ENABLED = String(process.env.ENV_MQTT_ENABLED || 'true').toLowerCase() !== 'false';
const ENV_MQTT_HOST = process.env.ENV_MQTT_HOST || '127.0.0.1';
const ENV_MQTT_PORT = Number(process.env.ENV_MQTT_PORT || MQTT_PORT || 1883);
const ENV_MQTT_USERNAME = process.env.ENV_MQTT_USERNAME || MQTT_USERNAME || '';
const ENV_MQTT_PASSWORD = process.env.ENV_MQTT_PASSWORD || MQTT_PASSWORD || '';

const WEARABLE_STATUS_TOPIC = `${INTEGRATION_ROOT}/wearable/${WEARABLE_NODE_ID}/status`;
const WEARABLE_AVAILABILITY_TOPIC = `${INTEGRATION_ROOT}/wearable/${WEARABLE_NODE_ID}/availability`;
const ENV_STATUS_TOPIC = `${INTEGRATION_ROOT}/env/${ENV_SENSOR_NODE_ID}/status`;
const ENV_EVENT_TOPIC = `${INTEGRATION_ROOT}/env/${ENV_SENSOR_NODE_ID}/event`;
const ENV_AVAILABILITY_TOPIC = `${INTEGRATION_ROOT}/env/${ENV_SENSOR_NODE_ID}/availability`;
const FAN_STATUS_TOPIC = `${INTEGRATION_ROOT}/env/${FAN_NODE_ID}/status`;
const FAN_ACK_TOPIC = `${INTEGRATION_ROOT}/env/${FAN_NODE_ID}/ack`;
const FAN_AVAILABILITY_TOPIC = `${INTEGRATION_ROOT}/env/${FAN_NODE_ID}/availability`;
const FAN_CMD_TOPIC = `${INTEGRATION_ROOT}/env/${FAN_NODE_ID}/command`;
const LIGHT_INTEGRATION_STATUS_TOPIC = `${INTEGRATION_ROOT}/light/${LIGHT_INTEGRATION_NODE_ID}/status`;
const LIGHT_INTEGRATION_ACK_TOPIC = `${INTEGRATION_ROOT}/light/${LIGHT_INTEGRATION_NODE_ID}/ack`;
const LIGHT_INTEGRATION_AVAILABILITY_TOPIC = `${INTEGRATION_ROOT}/light/${LIGHT_INTEGRATION_NODE_ID}/availability`;

// 各 Broker 分開訂閱：主 Broker 處理燈光；Wearable Broker 處理心律血氧；Env Broker 處理環境監測與風扇。
const LIGHT_INTEGRATION_SUB_TOPICS = [
  // 保留 Smart Platform v2 light 訂閱，給未來把 LED 從舊版 smartlight/bedroom01 遷移到 rpi_light01 使用。
  LIGHT_INTEGRATION_STATUS_TOPIC,
  LIGHT_INTEGRATION_ACK_TOPIC,
  LIGHT_INTEGRATION_AVAILABILITY_TOPIC
];

const WEARABLE_SUB_TOPICS = [
  WEARABLE_STATUS_TOPIC,
  WEARABLE_AVAILABILITY_TOPIC
];

const ENV_SUB_TOPICS = [
  ENV_STATUS_TOPIC,
  ENV_EVENT_TOPIC,
  ENV_AVAILABILITY_TOPIC,
  FAN_STATUS_TOPIC,
  FAN_ACK_TOPIC,
  FAN_AVAILABILITY_TOPIC
];

const INTEGRATION_SUB_TOPICS = [
  ...LIGHT_INTEGRATION_SUB_TOPICS,
  ...WEARABLE_SUB_TOPICS,
  ...ENV_SUB_TOPICS
];
const STATUS_POLL_INTERVAL_MS = Number(process.env.SMARTLIGHT_STATUS_POLL_INTERVAL_MS || 10000);
const STATUS_BACKGROUND_POLL_INTERVAL_MS = Number(process.env.SMARTLIGHT_STATUS_BACKGROUND_POLL_INTERVAL_MS || 30000);
const STATUS_BROADCAST_MIN_MS = Number(process.env.SMARTLIGHT_STATUS_BROADCAST_MIN_MS || 1500);
const STATUS_STALE_MS = Number(process.env.SMARTLIGHT_STATUS_STALE_MS || 15000);
const GET_STATUS_DEDUPE_MS = Number(process.env.SMARTLIGHT_GET_STATUS_DEDUPE_MS || 1000);
const LEGACY_LIGHT_ACTION_MIN_MS = Number(process.env.SMARTLIGHT_LEGACY_LIGHT_ACTION_MIN_MS || 1000);

function ensureLedProfileFiles() {
  try {
    fs.mkdirSync(path.dirname(LED_PROFILE_DEFAULT_PATH), { recursive: true });
    fs.mkdirSync(path.dirname(LED_PROFILE_USER_PATH), { recursive: true });
    if (!fs.existsSync(LED_PROFILE_USER_PATH) && fs.existsSync(LED_PROFILE_DEFAULT_PATH)) {
      fs.copyFileSync(LED_PROFILE_DEFAULT_PATH, LED_PROFILE_USER_PATH);
      console.log(`[LED profiles] created user profile from default: ${LED_PROFILE_USER_PATH}`);
    }
  } catch (error) {
    console.warn('[LED profiles] initialization failed:', error.message);
  }
}

ensureLedProfileFiles();

// 小測試：活動狀態 state → LED 簡易自動控制。
// 目前測試以 wearable payload.status.summary.state 為主：-1=未量測、1=靜止、2=輕微活動、3=劇烈活動；0 目前不使用。
// 預設啟用；若要關閉，可在啟動前設定 AUTO_ACTIVITY_LED_ENABLED=false。
const AUTO_ACTIVITY_LED_ENABLED = String(process.env.AUTO_ACTIVITY_LED_ENABLED || 'true').toLowerCase() !== 'false';
const AUTO_ACTIVITY_LED_COOLDOWN_MS = Number(process.env.AUTO_ACTIVITY_LED_COOLDOWN_MS || 10000);

// 保留上一版心律 → LED 測試邏輯，但預設關閉，避免與 state 自動控制互相搶 LED。
const AUTO_HR_LED_ENABLED = String(process.env.AUTO_HR_LED_ENABLED || 'false').toLowerCase() !== 'false';
const AUTO_HR_LED_COOLDOWN_MS = Number(process.env.AUTO_HR_LED_COOLDOWN_MS || 10000);
const AUTO_HR_LED_SAMPLE_SIZE = Number(process.env.AUTO_HR_LED_SAMPLE_SIZE || 3);
const AUTO_HR_VALID_MIN = Number(process.env.AUTO_HR_VALID_MIN || 30);
const AUTO_HR_VALID_MAX = Number(process.env.AUTO_HR_VALID_MAX || 220);

const WEB_MODE_DEFAULTS = {
  vacant: { label: '無人', scene: 'vacant', brightness: 0, kelvin: 2700, brightnessMin: 0, brightnessMax: 0, kelvinMin: 2700, kelvinMax: 6500 },
  sleep: { label: '睡眠', scene: 'sleep', brightness: 30, kelvin: 2700, brightnessMin: 0, brightnessMax: 30, kelvinMin: 2700, kelvinMax: 6500 },
  relax: { label: '放鬆', scene: 'relax', brightness: 40, kelvin: 3000, brightnessMin: 5, brightnessMax: 70, kelvinMin: 2700, kelvinMax: 6500 },
  work: { label: '工作', scene: 'work', brightness: 60, kelvin: 4500, brightnessMin: 0, brightnessMax: 100, kelvinMin: 2700, kelvinMax: 6500 },
  exercise: { label: '運動', scene: 'exercise', brightness: 60, kelvin: 3000, brightnessMin: 10, brightnessMax: 85, kelvinMin: 2700, kelvinMax: 6500 },
  custom: { label: '自定義模式', scene: null, brightness: 64, kelvin: 3900, brightnessMin: 0, brightnessMax: 100, kelvinMin: 2700, kelvinMax: 6500 }
};

const SMART_SCENE_TO_WEB_MODE = {
  vacant: 'vacant',
  sleep: 'sleep',
  relax: 'relax',
  work: 'work',
  exercise: 'exercise',
  none: null
};

const ALLOWED_WEB_ACTIONS = new Set([
  'get_status',
  'set_mode',
  'set_auto',
  'set_smart_mode',
  'set_led_power',
  // Legacy compatibility only. The current Dashboard UI uses set_custom_light
  // and must not use these direct brightness/kelvin control actions.
  'set_brightness',
  'set_kelvin',
  'set_color',
  'set_advanced_light',
  'set_custom_light',
  'apply_current_light',
  'set_fan_level',
  'set_fan_mode'
]);

const ERROR_TEXT = {
  invalid_json_object: '指令格式錯誤',
  unsupported_command: '不支援的操作',
  invalid_scene: '不支援的情境模式',
  tone_bias_out_of_range: '冷暖偏移超出範圍',
  color_temp_k_out_of_range: '色溫超出可用範圍，請設定 2700K–6500K',
  invalid_command_payload: '指令內容格式錯誤',
  invalid_color: '顏色格式錯誤',
  unsupported_dashboard_action: 'Dashboard 不支援此操作',
  unsupported_dashboard_mode: 'Dashboard 不支援此模式',
  invalid_fan_level: '風扇等級需為 0–10',
  invalid_fan_mode: '風扇模式需為 OFF / MANUAL / AUTO',
  mqtt_not_connected: 'MQTT 尚未連線，請稍後再試',
  request_body_too_large: '指令內容過大'
};

function readJsonFileSafe(filePath, fallback = {}) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch (_) {
    return fallback;
  }
}

function readLedProfiles() {
  ensureLedProfileFiles();
  const defaults = readJsonFileSafe(LED_PROFILE_DEFAULT_PATH, { version: 1, scene_profiles: {}, custom_profiles: {} });
  const user = readJsonFileSafe(LED_PROFILE_USER_PATH, defaults);
  return normalizeLedProfiles({
    version: user.version || defaults.version || 1,
    scene_profiles: { ...(defaults.scene_profiles || {}), ...(user.scene_profiles || {}) },
    custom_profiles: { ...(defaults.custom_profiles || {}), ...(user.custom_profiles || {}) }
  });
}

function writeLedProfiles(nextProfiles) {
  fs.mkdirSync(path.dirname(LED_PROFILE_USER_PATH), { recursive: true });
  fs.writeFileSync(LED_PROFILE_USER_PATH, `${JSON.stringify(nextProfiles, null, 2)}\n`, 'utf8');
}

function profileWithRange(profile) {
  const next = { ...profile };
  const target = Number(next.target_lux);
  const tolerance = Number(next.tolerance_lux);
  if (Number.isFinite(target) && Number.isFinite(tolerance)) {
    next.target_lux_min = Math.max(0, Math.round(target - tolerance));
    next.target_lux_max = Math.round(target + tolerance);
  }
  return next;
}

function normalizeCustomProfile(key, profile = {}) {
  const source = { ...(profile || {}) };
  const rgb = source.rgb && typeof source.rgb === 'object'
    ? {
      r: safeInt(source.rgb.r, 0, 255, 255),
      g: safeInt(source.rgb.g, 0, 255, 160),
      b: safeInt(source.rgb.b, 0, 255, 80)
    }
    : { r: 255, g: 160, b: 80 };
  return {
    label: '固定亮度',
    custom_control_type: 'fixed_brightness',
    fixed_brightness_pct: safeInt(source.fixed_brightness_pct ?? source.brightness ?? 64, 0, 100, 64),
    target_lux: null,
    tolerance_lux: null,
    target_lux_min: null,
    target_lux_max: null,
    min_output: null,
    max_output: null,
    color_mode: String(source.color_mode || source.colorMode || 'cct') === 'rgb' ? 'rgb' : 'cct',
    color_temp_k: safeInt(source.color_temp_k ?? source.kelvin ?? 3900, 2700, 6500, 3900),
    rgb,
    effect: ['off', 'static', 'breathing', 'flow'].includes(source.effect) ? source.effect : 'static',
    flow_preset: ['soft_rainbow', 'warm_wave', 'focus_scan', 'aurora'].includes(source.flow_preset) ? source.flow_preset : 'soft_rainbow',
    flow_speed: ['slow', 'medium', 'fast'].includes(source.flow_speed) ? source.flow_speed : 'medium',
    flow_brightness: safeInt(source.flow_brightness ?? source.fixed_brightness_pct ?? 64, 0, 100, 64),
    breathing_speed: ['slow', 'medium', 'fast'].includes(source.breathing_speed) ? source.breathing_speed : 'slow',
    breathing_strength: safeInt(source.breathing_strength, 0, 100, 50)
  };
}

function normalizeLedProfiles(profiles = {}) {
  const mergedCustom = {
    ...(profiles.custom_profiles?.fixed_brightness || {}),
    // 舊版自定義目標照度設定若仍存在，僅取可用的顏色/燈效偏好，並轉成 fixed_brightness。
    ...(profiles.custom_profiles?.[['target', 'lux', 'range'].join('_')] || {})
  };
  const next = {
    version: profiles.version || 1,
    scene_profiles: { ...(profiles.scene_profiles || {}) },
    custom_profiles: {
      fixed_brightness: normalizeCustomProfile('fixed_brightness', mergedCustom)
    }
  };
  return next;
}

function updateLedProfile(section, key, patch) {
  const profiles = normalizeLedProfiles(readLedProfiles());
  if (!['scene_profiles', 'custom_profiles'].includes(section)) throw new Error('invalid_profile_section');
  const normalizedKey = section === 'custom_profiles' ? 'fixed_brightness' : key;
  if (!profiles[section] || !profiles[section][normalizedKey]) throw new Error('invalid_profile_key');
  const nextProfile = { ...profiles[section][normalizedKey], ...(patch || {}) };
  profiles[section][normalizedKey] = section === 'custom_profiles'
    ? normalizeCustomProfile('fixed_brightness', nextProfile)
    : profileWithRange(nextProfile);
  const normalized = normalizeLedProfiles(profiles);
  writeLedProfiles(normalized);
  return normalized;
}

function resetLedProfile(section, key) {
  const defaults = readJsonFileSafe(LED_PROFILE_DEFAULT_PATH, { version: 1, scene_profiles: {}, custom_profiles: {} });
  const profiles = normalizeLedProfiles(readLedProfiles());
  if (!['scene_profiles', 'custom_profiles'].includes(section)) throw new Error('invalid_profile_section');
  const normalizedKey = section === 'custom_profiles' ? 'fixed_brightness' : key;
  if (!defaults[section] || !defaults[section][normalizedKey]) throw new Error('invalid_profile_key');
  profiles[section][normalizedKey] = section === 'custom_profiles'
    ? normalizeCustomProfile('fixed_brightness', defaults[section][normalizedKey])
    : profileWithRange(defaults[section][normalizedKey]);
  const normalized = normalizeLedProfiles(profiles);
  writeLedProfiles(normalized);
  return normalized;
}

const SMARTLIGHT_EVENT_TEXT = {
  scene_vacant_applied: '無人已套用',
  scene_sleep_applied: '睡眠已套用',
  scene_relax_applied: '放鬆模式已套用',
  scene_work_applied: '工作已套用',
  scene_exercise_applied: '運動已套用',
  static_applied: '自定義燈光已套用',
  static_breathing_updated: '呼吸燈效已更新',
  flow_soft_rainbow_applied: '柔和彩虹燈效已套用',
  flow_aurora_applied: '極光燈效已套用',
  auto_applied: '自動調節已啟用',
  power_off: 'LED 已關閉'
};

let availability = 'unknown';
let currentWebMode = 'work';
let lastReqSeq = 0;
let lastSmartLightStatus = null;
let lastSmartLightStatusAt = 0;
let lastGetStatusPublishedAt = 0;
let lastStatusBroadcastAt = 0;
let lastStatusBroadcastSummary = null;
let statusFallbackTimer = null;
let statusPollTimer = null;
const legacyLightActionLastAt = new Map();
let mqttClient = null;
let wearableMqttClient = null;
let envMqttClient = null;
const pendingCommands = new Map();
const recentUserEvents = new Map();
const autoHeartRateState = {
  samples: [],
  lastZone: null,
  lastCommandAt: 0,
  lastAverage: null
};

const autoActivityLedState = {
  lastState: null,
  lastCommandAt: 0
};

const smartControlState = {
  enabled: String(process.env.SMART_MODE_ENABLED || 'true').toLowerCase() !== 'false'
};

function smartControlSnapshot() {
  return {
    smartModeEnabled: smartControlState.enabled,
    smartModeLabel: smartControlState.enabled ? '智慧模式開啟' : '智慧模式關閉'
  };
}

function disableSmartControlForUserLightAction(reason = 'user_light_action') {
  if (smartControlState.enabled === false) return;
  smartControlState.enabled = false;
  autoActivityLedState.lastState = null;
  autoHeartRateState.lastZone = null;
  emitDebugEvent('Smart mode disabled by user light action', { reason });
  broadcastPatch({
    uiState: smartControlSnapshot(),
    sourceStatus: '智慧模式已關閉，使用者正在操作 LED',
    connectionState: currentConnectionState()
  });
}

let lastDashboardPayload = {
  uiState: smartControlSnapshot(),
  sensorData: {
    heartRate: null,
    spo2: null,
    aqi: null,
    temperature: null,
    humidity: null,
    comfort: null,
    pirDetected: null,
    pirTime: null,
    onlineDevices: 0,
    totalDevices: 9,
    systemStatus: '等待實機資料',
    moduleStatus: [],
    deviceConnectionText: ''
  },
  deviceState: {
    ledOn: null,
    brightness: null,
    kelvin: null,
    ledColor: '#2d3748',
    fanOn: null,
    fanSpeed: null
  },
  audioState: {
    connected: null,
    playing: false,
    mode: null,
    volume: null,
    track: null,
    progressSec: null,
    durationSec: null
  },
  mode: 'work',
  sourceStatus: '等待實機資料',
  connectionState: {
    websocket: true,
    mqtt: false,
    smartLight: 'unknown',
    totalDevices: 9,
    onlineDevices: 0,
    moduleStatus: []
  }
};

function safeInt(value, min, max, fallback = min) {
  const n = Number(value);
  if (!Number.isFinite(n)) return fallback;
  return Math.round(Math.max(min, Math.min(max, n)));
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

function readableError(reason, code) {
  return ERROR_TEXT[reason] || ERROR_TEXT[code] || reason || code || '未知錯誤';
}

function jsonResponse(res, status, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body)
  });
  res.end(body);
}

function sendFile(res, status, contentType, body) {
  res.writeHead(status, { 'Content-Type': contentType });
  res.end(body);
}

function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    req.on('data', chunk => {
      body += chunk;
      if (body.length > 1024 * 1024) {
        reject(new Error('request_body_too_large'));
        req.destroy();
      }
    });
    req.on('end', () => {
      try {
        resolve(body ? JSON.parse(body) : {});
      } catch (error) {
        reject(error);
      }
    });
    req.on('error', reject);
  });
}

function nowReqId(prefix = 'web') {
  lastReqSeq += 1;
  return `${prefix}_${Date.now()}_${String(lastReqSeq).padStart(4, '0')}`;
}

function allowLegacyLightAction(action) {
  const now = Date.now();
  const last = legacyLightActionLastAt.get(action) || 0;
  if (now - last < LEGACY_LIGHT_ACTION_MIN_MS) {
    emitDebugEvent('Legacy light action throttled', { action, minIntervalMs: LEGACY_LIGHT_ACTION_MIN_MS });
    return false;
  }
  legacyLightActionLastAt.set(action, now);
  return true;
}

function kelvinToHex(kelvin) {
  const k = safeInt(kelvin, 2700, 6500, 3500);
  if (k <= 2900) return '#ff9463';
  if (k <= 3400) return '#ffb75c';
  if (k <= 4000) return '#ffd89a';
  if (k <= 4700) return '#fff2d0';
  if (k <= 5600) return '#eef7ff';
  return '#d8ecff';
}

function normalizeHex(hex) {
  if (typeof hex !== 'string') return null;
  let value = hex.trim();
  if (!value.startsWith('#')) value = `#${value}`;
  if (/^#[0-9a-fA-F]{3}$/.test(value)) {
    value = `#${value[1]}${value[1]}${value[2]}${value[2]}${value[3]}${value[3]}`;
  }
  return /^#[0-9a-fA-F]{6}$/.test(value) ? value.toLowerCase() : null;
}

function hexToRgb(hex) {
  const normalized = normalizeHex(hex);
  if (!normalized) return null;
  return {
    r: parseInt(normalized.slice(1, 3), 16),
    g: parseInt(normalized.slice(3, 5), 16),
    b: parseInt(normalized.slice(5, 7), 16)
  };
}

const MODULE_DEFS = [
  { key: 'led', label: 'LED系統' },
  { key: 'wearable', label: '穿戴式手環' },
  { key: 'envComfort', label: '環境舒適度/節能控制節點' }
];
function moduleAlias(key) {
  if (['heart', 'spo2', 'wearable'].includes(key)) return 'wearable';
  if (['aqi', 'tempHumidity', 'pir', 'fan', 'env', 'envComfort'].includes(key)) return 'envComfort';
  return key;
}
const MODULE_STALE_MS = Number(process.env.DASHBOARD_MODULE_STALE_MS || 15000);
const moduleLastSeen = Object.fromEntries(MODULE_DEFS.map(item => [item.key, { at: 0, detail: '' }]));

function markModule(key, detail = '') {
  const alias = moduleAlias(key);
  if (!moduleLastSeen[alias]) return;
  moduleLastSeen[alias] = { at: Date.now(), detail: detail || moduleLastSeen[alias].detail || '' };
}

function setModuleOnline(key, online, detail = '') {
  const alias = moduleAlias(key);
  if (!moduleLastSeen[alias]) return;
  moduleLastSeen[alias] = {
    at: online ? Date.now() : 0,
    detail: detail || moduleLastSeen[alias].detail || ''
  };
}

function getField(data, names) {
  if (!data || typeof data !== 'object') return undefined;
  for (const name of names) {
    if (data[name] !== undefined && data[name] !== null && data[name] !== '') return data[name];
  }
  return undefined;
}

function getFieldIncludingNull(data, names) {
  if (!data || typeof data !== 'object') return undefined;
  for (const name of names) {
    if (Object.prototype.hasOwnProperty.call(data, name) && data[name] !== '') return data[name];
  }
  return undefined;
}

function asNumberOrNull(value) {
  if (value === null || value === undefined || value === '') return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function truthyState(value) {
  if (value === true || value === 1) return true;
  if (value === false || value === 0) return false;
  if (typeof value === 'string') {
    const v = value.trim().toLowerCase();
    if (['online', 'connected', 'on', 'true', 'yes', 'active', 'detected', 'motion'].includes(v)) return true;
    if (['offline', 'disconnected', 'off', 'false', 'no', 'inactive', 'none'].includes(v)) return false;
  }
  return null;
}


function integrationParts(topic) {
  const parts = String(topic || '').split('/');
  if (parts.length !== 6) return null;
  if (`${parts[0]}/${parts[1]}/${parts[2]}` !== INTEGRATION_ROOT) return null;
  return { domain: parts[3], nodeId: parts[4], type: parts[5] };
}

function payloadBody(data) {
  if (!data || typeof data !== 'object') return {};
  if (data.status && typeof data.status === 'object') return data.status;
  if (data.event && typeof data.event === 'object') return data.event;
  if (data.availability && typeof data.availability === 'object') return data.availability;
  return data;
}

function normalizeOnlineFromPayload(raw, data) {
  if (data && typeof data === 'object') {
    const v = getField(data.availability || data, ['online', 'connected', 'available', 'status']);
    const b = truthyState(v);
    if (b !== null) return b;
  }
  const b = truthyState(String(raw || '').trim());
  return b === null ? false : b;
}

function markAvailabilityModules(parts, online, detail) {
  if (!parts) return false;
  if (parts.domain === 'wearable') {
    setModuleOnline('heart', online, detail);
    setModuleOnline('spo2', online, detail);
    return true;
  }
  if (parts.domain === 'env' && parts.nodeId === ENV_SENSOR_NODE_ID) {
    setModuleOnline('tempHumidity', online, detail);
    setModuleOnline('aqi', online, detail);
    // PIR 模組圖示改由 occupancy 值決定：true=有人活動/綠色，false=無人活動/紅色。
    // availability online 不主動把 PIR 轉綠，避免 occupancy=false 後被 availability 覆蓋。
    if (!online) setModuleOnline('pir', false, detail);
    return true;
  }
  if (parts.domain === 'env' && parts.nodeId === FAN_NODE_ID) {
    setModuleOnline('fan', online, detail);
    return true;
  }
  if (parts.domain === 'light') {
    setModuleOnline('led', online, detail);
    return true;
  }
  if (parts.domain === 'audio') {
    setModuleOnline('audio', online, detail);
    return true;
  }
  if (parts.domain === 'camera' || parts.domain === 'vision') {
    setModuleOnline('camera', online, detail);
    return true;
  }
  return false;
}

function offlineSensorPatchForAvailability(parts, online) {
  if (!parts || online) return {};
  if (parts.domain === 'env' && parts.nodeId === ENV_SENSOR_NODE_ID) {
    return {
      temperature: null,
      humidity: null,
      aqi: null,
      pirDetected: null,
      pirTime: null
    };
  }
  return {};
}

function moduleSnapshot() {
  const now = Date.now();
  return MODULE_DEFS.map(def => {
    const seen = moduleLastSeen[def.key] || { at: 0, detail: '' };
    const online = seen.at > 0 && now - seen.at <= MODULE_STALE_MS;
    return {
      key: def.key,
      label: def.label,
      online,
      status: online ? 'online' : 'offline',
      lastSeenAt: seen.at || null,
      detail: online ? (seen.detail || '收到實機資料') : '--'
    };
  });
}

function mqttSourceStatus() {
  const connectedCount = [mqttClient, envMqttClient, wearableMqttClient].filter(client => client?.connected).length;
  const brokerSummary = `MQTT ${connectedCount}/3`;
  if (availability === 'online') return `SmartLight 已連線｜${brokerSummary}`;
  if (availability === 'offline') return `SmartLight 離線｜${brokerSummary}`;
  if (mqttClient?.connected) return `燈光 MQTT 已連線，等待 SmartLight｜${brokerSummary}`;
  return `MQTT 連線中｜${brokerSummary}`;
}

function getConnectionSummary() {
  const mqttOnline = mqttClient?.connected === true;
  const nodeOnline = mqttOnline && availability === 'online';
  const statusFresh = nodeOnline && lastSmartLightStatusAt > 0 && (Date.now() - lastSmartLightStatusAt <= STATUS_STALE_MS);
  if (statusFresh) markModule('led', `SmartLight NODE ${NODE_ID} status`);

  const devices = moduleSnapshot();
  const onlineDevices = devices.filter(item => item.online).length;
  const totalDevices = devices.length;
  const systemStatus = onlineDevices === totalDevices ? '正常運作' : onlineDevices > 0 ? '部分連線' : '等待實機資料';
  return {
    onlineDevices,
    totalDevices,
    systemStatus,
    deviceConnections: devices,
    moduleStatus: devices,
    deviceConnectionText: devices.map(item => `${item.label}：${item.online ? '在線' : '未連線'}`).join('｜')
  };
}

function currentConnectionState() {
  const summary = getConnectionSummary();
  return {
    websocket: true,
    mqtt: mqttClient?.connected === true,
    smartLight: availability,
    lightMqtt: mqttClient?.connected === true,
    wearableMqtt: wearableMqttClient?.connected === true,
    envMqtt: envMqttClient?.connected === true,
    onlineDevices: summary.onlineDevices,
    totalDevices: summary.totalDevices,
    systemStatus: summary.systemStatus,
    deviceConnections: summary.deviceConnections,
    moduleStatus: summary.moduleStatus,
    deviceConnectionText: summary.deviceConnectionText
  };
}

function mergePayload(patch = {}) {
  const { userEvent, debugEvent, event, ...statePatch } = patch;
  lastDashboardPayload = {
    ...lastDashboardPayload,
    ...statePatch,
    sensorData: {
      ...lastDashboardPayload.sensorData,
      ...(statePatch.sensorData || {})
    },
    deviceState: {
      ...lastDashboardPayload.deviceState,
      ...(statePatch.deviceState || {})
    },
    audioState: {
      ...lastDashboardPayload.audioState,
      ...(statePatch.audioState || {})
    },
    uiState: {
      ...lastDashboardPayload.uiState,
      ...smartControlSnapshot(),
      ...(statePatch.uiState || {})
    },
    connectionState: {
      ...lastDashboardPayload.connectionState,
      ...(statePatch.connectionState || {})
    }
  };

  return {
    ...lastDashboardPayload,
    ...(userEvent ? { userEvent } : {}),
    ...(debugEvent ? { debugEvent } : {})
  };
}

function broadcast(payload) {
  const text = JSON.stringify(payload);
  wss.clients.forEach(client => {
    if (client.readyState === client.OPEN) client.send(text);
  });
}

function withConnectionSummary(patch = {}) {
  const summary = getConnectionSummary();
  return {
    ...patch,
    sensorData: {
      ...(patch.sensorData || {}),
      onlineDevices: summary.onlineDevices,
      totalDevices: summary.totalDevices,
      systemStatus: summary.systemStatus,
      moduleStatus: summary.moduleStatus,
      deviceConnectionText: summary.deviceConnectionText
    },
    connectionState: {
      ...currentConnectionState(),
      ...(patch.connectionState || {})
    },
    uiState: {
      ...smartControlSnapshot(),
      ...(patch.uiState || {})
    }
  };
}

function broadcastPatch(patch) {
  broadcast(mergePayload(withConnectionSummary(patch)));
}

function toUnixMs(value) {
  if (typeof value === 'number' && Number.isFinite(value)) return Math.round(value);
  if (typeof value === 'string' && value.trim()) {
    const parsed = Date.parse(value);
    if (Number.isFinite(parsed)) return parsed;
  }
  return null;
}

function normalizeTimestampFields(payload) {
  if (!payload || typeof payload !== 'object') return payload;
  if (Object.prototype.hasOwnProperty.call(payload, 'ts')) {
    const unixMs = toUnixMs(payload.ts);
    if (unixMs !== null) {
      if (typeof payload.ts === 'string' && !payload.ts_iso) payload.ts_iso = payload.ts;
      payload.ts = unixMs;
    }
  }
  return payload;
}

function publishGetStatus(reason = 'status_refresh', options = {}) {
  if (!mqttClient?.connected) return null;
  if (availability !== 'online' && options.requireOnline !== false) return null;
  const now = Date.now();
  if (now - lastGetStatusPublishedAt < GET_STATUS_DEDUPE_MS) {
    emitDebugEvent('MQTT get_status skipped', {
      reason,
      dedupeMs: GET_STATUS_DEDUPE_MS,
      elapsedMs: now - lastGetStatusPublishedAt
    });
    return { ok: true, deduped: true, command: 'get_status', reason };
  }
  lastGetStatusPublishedAt = now;
  return publishSmartLightCommand('get_status', {}, nowReqId(reason), {
    action: 'get_status',
    silent: options.silent !== false
  });
}

function scheduleStatusFallback(reason = 'command') {
  clearTimeout(statusFallbackTimer);
  statusFallbackTimer = setTimeout(() => {
    statusFallbackTimer = null;
    try {
      publishGetStatus(`web_${reason}_status`);
    } catch (error) {
      console.warn('[Light MQTT] fallback get_status failed:', error.message);
    }
  }, 1000);
}

function cancelStatusFallback() {
  clearTimeout(statusFallbackTimer);
  statusFallbackTimer = null;
}

function emitUserEvent({ text, icon = 'device', accent = 'var(--green)', level = 'info', dedupeMs = 2500 }) {
  const plainKey = String(text).replace(/<[^>]+>/g, '');
  const now = Date.now();
  const last = recentUserEvents.get(plainKey) || 0;
  if (now - last < dedupeMs) return;
  recentUserEvents.set(plainKey, now);
  broadcastPatch({ userEvent: { text, icon, accent, level } });
}

function emitDebugEvent(title, detail) {
  broadcastPatch({
    debugEvent: {
      time: Date.now(),
      title,
      detail
    }
  });
}

function sceneCommandForMode(mode, brightness, kelvin, forceStatic = false) {
  const defaults = WEB_MODE_DEFAULTS[mode] || WEB_MODE_DEFAULTS.work;
  const scene = forceStatic ? null : defaults.scene;
  const finalBrightness = safeInt(brightness ?? defaults.brightness, defaults.brightnessMin ?? 0, defaults.brightnessMax ?? 100, defaults.brightness);
  const finalKelvin = safeInt(kelvin ?? defaults.kelvin, defaults.kelvinMin ?? 2700, defaults.kelvinMax ?? 6500, defaults.kelvin);

  if (!scene) {
    return {
      command: 'set_static',
      params: {
        brightness_pct: finalBrightness,
        color_temp_k: finalKelvin
      }
    };
  }

  return {
    command: 'set_scene',
    params: {
      scene
    }
  };
}

function modeLabel(mode) {
  return WEB_MODE_DEFAULTS[mode]?.label || '燈光模式';
}

function successTextForAck(ack, pending) {
  const action = pending?.action || '';
  const meta = pending?.meta || {};

  if (action === 'get_status' || ack.command === 'get_status') return null;
  if (action === 'set_fan_level') return `<strong>風扇：</strong>已調整為 ${escapeHtml(meta.fanLevel ?? meta.fan_level ?? '')} 級`;
  if (action === 'set_fan_mode') return `<strong>風扇模式：</strong>已切換為 ${escapeHtml(meta.mode || '')}`;
  if (action === 'set_mode') return `<strong>${escapeHtml(modeLabel(meta.mode))}：</strong>已套用`;
  if (action === 'set_auto' || ack.command === 'set_auto') return '<strong>自動調節：</strong>已啟用';
  if (action === 'set_led_power') return meta.on === false ? '<strong>LED：</strong>已關閉' : '<strong>LED：</strong>已開啟';
  if (action === 'set_brightness') return `<strong>亮度：</strong>已調整為 ${escapeHtml(meta.brightness)}%`;
  if (action === 'set_kelvin') return `<strong>色溫：</strong>已調整為 ${escapeHtml(meta.kelvin)}K`;
  if (action === 'set_color') return '<strong>自訂顏色：</strong>已套用';
  if (action === 'set_advanced_light') return '<strong>燈光進階設定：</strong>已套用';
  if (action === 'apply_current_light') return '<strong>燈光設定：</strong>已套用';

  if (ack.command === 'power_off') return '<strong>LED：</strong>已關閉';
  if (ack.command === 'set_scene') return '<strong>情境模式：</strong>已套用';
  if (ack.command === 'set_static') return '<strong>自定義燈光：</strong>已套用';
  return `<strong>${escapeHtml(ack.command || '指令')}：</strong>已完成`;
}

function publishSmartLightCommand(command, params = {}, reqId = '', meta = {}) {
  if (!mqttClient?.connected) {
    const error = new Error('mqtt_not_connected');
    if (!meta.silent) {
      emitUserEvent({
        text: `<strong>無法送出：</strong>${escapeHtml(readableError(error.message))}`,
        icon: 'alert',
        accent: 'var(--yellow)',
        level: 'warning'
      });
    }
    throw error;
  }

  const payload = {
    req_id: reqId || nowReqId('web'),
    cmd: command
  };

  if (params && Object.keys(params).length > 0) payload.params = params;

  pendingCommands.set(payload.req_id, {
    action: meta.action || command,
    command,
    params,
    meta,
    sentAt: Date.now()
  });

  const text = JSON.stringify(payload);
  mqttClient.publish(CMD_TOPIC, text, { qos: 1 }, error => {
    if (error) {
      pendingCommands.delete(payload.req_id);
      console.error('[MQTT] publish failed:', error.message);
      emitDebugEvent('MQTT publish failed', { topic: CMD_TOPIC, payload, error: error.message });
      emitUserEvent({
        text: `<strong>指令送出失敗：</strong>${escapeHtml(error.message)}`,
        icon: 'alert',
        accent: 'var(--yellow)',
        level: 'error'
      });
      return;
    }

    console.log(`[MQTT → SmartLight] ${CMD_TOPIC} ${text}`);
    emitDebugEvent('MQTT publish', { topic: CMD_TOPIC, payload });
    if (command !== 'get_status' && meta.silent !== true) scheduleStatusFallback(meta.action || command);
  });

  broadcastPatch({
    sourceStatus: '等待 SmartLight 回應',
    connectionState: currentConnectionState()
  });

  return payload;
}



function integrationClientForTopic(topic) {
  const parts = integrationParts(topic);
  if (parts?.domain === 'env') return envMqttClient || mqttClient;
  if (parts?.domain === 'wearable') return wearableMqttClient || mqttClient;
  return mqttClient;
}

function integrationClientLabelForTopic(topic) {
  const parts = integrationParts(topic);
  if (parts?.domain === 'env') return '環境監測 MQTT';
  if (parts?.domain === 'wearable') return '心律血氧 MQTT';
  return '燈光 MQTT';
}

function publishIntegrationCommand(topic, command, params = {}, reqId = '', meta = {}) {
  const targetClient = integrationClientForTopic(topic);
  if (!targetClient?.connected) {
    const error = new Error('mqtt_not_connected');
    if (!meta.silent) {
      emitUserEvent({
        text: `<strong>無法送出：</strong>${escapeHtml(readableError(error.message))}`,
        icon: 'alert',
        accent: 'var(--yellow)',
        level: 'warning'
      });
    }
    throw error;
  }

  const requestId = reqId || nowReqId('web_integration');
  const payload = {
    command,
    params: params || {},
    request_id: requestId,
    source: 'server'
  };

  pendingCommands.set(requestId, {
    action: meta.action || command,
    command,
    params,
    meta: { ...meta, integrationTopic: topic },
    sentAt: Date.now()
  });

  const text = JSON.stringify(payload);
  targetClient.publish(topic, text, { qos: 1 }, error => {
    if (error) {
      pendingCommands.delete(requestId);
      console.error('[MQTT] integration publish failed:', error.message);
      emitDebugEvent('MQTT integration publish failed', { topic, payload, error: error.message });
      emitUserEvent({
        text: `<strong>指令送出失敗：</strong>${escapeHtml(error.message)}`,
        icon: 'alert',
        accent: 'var(--yellow)',
        level: 'error'
      });
      return;
    }

    console.log(`[Dashboard → Integration] ${topic} ${text}`);
    emitDebugEvent('MQTT integration publish', { topic, payload });
    if (meta.silent !== true) scheduleStatusFallback(meta.action || command);
  });

  broadcastPatch({ sourceStatus: '等待模組回應', connectionState: currentConnectionState() });
  return payload;
}

function publishFanCommand(command, params = {}, reqId = '', meta = {}) {
  return publishIntegrationCommand(FAN_CMD_TOPIC, command, params, reqId || nowReqId('web_fan'), {
    ...meta,
    action: meta.action || (command === 'set_fan_level' ? 'set_fan_level' : 'set_fan_mode'),
    module: 'fan'
  });
}

function handleDashboardCommand(message) {
  const action = message.action;
  if (!ALLOWED_WEB_ACTIONS.has(action)) throw new Error('unsupported_dashboard_action');

  const currentDevice = lastDashboardPayload.deviceState || {};
  const requestedMode = message.mode || currentWebMode || 'work';
  if (requestedMode && !WEB_MODE_DEFAULTS[requestedMode]) throw new Error('unsupported_dashboard_mode');

  const mode = requestedMode || 'work';
  const modeDefaults = WEB_MODE_DEFAULTS[mode] || WEB_MODE_DEFAULTS.work;
  const manualControl = message.controlMode === 'manual';
  const brightness = safeInt(
    message.brightness ?? currentDevice.brightness ?? modeDefaults.brightness,
    modeDefaults.brightnessMin ?? 0,
    modeDefaults.brightnessMax ?? 100,
    modeDefaults.brightness
  );
  const kelvin = safeInt(
    message.kelvin ?? currentDevice.kelvin ?? modeDefaults.kelvin,
    modeDefaults.kelvinMin ?? 2700,
    modeDefaults.kelvinMax ?? 6500,
    modeDefaults.kelvin
  );

  if (action === 'get_status') {
    return publishGetStatus('web_get_status', { silent: message.silent === true, requireOnline: false });
  }

  if (action === 'set_smart_mode') {
    smartControlState.enabled = message.enabled !== false;
    autoActivityLedState.lastState = null;
    autoHeartRateState.lastZone = null;
    const requestedControlMode = ['auto', 'scene', 'manual'].includes(message.controlMode) ? message.controlMode : null;
    broadcastPatch({
      uiState: {
        ...smartControlSnapshot(),
        ...(requestedControlMode ? { controlMode: requestedControlMode } : {})
      },
      sourceStatus: smartControlState.enabled ? '智慧模式已開啟' : '智慧模式已關閉',
      connectionState: currentConnectionState()
    });
    if (message.silent !== true) {
      emitUserEvent({
        text: smartControlState.enabled
          ? '<strong>智慧模式：</strong>已開啟，Server 可依感測資料自動控制'
          : '<strong>智慧模式：</strong>已關閉，保留自定義設定',
        icon: 'bot',
        accent: smartControlState.enabled ? 'var(--green)' : 'var(--yellow)',
        level: smartControlState.enabled ? 'info' : 'warning'
      });
    }
    return { ok: true, smartModeEnabled: smartControlState.enabled };
  }

  if (action === 'set_auto') {
    autoActivityLedState.lastState = null;
    autoHeartRateState.lastZone = null;
    broadcastPatch({
      uiState: { ...smartControlSnapshot(), controlMode: 'scene' },
      sourceStatus: '情境模式環境光自適應已啟用',
      connectionState: currentConnectionState()
    });
    // 舊版相容：若收到 set_auto，前端仍視為情境模式。
    // 真正的環境光回授達標需 MCU 支援環境光感測與目標亮度控制。
    return publishSmartLightCommand('set_auto', { enabled: true }, nowReqId('web_auto'), { action: 'set_auto' });
  }

  if (action === 'set_mode') {
    disableSmartControlForUserLightAction('set_mode');
    currentWebMode = mode;
    broadcastPatch({
      uiState: { ...smartControlSnapshot(), controlMode: 'scene', emotionMode: currentWebMode },
      mode: currentWebMode,
      sourceStatus: '情境模式已切換',
      connectionState: currentConnectionState()
    });
    const { command, params } = sceneCommandForMode(currentWebMode, brightness, kelvin);
    return publishSmartLightCommand(command, params, nowReqId(`web_${currentWebMode}`), {
      action: 'set_mode',
      mode: currentWebMode
    });
  }

  if (action === 'set_led_power') {
    const on = message.on !== false;
    if (!on) {
      return publishSmartLightCommand('power_off', {}, nowReqId('web_power_off'), {
        action: 'set_led_power',
        on: false
      });
    }

    // power_off 後 status 可能回報 brightness = 0。
    // 重新開燈時不可再因 brightness <= 0 送 power_off，需使用目前模式預設亮度恢復。
    const restoreMode = manualControl ? 'custom' : mode;
    const restoreBrightness = brightness > 0
      ? brightness
      : (WEB_MODE_DEFAULTS[restoreMode]?.brightness || WEB_MODE_DEFAULTS.work.brightness);
    const restoreKelvin = kelvin || WEB_MODE_DEFAULTS[restoreMode]?.kelvin || WEB_MODE_DEFAULTS.work.kelvin;

    const { command, params } = sceneCommandForMode(restoreMode, restoreBrightness, restoreKelvin, manualControl);
    currentWebMode = restoreMode;
    return publishSmartLightCommand(command, params, nowReqId('web_power_on'), {
      action: 'set_led_power',
      on: true,
      mode: currentWebMode,
      ...(manualControl ? { brightness: params.brightness_pct, kelvin: params.color_temp_k } : {})
    });
  }

  if (action === 'set_brightness') {
    // Legacy compatibility for old external web clients. The current Dashboard UI
    // must use set_custom_light and never drive LED brightness through this path.
    if (!allowLegacyLightAction(action)) return { ok: false, legacy: true, reason: 'legacy_rate_limited', action };
    disableSmartControlForUserLightAction('set_brightness');
    if (manualControl && brightness <= 0) {
      return publishSmartLightCommand('power_off', {}, nowReqId('web_brightness_zero'), {
        action: 'set_led_power',
        on: false
      });
    }

    const forceStatic = manualControl;
    const commandMode = forceStatic ? 'custom' : mode;
    const { command, params } = sceneCommandForMode(commandMode, brightness, kelvin, forceStatic);
    currentWebMode = commandMode;
    return publishSmartLightCommand(command, params, nowReqId('web_brightness'), {
      action: 'set_brightness',
      legacy: true,
      brightness,
      kelvin,
      mode: commandMode
    });
  }

  if (action === 'set_kelvin') {
    // Legacy compatibility for old external web clients. The current Dashboard UI
    // must use set_custom_light and never drive LED kelvin through this path.
    if (!allowLegacyLightAction(action)) return { ok: false, legacy: true, reason: 'legacy_rate_limited', action };
    disableSmartControlForUserLightAction('set_kelvin');
    const forceStatic = manualControl;
    const commandMode = forceStatic ? 'custom' : mode;
    const { command, params } = sceneCommandForMode(commandMode, brightness || WEB_MODE_DEFAULTS[commandMode].brightness, kelvin, forceStatic);
    currentWebMode = commandMode;
    return publishSmartLightCommand(command, params, nowReqId('web_kelvin'), {
      action: 'set_kelvin',
      legacy: true,
      ...(forceStatic ? { brightness: params.brightness_pct } : {}),
      kelvin,
      mode: commandMode
    });
  }

  if (action === 'set_color') {
    disableSmartControlForUserLightAction('set_color');
    const rgb = hexToRgb(message.color || message.ledColor);
    if (!rgb) throw new Error('invalid_color');
    currentWebMode = 'custom';
    const finalBrightness = brightness || WEB_MODE_DEFAULTS.custom.brightness;
    return publishSmartLightCommand('set_static', {
      brightness_pct: finalBrightness,
      ...rgb
    }, nowReqId('web_static_rgb'), {
      action: 'set_color',
      brightness: finalBrightness,
      color: normalizeHex(message.color || message.ledColor) || '#9b5cff'
    });
  }

  if (action === 'set_advanced_light') {
    disableSmartControlForUserLightAction('set_advanced_light');
    currentWebMode = 'custom';

    // 流水燈模式：只送 set_flow，流水動畫亮度獨立使用 flowBrightness。
    // 不再先送 set_static，避免「靜態亮度」和「流水亮度」看起來是同一個功能。
    if (message.flowEnabled === true) {
      const flowBrightness = safeInt(message.flowBrightness ?? message.brightness ?? brightness ?? WEB_MODE_DEFAULTS.custom.brightness, 0, 100, WEB_MODE_DEFAULTS.custom.brightness);
      return publishSmartLightCommand('set_flow', {
        flow_preset: ['soft_rainbow', 'aurora'].includes(message.flowPreset) ? message.flowPreset : 'soft_rainbow',
        flow_speed: ['slow', 'medium'].includes(message.flowSpeed) ? message.flowSpeed : 'medium',
        flow_brightness: flowBrightness,
        soft_mode: false
      }, nowReqId('web_advanced_flow'), {
        action: 'set_advanced_light',
        flowBrightness
      });
    }

    // 靜態 / RGB / 呼吸燈模式：使用 brightness 作為靜態/RGB 亮度。
    const rgb = hexToRgb(message.color || message.ledColor);
    if (!rgb) throw new Error('invalid_color');

    const finalBrightness = safeInt(message.brightness ?? brightness ?? WEB_MODE_DEFAULTS.custom.brightness, 0, 100, WEB_MODE_DEFAULTS.custom.brightness);
    const published = [publishSmartLightCommand('set_static', {
      brightness_pct: finalBrightness,
      ...rgb
    }, nowReqId('web_advanced_static'), {
      action: 'set_advanced_light',
      brightness: finalBrightness,
      color: normalizeHex(message.color || message.ledColor) || '#9b5cff'
    })];

    if (message.breathingEnabled === true) {
      published.push(publishSmartLightCommand('set_static_breathing', {
        enabled: true,
        speed: ['slow', 'medium'].includes(message.breathingSpeed) ? message.breathingSpeed : 'slow',
        strength: ['low', 'medium'].includes(message.breathingStrength) ? message.breathingStrength : 'medium'
      }, nowReqId('web_advanced_breathing'), {
        action: 'set_advanced_light'
      }));
    }

    return published;
  }

  if (action === 'set_custom_light') {
    disableSmartControlForUserLightAction('set_custom_light');
    currentWebMode = 'custom';

    const colorMode = String(message.color_mode || message.colorMode || 'cct').trim();
    const rgbFromChannels = (
      message.r !== undefined || message.g !== undefined || message.b !== undefined ||
      message.rgb_r !== undefined || message.rgb_g !== undefined || message.rgb_b !== undefined
    ) ? {
      r: safeInt(message.r ?? message.rgb_r, 0, 255, 255),
      g: safeInt(message.g ?? message.rgb_g, 0, 255, 160),
      b: safeInt(message.b ?? message.rgb_b, 0, 255, 80)
    } : null;
    const rgbFromObject = message.rgb && typeof message.rgb === 'object' ? {
      r: safeInt(message.rgb.r, 0, 255, 255),
      g: safeInt(message.rgb.g, 0, 255, 160),
      b: safeInt(message.rgb.b, 0, 255, 80)
    } : null;
    const rgb = rgbFromChannels || rgbFromObject || hexToRgb(message.color || message.ledColor);
    const fixedBrightness = safeInt(message.fixed_brightness_pct ?? message.brightness ?? brightness ?? WEB_MODE_DEFAULTS.custom.brightness, 0, 100, WEB_MODE_DEFAULTS.custom.brightness);
    const effect = String(message.effect || 'static').trim();

    if (effect === 'flow') {
      return publishSmartLightCommand('set_flow', {
        flow_preset: ['soft_rainbow', 'warm_wave', 'focus_scan', 'aurora'].includes(message.flow_preset) ? message.flow_preset : 'soft_rainbow',
        flow_speed: ['slow', 'medium', 'fast'].includes(message.flow_speed) ? message.flow_speed : 'medium',
        flow_brightness: safeInt(message.flow_brightness ?? fixedBrightness, 0, 100, fixedBrightness),
        soft_mode: false
      }, nowReqId('web_custom_fixed_flow'), {
        action: 'set_custom_light',
        custom_control_type: 'fixed_brightness',
        fallback_command: 'set_flow'
      });
    }

    const staticParams = colorMode === 'rgb'
      ? (() => {
        if (!rgb) throw new Error('invalid_color');
        return { brightness_pct: fixedBrightness, r: rgb.r, g: rgb.g, b: rgb.b };
      })()
      : { brightness_pct: fixedBrightness, color_temp_k: safeInt(message.color_temp_k ?? message.kelvin ?? kelvin ?? WEB_MODE_DEFAULTS.custom.kelvin, 2700, 6500, WEB_MODE_DEFAULTS.custom.kelvin) };

    const published = [publishSmartLightCommand('set_static', staticParams, nowReqId('web_custom_fixed_static'), {
      action: 'set_custom_light',
      custom_control_type: 'fixed_brightness',
      fallback_command: 'set_static',
      brightness: fixedBrightness
    })];

    if (effect === 'breathing') {
      const strengthValue = safeInt(message.breathing_strength ?? message.breathingStrength, 0, 100, 50);
      published.push(publishSmartLightCommand('set_static_breathing', {
        enabled: true,
        speed: ['slow', 'medium', 'fast'].includes(message.breathing_speed) ? message.breathing_speed : 'slow',
        strength: strengthValue < 34 ? 'low' : 'medium'
      }, nowReqId('web_custom_fixed_breathing'), {
        action: 'set_custom_light',
        custom_control_type: 'fixed_brightness',
        fallback_command: 'set_static_breathing'
      }));
    }

    return published;
  }


  if (action === 'set_fan_level') {
    const fanLevel = safeInt(message.fanLevel ?? message.fan_level ?? message.fanSpeed ?? message.fan_speed, 0, 10, 0);
    return publishFanCommand('set_fan_level', { fan_level: fanLevel }, nowReqId('web_fan_level'), {
      action: 'set_fan_level',
      fanLevel
    });
  }

  if (action === 'set_fan_mode') {
    const fanMode = String(message.mode || message.fanMode || '').trim().toUpperCase();
    if (!['OFF', 'MANUAL', 'AUTO'].includes(fanMode)) throw new Error('invalid_fan_mode');
    return publishFanCommand('set_mode', { mode: fanMode }, nowReqId('web_fan_mode'), {
      action: 'set_fan_mode',
      mode: fanMode
    });
  }

  if (action === 'apply_current_light') {
    disableSmartControlForUserLightAction('apply_current_light');
    const forceStatic = manualControl || mode === 'custom';
    const commandMode = forceStatic ? 'custom' : mode;
    const { command, params } = sceneCommandForMode(commandMode, brightness, kelvin, forceStatic);
    currentWebMode = commandMode;
    return publishSmartLightCommand(command, params, nowReqId('web_apply'), {
      action: 'apply_current_light',
      brightness,
      kelvin,
      mode: commandMode
    });
  }

  throw new Error('unsupported_dashboard_action');
}

function mapStatusToDashboard(status) {
  lastSmartLightStatus = status;
  lastSmartLightStatusAt = Date.now();
  markModule('led', 'SmartLight status');

  const activeScene = status.active_scene || 'none';
  const modeFromScene = SMART_SCENE_TO_WEB_MODE[activeScene] || null;
  const customSubmode = status.custom_submode || null;

  if (modeFromScene) currentWebMode = modeFromScene;
  if (status.active_mode === 'custom' || customSubmode) currentWebMode = 'custom';

  let brightness = asNumberOrNull(getField(status, ['led_output_percent', 'brightness_pct', 'brightness', 'flow_brightness']));
  if (brightness !== null) brightness = safeInt(brightness, 0, 100, 0);

  let kelvin = asNumberOrNull(getField(status, ['color_temp_k', 'kelvin', 'colorTemperature']));
  if (kelvin !== null && kelvin > 0) kelvin = safeInt(kelvin, 2700, 6500, 3500);
  else kelvin = null;

  const currentLux = asNumberOrNull(getField(status, ['current_lux', 'ambient_lux', 'lux']));
  const targetLux = asNumberOrNull(getFieldIncludingNull(status, ['target_lux']));
  const toleranceLux = asNumberOrNull(getFieldIncludingNull(status, ['tolerance_lux']));
  const targetLuxMin = asNumberOrNull(getFieldIncludingNull(status, ['target_lux_min']));
  const targetLuxMax = asNumberOrNull(getFieldIncludingNull(status, ['target_lux_max']));
  const sensorOk = truthyState(getField(status, ['sensor_ok', 'lux_sensor_ok', 'light_sensor_ok']));
  const controlState = getField(status, ['control_state']);
  const adaptiveProfile = getField(status, ['adaptive_profile']);

  const powerState = truthyState(getField(status, ['led_on', 'ledOn', 'power', 'enabled']));
  const flowEnabled = status.flow_enabled === true;
  const ledOn = powerState !== null ? powerState : (brightness !== null ? brightness > 0 || flowEnabled : null);

  const fanRaw = getField(status, ['fan_speed', 'fanSpeed', 'fan_level', 'fanLevel']);
  const fanSpeed = asNumberOrNull(fanRaw);
  const fanOnRaw = getField(status, ['fan_on', 'fanOn', 'fan_enabled', 'fanEnabled']);
  const fanOn = truthyState(fanOnRaw);
  // 舊版 smartlight/bedroom01 只代表 LED，不再用它判定風扇模組狀態。

  const messageMode = currentWebMode || 'work';
  const connectionSummary = getConnectionSummary();
  const deviceState = {
    ...(ledOn !== null ? { ledOn } : {}),
    ...(brightness !== null ? { brightness } : {}),
    ...(kelvin !== null ? { kelvin, ledColor: kelvinToHex(kelvin) } : {}),
    ...(fanSpeed !== null ? { fanSpeed: safeInt(fanSpeed, 0, 10, 0) } : {}),
    ...(fanOn !== null ? { fanOn } : {})
  };

  return {
    sensorData: {
      onlineDevices: connectionSummary.onlineDevices,
      totalDevices: connectionSummary.totalDevices,
      systemStatus: connectionSummary.systemStatus,
      moduleStatus: connectionSummary.moduleStatus,
      deviceConnectionText: connectionSummary.deviceConnectionText,
      ...(currentLux !== null ? { currentLux } : {}),
      ...(targetLux !== null ? { targetLux } : {}),
      ...(toleranceLux !== null ? { toleranceLux } : {}),
      ...(targetLuxMin !== null ? { targetLuxMin } : {}),
      ...(targetLuxMax !== null ? { targetLuxMax } : {}),
      ...(brightness !== null ? { ledOutputPercent: brightness } : {}),
      ...(status.active_mode !== undefined ? { activeMode: status.active_mode } : {}),
      ...(status.active_scene !== undefined ? { activeScene: status.active_scene } : {}),
      ...(sensorOk !== null ? { sensorOk } : {}),
      ...(controlState !== undefined ? { controlState } : {}),
      ...(adaptiveProfile !== undefined ? { adaptiveProfile } : {})
    },
    deviceState,
    mode: messageMode,
    sourceStatus: mqttSourceStatus(),
    connectionState: currentConnectionState()
  };
}

function statusPatchSummary(patch) {
  const sensorData = patch.sensorData || {};
  const deviceState = patch.deviceState || {};
  return {
    currentLux: asNumberOrNull(sensorData.currentLux),
    ledOutputPercent: asNumberOrNull(sensorData.ledOutputPercent ?? deviceState.brightness),
    activeScene: sensorData.activeScene ?? null,
    activeMode: sensorData.activeMode ?? null,
    controlState: sensorData.controlState ?? null,
    sensorOk: sensorData.sensorOk ?? null,
    mode: patch.mode ?? null,
    ledOn: deviceState.ledOn ?? null,
    kelvin: deviceState.kelvin ?? null,
    fanOn: deviceState.fanOn ?? null,
    fanSpeed: deviceState.fanSpeed ?? null
  };
}

function shouldBroadcastStatusPatch(patch) {
  const now = Date.now();
  const next = statusPatchSummary(patch);
  const prev = lastStatusBroadcastSummary;
  if (!prev) {
    lastStatusBroadcastSummary = next;
    lastStatusBroadcastAt = now;
    return true;
  }

  const criticalKeys = ['activeScene', 'activeMode', 'controlState', 'sensorOk', 'mode', 'ledOn'];
  const criticalChanged = criticalKeys.some(key => next[key] !== prev[key]);
  const luxChanged = next.currentLux !== null && (prev.currentLux === null || Math.abs(next.currentLux - prev.currentLux) >= 20);
  const outputChanged = next.ledOutputPercent !== null && (prev.ledOutputPercent === null || Math.abs(next.ledOutputPercent - prev.ledOutputPercent) >= 5);
  const otherChanged = ['kelvin', 'fanOn', 'fanSpeed'].some(key => next[key] !== prev[key]);
  const changed = criticalChanged || luxChanged || outputChanged || otherChanged;

  if (!changed) return false;
  if (!criticalChanged && now - lastStatusBroadcastAt < STATUS_BROADCAST_MIN_MS) return false;

  lastStatusBroadcastSummary = next;
  lastStatusBroadcastAt = now;
  return true;
}


function handleAvailability(topic, raw, data = null) {
  const parts = integrationParts(topic);
  if (parts) {
    const online = normalizeOnlineFromPayload(raw, data);
    const changed = markAvailabilityModules(parts, online, `${parts.domain}/${parts.nodeId} availability`);
    broadcastPatch({
      sensorData: offlineSensorPatchForAvailability(parts, online),
      sourceStatus: mqttSourceStatus(),
      connectionState: currentConnectionState()
    });
    if (changed) {
      emitDebugEvent('Integration availability', { topic, online, payload: data || raw });
    }
    return;
  }

  const nextOnline = normalizeOnlineFromPayload(raw, data);
  const nextAvailability = nextOnline ? 'online' : 'offline';
  if (nextAvailability !== 'online') lastSmartLightStatusAt = 0;
  const changed = nextAvailability !== availability;
  availability = nextAvailability;
  setModuleOnline('led', nextOnline, `SmartLight ${NODE_ID} availability`);

  broadcastPatch({
    sensorData: {},
    sourceStatus: mqttSourceStatus(),
    connectionState: currentConnectionState()
  });

  if (changed) {
    emitUserEvent({
      text: availability === 'online'
        ? '<strong>SmartLight：</strong>已連線'
        : '<strong>SmartLight：</strong>已離線',
      icon: 'device',
      accent: availability === 'online' ? 'var(--green)' : 'var(--yellow)',
      level: availability === 'online' ? 'success' : 'warning',
      dedupeMs: 800
    });
  }

  if (availability === 'online' && mqttClient?.connected) {
    try {
      publishGetStatus('web_availability_status');
    } catch (error) {
      console.warn('[MQTT] get_status after availability failed:', error.message);
    }
  }
}

function handleAck(data) {
  const ok = data.ok === true;
  const reqId = data.req_id || data.request_id;
  const pending = reqId ? pendingCommands.get(reqId) : null;
  if (reqId) pendingCommands.delete(reqId);
  if (!data.command && pending?.command) data.command = pending.command;

  broadcastPatch({
    sourceStatus: mqttSourceStatus(),
    connectionState: currentConnectionState()
  });

  if (ok) {
    const text = successTextForAck(data, pending);
    if (text) {
      emitUserEvent({ text, icon: 'bulb', accent: 'var(--green)', level: 'success' });
    }
    return;
  }

  emitUserEvent({
    text: `<strong>指令失敗：</strong>${escapeHtml(readableError(data.reason, data.code))}`,
    icon: 'alert',
    accent: 'var(--yellow)',
    level: 'error'
  });
}

function handleSmartLightEvent(data) {
  const messageKey = data.message || data.event_type || '';
  const translated = SMARTLIGHT_EVENT_TEXT[messageKey];
  if (!translated) return;

  emitUserEvent({
    text: `<strong>SmartLight：</strong>${escapeHtml(translated)}`,
    icon: 'device',
    accent: 'var(--cyan)',
    level: 'info',
    dedupeMs: 3500
  });
}



function mapGenericModulePayload(topic, data) {
  if (!data || typeof data !== 'object') return null;
  const topicLower = String(topic || '').toLowerCase();
  const parts = integrationParts(topic);
  const body = payloadBody(data);
  const sensorData = {};
  const deviceState = {};
  const audioState = {};
  let touched = false;

  const heart = asNumberOrNull(getField(body, ['heartRate', 'heart_rate', 'bpm', 'hr', 'heart']));
  if (heart !== null || topicLower.includes('heart') || parts?.domain === 'wearable') {
    if (heart !== null) sensorData.heartRate = heart;
    markModule('heart', topic);
    touched = true;
  }

  const spo2 = asNumberOrNull(getField(body, ['spo2', 'SpO2', 'blood_oxygen', 'bloodOxygen', 'oxygen']));
  if (spo2 !== null || topicLower.includes('spo2') || topicLower.includes('oxygen') || parts?.domain === 'wearable') {
    if (spo2 !== null) sensorData.spo2 = spo2;
    markModule('spo2', topic);
    touched = true;
  }

  const aqiRawValue = getFieldIncludingNull(body, ['aqi', 'AQI', 'air_quality', 'airQuality', 'air_quality_index', 'air_raw', 'pm25', 'pm2_5']);
  const aqiExplicitNull = aqiRawValue === null;
  const aqi = aqiExplicitNull ? null : asNumberOrNull(aqiRawValue);
  if (aqiExplicitNull) {
    sensorData.aqi = null;
    setModuleOnline('aqi', false, topic + ' air_raw null');
    touched = true;
  } else if (aqi !== null || topicLower.includes('aqi') || topicLower.includes('air')) {
    if (aqi !== null) sensorData.aqi = aqi;
    markModule('aqi', topic);
    touched = true;
  }

  const tempRawValue = getFieldIncludingNull(body, ['temperature', 'temp', 'temperature_c', 'temperatureC']);
  const humidityRawValue = getFieldIncludingNull(body, ['humidity', 'humid', 'humidity_percent', 'rh']);
  const tempExplicitNull = tempRawValue === null;
  const humidityExplicitNull = humidityRawValue === null;
  const temp = tempExplicitNull ? null : asNumberOrNull(tempRawValue);
  const humidity = humidityExplicitNull ? null : asNumberOrNull(humidityRawValue);
  if (tempExplicitNull || humidityExplicitNull) {
    if (tempExplicitNull) sensorData.temperature = null;
    else if (temp !== null) sensorData.temperature = temp;

    if (humidityExplicitNull) sensorData.humidity = null;
    else if (humidity !== null) sensorData.humidity = humidity;

    if (temp !== null || humidity !== null) markModule('tempHumidity', topic);
    else setModuleOnline('tempHumidity', false, topic + ' temperature/humidity null');
    touched = true;
  } else if (temp !== null || humidity !== null || topicLower.includes('temp') || topicLower.includes('humid') || (parts?.domain === 'env' && parts?.nodeId === ENV_SENSOR_NODE_ID)) {
    if (temp !== null) sensorData.temperature = temp;
    if (humidity !== null) sensorData.humidity = humidity;
    markModule('tempHumidity', topic);
    touched = true;
  }

  const pirRawValue = getFieldIncludingNull(body, ['pir', 'motion', 'presence', 'occupancy', 'human', 'humanDetected', 'detected', 'value']);
  const pirExplicitNull = pirRawValue === null;
  const pir = pirExplicitNull ? null : truthyState(pirRawValue);
  if (pirExplicitNull) {
    sensorData.pirDetected = null;
    sensorData.pirTime = null;
    setModuleOnline('pir', false, topic + ' occupancy null');
    touched = true;
  } else if (pir !== null || topicLower.includes('pir') || topicLower.includes('motion') || topicLower.includes('presence') || topicLower.includes('occupancy')) {
    if (pir !== null) {
      sensorData.pirDetected = pir;
      if (pir) {
        sensorData.pirTime = new Date().toLocaleTimeString('zh-TW', { hour12: false });
        markModule('pir', topic + ' occupancy true');
      } else {
        // 規則：occupancy=false 代表無人活動，PIR 顯示紅色，不計入連線中。
        setModuleOnline('pir', false, topic + ' occupancy false');
      }
    } else {
      markModule('pir', topic);
    }
    touched = true;
  }

  const comfortLabel = getField(body, ['comfort_label', 'comfortLabel']);
  if (body.comfort && typeof body.comfort === 'object') {
    sensorData.comfort = body.comfort.label || body.comfort.level || null;
    touched = true;
  } else if (comfortLabel !== undefined) {
    sensorData.comfort = String(comfortLabel);
    touched = true;
  }

  const fanSpeed = asNumberOrNull(getField(body, ['fanSpeed', 'fan_speed', 'fanLevel', 'fan_level', 'speed']));
  const fanOn = truthyState(getField(body, ['fanOn', 'fan_on', 'fanEnabled', 'fan_enabled', 'enabled']));
  const duty = asNumberOrNull(getField(body, ['duty', 'pwm_duty', 'duty_percent']));
  const fanMode = getField(body, ['mode', 'fan_mode', 'fanMode']);
  if (fanSpeed !== null || fanOn !== null || duty !== null || parts?.nodeId === FAN_NODE_ID || topicLower.includes('fan')) {
    if (fanSpeed !== null) deviceState.fanSpeed = safeInt(fanSpeed, 0, 10, 0);
    if (fanOn !== null) deviceState.fanOn = fanOn;
    else if (fanSpeed !== null) deviceState.fanOn = fanSpeed > 0;
    if (duty !== null) deviceState.fanDuty = safeInt(duty, 0, 100, 0);
    if (fanMode !== undefined) deviceState.fanMode = String(fanMode).toUpperCase();
    markModule('fan', topic);
    touched = true;
  }

  // rpi_env02 status 會帶入目前採用的環境值；若 Dashboard 沒有直接收到 rpi_env01，仍可用它更新畫面。
  if (parts?.nodeId === FAN_NODE_ID && (temp !== null || humidity !== null || aqi !== null || pir !== null)) {
    if (temp !== null || humidity !== null) markModule('tempHumidity', `${topic} adopted env`);
    if (aqi !== null) markModule('aqi', `${topic} adopted env`);
    if (pir === true) markModule('pir', `${topic} adopted env occupancy true`);
    else if (pir === false) setModuleOnline('pir', false, `${topic} adopted env occupancy false`);
  }

  const audioConnected = truthyState(getField(body, ['audioConnected', 'bluetoothConnected', 'connected', 'online']));
  const audioPlaying = truthyState(getField(body, ['playing', 'isPlaying']));
  const audioVolume = asNumberOrNull(getField(body, ['volume', 'volume_pct', 'volumePct']));
  const audioTrack = getField(body, ['track', 'title', 'song']);
  if (audioConnected !== null || audioPlaying !== null || audioVolume !== null || audioTrack !== undefined || topicLower.includes('audio') || topicLower.includes('bluetooth')) {
    if (audioConnected !== null) audioState.connected = audioConnected;
    if (audioPlaying !== null) audioState.playing = audioPlaying;
    if (audioVolume !== null) audioState.volume = safeInt(audioVolume, 0, 100, 0);
    if (audioTrack !== undefined) audioState.track = String(audioTrack);
    const mode = getField(body, ['mode', 'audioMode']);
    if (mode !== undefined) audioState.mode = String(mode);
    const progress = asNumberOrNull(getField(body, ['progressSec', 'progress_sec', 'positionSec', 'position']));
    if (progress !== null) audioState.progressSec = progress;
    const duration = asNumberOrNull(getField(body, ['durationSec', 'duration_sec', 'duration']));
    if (duration !== null) audioState.durationSec = duration;
    markModule('audio', topic);
    touched = true;
  }

  const cameraOnline = truthyState(getField(body, ['cameraOnline', 'camera_connected', 'cameraConnected', 'online', 'connected']));
  const snapshotUrl = getField(body, ['snapshotUrl', 'snapshot_url', 'imageUrl', 'image_url', 'streamUrl', 'stream_url']);
  if (cameraOnline !== null || snapshotUrl !== undefined || topicLower.includes('camera') || topicLower.includes('vision')) {
    if (cameraOnline === false) setModuleOnline('camera', false, topic);
    else markModule('camera', topic);
    if (snapshotUrl !== undefined) sensorData.cameraSnapshotUrl = String(snapshotUrl);
    touched = true;
  }

  if (!touched) return null;
  return {
    ...(Object.keys(sensorData).length ? { sensorData } : {}),
    ...(Object.keys(deviceState).length ? { deviceState } : {}),
    ...(Object.keys(audioState).length ? { audioState } : {}),
    sourceStatus: mqttSourceStatus(),
    connectionState: currentConnectionState()
  };
}



function normalizeActivityState(value) {
  if (value === undefined || value === null || value === '') return null;
  const numeric = Number(value);
  if (Number.isFinite(numeric)) {
    const rounded = Math.round(numeric);
    return [-1, 1, 2, 3].includes(rounded) ? rounded : null;
  }

  const text = String(value).trim().toLowerCase();
  if (['-1', 'none', 'no_measurement', 'not_measured', 'unmeasured', 'no_data', 'nodata', '未量測', '沒有量測', '無量測'].includes(text)) return -1;
  if (['1', 'still', 'static', 'idle', 'rest', 'resting', '靜止', '靜止狀態'].includes(text)) return 1;
  if (['2', 'light', 'light_activity', 'mild', 'walk', 'walking', '輕微活動'].includes(text)) return 2;
  if (['3', 'heavy', 'intense', 'vigorous', 'running', '劇烈活動'].includes(text)) return 3;
  return null;
}

function activityStateLabel(activityState) {
  if (activityState === -1) return '未量測';
  if (activityState === 1) return '靜止狀態';
  if (activityState === 2) return '輕微活動';
  if (activityState === 3) return '劇烈活動';
  return '未知狀態';
}

function getActivityStateAutoZone(activityState) {
  const state = normalizeActivityState(activityState);
  if (state === null) return null;

  if (state === -1) {
    return {
      state,
      key: 'not_measured',
      label: '未量測狀態測試燈號',
      command: 'set_static',
      params: { brightness_pct: 10, r: 180, g: 180, b: 180 },
      message: '目前沒有量測到活動狀態，LED 切換為灰白色測試燈號，亮度固定 10%'
    };
  }

  if (state === 1) {
    return {
      state,
      key: 'still',
      label: '靜止狀態測試燈號',
      command: 'set_static',
      params: { brightness_pct: 10, r: 0, g: 96, b: 255 },
      message: '偵測為靜止狀態，LED 切換為藍色測試燈號，亮度固定 10%'
    };
  }

  if (state === 2) {
    return {
      state,
      key: 'light',
      label: '輕微活動測試燈號',
      command: 'set_static',
      params: { brightness_pct: 10, r: 0, g: 180, b: 90 },
      message: '偵測為輕微活動，LED 切換為綠色測試燈號，亮度固定 10%'
    };
  }

  return {
    state,
    key: 'vigorous',
    label: '劇烈活動測試燈號',
    command: 'set_static',
    params: { brightness_pct: 10, r: 255, g: 0, b: 0 },
    message: '偵測為劇烈活動，LED 切換為紅色測試燈號，亮度固定 10%'
  };
}

function handleActivityStateLedAuto(rawActivityState, sourceTopic = '') {
  if (!AUTO_ACTIVITY_LED_ENABLED || !smartControlState.enabled) return;

  const zone = getActivityStateAutoZone(rawActivityState);
  if (!zone) {
    emitDebugEvent('Auto activity LED skipped invalid state', { rawActivityState, sourceTopic });
    return;
  }

  const now = Date.now();
  const stateChanged = zone.state !== autoActivityLedState.lastState;
  const cooldownPassed = now - autoActivityLedState.lastCommandAt >= AUTO_ACTIVITY_LED_COOLDOWN_MS;
  if (!stateChanged && !cooldownPassed) return;

  if (!mqttClient?.connected) {
    emitDebugEvent('Auto activity LED skipped because light MQTT is disconnected', {
      activityState: zone.state,
      label: activityStateLabel(zone.state),
      sourceTopic
    });
    return;
  }

  try {
    publishSmartLightCommand(zone.command, zone.params, nowReqId(`auto_activity_${zone.key}`), {
      action: 'auto_activity_led',
      silent: true,
      activityState: zone.state,
      activityStateLabel: activityStateLabel(zone.state),
      zone: zone.key,
      label: zone.label
    });

    autoActivityLedState.lastState = zone.state;
    autoActivityLedState.lastCommandAt = now;
    emitUserEvent({
      text: `<strong>活動狀態自動控制測試：</strong>${escapeHtml(zone.message)}`,
      icon: 'device',
      accent: zone.key === 'vigorous' ? 'var(--red)' : (zone.key === 'light' ? 'var(--green)' : (zone.key === 'not_measured' ? 'var(--muted)' : 'var(--cyan)')),
      level: zone.key === 'vigorous' ? 'warning' : 'info',
      dedupeMs: 3000
    });
  } catch (error) {
    emitDebugEvent('Auto activity LED publish failed', {
      error: error.message,
      activityState: zone.state,
      label: activityStateLabel(zone.state),
      sourceTopic
    });
  }
}


function getHeartRateAutoZone(avgHeartRate) {
  const hr = Number(avgHeartRate);
  if (!Number.isFinite(hr)) return null;

  if (hr < 50) {
    return {
      key: 'low',
      label: '心率偏低測試燈號',
      command: 'set_static',
      params: { brightness_pct: 40, r: 0, g: 96, b: 255 },
      message: `心率平均 ${Math.round(hr)} bpm，LED 切換為藍色測試燈號`
    };
  }

  if (hr < 100) {
    return {
      key: 'normal',
      label: '心率正常測試燈號',
      command: 'set_static',
      params: { brightness_pct: 50, r: 0, g: 180, b: 90 },
      message: `心率平均 ${Math.round(hr)} bpm，LED 切換為綠色測試燈號`
    };
  }

  if (hr < 120) {
    return {
      key: 'high',
      label: '心率偏高測試燈號',
      command: 'set_static',
      params: { brightness_pct: 65, r: 255, g: 180, b: 0 },
      message: `心率平均 ${Math.round(hr)} bpm，LED 切換為黃色測試燈號`
    };
  }

  return {
    key: 'alert',
    label: '心率高值測試燈號',
    command: 'set_static',
    params: { brightness_pct: 80, r: 255, g: 0, b: 0 },
    message: `心率平均 ${Math.round(hr)} bpm，LED 切換為紅色測試燈號`
  };
}

function handleHeartRateLedAuto(rawHeartRate, sourceTopic = '') {
  if (!AUTO_HR_LED_ENABLED || !smartControlState.enabled) return;

  const heartRate = Number(rawHeartRate);
  if (!Number.isFinite(heartRate)) return;

  // 避免異常資料直接控制 LED。即時畫面仍可顯示，但不進入自動控制判斷。
  if (heartRate < AUTO_HR_VALID_MIN || heartRate > AUTO_HR_VALID_MAX) {
    emitDebugEvent('Auto HR LED skipped invalid heart rate', { heartRate, sourceTopic });
    return;
  }

  autoHeartRateState.samples.push(heartRate);
  while (autoHeartRateState.samples.length > Math.max(1, AUTO_HR_LED_SAMPLE_SIZE)) {
    autoHeartRateState.samples.shift();
  }

  if (autoHeartRateState.samples.length < Math.max(1, AUTO_HR_LED_SAMPLE_SIZE)) {
    emitDebugEvent('Auto HR LED waiting samples', {
      samples: autoHeartRateState.samples,
      required: Math.max(1, AUTO_HR_LED_SAMPLE_SIZE)
    });
    return;
  }

  const avg = autoHeartRateState.samples.reduce((sum, item) => sum + item, 0) / autoHeartRateState.samples.length;
  autoHeartRateState.lastAverage = avg;
  const zone = getHeartRateAutoZone(avg);
  if (!zone) return;

  const now = Date.now();
  const zoneChanged = zone.key !== autoHeartRateState.lastZone;
  const cooldownPassed = now - autoHeartRateState.lastCommandAt >= AUTO_HR_LED_COOLDOWN_MS;
  if (!zoneChanged && !cooldownPassed) return;
  if (!mqttClient?.connected) {
    emitDebugEvent('Auto HR LED skipped because light MQTT is disconnected', { heartRate, avg, zone: zone.key });
    return;
  }

  try {
    publishSmartLightCommand(zone.command, zone.params, nowReqId(`auto_hr_${zone.key}`), {
      action: 'auto_hr_led',
      silent: true,
      heartRate,
      heartRateAvg: Math.round(avg),
      zone: zone.key,
      label: zone.label
    });

    autoHeartRateState.lastZone = zone.key;
    autoHeartRateState.lastCommandAt = now;
    emitUserEvent({
      text: `<strong>心律自動控制測試：</strong>${escapeHtml(zone.message)}`,
      icon: 'heart',
      accent: zone.key === 'alert' ? 'var(--red)' : (zone.key === 'high' ? 'var(--yellow)' : 'var(--green)'),
      level: zone.key === 'alert' ? 'warning' : 'info',
      dedupeMs: 3000
    });
  } catch (error) {
    emitDebugEvent('Auto HR LED publish failed', { error: error.message, heartRate, avg, zone: zone.key });
  }
}

function extractWearableActivityState(topic, data) {
  const parts = integrationParts(topic);
  if (!parts || parts.domain !== 'wearable') return null;
  if (!data || typeof data !== 'object') return null;

  const body = payloadBody(data);
  const summary = body.summary && typeof body.summary === 'object' ? body.summary : {};

  // 主要讀取使用者提供的 payload.status.summary.state；不依賴平均心率欄位。
  const stateFromSummary = normalizeActivityState(getField(summary, ['state', 'activity_state', 'activityState']));
  if (stateFromSummary !== null) return stateFromSummary;

  // 備援：若後續 payload 直接把 state 放在 status 第一層，也可以接。
  const stateFromBody = normalizeActivityState(getField(body, ['state', 'activity_state_code', 'activityStateCode']));
  if (stateFromBody !== null) return stateFromBody;

  // 備援：activity_state 文字，例如 rest / light / intense。
  return normalizeActivityState(getField(body, ['activity_state', 'activityState']));
}


function extractWearableHeartRate(topic, data) {
  const parts = integrationParts(topic);
  if (!parts || parts.domain !== 'wearable') return null;
  if (!data || typeof data !== 'object') return null;
  const body = payloadBody(data);
  return asNumberOrNull(getField(body, ['heartRate', 'heart_rate', 'bpm', 'hr', 'heart']));
}

function handleMqttMessage(topic, rawBuffer) {
  const raw = rawBuffer.toString();
  console.log(`[MQTT → Dashboard] ${topic} ${raw}`);

  const parts = integrationParts(topic);
  let data = null;
  try {
    data = JSON.parse(raw);
    normalizeTimestampFields(data);
  } catch (error) {
    data = null;
  }

  if (topic.endsWith('/availability')) {
    emitDebugEvent('MQTT availability', { topic, payload: data || raw.trim() });
    handleAvailability(topic, raw, data);
    return;
  }

  if (!data) {
    console.warn('[MQTT] non-json payload:', raw);
    emitDebugEvent('MQTT non-json payload', { topic, payload: raw });
    return;
  }

  emitDebugEvent('MQTT message', { topic, payload: data });

  if (topic.endsWith('/status')) {
    const isLegacyLight = topic.startsWith(`${BASE_TOPIC}/`);
    const isIntegrationLight = parts?.domain === 'light';
    const lightPatch = (isLegacyLight || isIntegrationLight) ? mapStatusToDashboard(normalizeTimestampFields(payloadBody(data))) : {};
    const genericPatch = mapGenericModulePayload(topic, data) || {};
    const wearableActivityState = extractWearableActivityState(topic, data);
    if (wearableActivityState !== null) handleActivityStateLedAuto(wearableActivityState, topic);
    const wearableHeartRate = extractWearableHeartRate(topic, data);
    if (wearableHeartRate !== null) handleHeartRateLedAuto(wearableHeartRate, topic);
    const patch = {
      ...lightPatch,
      ...genericPatch,
      sensorData: { ...(lightPatch.sensorData || {}), ...(genericPatch.sensorData || {}) },
      deviceState: { ...(lightPatch.deviceState || {}), ...(genericPatch.deviceState || {}) },
      audioState: { ...(lightPatch.audioState || {}), ...(genericPatch.audioState || {}) },
      connectionState: currentConnectionState()
    };
    if (isLegacyLight || isIntegrationLight) {
      cancelStatusFallback();
      if (!shouldBroadcastStatusPatch(patch)) return;
    }
    broadcastPatch(patch);
    return;
  }

  if (topic.endsWith('/ack')) {
    handleAck(data);
    return;
  }

  if (topic.endsWith('/event')) {
    const genericPatch = mapGenericModulePayload(topic, data);
    broadcastPatch({
      ...(genericPatch || {}),
      sourceStatus: mqttSourceStatus(),
      connectionState: currentConnectionState()
    });
    handleSmartLightEvent(data);
    return;
  }

  const genericPatch = mapGenericModulePayload(topic, data);
  if (genericPatch) {
    broadcastPatch(genericPatch);
  }
}

const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

    if (req.method === 'GET' && url.pathname === '/api/dashboard') {
      jsonResponse(res, 200, mergePayload(withConnectionSummary({}))); 
      return;
    }

    if (req.method === 'GET' && url.pathname === '/api/led-profiles') {
      jsonResponse(res, 200, { ok: true, profiles: readLedProfiles() });
      return;
    }

    if (req.method === 'POST' && url.pathname === '/api/led-profiles') {
      const body = await readJsonBody(req);
      const profiles = updateLedProfile(body.section, body.key, body.profile || body.patch || {});
      jsonResponse(res, 200, { ok: true, profiles });
      return;
    }

    if (req.method === 'POST' && url.pathname === '/api/led-profiles/reset') {
      const body = await readJsonBody(req);
      const profiles = resetLedProfile(body.section, body.key);
      jsonResponse(res, 200, { ok: true, profiles });
      return;
    }

    if (req.method === 'POST' && url.pathname === '/api/smartlight/command') {
      const body = await readJsonBody(req);
      const result = handleDashboardCommand(body);
      jsonResponse(res, 200, { ok: true, result });
      return;
    }

    const requestPath = url.pathname === '/' ? '/index.html' : decodeURIComponent(url.pathname);
    const filePath = path.join(ROOT, requestPath);
    if (!filePath.startsWith(ROOT)) {
      sendFile(res, 403, 'text/plain; charset=utf-8', 'Forbidden');
      return;
    }

    fs.readFile(filePath, (error, data) => {
      if (error) {
        sendFile(res, 404, 'text/plain; charset=utf-8', 'Not found');
        return;
      }

      const ext = path.extname(filePath).toLowerCase();
      const typeMap = {
        '.html': 'text/html; charset=utf-8',
        '.css': 'text/css; charset=utf-8',
        '.js': 'application/javascript; charset=utf-8',
        '.json': 'application/json; charset=utf-8',
        '.png': 'image/png',
        '.jpg': 'image/jpeg',
        '.jpeg': 'image/jpeg',
        '.svg': 'image/svg+xml; charset=utf-8'
      };
      sendFile(res, 200, typeMap[ext] || 'application/octet-stream', data);
    });
  } catch (error) {
    const reason = readableError(error.message);
    console.error('[HTTP] request failed:', error.message);
    jsonResponse(res, 400, { ok: false, error: error.message, reason });
  }
});

const wss = new WebSocketServer({ server, path: '/ws/dashboard' });

function hasVisibleDashboardClient() {
  for (const client of wss.clients) {
    if (client.readyState === client.OPEN && client.dashboardVisible !== false) return true;
  }
  return false;
}

function currentStatusPollIntervalMs() {
  return hasVisibleDashboardClient() ? STATUS_POLL_INTERVAL_MS : STATUS_BACKGROUND_POLL_INTERVAL_MS;
}

wss.on('connection', ws => {
  ws.dashboardVisible = true;
  ws.send(JSON.stringify(mergePayload(withConnectionSummary({}))));
  if (lastSmartLightStatus) ws.send(JSON.stringify(mapStatusToDashboard(lastSmartLightStatus)));

  ws.on('message', message => {
    let data = null;
    try {
      data = JSON.parse(message.toString());
      if (data.type === 'dashboard_visibility') {
        const wasVisible = ws.dashboardVisible !== false;
        ws.dashboardVisible = data.visible !== false;
        if (!wasVisible && ws.dashboardVisible) {
          try {
            publishGetStatus('web_visible_status');
          } catch (error) {
            console.warn('[Light MQTT] visible get_status failed:', error.message);
          }
        }
        scheduleNextStatusPoll();
        return;
      }
      if (data.type === 'dashboard_command') {
        const result = handleDashboardCommand(data);
        ws.send(JSON.stringify({
          sourceStatus: '等待 SmartLight 回應',
          connectionState: currentConnectionState(),
          debugEvent: {
            time: Date.now(),
            title: 'Dashboard command accepted',
            detail: { action: data.action, result }
          }
        }));
      }
    } catch (error) {
      console.error('[WS] command failed:', error.message);
      const reason = readableError(error.message);
      ws.send(JSON.stringify({
        sourceStatus: '指令無法送出',
        connectionState: currentConnectionState(),
        ...(data.silent ? {} : { userEvent: {
          text: `<strong>指令無法送出：</strong>${escapeHtml(reason)}`,
          icon: 'alert',
          accent: 'var(--yellow)',
          level: 'error'
        }}),
        debugEvent: {
          time: Date.now(),
          title: 'Dashboard command rejected',
          detail: { error: error.message }
        }
      }));
    }
  });
});


function buildMqttOptions(clientIdPrefix, username = '', password = '') {
  const options = {
    clientId: `${clientIdPrefix}_${Math.random().toString(16).slice(2)}`,
    clean: true,
    reconnectPeriod: 5000,
    keepalive: 30
  };
  if (username) {
    options.username = username;
    options.password = password;
  }
  return options;
}

function connectBridgeClient({ name, host, port, username, password, subscribeTopics, onConnect }) {
  const url = `mqtt://${host}:${port}`;
  const client = mqtt.connect(url, buildMqttOptions(`dashboard_${name.toLowerCase().replace(/[^a-z0-9]+/g, '_')}`, username, password));
  let reconnectAttempts = 0;

  client.on('connect', () => {
    reconnectAttempts = 0;
    client.options.reconnectPeriod = 5000;
    console.log(`[${name}] connected: ${url}`);
    const uniqueTopics = [...new Set((subscribeTopics || []).filter(Boolean))];
    if (uniqueTopics.length > 0) {
      client.subscribe(uniqueTopics, { qos: 0 }, error => {
        if (error) console.error(`[${name}] subscribe failed:`, error.message);
        else console.log(`[${name}] subscribed: ${uniqueTopics.join(', ')}`);
      });
    }

    broadcastPatch({
      sourceStatus: mqttSourceStatus(),
      connectionState: currentConnectionState()
    });

    emitDebugEvent(`${name} connected`, { url, subscribeTopics: uniqueTopics });

    if (typeof onConnect === 'function') {
      try {
        onConnect(client);
      } catch (error) {
        console.warn(`[${name}] onConnect failed:`, error.message);
      }
    }
  });

  client.on('message', handleMqttMessage);

  client.on('reconnect', () => {
    reconnectAttempts += 1;
    client.options.reconnectPeriod = reconnectAttempts <= 1 ? 5000 : (reconnectAttempts === 2 ? 10000 : 30000);
    console.log(`[${name}] reconnecting... next retry in ${client.options.reconnectPeriod}ms`);
    broadcastPatch({
      sourceStatus: `${name} 重連中`,
      connectionState: currentConnectionState()
    });
  });

  client.on('close', () => {
    console.log(`[${name}] connection closed`);
    broadcastPatch({
      sourceStatus: mqttSourceStatus(),
      connectionState: currentConnectionState()
    });
  });

  client.on('error', error => {
    console.error(`[${name}] error:`, error.message);
    emitDebugEvent(`${name} error`, { error: error.message });
    broadcastPatch({
      sourceStatus: `${name} 錯誤`,
      connectionState: currentConnectionState()
    });
  });

  return client;
}

const lightMqttUrl = `mqtt://${MQTT_HOST}:${MQTT_PORT}`;
mqttClient = connectBridgeClient({
  name: 'Light MQTT',
  host: MQTT_HOST,
  port: MQTT_PORT,
  username: MQTT_USERNAME,
  password: MQTT_PASSWORD,
  subscribeTopics: [...LED_SUB_TOPICS, ...LIGHT_INTEGRATION_SUB_TOPICS, ...EXTRA_SUB_TOPICS],
  onConnect: () => {
    emitUserEvent({
      text: '<strong>燈光 MQTT：</strong>已連線，等待 SmartLight 狀態',
      icon: 'device',
      accent: 'var(--green)',
      level: 'success',
      dedupeMs: 1500
    });

    try {
      publishGetStatus('web_start_status', { requireOnline: false });
    } catch (error) {
      console.warn('[Light MQTT] initial get_status failed:', error.message);
    }
  }
});

if (ENV_MQTT_ENABLED) {
  const sameAsLight = ENV_MQTT_HOST === MQTT_HOST && ENV_MQTT_PORT === MQTT_PORT;
  envMqttClient = sameAsLight ? mqttClient : connectBridgeClient({
    name: 'Env MQTT',
    host: ENV_MQTT_HOST,
    port: ENV_MQTT_PORT,
    username: ENV_MQTT_USERNAME,
    password: ENV_MQTT_PASSWORD,
    subscribeTopics: ENV_SUB_TOPICS,
    onConnect: () => {
      emitUserEvent({
        text: '<strong>環境監測 MQTT：</strong>已連線，等待溫溼度 / 空氣品質 / PIR / 風扇資料',
        icon: 'device',
        accent: 'var(--green)',
        level: 'success',
        dedupeMs: 1500
      });
    }
  });
}

if (WEARABLE_MQTT_ENABLED) {
  const sameAsLight = WEARABLE_MQTT_HOST === MQTT_HOST && WEARABLE_MQTT_PORT === MQTT_PORT;
  const sameAsEnv = envMqttClient && WEARABLE_MQTT_HOST === ENV_MQTT_HOST && WEARABLE_MQTT_PORT === ENV_MQTT_PORT;
  wearableMqttClient = sameAsLight ? mqttClient : (sameAsEnv ? envMqttClient : connectBridgeClient({
    name: 'Wearable MQTT',
    host: WEARABLE_MQTT_HOST,
    port: WEARABLE_MQTT_PORT,
    username: WEARABLE_MQTT_USERNAME,
    password: WEARABLE_MQTT_PASSWORD,
    subscribeTopics: WEARABLE_SUB_TOPICS,
    onConnect: () => {
      emitUserEvent({
        text: '<strong>心律血氧 MQTT：</strong>已連線，等待心律 / 血氧實機資料',
        icon: 'device',
        accent: 'var(--green)',
        level: 'success',
        dedupeMs: 1500
      });
    }
  }));
}

function pollSmartLightStatus() {
  const now = Date.now();
  for (const [reqId, item] of pendingCommands.entries()) {
    if (now - (item.sentAt || 0) > 60000) pendingCommands.delete(reqId);
  }
  if (!mqttClient?.connected || availability !== 'online') {
    broadcastPatch({ sourceStatus: mqttSourceStatus(), connectionState: currentConnectionState() });
    return;
  }
  try {
    publishGetStatus('web_poll_status');
  } catch (error) {
    console.warn('[Light MQTT] periodic get_status failed:', error.message);
  }
  broadcastPatch({ sourceStatus: mqttSourceStatus(), connectionState: currentConnectionState() });
}

function scheduleNextStatusPoll() {
  clearTimeout(statusPollTimer);
  statusPollTimer = setTimeout(() => {
    pollSmartLightStatus();
    scheduleNextStatusPoll();
  }, currentStatusPollIntervalMs());
}
scheduleNextStatusPoll();

server.listen(PORT, () => {
  console.log('============================================================');
  console.log('Dashboard Bridge - Multi Broker Hardware Mode');
  console.log(`Dashboard URL       : http://localhost:${PORT}`);
  console.log(`WebSocket           : ws://localhost:${PORT}/ws/dashboard`);
  console.log(`Light MQTT Broker   : ${lightMqttUrl}`);
  console.log(`Light Topics        : ${[...LED_SUB_TOPICS, ...LIGHT_INTEGRATION_SUB_TOPICS, ...EXTRA_SUB_TOPICS].join(', ')}`);
  console.log(`Light Command       : ${CMD_TOPIC}`);
  console.log(`Wearable MQTT Broker: mqtt://${WEARABLE_MQTT_HOST}:${WEARABLE_MQTT_PORT}`);
  console.log(`Wearable Topics     : ${WEARABLE_SUB_TOPICS.join(', ')}`);
  console.log(`Env MQTT Broker     : mqtt://${ENV_MQTT_HOST}:${ENV_MQTT_PORT}`);
  console.log(`Env Topics          : ${ENV_SUB_TOPICS.join(', ')}`);
  console.log(`Auto State → LED    : ${AUTO_ACTIVITY_LED_ENABLED ? 'enabled' : 'disabled'} / cooldown=${AUTO_ACTIVITY_LED_COOLDOWN_MS}ms`);
  console.log(`Auto HR → LED Test  : ${AUTO_HR_LED_ENABLED ? 'enabled' : 'disabled'} / sample=${AUTO_HR_LED_SAMPLE_SIZE} / cooldown=${AUTO_HR_LED_COOLDOWN_MS}ms`);
  console.log('============================================================');
});
