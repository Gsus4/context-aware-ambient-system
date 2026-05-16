/* Dashboard LED profile panel: target-lux sliders and custom advanced settings. */
(function () {
  'use strict';

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));
  const apiBase = '/api/led-profiles';
  const configProfiles = window.DASHBOARD_CONFIG?.smartLight?.sceneProfiles || {};
  const sceneOrder = ['vacant', 'sleep', 'relax', 'work', 'exercise'];
  const sceneLabels = {
    vacant: '\u7121\u4eba',
    sleep: '\u7761\u7720',
    relax: '\u653e\u9b06',
    work: '\u5de5\u4f5c',
    exercise: '\u904b\u52d5'
  };
  const sceneSliderLimits = {
    vacant: { enabled: false },
    sleep: { min: 0, max: 150, step: 10 },
    relax: { min: 50, max: 500, step: 10 },
    work: { min: 200, max: 1000, step: 10 },
    exercise: { min: 100, max: 1000, step: 10 }
  };

  let profiles = normalizeProfiles(null);
  let customControlType = 'fixed_brightness';
  let advancedDraft = null;
  let isRendering = false;
  const sliderCommitState = new WeakMap();

  function state() {
    return window.SmartDashboard?.getState?.() || {};
  }

  function smartModeOn() {
    return state().uiState?.smartModeEnabled !== false;
  }

  function controlMode() {
    const mode = state().uiState?.controlMode || 'scene';
    return mode === 'auto' ? 'scene' : mode;
  }

  function sceneMode() {
    return state().uiState?.emotionMode || 'work';
  }

  function n(value, fallback = 0) {
    const num = Number(value);
    return Number.isFinite(num) ? num : fallback;
  }

  function clamp(value, min, max) {
    return Math.min(Math.max(n(value, min), min), max);
  }

  function dash(value, suffix = '') {
    return value === null || value === undefined || value === '' ? '--' : `${value}${suffix}`;
  }

  function sceneTargetLimit(mode = sceneMode()) {
    return sceneSliderLimits[mode] || { min: 0, max: 1500, step: 10 };
  }

  function rangeFrom(profileOrPatch, fallbackTolerance = 75) {
    const target = n(profileOrPatch.target_lux, NaN);
    const tolerance = n(profileOrPatch.tolerance_lux, fallbackTolerance);
    if (!Number.isFinite(target)) {
      return {
        min: profileOrPatch.target_lux_min ?? null,
        max: profileOrPatch.target_lux_max ?? null
      };
    }
    return {
      min: Math.max(0, Math.round(target - tolerance)),
      max: Math.round(target + tolerance)
    };
  }

  function profileFromConfig(mode) {
    const src = configProfiles[mode] || {};
    return {
      label: sceneLabels[mode] || src.label || mode,
      scene: src.scene || mode,
      target_lux: src.targetLux ?? null,
      tolerance_lux: src.toleranceLux ?? 75,
      target_lux_min: src.targetLuxMin ?? null,
      target_lux_max: src.targetLuxMax ?? null,
      color_mode: src.colorMode || (src.rgb ? 'rgb' : 'cct'),
      color_temp_k: src.kelvinDefault ?? 3900,
      rgb: src.rgb || { r: 255, g: 160, b: 80 },
      effect: mode === 'vacant' ? 'off' : 'static',
      min_output: src.minOutput ?? 0,
      max_output: src.maxOutput ?? 100,
      adaptive_profile: src.adaptiveProfile || 'normal'
    };
  }

  function normalizeProfiles(raw) {
    const scene_profiles = {};
    sceneOrder.forEach((mode) => {
      scene_profiles[mode] = {
        ...profileFromConfig(mode),
        ...(raw?.scene_profiles?.[mode] || {})
      };
    });
    return {
      version: raw?.version || 1,
      scene_profiles,
      custom_profiles: {
        fixed_brightness: {
          label: '\u56fa\u5b9a\u4eae\u5ea6',
          custom_control_type: 'fixed_brightness',
          fixed_brightness_pct: 64,
          target_lux: null,
          tolerance_lux: null,
          target_lux_min: null,
          target_lux_max: null,
          color_mode: 'cct',
          color_temp_k: 3900,
          rgb: { r: 255, g: 160, b: 80 },
          effect: 'static',
          breathing_speed: 'slow',
          breathing_strength: 50,
          flow_preset: 'soft_rainbow',
          flow_speed: 'slow',
          flow_brightness: 64,
          min_output: 0,
          max_output: 100,
          ...(raw?.custom_profiles?.fixed_brightness || {})
        }
      }
    };
  }

  async function loadProfiles() {
    try {
      const res = await fetch(apiBase, { cache: 'no-store' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      profiles = normalizeProfiles(data.profiles);
    } catch (error) {
      console.warn('[LED profile panel] profile API unavailable, using bundled config:', error.message);
      profiles = normalizeProfiles(null);
    }
    renderPanel();
  }

  async function postJson(url, body) {
    const res = await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    if (data.profiles) profiles = normalizeProfiles(data.profiles);
    return data;
  }

  function toast(message) {
    const toastEl = $('#toast');
    if (!toastEl) return;
    toastEl.textContent = message;
    toastEl.classList.add('show');
    clearTimeout(toast.timer);
    toast.timer = setTimeout(() => toastEl.classList.remove('show'), 2300);
  }

  function addEvent(text, icon = 'bulb', accent = 'var(--purple)') {
    window.SmartDashboard?.addEvent?.(text, icon, accent);
  }

  function ensurePanel() {
    if ($('#ledProfilePanel')) return;
    const advancedEntry = $('.advanced-entry');
    const mountAfter = advancedEntry || $('#kelvinRange')?.closest('.slider-block');
    if (!mountAfter) return;
    const panel = document.createElement('section');
    panel.id = 'ledProfilePanel';
    panel.className = 'led-profile-panel';
    panel.innerHTML = `
      <div class="led-profile-head">
        <div>
          <div class="section-label" id="ledProfileModeTitle">\u60c5\u5883\u8a2d\u5b9a</div>
          <div class="led-profile-sub" id="ledProfileModeSub"></div>
        </div>
      </div>
      <div class="led-profile-grid" id="ledProfileFields"></div>
      <div class="led-live-status" id="ledLiveStatus">
        <div class="led-live-title">\u5373\u6642\u72c0\u614b</div>
        <div class="led-live-grid">
          <div><span>\u76ee\u524d\u74b0\u5883\u5149</span><b id="liveCurrentLux">--</b></div>
          <div><span>LED \u5be6\u969b\u8f38\u51fa</span><b id="liveLedOutput">--</b></div>
        </div>
      </div>
    `;
    mountAfter.insertAdjacentElement('afterend', panel);
  }

  function ensureAdvancedModal() {
    if ($('#ledAdvancedSettingsModal')) return;
    const modal = document.createElement('div');
    modal.id = 'ledAdvancedSettingsModal';
    modal.className = 'led-advanced-modal-backdrop';
    modal.setAttribute('aria-hidden', 'true');
    modal.innerHTML = `
      <div class="led-advanced-dialog" role="dialog" aria-modal="true" aria-labelledby="ledAdvancedTitle">
        <div class="led-advanced-dialog-head">
          <div>
            <div class="led-advanced-kicker">CUSTOM</div>
            <h2 id="ledAdvancedTitle">\u9032\u968e\u8a2d\u5b9a</h2>
          </div>
          <button type="button" class="led-advanced-close" data-led-advanced-close aria-label="\u95dc\u9589">x</button>
        </div>
        <div class="led-advanced-body" id="ledAdvancedFields"></div>
        <div class="led-advanced-actions">
          <button type="button" class="footer-btn" data-led-advanced-close>\u53d6\u6d88</button>
          <button type="button" class="footer-btn primary" id="saveLedAdvancedBtn">\u5132\u5b58\u9032\u968e\u8a2d\u5b9a</button>
        </div>
      </div>
    `;
    document.body.appendChild(modal);
  }

  function sliderMarkup({ label, key, value, min, max, step, unit = '', source = 'profile', disabled = false, className = '' }) {
    const safeValue = Math.round(clamp(value, min, max));
    const pct = max === min ? 0 : ((safeValue - min) / (max - min)) * 100;
    const attr = source === 'advanced' ? 'data-advanced-field' : 'data-profile-field';
    return `
      <div class="led-slider-field${disabled ? ' is-disabled' : ''}${className ? ` ${className}` : ''}">
        <div class="led-slider-head">
          <span>${label}</span>
          <b>\u76ee\u524d\u503c\uff1a<span data-slider-value-for="${key}">${safeValue}</span>${unit ? ` ${unit}` : ''}</b>
        </div>
        <input ${attr}="${key}" type="range" min="${min}" max="${max}" step="${step}" value="${safeValue}" style="--percent:${pct}%"${disabled ? ' disabled' : ''}>
        <div class="led-slider-range"><span>${min}${unit ? ` ${unit}` : ''}</span><span>${max}${unit ? ` ${unit}` : ''}</span></div>
        <div class="led-slider-limit-text">\u53ef\u8a2d\u5b9a\u7bc4\u570d\uff1a${min}\uff5e${max}${unit ? ` ${unit}` : ''}</div>
      </div>
    `;
  }

  function selectField(label, key, value, choices, className = '') {
    const options = choices.map((item) => {
      const itemValue = typeof item === 'object' ? item.value : item;
      const itemLabel = typeof item === 'object' ? item.label : item;
      return `<option value="${itemValue}"${String(itemValue) === String(value) ? ' selected' : ''}>${itemLabel}</option>`;
    }).join('');
    return `<label class="led-profile-field${className ? ` ${className}` : ''}"><span>${label}</span><select data-advanced-field="${key}">${options}</select></label>`;
  }


  function advancedCard(title, hint, content, className = '', attrs = '') {
    return `
      <section class="led-advanced-card ${className}"${attrs ? ` ${attrs}` : ''}>
        <div class="led-advanced-card-head">
          <h3>${title}</h3>
        </div>
        <div class="led-advanced-card-grid">${content}</div>
      </section>
    `;
  }

  function rgbToHex(rgb = {}) {
    const toHex = (value) => clamp(value ?? 0, 0, 255).toString(16).padStart(2, '0');
    return `#${toHex(rgb.r)}${toHex(rgb.g)}${toHex(rgb.b)}`;
  }

  function hexToRgb(hex) {
    const clean = String(hex || '').replace('#', '').trim();
    if (!/^[0-9a-fA-F]{6}$/.test(clean)) return null;
    return {
      r: parseInt(clean.slice(0, 2), 16),
      g: parseInt(clean.slice(2, 4), 16),
      b: parseInt(clean.slice(4, 6), 16)
    };
  }

  function colorPaletteMarkup(rgb) {
    const activeHex = rgbToHex(rgb);
    const presets = [
      ['暖白', '#ffd2a1'], ['自然白', '#fff4d6'], ['冷白', '#d8ecff'], ['專注藍', '#6fb7ff'],
      ['放鬆橘', '#ff8a50'], ['睡眠琥珀', '#ffb56a'], ['運動紅', '#ff4f5e'], ['柔紫', '#9b5cff'],
      ['森林綠', '#5ce08a'], ['彩虹粉', '#ff74c7']
    ];
    const presetButtons = presets.map(([label, hex]) => `
      <button type="button" class="led-color-swatch${hex.toLowerCase() === activeHex.toLowerCase() ? ' active' : ''}" data-color-preset="${hex}" title="${label}" style="--swatch:${hex}">
        <span>${label}</span>
      </button>
    `).join('');
    return `
      <div class="led-color-palette-panel">
        <div class="led-color-picker-row">
          <label class="led-color-picker-box">
            <span>自選顏色</span>
            <input type="color" value="${activeHex}" data-color-picker aria-label="自選 RGB 顏色">
          </label>
          <div class="led-color-preview" data-color-preview style="--preview-color:${activeHex}">
            <b>目前顏色</b><em>${activeHex.toUpperCase()}</em>
          </div>
        </div>
        <div class="led-color-swatch-grid" aria-label="常用色調色盤">${presetButtons}</div>
      </div>
    `;
  }

  function effectChoiceMarkup(value = 'static') {
    const current = ['off', 'static', 'breathing', 'flow'].includes(value) ? value : 'static';
    const choices = [
      { label: '關閉', value: 'off' },
      { label: '靜態', value: 'static' },
      { label: '呼吸', value: 'breathing' },
      { label: '流水', value: 'flow' }
    ];
    return `
      <div class="led-effect-choice-group" role="group" aria-label="燈效選擇">
        ${choices.map((item) => `
          <label class="led-effect-choice${current === item.value ? ' active' : ''}">
            <input type="checkbox" data-effect-option="${item.value}" value="${item.value}"${current === item.value ? ' checked' : ''}>
            <span>${item.label}</span>
          </label>
        `).join('')}
      </div>
    `;
  }

  function sceneProfile() {
    return profiles.scene_profiles[sceneMode()] || profiles.scene_profiles.work;
  }

  function customBaseProfile() {
    return profiles.custom_profiles.fixed_brightness || normalizeProfiles(null).custom_profiles.fixed_brightness;
  }

  function isAdvancedSettingsOpen() {
    const modal = $('#ledAdvancedSettingsModal');
    return Boolean(modal?.classList.contains('show'));
  }

  function cloneProfile(profile) {
    return JSON.parse(JSON.stringify(profile || {}));
  }

  function activeAdvancedProfile() {
    customControlType = 'fixed_brightness';
    const base = profiles.custom_profiles.fixed_brightness || normalizeProfiles(null).custom_profiles.fixed_brightness;
    return { ...base, ...(advancedDraft || {}) };
  }

  function startAdvancedDraft() {
    customControlType = 'fixed_brightness';
    advancedDraft = {
      ...cloneProfile(profiles.custom_profiles.fixed_brightness || normalizeProfiles(null).custom_profiles.fixed_brightness),
      custom_control_type: 'fixed_brightness'
    };
  }

  function mergeAdvancedDraft(patch = {}) {
    customControlType = 'fixed_brightness';
    const cleanedPatch = { ...(patch || {}) };
    delete cleanedPatch.custom_control_type;
    delete cleanedPatch.target_lux;
    delete cleanedPatch.tolerance_lux;
    delete cleanedPatch.target_lux_min;
    delete cleanedPatch.target_lux_max;
    delete cleanedPatch.min_output;
    delete cleanedPatch.max_output;
    advancedDraft = {
      ...(advancedDraft || cloneProfile(profiles.custom_profiles.fixed_brightness || normalizeProfiles(null).custom_profiles.fixed_brightness)),
      ...cleanedPatch,
      custom_control_type: 'fixed_brightness'
    };
  }

  function refreshAdvancedModeUi() {
    customControlType = 'fixed_brightness';
    $('.led-advanced-card[data-control-mode]').forEach((card) => {
      card.dataset.controlMode = 'fixed_brightness';
    });
    const colorMode = $('[data-advanced-field="color_mode"]')?.value || advancedDraft?.color_mode || activeAdvancedProfile().color_mode || 'cct';
    updateColorModeVisibility(colorMode);
    const effect = $('[data-effect-option]:checked')?.value || advancedDraft?.effect || activeAdvancedProfile().effect || 'static';
    updateEffectChoiceUi(effect);
  }

  function renderBasicFields() {
    if (smartModeOn()) return '';
    const isScene = controlMode() === 'scene';
    if (isScene) {
      const profile = sceneProfile();
      const limit = sceneTargetLimit(sceneMode());
      const disabled = limit.enabled === false;
      return disabled
        ? '<div class="led-target-disabled">無人模式不需要目標亮度</div>'
        : sliderMarkup({
          label: '目標亮度',
          key: 'target_lux',
          value: profile.target_lux ?? limit.min ?? 0,
          min: limit.min ?? 0,
          max: limit.max ?? 1500,
          step: limit.step ?? 10,
          unit: 'lux'
        });
    }

    const profile = customBaseProfile();
    return [
      sliderMarkup({
        label: '亮度',
        key: 'fixed_brightness_pct',
        value: profile.fixed_brightness_pct ?? 64,
        min: 0,
        max: 100,
        step: 1,
        unit: '%',
        source: 'advanced'
      }),
      '<button type="button" class="led-profile-secondary-action" id="openLedAdvancedBtn">進階設定</button>'
    ].join('');
  }

  function renderAdvancedFields() {
    customControlType = 'fixed_brightness';
    const profile = activeAdvancedProfile();
    const rgb = profile.rgb || {};
    const colorMode = profile.color_mode || 'cct';
    const effect = profile.effect || 'static';

    const brightnessCard = advancedCard('亮度設定', '', [
      sliderMarkup({ label: '亮度', key: 'fixed_brightness_pct', value: profile.fixed_brightness_pct ?? 64, min: 0, max: 100, step: 1, unit: '%', source: 'advanced' })
    ].join(''), 'led-advanced-card-brightness', 'data-control-mode="fixed_brightness"');

    const colorCard = advancedCard('顏色設定', '', [
      selectField('色彩模式', 'color_mode', colorMode, [
        { label: '色溫', value: 'cct' },
        { label: 'RGB 顏色', value: 'rgb' }
      ]),
      sliderMarkup({ label: '色溫', key: 'color_temp_k', value: profile.color_temp_k ?? 3900, min: 2700, max: 6500, step: 100, unit: 'K', source: 'advanced', className: 'led-color-cct-field' }),
      `<div class="led-color-rgb-field">${colorPaletteMarkup(rgb)}</div>`,
      sliderMarkup({ label: 'RGB 紅色', key: 'rgb_r', value: rgb.r ?? 255, min: 0, max: 255, step: 1, source: 'advanced', className: 'led-color-rgb-field' }),
      sliderMarkup({ label: 'RGB 綠色', key: 'rgb_g', value: rgb.g ?? 160, min: 0, max: 255, step: 1, source: 'advanced', className: 'led-color-rgb-field' }),
      sliderMarkup({ label: 'RGB 藍色', key: 'rgb_b', value: rgb.b ?? 80, min: 0, max: 255, step: 1, source: 'advanced', className: 'led-color-rgb-field' })
    ].join(''), 'led-advanced-card-color', `data-color-mode="${colorMode}"`);

    const effectCard = advancedCard('燈效設定', '', [
      effectChoiceMarkup(effect),
      sliderMarkup({ label: '呼吸強度', key: 'breathing_strength', value: profile.breathing_strength ?? 50, min: 0, max: 100, step: 1, unit: '%', source: 'advanced', className: 'led-effect-breathing-field' }),
      selectField('呼吸速度', 'breathing_speed', profile.breathing_speed || 'slow', [
        { label: '慢', value: 'slow' },
        { label: '中', value: 'medium' },
        { label: '快', value: 'fast' }
      ], 'led-effect-breathing-field'),
      sliderMarkup({ label: '流水亮度', key: 'flow_brightness', value: profile.flow_brightness ?? profile.fixed_brightness_pct ?? 64, min: 0, max: 100, step: 1, unit: '%', source: 'advanced', className: 'led-effect-flow-field' }),
      selectField('流水樣式', 'flow_preset', profile.flow_preset || 'soft_rainbow', [
        { label: '柔和彩虹', value: 'soft_rainbow' },
        { label: '暖色波浪', value: 'warm_wave' },
        { label: '專注掃描', value: 'focus_scan' },
        { label: '極光', value: 'aurora' }
      ], 'led-effect-flow-field'),
      selectField('流水速度', 'flow_speed', profile.flow_speed || 'slow', [
        { label: '慢', value: 'slow' },
        { label: '中', value: 'medium' },
        { label: '快', value: 'fast' }
      ], 'led-effect-flow-field')
    ].join(''), 'led-advanced-card-effect', `data-effect-mode="${effect}"`);

    return brightnessCard + colorCard + effectCard;
  }

  function renderLiveStatus() {
    const sensor = state().sensorData || {};
    const device = state().deviceState || {};
    if ($('#liveCurrentLux')) $('#liveCurrentLux').textContent = dash(sensor.currentLux, ' lux');
    if ($('#liveLedOutput')) $('#liveLedOutput').textContent = dash(sensor.ledOutputPercent ?? device.brightness, '%');
  }

  function setFooterButtonsLocked(locked) {
    ['#applyBtn', '#resetBtn'].forEach((selector) => {
      const button = $(selector);
      if (!button) return;
      button.disabled = locked;
      button.classList.toggle('disabled', locked);
      button.setAttribute('aria-disabled', String(locked));
    });
  }

  function keepFanControlUsable() {
    if (document.body.classList.contains('fan-module-offline')) return;
    if (smartModeOn()) return;
    const fanRange = $('#fanRange');
    if (fanRange) {
      fanRange.disabled = false;
      fanRange.classList.remove('disabled', 'module-control-locked');
      fanRange.setAttribute('aria-disabled', 'false');
    }
    const fanRow = $('.fan-row');
    if (fanRow) fanRow.classList.remove('module-control-locked', 'disabled');
  }

  function renderPanel() {
    if (isRendering) return;
    isRendering = true;
    ensurePanel();
    ensureAdvancedModal();
    const mode = controlMode();
    const isScene = mode === 'scene';
    const fields = $('#ledProfileFields');
    document.body.classList.toggle('led-scene-profile-mode', isScene);
    document.body.classList.toggle('led-custom-profile-mode', mode === 'manual');
    document.body.classList.remove('led-custom-target-mode');
    document.body.classList.toggle('led-custom-fixed-mode', mode === 'manual');
    document.body.classList.toggle('led-profile-smart-locked', smartModeOn());
    if ($('#ledProfileModeTitle')) $('#ledProfileModeTitle').textContent = isScene ? '\u60c5\u5883\u8a2d\u5b9a' : '\u81ea\u5b9a\u7fa9\u8a2d\u5b9a';
    if ($('#ledProfileModeSub')) $('#ledProfileModeSub').textContent = '';
    if (fields) fields.innerHTML = renderBasicFields();
    if ($('#ledAdvancedFields') && !isAdvancedSettingsOpen()) $('#ledAdvancedFields').innerHTML = renderAdvancedFields();
    setFooterButtonsLocked(smartModeOn());
    keepFanControlUsable();
    renderLiveStatus();
    isRendering = false;
  }

  function collectBasicPatch(profile) {
    const targetInput = $('[data-profile-field="target_lux"]');
    const inputMin = Number(targetInput?.min ?? 0);
    const inputMax = Number(targetInput?.max ?? 1500);
    const target = targetInput ? Math.round(clamp(targetInput.value, inputMin, inputMax)) : profile.target_lux;
    const tolerance = profile.tolerance_lux ?? 75;
    const range = rangeFrom({ target_lux: target, tolerance_lux: tolerance }, tolerance);
    return { target_lux: target, target_lux_min: range.min, target_lux_max: range.max };
  }

  function collectAdvancedPatch() {
    const patch = {};
    let hasRgbFields = false;
    $$('[data-advanced-field]').forEach((input) => {
      const key = input.dataset.advancedField;
      if (['custom_control_type', 'color_mode', 'breathing_speed', 'flow_preset', 'flow_speed'].includes(key)) {
        patch[key] = input.value;
        return;
      }
      const value = input.value === '' ? null : Number(input.value);
      patch[key] = Number.isFinite(value) ? Math.round(value) : null;
      if (['rgb_r', 'rgb_g', 'rgb_b'].includes(key)) hasRgbFields = true;
    });
    const effectOption = $('[data-effect-option]:checked');
    if (effectOption) patch.effect = effectOption.value || 'off';
    if (hasRgbFields) {
      patch.rgb = {
        r: clamp(patch.rgb_r ?? 255, 0, 255),
        g: clamp(patch.rgb_g ?? 160, 0, 255),
        b: clamp(patch.rgb_b ?? 80, 0, 255)
      };
      delete patch.rgb_r;
      delete patch.rgb_g;
      delete patch.rgb_b;
    }
    return patch;
  }

  function currentCustomPayload() {
    const livePatch = collectAdvancedPatch();
    const fixed = profiles.custom_profiles.fixed_brightness || normalizeProfiles(null).custom_profiles.fixed_brightness;
    return {
      ...fixed,
      ...livePatch,
      custom_control_type: 'fixed_brightness',
      fixed_brightness_pct: livePatch.fixed_brightness_pct ?? fixed.fixed_brightness_pct ?? 64,
      target_lux: null,
      tolerance_lux: null,
      target_lux_min: null,
      target_lux_max: null,
      min_output: null,
      max_output: null
    };
  }

  function sendDashboardCommand(action, payload) {
    if (window.DashboardDataAdapter?.sendDashboardCommand) {
      window.DashboardDataAdapter.sendDashboardCommand(action, payload);
      return;
    }
    fetch(window.DASHBOARD_CONFIG?.smartLight?.commandUrl || '/api/smartlight/command', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action, ...payload })
    }).catch((error) => console.warn('[LED profile panel] command failed:', error.message));
  }

  function livePreviewCustom() {
    if (smartModeOn() || controlMode() !== 'manual') return;
    const payload = currentCustomPayload();
    sendDashboardCommand('set_custom_light', payload);
    toast('已套用目前燈光預覽');
  }

  function commitSliderPreview(slider) {
    if (!slider || slider.type !== 'range' || slider.disabled) return;
    updateSliderUi(slider);
    const key = slider.dataset.profileField || slider.dataset.advancedField || '';
    const value = slider.value;
    const now = Date.now();
    const last = sliderCommitState.get(slider);
    if (last && last.key === key && last.value === value && now - last.at < 250) return;
    sliderCommitState.set(slider, { key, value, at: now });
    const colorModeSelect = $('[data-advanced-field="color_mode"]');
    if (slider.dataset.advancedField) mergeAdvancedDraft({ [key]: Number(value) });
    if (['rgb_r', 'rgb_g', 'rgb_b'].includes(key) && colorModeSelect) {
      colorModeSelect.value = 'rgb';
      mergeAdvancedDraft({ color_mode: 'rgb' });
      updateColorModeVisibility('rgb');
    }
    if (key === 'color_temp_k' && colorModeSelect) {
      colorModeSelect.value = 'cct';
      mergeAdvancedDraft({ color_mode: 'cct' });
      updateColorModeVisibility('cct');
    }
    if (key === 'flow_brightness') { mergeAdvancedDraft({ effect: 'flow' }); updateEffectChoiceUi('flow'); }
    if (key === 'breathing_strength') { mergeAdvancedDraft({ effect: 'breathing' }); updateEffectChoiceUi('breathing'); }
    if (slider.dataset.profileField && controlMode() === 'scene') {
      const scene = sceneMode();
      profiles.scene_profiles[scene] = {
        ...profiles.scene_profiles[scene],
        ...collectBasicPatch(sceneProfile())
      };
      toast('\u5df2\u66f4\u65b0\u76ee\u6a19\u4eae\u5ea6\uff0c\u6309\u5957\u7528\u8a2d\u5b9a\u53ef\u5132\u5b58\u504f\u597d');
      return;
    }
    if (controlMode() === 'manual') livePreviewCustom();
  }

  async function saveCurrentScene() {
    if (controlMode() !== 'scene') return false;
    if (smartModeOn()) return true;
    if (sceneMode() === 'vacant') {
      toast('\u7121\u4eba\u6a21\u5f0f\u4e0d\u9700\u8981\u8a2d\u5b9a\u76ee\u6a19\u4eae\u5ea6');
      return true;
    }
    const scene = sceneMode();
    try {
      const patch = collectBasicPatch(sceneProfile());
      await postJson(apiBase, { section: 'scene_profiles', key: scene, profile: patch });
      addEvent(`<strong>${sceneLabels[scene] || scene}\uff1a</strong>\u5df2\u5132\u5b58\u4f7f\u7528\u8005\u504f\u597d`, 'bulb', 'var(--purple)');
      toast('\u5df2\u5132\u5b58\u4f7f\u7528\u8005\u504f\u597d');
      renderPanel();
    } catch (error) {
      toast(`\u60c5\u5883\u504f\u597d\u5132\u5b58\u5931\u6557\uff1a${error.message}`);
    }
    return true;
  }

  async function applyCustom() {
    if (smartModeOn()) return;
    try {
      const payload = currentCustomPayload();
      await postJson(apiBase, { section: 'custom_profiles', key: 'fixed_brightness', profile: payload });
      addEvent('<strong>\u81ea\u5b9a\u7fa9\uff1a</strong>\u5df2\u5132\u5b58\u4f7f\u7528\u8005\u504f\u597d', 'tap', 'var(--blue)');
      toast('\u5df2\u5132\u5b58\u4f7f\u7528\u8005\u504f\u597d');
      renderPanel();
    } catch (error) {
      toast(`\u81ea\u5b9a\u7fa9\u504f\u597d\u5132\u5b58\u5931\u6557\uff1a${error.message}`);
    }
  }

  async function saveAdvancedSettings() {
    try {
      const patch = collectAdvancedPatch();
      delete patch.custom_control_type;
      delete patch.target_lux;
      delete patch.tolerance_lux;
      delete patch.target_lux_min;
      delete patch.target_lux_max;
      delete patch.min_output;
      delete patch.max_output;
      customControlType = 'fixed_brightness';
      await postJson(apiBase, {
        section: 'custom_profiles',
        key: 'fixed_brightness',
        profile: {
          ...patch,
          custom_control_type: 'fixed_brightness',
          target_lux: null,
          tolerance_lux: null,
          target_lux_min: null,
          target_lux_max: null,
          min_output: null,
          max_output: null
        }
      });
      closeAdvancedSettings({ discardDraft: false });
      advancedDraft = null;
      toast('已儲存使用者偏好');
      renderPanel();
    } catch (error) {
      toast(`進階設定儲存失敗：${error.message}`);
    }
  }

  async function resetCurrentModeDefault() {
    const mode = controlMode();
    try {
      if (mode === 'scene') {
        await postJson(`${apiBase}/reset`, { section: 'scene_profiles', key: sceneMode() });
      } else {
        await postJson(`${apiBase}/reset`, { section: 'custom_profiles', key: 'fixed_brightness' });
      }
      toast('\u5df2\u6062\u5fa9\u9810\u8a2d\u503c');
      renderPanel();
      if (mode === 'manual') setTimeout(livePreviewCustom, 0);
    } catch (error) {
      toast(`\u6062\u5fa9\u9810\u8a2d\u5931\u6557\uff1a${error.message}`);
    }
    return true;
  }

  function openAdvancedSettings() {
    if (smartModeOn()) return;
    ensureAdvancedModal();
    const modal = $('#ledAdvancedSettingsModal');
    if (!modal) return;
    startAdvancedDraft();
    $('#ledAdvancedFields').innerHTML = renderAdvancedFields();
    modal.classList.add('show');
    modal.setAttribute('aria-hidden', 'false');
    refreshAdvancedModeUi();
  }

  function closeAdvancedSettings({ discardDraft = true } = {}) {
    const modal = $('#ledAdvancedSettingsModal');
    if (!modal) return;
    modal.classList.remove('show');
    modal.setAttribute('aria-hidden', 'true');
    if (discardDraft) advancedDraft = null;
  }

  function updateSliderUi(slider) {
    const min = Number(slider.min || 0);
    const max = Number(slider.max || 100);
    const value = Math.round(clamp(slider.value, min, max));
    slider.value = String(value);
    slider.style.setProperty('--percent', `${max === min ? 0 : ((value - min) / (max - min)) * 100}%`);
    const label = document.querySelector(`[data-slider-value-for="${slider.dataset.profileField || slider.dataset.advancedField}"]`);
    if (label && label.closest('.led-slider-field') === slider.closest('.led-slider-field')) label.textContent = String(value);
  }


  function rgbFieldSliders() {
    return {
      r: $('[data-advanced-field="rgb_r"]'),
      g: $('[data-advanced-field="rgb_g"]'),
      b: $('[data-advanced-field="rgb_b"]')
    };
  }

  function currentRgbFromControls() {
    const sliders = rgbFieldSliders();
    return {
      r: n(sliders.r?.value, 255),
      g: n(sliders.g?.value, 160),
      b: n(sliders.b?.value, 80)
    };
  }

  function updateColorPickerPreview(rgb = currentRgbFromControls()) {
    const hex = rgbToHex(rgb);
    const picker = $('[data-color-picker]');
    if (picker && picker.value.toLowerCase() !== hex.toLowerCase()) picker.value = hex;
    const preview = $('[data-color-preview]');
    if (preview) {
      preview.style.setProperty('--preview-color', hex);
      const label = preview.querySelector('em');
      if (label) label.textContent = hex.toUpperCase();
    }
    $$('[data-color-preset]').forEach((button) => {
      button.classList.toggle('active', String(button.dataset.colorPreset || '').toLowerCase() === hex.toLowerCase());
    });
  }

  function updateColorModeVisibility(mode) {
    const card = $('.led-advanced-card-color');
    if (card) card.dataset.colorMode = mode || 'cct';
  }

  function updateEffectChoiceUi(effect = 'static') {
    const mode = ['off', 'static', 'breathing', 'flow'].includes(effect) ? effect : 'static';
    const card = $('.led-advanced-card-effect');
    if (card) card.dataset.effectMode = mode;
    $$('[data-effect-option]').forEach((input) => {
      const active = input.value === mode;
      input.checked = active;
      const tile = input.closest('.led-effect-choice');
      if (tile) tile.classList.toggle('active', active);
    });
  }

  function setRgbControls(rgb, { commit = false } = {}) {
    const sliders = rgbFieldSliders();
    [['r', sliders.r], ['g', sliders.g], ['b', sliders.b]].forEach(([key, slider]) => {
      if (!slider) return;
      slider.value = String(clamp(rgb[key], 0, 255));
      updateSliderUi(slider);
    });
    const colorModeSelect = $('[data-advanced-field="color_mode"]');
    if (colorModeSelect) colorModeSelect.value = 'rgb';
    updateColorModeVisibility('rgb');
    mergeAdvancedDraft({ color_mode: 'rgb', rgb, rgb_r: rgb.r, rgb_g: rgb.g, rgb_b: rgb.b });
    updateColorPickerPreview(rgb);
    if (commit && controlMode() === 'manual') livePreviewCustom();
  }

  function clampSceneValue(fieldName, value, mode = sceneMode()) {
    const profile = profiles.scene_profiles[mode] || profiles.scene_profiles.work;
    if (fieldName === 'brightness') return Math.round(clamp(value, profile.min_output ?? 0, profile.max_output ?? 100));
    if (fieldName === 'kelvin') return Math.round(clamp(value, 2700, 6500) / 100) * 100;
    return value;
  }

  function getSceneProfile(mode) {
    return { ...(profiles.scene_profiles[mode] || profiles.scene_profiles.work) };
  }

  function bindDashboardHooks() {
    const dashboard = window.SmartDashboard;
    if (!dashboard || dashboard.__ledProfilePanelHooked__) return;
    ['updateDeviceState', 'updateSensorData', 'updateUiState', 'updateMode'].forEach((name) => {
      const original = dashboard[name];
      if (typeof original !== 'function') return;
      dashboard[name] = function patchedDashboardUpdate(...args) {
        const result = original.apply(this, args);
        setTimeout(renderPanel, 0);
        return result;
      };
    });
    dashboard.__ledProfilePanelHooked__ = true;
  }

  function bindEvents() {
    document.addEventListener('input', (event) => {
      const colorPicker = event.target.closest('[data-color-picker]');
      if (colorPicker) {
        const rgb = hexToRgb(colorPicker.value);
        if (rgb) setRgbControls(rgb, { commit: false });
        return;
      }
      const slider = event.target.closest('[data-profile-field], [data-advanced-field]');
      if (slider?.type === 'range') {
        updateSliderUi(slider);
        if (slider.dataset.advancedField) {
          const key = slider.dataset.advancedField;
          mergeAdvancedDraft({ [key]: Number(slider.value) });
        }
        if (['rgb_r', 'rgb_g', 'rgb_b'].includes(slider.dataset.advancedField)) updateColorPickerPreview();
      }
    });

    document.addEventListener('change', (event) => {
      const colorPicker = event.target.closest('[data-color-picker]');
      if (colorPicker) {
        const rgb = hexToRgb(colorPicker.value);
        if (rgb) setRgbControls(rgb, { commit: true });
        return;
      }
      const slider = event.target.closest('[data-profile-field], [data-advanced-field]');
      if (slider?.type === 'range') commitSliderPreview(slider);
      if (event.target.matches('[data-advanced-field="color_mode"]')) {
        const colorMode = event.target.value || 'cct';
        mergeAdvancedDraft({ color_mode: colorMode });
        updateColorModeVisibility(colorMode);
        if (controlMode() === 'manual') livePreviewCustom();
        return;
      }
      const effectInput = event.target.closest('[data-effect-option]');
      if (effectInput) {
        const nextEffect = effectInput.checked ? effectInput.value : 'off';
        mergeAdvancedDraft({ effect: nextEffect });
        updateEffectChoiceUi(nextEffect);
        if (controlMode() === 'manual') livePreviewCustom();
        return;
      }
    });

    ['pointerup', 'mouseup', 'touchend'].forEach((type) => {
      document.addEventListener(type, (event) => {
        const slider = event.target.closest('[data-profile-field], [data-advanced-field]');
        if (slider?.type === 'range') commitSliderPreview(slider);
      }, true);
    });

    document.addEventListener('click', (event) => {
      const swatch = event.target.closest('[data-color-preset]');
      if (swatch) {
        event.preventDefault();
        const rgb = hexToRgb(swatch.dataset.colorPreset);
        if (rgb) setRgbControls(rgb, { commit: true });
        return;
      }
      const advancedButton = event.target.closest('#openLedAdvancedBtn');
      if (advancedButton) {
        event.preventDefault();
        openAdvancedSettings();
        return;
      }
      if (event.target.closest('[data-led-advanced-close]')) {
        event.preventDefault();
        closeAdvancedSettings();
        return;
      }
      if (event.target.closest('#saveLedAdvancedBtn')) {
        event.preventDefault();
        saveAdvancedSettings();
        return;
      }
      const apply = event.target.closest('#applyBtn');
      const reset = event.target.closest('#resetBtn');
      if (apply || reset) {
        event.preventDefault();
        event.stopImmediatePropagation();
        if (apply) {
          if (controlMode() === 'scene') saveCurrentScene();
          else applyCustom();
        } else {
          resetCurrentModeDefault();
        }
      }
    }, true);

    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape') closeAdvancedSettings();
    });

    document.addEventListener('click', (event) => {
      if (event.target.id === 'ledAdvancedSettingsModal') closeAdvancedSettings();
      if (event.target.closest('.mode-card, .small-mode, '.concat('.switch[data-toggle="led"], #smartModeSwitch'))) setTimeout(renderPanel, 0);
    });
  }

  window.DashboardSceneMemory = {
    saveCurrentScene,
    applyCustom,
    resetCurrentModeDefault,
    applySceneProfile: () => renderPanel(),
    clampSceneValue,
    clampSceneState: (payload = {}, mode = sceneMode()) => {
      const next = { ...payload };
      if (Object.prototype.hasOwnProperty.call(next, 'brightness')) next.brightness = clampSceneValue('brightness', next.brightness, mode);
      if (Object.prototype.hasOwnProperty.call(next, 'kelvin')) next.kelvin = clampSceneValue('kelvin', next.kelvin, mode);
      return next;
    },
    getSceneProfile,
    getSceneDefaults: getSceneProfile,
    markLightSliderEdit: () => {},
    clearLightSliderEditGuard: () => {},
    getMemory: () => ({})
  };

  function init() {
    ensurePanel();
    ensureAdvancedModal();
    bindDashboardHooks();
    bindEvents();
    loadProfiles();
    renderPanel();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
