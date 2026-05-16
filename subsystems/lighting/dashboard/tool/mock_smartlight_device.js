/*
 * SmartLight virtual device for Dashboard validation.
 *
 * Purpose:
 * - Simulate Gateway + MCU on MQTT without real hardware.
 * - Validate Dashboard payload format before real-device testing.
 * - Reject legacy / unsupported payloads such as cmd:set_custom in custom brightness-only mode.
 *
 * Usage:
 *   node mock_smartlight_device.js
 *
 * Environment variables:
 *   MQTT_HOST=127.0.0.1
 *   MQTT_PORT=1883
 *   SMARTLIGHT_NODE_ID=bedroom01
 */
const mqtt = require('mqtt');

const host = process.env.MQTT_HOST || process.env.MQTT_BROKER_HOST || '127.0.0.1';
const port = Number(process.env.MQTT_PORT || process.env.MQTT_BROKER_PORT || 1883);
const NODE_ID = process.env.SMARTLIGHT_NODE_ID || 'bedroom01';
const TOPIC_ROOT = process.env.SMARTLIGHT_TOPIC_ROOT || 'smartlight';
const BROKER_URL = `mqtt://${host}:${port}`;

const TOPIC_CMD = `${TOPIC_ROOT}/${NODE_ID}/cmd`;
const TOPIC_ACK = `${TOPIC_ROOT}/${NODE_ID}/ack`;
const TOPIC_STATUS = `${TOPIC_ROOT}/${NODE_ID}/status`;
const TOPIC_EVENT = `${TOPIC_ROOT}/${NODE_ID}/event`;
const TOPIC_AVAILABILITY = `${TOPIC_ROOT}/${NODE_ID}/availability`;

let statusTimer = null;

const state = {
  active_mode: 'scene',
  active_scene: 'vacant',
  custom_submode: null,
  scene_modified: false,
  brightness_pct: 0,
  tone_bias: 0,
  color_temp_k: 4500,
  flow_enabled: false,
  flow_preset: 'none',
  flow_speed: 'slow',
  flow_brightness: 0,
  flow_soft_mode: false,
  breathing_enabled: false,
  breathing_speed: 'slow',
  breathing_strength: 'low',
  manual_override: true,
  current_lux: 320,
  target_lux: -1,
  tolerance_lux: -1,
  target_lux_min: -1,
  target_lux_max: -1,
  led_output_percent: 0,
  control_state: 'off',
  sensor_ok: true,
};

function clampNumber(value, min, max, fallback) {
  const n = Number(value);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(min, Math.min(max, n));
}

function nowIso() {
  return new Date().toISOString();
}

function uptimeMs() {
  return Date.now() % 10000000;
}

function publish(topic, payload, options) {
  client.publish(topic, typeof payload === 'string' ? payload : JSON.stringify(payload), options || {});
}

function publishAck(reqId, command, ok = true, code = 'OK', reason = '') {
  const payload = {
    req_id: reqId || null,
    ts: nowIso(),
    type: 'ack',
    node_id: NODE_ID,
    command,
    ok,
    code,
    reason,
    uptime_ms: uptimeMs(),
  };
  publish(TOPIC_ACK, payload);
  console.log(`[MOCK ACK] ${command} ok=${ok}${reason ? ` reason=${reason}` : ''}`);
}

function publishEvent(message, eventType = 'mode_changed') {
  publish(TOPIC_EVENT, {
    ts: nowIso(),
    type: 'event',
    node_id: NODE_ID,
    event_type: eventType,
    code: 'OK',
    message,
    uptime_ms: uptimeMs(),
  });
}

function publishStatus(reqId = null) {
  // Simulate slight lux movement so Dashboard status is visibly alive.
  const drift = Math.round((Math.random() * 8 - 4) * 10) / 10;
  state.current_lux = clampNumber(state.current_lux + drift, 0, 1500, 320);

  publish(TOPIC_STATUS, {
    req_id: reqId,
    ts: nowIso(),
    type: 'status',
    node_id: NODE_ID,
    uptime_ms: uptimeMs(),
    active_mode: state.active_mode,
    active_scene: state.active_scene,
    custom_submode: state.custom_submode,
    scene_modified: state.scene_modified,
    brightness_pct: state.brightness_pct,
    tone_bias: state.tone_bias,
    color_temp_k: state.color_temp_k,
    flow_enabled: state.flow_enabled,
    flow_preset: state.flow_preset,
    flow_speed: state.flow_speed,
    flow_brightness: state.flow_brightness,
    flow_soft_mode: state.flow_soft_mode,
    breathing_enabled: state.breathing_enabled,
    breathing_speed: state.breathing_speed,
    breathing_strength: state.breathing_strength,
    manual_override: state.manual_override,
    current_lux: state.current_lux,
    target_lux: state.target_lux,
    tolerance_lux: state.tolerance_lux,
    target_lux_min: state.target_lux_min,
    target_lux_max: state.target_lux_max,
    led_output_percent: state.led_output_percent,
    control_state: state.control_state,
    sensor_ok: state.sensor_ok,
  });
}

function rejectInvalidPayload(reqId, cmd, reason) {
  publishAck(reqId, cmd || 'unknown', false, 'invalid_command_payload', reason);
}

function hasAny(obj, keys) {
  return keys.some((key) => Object.prototype.hasOwnProperty.call(obj, key));
}

function handleGetStatus(reqId, cmd) {
  publishAck(reqId, cmd);
  publishStatus(reqId);
}

function handleScene(reqId, cmd, params) {
  const scene = params.scene;
  const allowed = ['vacant', 'sleep', 'relax', 'work', 'exercise'];
  if (!allowed.includes(scene)) {
    rejectInvalidPayload(reqId, cmd, 'invalid_scene');
    return;
  }

  const sceneDefaults = {
    vacant: { brightness: 0, target: -1, tolerance: -1, kelvin: 0, state: 'off' },
    sleep: { brightness: 5, target: 50, tolerance: 20, kelvin: 2700, state: 'in_range' },
    relax: { brightness: 35, target: 300, tolerance: 75, kelvin: 3000, state: 'in_range' },
    work: { brightness: 60, target: 500, tolerance: 75, kelvin: 4500, state: 'below_range' },
    exercise: { brightness: 80, target: 400, tolerance: 100, kelvin: 5000, state: 'below_range' },
  };
  const p = sceneDefaults[scene];

  state.active_mode = 'scene';
  state.active_scene = scene;
  state.custom_submode = null;
  state.scene_modified = false;
  state.brightness_pct = p.brightness;
  state.led_output_percent = p.brightness;
  state.color_temp_k = p.kelvin;
  state.flow_enabled = false;
  state.flow_preset = 'none';
  state.flow_brightness = 0;
  state.breathing_enabled = false;
  state.target_lux = p.target;
  state.tolerance_lux = p.tolerance;
  state.target_lux_min = p.target > 0 ? p.target - p.tolerance : -1;
  state.target_lux_max = p.target > 0 ? p.target + p.tolerance : -1;
  state.control_state = p.state;

  publishAck(reqId, cmd);
  publishEvent(`scene_${scene}_applied`);
  publishStatus(reqId);
}

function handlePowerOff(reqId, cmd) {
  state.active_mode = 'off';
  state.active_scene = 'none';
  state.custom_submode = null;
  state.brightness_pct = 0;
  state.led_output_percent = 0;
  state.flow_enabled = false;
  state.flow_preset = 'none';
  state.flow_brightness = 0;
  state.breathing_enabled = false;
  state.target_lux = -1;
  state.tolerance_lux = -1;
  state.target_lux_min = -1;
  state.target_lux_max = -1;
  state.control_state = 'off';

  publishAck(reqId, cmd);
  publishEvent('power_off_applied');
  publishStatus(reqId);
}

function validateStaticParams(reqId, cmd, params) {
  const hasRgb = hasAny(params, ['r', 'g', 'b']);
  const hasKelvin = Object.prototype.hasOwnProperty.call(params, 'color_temp_k');

  if (hasRgb && hasKelvin) {
    rejectInvalidPayload(reqId, cmd, 'rgb_and_kelvin_conflict');
    return false;
  }
  if (hasRgb) {
    for (const key of ['r', 'g', 'b']) {
      const value = Number(params[key]);
      if (!Number.isInteger(value) || value < 0 || value > 255) {
        rejectInvalidPayload(reqId, cmd, `invalid_${key}`);
        return false;
      }
    }
  }
  if (hasKelvin) {
    const kelvin = Number(params.color_temp_k);
    if (!Number.isFinite(kelvin) || kelvin < 2700 || kelvin > 6500) {
      rejectInvalidPayload(reqId, cmd, 'invalid_color_temp_k');
      return false;
    }
  }
  return true;
}

function handleSetStatic(reqId, cmd, params) {
  if (!validateStaticParams(reqId, cmd, params)) return;

  const brightness = clampNumber(params.brightness_pct, 0, 100, 50);
  state.active_mode = 'custom';
  state.active_scene = 'none';
  state.custom_submode = 'fixed';
  state.brightness_pct = brightness;
  state.led_output_percent = brightness;
  state.flow_enabled = false;
  state.flow_preset = 'none';
  state.flow_brightness = 0;
  state.breathing_enabled = false;
  state.target_lux = -1;
  state.tolerance_lux = -1;
  state.target_lux_min = -1;
  state.target_lux_max = -1;
  state.control_state = brightness > 0 ? 'manual' : 'off';
  state.color_temp_k = Object.prototype.hasOwnProperty.call(params, 'color_temp_k')
    ? Number(params.color_temp_k)
    : 0;

  publishAck(reqId, cmd);
  publishEvent('custom_applied');
  publishStatus(reqId);
}

function handleSetFlow(reqId, cmd, params) {
  const brightness = clampNumber(params.flow_brightness ?? params.brightness_pct, 0, 100, 60);
  state.active_mode = 'custom';
  state.active_scene = 'none';
  state.custom_submode = 'flow';
  state.flow_enabled = true;
  state.breathing_enabled = false;
  state.flow_preset = params.flow_preset || 'soft_rainbow';
  state.flow_speed = params.flow_speed || 'medium';
  state.flow_brightness = brightness;
  state.brightness_pct = brightness;
  state.led_output_percent = brightness;
  state.target_lux = -1;
  state.tolerance_lux = -1;
  state.target_lux_min = -1;
  state.target_lux_max = -1;
  state.control_state = brightness > 0 ? 'manual' : 'off';

  publishAck(reqId, cmd);
  publishEvent('custom_applied');
  publishStatus(reqId);
}

function handleBreathing(reqId, cmd, params) {
  state.active_mode = 'custom';
  state.active_scene = 'none';
  state.custom_submode = 'breathing';
  state.flow_enabled = false;
  state.flow_preset = 'none';
  state.breathing_enabled = true;
  state.breathing_speed = params.breathing_speed || 'slow';
  state.breathing_strength = params.breathing_strength || 'low';
  state.control_state = 'manual';

  publishAck(reqId, cmd);
  publishEvent('custom_applied');
  publishStatus(reqId);
}

function handleCommand(message) {
  let data;
  try {
    data = JSON.parse(message.toString());
  } catch (err) {
    console.log('[MOCK REJECT] invalid JSON');
    return;
  }

  const reqId = data.req_id || null;
  const cmd = data.cmd;
  const params = data.params || {};
  console.log('[MOCK CMD]', JSON.stringify(data));

  switch (cmd) {
    case 'get_status':
      handleGetStatus(reqId, cmd);
      return;
    case 'set_scene':
      handleScene(reqId, cmd, params);
      return;
    case 'power_off':
      handlePowerOff(reqId, cmd);
      return;
    case 'set_static':
      handleSetStatic(reqId, cmd, params);
      return;
    case 'set_flow':
      handleSetFlow(reqId, cmd, params);
      return;
    case 'set_static_breathing':
      handleBreathing(reqId, cmd, params);
      return;
    case 'set_custom':
      rejectInvalidPayload(reqId, cmd, 'set_custom_should_not_be_used_in_custom_brightness_only');
      return;
    default:
      rejectInvalidPayload(reqId, cmd || 'unknown', 'unsupported_command');
  }
}

const client = mqtt.connect(BROKER_URL, {
  reconnectPeriod: 2000,
  keepalive: 30,
});

client.on('connect', () => {
  console.log(`[MOCK] connected: ${BROKER_URL}`);
  console.log(`[MOCK] subscribing: ${TOPIC_CMD}`);
  client.subscribe(TOPIC_CMD, (err) => {
    if (err) console.error('[MOCK] subscribe failed:', err.message);
  });
  publish(TOPIC_AVAILABILITY, 'online', { retain: true });
  publishStatus(null);
  if (!statusTimer) {
    statusTimer = setInterval(() => publishStatus(null), 10000);
  }
});

client.on('message', (topic, message) => {
  if (topic === TOPIC_CMD) handleCommand(message);
});

client.on('error', (err) => {
  console.error('[MOCK] MQTT error:', err.message);
});

process.on('SIGINT', () => {
  console.log('\n[MOCK] shutting down...');
  if (statusTimer) clearInterval(statusTimer);
  publish(TOPIC_AVAILABILITY, 'offline', { retain: true });
  client.end(true, () => process.exit(0));
});
