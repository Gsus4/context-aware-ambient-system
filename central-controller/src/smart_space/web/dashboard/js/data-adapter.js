/*
  Dashboard 資料對接層｜LED 控制面板開發版 / 實機版共用
  ------------------------------------------------------------
  開發版 dev：
  - 不需要 Broker / NODE / LED 實體。
  - LED 控制操作只更新 WEB 模擬狀態。
  - 事件紀錄顯示「模擬」結果，不顯示 MQTT 原始資訊。

  實機版 mqtt：
  - 前端透過 WebSocket / REST 將操作交給 FastAPI Backend。
  - Backend 再轉 MQTT 給 SmartLight NODE。
  - status 只更新畫面，ack / event 經整理後才進使用者事件紀錄。
*/
(function () {
  'use strict';

  const config = window.DASHBOARD_CONFIG || {};
  const source = config.dataSource || { mode: 'none' };
  const smartLightConfig = config.smartLight || {};
  const runtimeMode = smartLightConfig.runtimeMode || config.runtimeMode || 'dev';
  const isDevMode = runtimeMode === 'dev' || runtimeMode === 'mock';

  let lastConnectionState = isDevMode
    ? { websocket: false, mqtt: false, smartLight: 'mock', onlineDevices: 0, totalDevices: 5, moduleStatus: [] }
    : { websocket: false, mqtt: false, smartLight: 'unknown', onlineDevices: 0, totalDevices: 5, moduleStatus: [] };
  let debugLogEntries = [];

  const MODE_LABEL = {
    vacant: '無人',
    sleep: '睡眠',
    relax: '放鬆',
    work: '工作',
    exercise: '運動',
    custom: '自定義模式'
  };

  const CONTROL_MODE_LABEL = {
    auto: '情境模式',
    scene: '情境模式',
    manual: '自定義模式'
  };

  function safeParseJSON(raw) {
    try {
      return typeof raw === 'string' ? JSON.parse(raw) : raw;
    } catch (error) {
      console.error('[Dashboard Adapter] JSON 解析失敗：', error, raw);
      return null;
    }
  }

  function setText(selector, text) {
    const el = document.querySelector(selector);
    if (el) el.textContent = text;
  }

  function applySourceStatus(statusText) {
    if (!statusText) return;
    setText('#controlStatusSource', statusText);
  }

  function clampInt(value, min, max, fallback = min) {
    const n = Number(value);
    if (!Number.isFinite(n)) return fallback;
    return Math.round(Math.max(min, Math.min(max, n)));
  }

  function normalizeKelvin(value) {
    const range = smartLightConfig.kelvin || {};
    const min = Number(range.min || 2700);
    const max = Number(range.max || 6500);
    const step = Number(range.step || 100);
    const n = clampInt(value, min, max, 3500);
    return clampInt(Math.round(n / step) * step, min, max, 3500);
  }

  function normalizeBrightness(value) {
    const range = smartLightConfig.brightness || {};
    return clampInt(value, Number(range.min || 0), Number(range.max || 100), 72);
  }

  function debounce(fn, delay = 400) {
    let timer = null;
    return (...args) => {
      clearTimeout(timer);
      timer = setTimeout(() => fn(...args), delay);
    };
  }

  function parseTimestamp(value, fallback = Date.now()) {
    if (typeof value === 'number' && Number.isFinite(value)) return value;
    if (typeof value === 'string' && value.trim()) {
      const parsed = Date.parse(value);
      if (Number.isFinite(parsed)) return parsed;
    }
    return fallback;
  }

  function formatDebugTime(value) {
    return new Date(parseTimestamp(value)).toLocaleTimeString('zh-TW', { hour12: false });
  }

  function addUserEvent(event) {
    if (!event || !window.SmartDashboard?.addEvent) return;
    window.SmartDashboard.addEvent(
      event.text || '<strong>系統：</strong>收到新事件',
      event.icon || 'device',
      event.accent || 'var(--green)'
    );
  }

  function appendDebugEvent(event) {
    if (!event) return;
    const entry = {
      time: parseTimestamp(event.time ?? event.ts),
      title: event.title || 'Debug',
      detail: event.detail ?? event
    };
    debugLogEntries.unshift(entry);
    debugLogEntries = debugLogEntries.slice(0, 40);

    const el = document.querySelector('#debugLog');
    if (el) {
      el.textContent = debugLogEntries
        .map(item => {
          const detail = typeof item.detail === 'string'
            ? item.detail
            : JSON.stringify(item.detail, null, 2);
          return `[${formatDebugTime(item.time)}] ${item.title}\n${detail}`;
        })
        .join('\n\n');
    }

    if (config.debugLogToConsole !== false) {
      console.debug('[Dashboard Debug]', entry.title, entry.detail);
    }
  }

  function moduleAlias(key) {
    if (['heart', 'spo2', 'wearable'].includes(key)) return 'wearable';
    if (['aqi', 'tempHumidity', 'pir', 'fan', 'env', 'envComfort'].includes(key)) return 'envComfort';
    return key;
  }

  function moduleOnline(key) {
    const list = lastConnectionState.moduleStatus || lastConnectionState.deviceConnections || [];
    if (!Array.isArray(list)) return false;
    const alias = moduleAlias(key);
    return list.some(module => {
      const moduleKey = moduleAlias(module?.key || module?.module || '');
      return moduleKey === alias && (module?.online === true || module?.status === 'online');
    });
  }

  function smartModeEnabled() {
    return window.SmartDashboard?.getState?.().uiState?.smartModeEnabled !== false;
  }

  function canControlSmartLight() {
    if (isDevMode) return true;
    return lastConnectionState.mqtt === true && (lastConnectionState.smartLight === 'online' || moduleOnline('led'));
  }

  function canSendCommand(action) {
    if (isDevMode || action === 'get_status' || action === 'set_smart_mode') return true;
    if (lastConnectionState.mqtt !== true) return false;
    if (action === 'set_fan_level' || action === 'set_fan_mode') return moduleOnline('fan');
    return canControlSmartLight();
  }


  function setControlDisabled(disabled) {
    document
      .querySelectorAll('.mode-card, .small-mode, .switch[data-toggle="led"], #brightnessRange, #kelvinRange, .color-dot, #applyBtn, #resetBtn, #applyRecommendBtn, #openAdvancedLightBtn')
      .forEach(el => {
        el.disabled = disabled;
        el.classList.toggle('disabled', disabled);
        el.setAttribute('aria-disabled', String(disabled));
      });
  }

  function deriveConnectionSensorData(connectionState) {
    if (!connectionState) return null;
    const moduleStatus = connectionState.moduleStatus || connectionState.deviceConnections || null;
    const total = Number(connectionState.totalDevices || (moduleStatus ? 5 : 5));
    if (typeof connectionState.onlineDevices === 'number') {
      return {
        onlineDevices: Math.max(0, Math.min(total, Math.round(connectionState.onlineDevices))),
        totalDevices: total,
        systemStatus: connectionState.systemStatus || (connectionState.onlineDevices >= total ? '正常運作' : connectionState.onlineDevices > 0 ? '部分連線' : '等待實機資料'),
        deviceConnectionText: connectionState.deviceConnectionText || '',
        moduleStatus
      };
    }
    const online = Array.isArray(moduleStatus)
      ? moduleStatus.filter(item => item?.online === true || item?.status === 'online').length
      : [connectionState.websocket === true, connectionState.mqtt === true, connectionState.smartLight === 'online'].filter(Boolean).length;
    return {
      onlineDevices: online,
      totalDevices: total,
      systemStatus: online >= total ? '正常運作' : online > 0 ? '部分連線' : '等待實機資料',
      deviceConnectionText: connectionState.deviceConnectionText || '',
      moduleStatus
    };
  }

  function applyConnectionState(connectionState) {
    if (!connectionState) return;
    lastConnectionState = {
      ...lastConnectionState,
      ...connectionState
    };

    const connectionSensorData = deriveConnectionSensorData(lastConnectionState);
    if (connectionSensorData) {
      window.SmartDashboard?.updateSensorData?.(connectionSensorData);
    }

    if (isDevMode) {
      document.body.classList.remove('smartlight-offline');
      document.body.classList.add('dashboard-dev-mode');
      setControlDisabled(false);
      applySourceStatus('已連線');
      return;
    }

    const canControl = canControlSmartLight();
    document.body.classList.toggle('smartlight-offline', !canControl);
    // 各模組由 dashboard-app.js 依 moduleStatus 個別灰階 / 鎖定；不要因 LED 離線鎖住風扇等其他模組。
    setControlDisabled(false);
  }

  function applyDashboardPayload(payload) {
    if (!payload || !window.SmartDashboard) return;

    // 原始 MQTT / Backend 除錯資訊只進 Debug Log。
    if (payload.debugEvent) {
      appendDebugEvent(payload.debugEvent);
    }

    // 使用者事件紀錄只接收整理後的 userEvent。
    if (payload.userEvent) {
      addUserEvent(payload.userEvent);
    }

    // 舊版 event 欄位不再直接顯示，避免原始 MQTT 阻礙使用者操作。
    if (payload.event) {
      if (payload.event.userVisible === true) addUserEvent(payload.event);
      else appendDebugEvent({ title: 'Legacy event suppressed', detail: payload.event });
    }

    if (payload.connectionState) {
      applyConnectionState(payload.connectionState);
    }

    // status 類資料只更新畫面，不新增事件紀錄。
    if (payload.sensorData) {
      window.SmartDashboard.updateSensorData(payload.sensorData);
    }

    if (payload.deviceState) {
      window.SmartDashboard.updateDeviceState(payload.deviceState);
    }

    if (payload.uiState) {
      window.SmartDashboard.updateUiState?.(payload.uiState);
    }

    if (payload.audioState) {
      window.SmartDashboard.updateAudioState(payload.audioState);
    }

    if (payload.mode) {
      const currentMode = window.SmartDashboard.getState?.().uiState?.emotionMode;
      if (currentMode !== payload.mode) {
        // Backend / NODE 若只回傳模式但亮度、色溫仍是上一個情境，
        // 前端要先套用該情境預設，避免夜晚模式顯示專注模式的參數。
        window.SmartDashboard.updateMode(payload.mode, true, { silent: true });
      }

      const currentUi = window.SmartDashboard.getState?.().uiState || {};
      if (currentUi.controlMode === 'scene' && ['vacant', 'sleep', 'relax', 'work', 'exercise'].includes(payload.mode)) {
        window.DashboardSceneMemory?.applySceneProfile?.(payload.mode, true);
      }
    }

    if (payload.sourceStatus) {
      applySourceStatus(payload.sourceStatus);
    }

    // 支援單類型封包：{ type: 'sensor'|'device'|'audio'|'mode', data: {...} }
    if (payload.type && payload.data) {
      if (payload.type === 'sensor') window.SmartDashboard.updateSensorData(payload.data);
      if (payload.type === 'device') window.SmartDashboard.updateDeviceState(payload.data);
      if (payload.type === 'audio') window.SmartDashboard.updateAudioState(payload.data);
      if (payload.type === 'mode') window.SmartDashboard.updateMode(payload.data.mode || payload.data, false, { silent: true });
    }
  }

  async function startRestPolling(restConfig) {
    const url = restConfig.url || '/api/dashboard';
    const intervalMs = Number(restConfig.intervalMs || 1000);

    addUserEvent({ text: '<strong>Dashboard：</strong>REST 資料同步啟動', icon: 'device', accent: 'var(--cyan)' });

    async function poll() {
      try {
        const response = await fetch(url, { cache: 'no-store' });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const payload = await response.json();
        applyDashboardPayload(payload);
      } catch (error) {
        console.error('[Dashboard Adapter] REST 更新失敗：', error);
        applySourceStatus('REST 連線失敗');
        appendDebugEvent({ title: 'REST poll failed', detail: error.message });
      }
    }

    await poll();
    window.__SMART_DASHBOARD_REST_TIMER__ = setInterval(poll, intervalMs);
  }

  function startWebSocket(wsConfig) {
    const url = wsConfig.url;
    if (!url) {
      console.warn('[Dashboard Adapter] WebSocket URL 未設定');
      return;
    }

    const ws = new WebSocket(url);
    window.__SMART_DASHBOARD_WS__ = ws;
    let isIdle = false;
    let idleTimer = null;
    const sendVisibility = () => {
      if (ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({
        type: 'dashboard_visibility',
        visible: document.visibilityState !== 'hidden' && !isIdle,
        active: !isIdle,
        ts: Date.now()
      }));
    };
    const markActive = () => {
      const wasIdle = isIdle;
      isIdle = false;
      clearTimeout(idleTimer);
      idleTimer = setTimeout(() => {
        isIdle = true;
        sendVisibility();
      }, 60000);
      if (wasIdle) {
        sendVisibility();
        sendDashboardCommand('get_status', {}, { silent: true, allowWhenOffline: true });
      }
    };

    ws.addEventListener('open', () => {
      lastConnectionState.websocket = true;
      applySourceStatus('Backend 已連線，等待 SmartLight 狀態');
      addUserEvent({ text: '<strong>Dashboard：</strong>已連上 Backend', icon: 'device', accent: 'var(--green)' });
      markActive();
      sendVisibility();
      sendDashboardCommand('get_status', {}, { silent: true, allowWhenOffline: true });
    });

    ws.addEventListener('message', (event) => {
      const payload = safeParseJSON(event.data);
      applyDashboardPayload(payload);
    });

    ws.addEventListener('close', () => {
      lastConnectionState.websocket = false;
      applyConnectionState({ websocket: false, mqtt: false, smartLight: 'unknown' });
      applySourceStatus('Backend 已斷線');
      addUserEvent({ text: '<strong>Dashboard：</strong>Backend 已斷線', icon: 'alert', accent: 'var(--yellow)' });
    });

    ws.addEventListener('error', (error) => {
      console.error('[Dashboard Adapter] WebSocket 錯誤：', error);
      applySourceStatus('Backend 連線錯誤');
      appendDebugEvent({ title: 'WebSocket error', detail: String(error?.message || error) });
    });

    document.addEventListener('visibilitychange', () => {
      sendVisibility();
      if (document.visibilityState !== 'hidden') {
        markActive();
        sendDashboardCommand('get_status', {}, { silent: true, allowWhenOffline: true });
      }
    });
    ['pointerdown', 'keydown', 'touchstart'].forEach(type => {
      document.addEventListener(type, markActive, { passive: true });
    });
  }

  function getCurrentLightState() {
    const snapshot = window.SmartDashboard?.getState?.() || {};
    const device = snapshot.deviceState || {};
    const ui = snapshot.uiState || {};
    let light = {
      mode: ui.emotionMode || 'work',
      controlMode: ui.controlMode || 'scene',
      ledOn: device.ledOn !== false,
      brightness: normalizeBrightness(device.brightness ?? 40),
      kelvin: normalizeKelvin(device.kelvin ?? 3000),
      ledColor: device.ledColor || '#ff8a5c'
    };
    if (light.controlMode === 'scene' && window.DashboardSceneMemory?.clampSceneState) {
      light = { ...light, ...window.DashboardSceneMemory.clampSceneState(light, light.mode) };
    }
    return light;
  }

  function markLightSliderEdit(field, value, durationMs) {
    window.DashboardSceneMemory?.markLightSliderEdit?.(field, value, durationMs);
  }

  async function sendByRest(message) {
    const url = smartLightConfig.commandUrl || '/api/smartlight/command';
    const response = await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(message)
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.reason || data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  async function sendMusicByRest(message) {
    const response = await fetch('/api/music/command', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(message)
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.reason || data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  function sendByWebSocket(message) {
    const ws = window.__SMART_DASHBOARD_WS__;
    if (!ws || ws.readyState !== WebSocket.OPEN) return false;
    ws.send(JSON.stringify(message));
    return true;
  }

  async function sendMusicCommand(action, data = {}, options = {}) {
    if (isDevMode) {
      if (action === 'set_volume') {
        window.SmartDashboard?.setAudioVolume?.(data.volume);
      } else {
        window.SmartDashboard?.audioAction?.(action);
      }
      return { ok: true, mode: 'dev' };
    }

    const message = {
      type: 'music_command',
      action,
      ...data,
      ts: Date.now(),
      silent: options.silent === true
    };

    try {
      const sentByWs = sendByWebSocket(message);
      if (!sentByWs) {
        const result = await sendMusicByRest(message);
        applyDashboardPayload({ audioState: result.result || result });
      }
      if (!options.silent) applySourceStatus('等待音樂服務回應');
    } catch (error) {
      console.error('[Dashboard Adapter] 音樂指令送出失敗：', error);
      applySourceStatus('音樂指令送出失敗');
      appendDebugEvent({ title: 'Music command failed', detail: { action, error: error.message } });
      addUserEvent({
        text: `<strong>音樂指令失敗：</strong>${error.message || '請確認 Backend 是否啟動'}`,
        icon: 'alert',
        accent: 'var(--yellow)'
      });
    }
  }

  function getModeLabel(mode) {
    return MODE_LABEL[mode] || mode || '目前模式';
  }

  function syncMockStatus(sourceStatus = '已連線') {
    const light = getCurrentLightState();
    applyDashboardPayload({
      sourceStatus,
      connectionState: { websocket: false, mqtt: false, smartLight: 'mock' },
      deviceState: {
        ledOn: light.ledOn,
        brightness: light.brightness,
        kelvin: light.kelvin,
        ledColor: light.ledColor
      },
      mode: light.mode
    });
  }

  function handleMockCommand(action, data = {}, options = {}) {
    const light = getCurrentLightState();
    const silent = options.silent === true || data.silent === true;

    appendDebugEvent({
      title: 'DEV mock command',
      detail: { action, data, currentLightState: light }
    });

    switch (action) {
      case 'get_status': {
        syncMockStatus('已連線');
        if (!silent) {
          addUserEvent({ text: '<strong>狀態同步：</strong>已更新目前狀態', icon: 'device', accent: 'var(--cyan)' });
        }
        return { ok: true, mode: 'dev' };
      }

      case 'set_auto': {
        syncMockStatus('已連線');
        window.SmartDashboard.updateUiState?.({ controlMode: 'auto' });
        if (!silent) {
          addUserEvent({ text: '<strong>自動模式：</strong>已啟用', icon: 'bot', accent: 'var(--green)' });
        }
        return { ok: true, mode: 'dev' };
      }

      case 'set_smart_mode': {
        const enabled = data.enabled !== false;
        syncMockStatus('已連線');
        window.SmartDashboard.updateUiState?.({ smartModeEnabled: enabled });
        if (!silent) {
          addUserEvent({ text: enabled ? '<strong>智慧模式：</strong>已開啟' : '<strong>智慧模式：</strong>已關閉，保留自定義設定', icon: 'bot', accent: enabled ? 'var(--green)' : 'var(--yellow)' });
        }
        return { ok: true, mode: 'dev' };
      }

      case 'set_mode': {
        const mode = MODE_LABEL[data.mode] ? data.mode : light.mode;
        window.SmartDashboard.updateMode?.(mode, false, { silent: true });
        syncMockStatus('已連線');
        if (!silent) {
          addUserEvent({ text: `<strong>${getModeLabel(mode)}：</strong>已套用`, icon: 'relax', accent: 'var(--purple)' });
        }
        return { ok: true, mode: 'dev' };
      }

      case 'set_led_power': {
        const on = data.on !== false;
        window.SmartDashboard.updateDeviceState?.({ ledOn: on });
        syncMockStatus('已連線');
        if (!silent) {
          addUserEvent({ text: `<strong>LED：</strong>${on ? '已開啟' : '已關閉'}`, icon: 'bulb', accent: on ? 'var(--green)' : 'var(--yellow)' });
        }
        return { ok: true, mode: 'dev' };
      }

      case 'set_brightness': {
        const brightness = normalizeBrightness(data.brightness ?? light.brightness);
        window.SmartDashboard.updateDeviceState?.({ brightness, ledOn: brightness > 0 });
        syncMockStatus('已連線');
        if (!silent) {
          addUserEvent({ text: `<strong>亮度：</strong>已調整為 ${brightness}%`, icon: 'bulb', accent: 'var(--purple)' });
        }
        return { ok: true, mode: 'dev' };
      }

      case 'set_kelvin': {
        const kelvin = normalizeKelvin(data.kelvin ?? light.kelvin);
        window.SmartDashboard.updateDeviceState?.({ kelvin, ledOn: true });
        syncMockStatus('已連線');
        if (!silent) {
          addUserEvent({ text: `<strong>色溫：</strong>已調整為 ${kelvin}K`, icon: 'temperature', accent: 'var(--orange)' });
        }
        return { ok: true, mode: 'dev' };
      }

      case 'set_color': {
        const color = typeof data.color === 'string' && /^#[0-9a-fA-F]{6}$/.test(data.color)
          ? data.color
          : light.ledColor;
        window.SmartDashboard.updateMode?.('custom', false, { silent: true });
        window.SmartDashboard.updateDeviceState?.({ ledColor: color, ledOn: true });
        syncMockStatus('已連線');
        if (!silent) {
          addUserEvent({ text: '<strong>顏色：</strong>已套用自訂顏色', icon: 'bulb', accent: 'var(--purple)' });
        }
        return { ok: true, mode: 'dev' };
      }

      case 'apply_current_light': {
        syncMockStatus('已連線');
        if (!silent) {
          addUserEvent({ text: '<strong>套用設定：</strong>LED 設定已套用', icon: 'device', accent: 'var(--green)' });
        }
        return { ok: true, mode: 'dev' };
      }

      default: {
        addUserEvent({ text: '<strong>操作失敗：</strong>不支援此操作', icon: 'alert', accent: 'var(--yellow)' });
        return { ok: false, reason: 'unsupported_dev_action' };
      }
    }
  }

  async function sendDashboardCommand(action, data = {}, options = {}) {
    if (smartLightConfig.enabled === false) return;

    if (isDevMode) {
      return handleMockCommand(action, data, options);
    }

    if (!options.allowWhenOffline && !canSendCommand(action)) {
      const isFanAction = action === 'set_fan_level' || action === 'set_fan_mode';
      const fanLockedBySmartMode = isFanAction && smartModeEnabled();
      addUserEvent({
        text: fanLockedBySmartMode
          ? '<strong>智慧模式：</strong>風扇由系統自動控制，請先關閉智慧模式'
          : isFanAction
          ? '<strong>風扇模組：</strong>目前未連線，無法送出控制指令'
          : '<strong>LED 模組：</strong>目前未連線，無法送出控制指令',
        icon: 'alert',
        accent: 'var(--yellow)'
      });
      return;
    }

    const message = {
      type: 'dashboard_command',
      action,
      node_id: smartLightConfig.nodeId || 'bedroom01',
      ...getCurrentLightState(),
      ...data,
      ts: Date.now(),
      silent: options.silent === true
    };

    try {
      const sentByWs = sendByWebSocket(message);
      if (!sentByWs) await sendByRest(message);

      if (!options.silent) {
        applySourceStatus('等待 SmartLight 回應');
      }
    } catch (error) {
      console.error('[Dashboard Adapter] 指令送出失敗：', error);
      applySourceStatus('指令送出失敗');
      appendDebugEvent({ title: 'Dashboard command failed', detail: { action, error: error.message } });
      addUserEvent({
        text: `<strong>指令送出失敗：</strong>${error.message || '請確認 Backend 是否啟動'}`,
        icon: 'alert',
        accent: 'var(--yellow)'
      });
    }
  }

  function bindSmartLightUiCommands() {
    if (smartLightConfig.enabled === false) return;

    document.addEventListener('click', (event) => {
      if (event.target.closest('.module-control-locked,.disabled,[disabled]')) return;
      const smartModeSwitch = event.target.closest('#smartModeSwitch,[data-toggle="smart-mode"]');
      if (smartModeSwitch) {
        const enabled = window.SmartDashboard.getState?.().uiState?.smartModeEnabled !== false;
        setTimeout(() => sendDashboardCommand('set_smart_mode', { enabled }, { allowWhenOffline: true }), 0);
        return;
      }

      const controlModeButton = event.target.closest('.mode-card');
      if (controlModeButton) {
        const controlMode = controlModeButton.dataset.controlMode;
        if (isDevMode) {
          setTimeout(() => {
            const label = CONTROL_MODE_LABEL[controlMode] || '情境模式';
            const icon = controlMode === 'auto' ? 'bot' : (controlMode === 'scene' ? 'relax' : 'tap');
            const accent = controlMode === 'auto' ? 'var(--green)' : (controlMode === 'scene' ? 'var(--purple)' : 'var(--blue)');
            addUserEvent({ text: `<strong>控制模式：</strong>${label}`, icon, accent });
          }, 0);
        } else if (controlMode === 'scene' || controlMode === 'manual' || controlMode === 'auto') {
          // 使用者切到情境 / 自定義時，代表要離開 Server 自動控制。
          // 先通知 Backend 關閉智慧模式，避免後端 state→LED 測試又把燈拉回自動 / 測試燈號。
          setTimeout(() => sendDashboardCommand('set_smart_mode', { enabled: false, silent: true, controlMode }, { allowWhenOffline: true, silent: true }), 0);
        }
        return;
      }

      const modeButton = event.target.closest('.small-mode');
      if (modeButton) {
        // 情境參數由 dashboard-app 先同步到畫面與狀態；這裡只在下一個 tick 送出目前狀態。
        // 移除原本 120ms 延遲與重複 apply，降低模式切換時的卡頓感。
        const nextMode = modeButton.dataset.mode;
        setTimeout(() => {
          const light = getCurrentLightState();
          // 點選情境模式代表使用者要手動指定 LED 情境；先關閉 Server 智慧自動控制，
          // 避免 wearable state 自動測試邏輯在下一筆資料進來時又覆蓋情境模式。
          sendDashboardCommand('set_smart_mode', { enabled: false, silent: true, controlMode: 'scene' }, { allowWhenOffline: true, silent: true });
          sendDashboardCommand('set_mode', {
            mode: nextMode,
            brightness: light.brightness,
            kelvin: light.kelvin
          });
        }, 0);
        return;
      }

      const ledSwitch = event.target.closest('.switch[data-toggle="led"]');
      if (ledSwitch) {
        setTimeout(() => {
          const light = getCurrentLightState();
          sendDashboardCommand('set_led_power', { on: light.ledOn });
        }, 0);
        return;
      }

      const colorDot = event.target.closest('.color-dot');
      if (colorDot) {
        setTimeout(() => sendDashboardCommand('set_color', { color: colorDot.dataset.color, mode: 'custom', controlMode: 'manual' }), 0);
        return;
      }

      const resetButton = event.target.closest('#resetBtn');
      if (resetButton) {
        // New LED profile panel owns reset; never fall back to old live-control commands here.
        window.DashboardSceneMemory?.resetCurrentModeDefault?.();
        return;
      }

      const applyRecommendButton = event.target.closest('#applyRecommendBtn');
      if (applyRecommendButton) {
        setTimeout(() => {
          const light = getCurrentLightState();
          sendDashboardCommand('set_mode', { mode: light.mode });
        }, 0);
        return;
      }

      const applyButton = event.target.closest('#applyBtn');
      if (applyButton) {
        // Apply stores user preferences only. Live LED preview is handled by slider release.
        setTimeout(() => {
          const panel = window.DashboardSceneMemory;
          const light = getCurrentLightState();
          if (light.controlMode === 'manual') panel?.applyCustom?.();
          else panel?.saveCurrentScene?.();
        }, 0);
        return;
      }
    });

    const brightnessRange = document.querySelector('#brightnessRange');
    if (brightnessRange) {
      const range = smartLightConfig.brightness || {};
      brightnessRange.min = String(range.min ?? 0);
      brightnessRange.max = String(range.max ?? 100);
      brightnessRange.step = String(range.step ?? 1);
      brightnessRange.addEventListener('input', () => {
        const value = normalizeBrightness(brightnessRange.value);
        brightnessRange.value = String(value);
      });
      brightnessRange.addEventListener('change', () => {
        const value = normalizeBrightness(brightnessRange.value);
        brightnessRange.value = String(value);
      });
    }

    const kelvinRange = document.querySelector('#kelvinRange');
    if (kelvinRange) {
      const range = smartLightConfig.kelvin || {};
      kelvinRange.min = String(range.min ?? 2700);
      kelvinRange.max = String(range.max ?? 6500);
      kelvinRange.step = String(range.step ?? 100);
      kelvinRange.addEventListener('input', () => {
        const value = normalizeKelvin(kelvinRange.value);
        kelvinRange.value = String(value);
      });
      kelvinRange.addEventListener('change', () => {
        kelvinRange.value = String(normalizeKelvin(kelvinRange.value));
      });
    }

    const fanRange = document.querySelector('#fanRange');
    if (fanRange) {
      fanRange.min = '0';
      fanRange.max = '10';
      fanRange.step = '1';
      fanRange.addEventListener('change', () => {
        if (fanRange.disabled) return;
        const fanLevel = clampInt(fanRange.value, 0, 10, 0);
        if (smartModeEnabled()) {
          sendDashboardCommand('set_smart_mode', { enabled: false, silent: true, controlMode: 'manual' }, { allowWhenOffline: true, silent: true });
        }
        sendDashboardCommand('set_fan_level', { fanLevel, fanSpeed: fanLevel });
      });
    }

  }

  function bindMusicUiCommands() {
    document.addEventListener('click', (event) => {
      if (event.target.closest('.module-control-locked,.disabled,[disabled]')) return;
      const audioButton = event.target.closest('[data-audio-action]');
      if (!audioButton) return;
      sendMusicCommand(audioButton.dataset.audioAction);
    });

    const audioVolumeRange = document.querySelector('#audioVolumeRange');
    if (audioVolumeRange) {
      audioVolumeRange.addEventListener('change', () => {
        if (audioVolumeRange.disabled) return;
        sendMusicCommand('set_volume', { volume: clampInt(audioVolumeRange.value, 0, 100, 50) });
      });
    }
  }

  function startDevMode() {
    document.body.classList.add('dashboard-dev-mode');
    applyConnectionState({ websocket: false, mqtt: false, smartLight: 'mock' });
    applySourceStatus('已連線');
    addUserEvent({ text: '<strong>Dashboard：</strong>控制面板已就緒', icon: 'device', accent: 'var(--green)' });
    appendDebugEvent({ title: 'Runtime mode', detail: { runtimeMode: 'dev', brokerRequired: false, nodeRequired: false } });
    syncMockStatus('已連線');
  }

  function startDataAdapter() {
    if (!window.SmartDashboard) {
      console.error('[Dashboard Adapter] SmartDashboard 尚未載入');
      return;
    }

    bindSmartLightUiCommands();
    bindMusicUiCommands();

    if (isDevMode) {
      startDevMode();
      return;
    }

    if (config.useFakeData === false) {
      window.SmartDashboard.stopFakeData?.({ silent: true });
      window.SmartDashboard.clearEvents?.();
      addUserEvent({ text: '<strong>Dashboard：</strong>LED 實機對接模式已啟動', icon: 'bulb', accent: 'var(--purple)' });
    }

    applyConnectionState(lastConnectionState);

    if (source.mode === 'rest' && source.rest?.enabled) {
      startRestPolling(source.rest);
      return;
    }

    if (source.mode === 'websocket' && source.websocket?.enabled) {
      startWebSocket(source.websocket);
      return;
    }

    console.info('[Dashboard Adapter] 目前未啟用真實資料來源，使用展示 / 模擬模式。');
  }

  window.DashboardDataAdapter = {
    applyDashboardPayload,
    sendDashboardCommand,
    sendMusicCommand,
    getCurrentLightState,
    startDataAdapter,
    appendDebugEvent,
    runtimeMode
  };

  startDataAdapter();
})();
