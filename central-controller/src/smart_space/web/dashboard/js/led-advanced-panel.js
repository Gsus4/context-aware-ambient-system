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
  let customControlType = 'target_lux_range';
  let isRendering = false;
  let advancedDraft = null;
  let advancedOriginalPayload = null;
  let advancedPreviewTimer = null;
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

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  function advancedModalOpen() {
    return $('#ledAdvancedSettingsModal')?.classList.contains('show') === true;
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
        },
        target_lux_range: {
          label: '\u76ee\u6a19\u7167\u5ea6\u88dc\u5149',
          custom_control_type: 'target_lux_range',
          fixed_brightness_pct: null,
          target_lux: 500,
          tolerance_lux: 75,
          target_lux_min: 425,
          target_lux_max: 575,
          color_mode: 'cct',
          color_temp_k: 3900,
          rgb: { r: 255, g: 160, b: 80 },
          effect: 'static',
          min_output: 0,
          max_output: 100,
          adaptive_profile: 'normal',
          ...(raw?.custom_profiles?.target_lux_range || {})
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
          <div class="led-profile-sub" id="ledProfileModeSub">\u53ea\u8abf\u6574\u76ee\u6a19\u4eae\u5ea6</div>
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

  function sliderMarkup({ label, key, value, min, max, step, unit = '', source = 'profile', disabled = false }) {
    const safeValue = Math.round(clamp(value, min, max));
    const pct = max === min ? 0 : ((safeValue - min) / (max - min)) * 100;
    const attr = source === 'advanced' ? 'data-advanced-field' : 'data-profile-field';
    return `
      <div class="led-slider-field${disabled ? ' is-disabled' : ''}">
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

  function selectField(label, key, value, choices) {
    const options = choices.map((item) => {
      const itemValue = typeof item === 'object' ? item.value : item;
      const itemLabel = typeof item === 'object' ? item.label : item;
      return `<option value="${itemValue}"${String(itemValue) === String(value) ? ' selected' : ''}>${itemLabel}</option>`;
    }).join('');
    return `<label class="led-profile-field"><span>${label}</span><select data-advanced-field="${key}">${options}</select></label>`;
  }

  function sceneProfile() {
    return profiles.scene_profiles[sceneMode()] || profiles.scene_profiles.work;
  }

  function customBaseProfile() {
    return profiles.custom_profiles.target_lux_range || normalizeProfiles(null).custom_profiles.target_lux_range;
  }

  function activeAdvancedProfile() {
    if (advancedDraft) return advancedDraft;
    return profiles.custom_profiles[customControlType] || profiles.custom_profiles.target_lux_range;
  }

  function renderBasicFields() {
    if (smartModeOn()) return '';
    const isScene = controlMode() === 'scene';
    const profile = isScene ? sceneProfile() : customBaseProfile();
    const limit = isScene ? sceneTargetLimit(sceneMode()) : { min: 0, max: 1500, step: 10 };
    const disabled = isScene && limit.enabled === false;
    const fields = [
      disabled
        ? '<div class="led-target-disabled">\u7121\u4eba\u6a21\u5f0f\u4e0d\u9700\u8981\u76ee\u6a19\u4eae\u5ea6</div>'
        : sliderMarkup({
          label: '\u76ee\u6a19\u4eae\u5ea6',
          key: 'target_lux',
          value: profile.target_lux ?? limit.min ?? 0,
          min: limit.min ?? 0,
          max: limit.max ?? 1500,
          step: limit.step ?? 10,
          unit: 'lux'
        })
    ];
    if (!isScene) fields.push('<button type="button" class="led-profile-secondary-action" id="openLedAdvancedBtn">\u9032\u968e\u8a2d\u5b9a</button>');
    return fields.join('');
  }

  function renderAdvancedFields() {
    const profile = activeAdvancedProfile();
    const rgb = profile.rgb || {};
    const isFixed = customControlType === 'fixed_brightness';
    return [
      selectField('\u81ea\u5b9a\u7fa9\u63a7\u5236\u65b9\u5f0f', 'custom_control_type', customControlType, [
        { label: '\u76ee\u6a19\u7167\u5ea6\u88dc\u5149', value: 'target_lux_range' },
        { label: '\u56fa\u5b9a\u4eae\u5ea6', value: 'fixed_brightness' }
      ]),
      ...(isFixed ? [
        sliderMarkup({ label: '\u56fa\u5b9a\u4eae\u5ea6', key: 'fixed_brightness_pct', value: profile.fixed_brightness_pct ?? 64, min: 0, max: 100, step: 1, unit: '%', source: 'advanced' }),
        sliderMarkup({ label: '\u547c\u5438\u5f37\u5ea6', key: 'breathing_strength', value: profile.breathing_strength ?? 50, min: 0, max: 100, step: 1, unit: '%', source: 'advanced' }),
        sliderMarkup({ label: '\u6d41\u6c34\u4eae\u5ea6', key: 'flow_brightness', value: profile.flow_brightness ?? profile.fixed_brightness_pct ?? 64, min: 0, max: 100, step: 1, unit: '%', source: 'advanced' }),
        selectField('\u547c\u5438\u901f\u5ea6', 'breathing_speed', profile.breathing_speed || 'slow', [
          { label: '\u6162', value: 'slow' },
          { label: '\u4e2d', value: 'medium' },
          { label: '\u5feb', value: 'fast' }
        ]),
        selectField('\u6d41\u6c34\u6a23\u5f0f', 'flow_preset', profile.flow_preset || 'soft_rainbow', [
          { label: '\u67d4\u548c\u5f69\u8679', value: 'soft_rainbow' },
          { label: '\u6696\u8272\u6ce2\u6d6a', value: 'warm_wave' },
          { label: '\u5c08\u6ce8\u6383\u63cf', value: 'focus_scan' },
          { label: '\u6975\u5149', value: 'aurora' }
        ]),
        selectField('\u6d41\u6c34\u901f\u5ea6', 'flow_speed', profile.flow_speed || 'slow', [
          { label: '\u6162', value: 'slow' },
          { label: '\u4e2d', value: 'medium' },
          { label: '\u5feb', value: 'fast' }
        ])
      ] : [
        sliderMarkup({ label: '\u76ee\u6a19\u4eae\u5ea6', key: 'target_lux', value: profile.target_lux ?? 500, min: 0, max: 1500, step: 10, unit: 'lux', source: 'advanced' }),
        sliderMarkup({ label: '\u5bb9\u8a31\u7bc4\u570d', key: 'tolerance_lux', value: profile.tolerance_lux ?? 75, min: 0, max: 300, step: 10, unit: 'lux', source: 'advanced' }),
        sliderMarkup({ label: '\u6700\u4f4e\u8f38\u51fa', key: 'min_output', value: profile.min_output ?? 0, min: 0, max: 100, step: 1, unit: '%', source: 'advanced' }),
        sliderMarkup({ label: '\u6700\u9ad8\u8f38\u51fa', key: 'max_output', value: profile.max_output ?? 100, min: 0, max: 100, step: 1, unit: '%', source: 'advanced' })
      ]),
      selectField('\u8272\u5f69\u6a21\u5f0f', 'color_mode', profile.color_mode || 'cct', [
        { label: '\u8272\u6eab', value: 'cct' },
        { label: 'RGB \u984f\u8272', value: 'rgb' }
      ]),
      sliderMarkup({ label: '\u8272\u6eab', key: 'color_temp_k', value: profile.color_temp_k ?? 3900, min: 2700, max: 6500, step: 100, unit: 'K', source: 'advanced' }),
      sliderMarkup({ label: 'RGB \u7d05\u8272', key: 'rgb_r', value: rgb.r ?? 255, min: 0, max: 255, step: 1, source: 'advanced' }),
      sliderMarkup({ label: 'RGB \u7da0\u8272', key: 'rgb_g', value: rgb.g ?? 160, min: 0, max: 255, step: 1, source: 'advanced' }),
      sliderMarkup({ label: 'RGB \u85cd\u8272', key: 'rgb_b', value: rgb.b ?? 80, min: 0, max: 255, step: 1, source: 'advanced' }),
      selectField('\u71c8\u6548', 'effect', profile.effect || 'static', [
        { label: '\u95dc\u9589', value: 'off' },
        { label: '\u975c\u614b', value: 'static' },
        { label: '\u547c\u5438', value: 'breathing' },
        { label: '\u6d41\u6c34', value: 'flow' }
      ])
    ].join('');
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
    document.body.classList.toggle('led-custom-target-mode', mode === 'manual' && customControlType === 'target_lux_range');
    document.body.classList.toggle('led-custom-fixed-mode', mode === 'manual' && customControlType === 'fixed_brightness');
    document.body.classList.toggle('led-profile-smart-locked', smartModeOn());
    if ($('#ledProfileModeTitle')) $('#ledProfileModeTitle').textContent = isScene ? '\u60c5\u5883\u8a2d\u5b9a' : '\u81ea\u5b9a\u7fa9\u8a2d\u5b9a';
    if ($('#ledProfileModeSub')) {
      $('#ledProfileModeSub').textContent = smartModeOn()
        ? '\u667a\u6167\u6a21\u5f0f\u958b\u555f\u4e2d\uff0c\u624b\u52d5 LED \u63a7\u5236\u5df2\u505c\u7528'
        : (isScene ? '\u8abf\u6574\u76ee\u6a19\u4eae\u5ea6\uff0c\u6309\u5957\u7528\u8a2d\u5b9a\u5132\u5b58\u504f\u597d' : '\u653e\u958b\u6ed1\u584a\u6703\u7acb\u5373\u9810\u89bd\u71c8\u5149');
    }
    if (fields) fields.innerHTML = renderBasicFields();
    if ($('#ledAdvancedFields') && !advancedModalOpen()) $('#ledAdvancedFields').innerHTML = renderAdvancedFields();
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
      if (['custom_control_type', 'color_mode', 'effect', 'breathing_speed', 'flow_preset', 'flow_speed'].includes(key)) {
        patch[key] = input.value;
        return;
      }
      const value = input.value === '' ? null : Number(input.value);
      patch[key] = Number.isFinite(value) ? Math.round(value) : null;
      if (['rgb_r', 'rgb_g', 'rgb_b'].includes(key)) hasRgbFields = true;
    });
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

  function defaultCustomProfile(type) {
    return clone(normalizeProfiles(null).custom_profiles[type] || normalizeProfiles(null).custom_profiles.target_lux_range);
  }

  function storedCustomProfile(type) {
    return clone(profiles.custom_profiles[type] || defaultCustomProfile(type));
  }

  function currentCustomPayload() {
    const livePatch = collectAdvancedPatch();
    const baseTarget = customBaseProfile();
    const fixed = advancedDraft?.custom_control_type === 'fixed_brightness'
      ? advancedDraft
      : storedCustomProfile('fixed_brightness');
    const target = advancedDraft?.custom_control_type === 'target_lux_range'
      ? advancedDraft
      : storedCustomProfile('target_lux_range');
    if (customControlType === 'fixed_brightness') {
      return {
        ...fixed,
        ...livePatch,
        custom_control_type: 'fixed_brightness',
        fixed_brightness_pct: livePatch.fixed_brightness_pct ?? fixed.fixed_brightness_pct ?? 64,
        target_lux: null,
        tolerance_lux: null,
        target_lux_min: null,
        target_lux_max: null
      };
    }
    const payload = {
      ...target,
      ...livePatch,
      custom_control_type: 'target_lux_range',
      fixed_brightness_pct: null
    };
    if (!advancedModalOpen()) return { ...payload, ...collectBasicPatch(baseTarget) };
    const range = rangeFrom(payload, payload.tolerance_lux ?? 75);
    return { ...payload, target_lux_min: range.min, target_lux_max: range.max };
  }

  function syncAdvancedDraftFromFields() {
    if (!advancedModalOpen()) return;
    advancedDraft = clone(currentCustomPayload());
  }

  function sendDashboardCommand(action, payload, options = {}) {
    if (window.DashboardDataAdapter?.sendDashboardCommand) {
      window.DashboardDataAdapter.sendDashboardCommand(action, payload, options);
      return;
    }
    fetch(window.DASHBOARD_CONFIG?.smartLight?.commandUrl || '/api/smartlight/command', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action, ...payload, silent: options.silent === true })
    }).catch((error) => console.warn('[LED profile panel] command failed:', error.message));
  }

  function livePreviewCustom(options = {}) {
    if (smartModeOn() || controlMode() !== 'manual') return;
    const payload = currentCustomPayload();
    sendDashboardCommand('set_custom_light', payload, { silent: options.silent === true });
    if (options.notify !== false) {
      toast(payload.custom_control_type === 'target_lux_range'
        ? '\u5df2\u9001\u51fa\u76ee\u6a19\u7167\u5ea6\u9810\u89bd\uff0c\u786c\u9ad4\u652f\u63f4\u5f85\u78ba\u8a8d'
        : '\u5df2\u5957\u7528\u76ee\u524d\u71c8\u5149\u9810\u89bd');
    }
  }

  function scheduleAdvancedPreview() {
    if (!advancedModalOpen() || controlMode() !== 'manual') return;
    syncAdvancedDraftFromFields();
    clearTimeout(advancedPreviewTimer);
    advancedPreviewTimer = setTimeout(() => {
      advancedPreviewTimer = null;
      livePreviewCustom({ silent: true, notify: false });
    }, 80);
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
    const effectSelect = $('[data-advanced-field="effect"]');
    if (['rgb_r', 'rgb_g', 'rgb_b'].includes(key) && colorModeSelect) colorModeSelect.value = 'rgb';
    if (key === 'color_temp_k' && colorModeSelect) colorModeSelect.value = 'cct';
    if (key === 'flow_brightness' && effectSelect) effectSelect.value = 'flow';
    if (key === 'breathing_strength' && effectSelect) effectSelect.value = 'breathing';
    if (slider.dataset.advancedField) syncAdvancedDraftFromFields();
    if (slider.dataset.profileField && controlMode() === 'scene') {
      const scene = sceneMode();
      profiles.scene_profiles[scene] = {
        ...profiles.scene_profiles[scene],
        ...collectBasicPatch(sceneProfile())
      };
      toast('\u5df2\u66f4\u65b0\u76ee\u6a19\u4eae\u5ea6\uff0c\u6309\u5957\u7528\u8a2d\u5b9a\u53ef\u5132\u5b58\u504f\u597d');
      return;
    }
    if (controlMode() === 'manual') livePreviewCustom({ silent: advancedModalOpen(), notify: !advancedModalOpen() });
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
      await postJson(apiBase, { section: 'custom_profiles', key: payload.custom_control_type, profile: payload });
      advancedDraft = null;
      advancedOriginalPayload = null;
      addEvent('<strong>\u81ea\u5b9a\u7fa9\uff1a</strong>\u5df2\u5132\u5b58\u4f7f\u7528\u8005\u504f\u597d', 'tap', 'var(--blue)');
      toast('\u5df2\u5132\u5b58\u4f7f\u7528\u8005\u504f\u597d');
      renderPanel();
    } catch (error) {
      toast(`\u81ea\u5b9a\u7fa9\u504f\u597d\u5132\u5b58\u5931\u6557\uff1a${error.message}`);
    }
  }

  async function saveAdvancedSettings() {
    try {
      syncAdvancedDraftFromFields();
      const payload = advancedDraft || currentCustomPayload();
      customControlType = payload.custom_control_type || customControlType;
      if (payload.custom_control_type === 'fixed_brightness') {
        payload.target_lux = null;
        payload.tolerance_lux = null;
        payload.target_lux_min = null;
        payload.target_lux_max = null;
      } else {
        payload.fixed_brightness_pct = null;
        const range = rangeFrom(payload, payload.tolerance_lux ?? 75);
        payload.target_lux_min = range.min;
        payload.target_lux_max = range.max;
      }
      await postJson(apiBase, { section: 'custom_profiles', key: customControlType, profile: payload });
      clearTimeout(advancedPreviewTimer);
      advancedPreviewTimer = null;
      sendDashboardCommand('set_custom_light', payload, { silent: true });
      advancedDraft = null;
      advancedOriginalPayload = null;
      closeAdvancedSettings({ restore: false });
      toast('\u5df2\u5132\u5b58\u4f7f\u7528\u8005\u504f\u597d');
      renderPanel();
    } catch (error) {
      toast(`\u9032\u968e\u8a2d\u5b9a\u5132\u5b58\u5931\u6557\uff1a${error.message}`);
    }
  }

  async function resetCurrentModeDefault() {
    const mode = controlMode();
    try {
      if (mode === 'scene') {
        await postJson(`${apiBase}/reset`, { section: 'scene_profiles', key: sceneMode() });
      } else {
        await postJson(`${apiBase}/reset`, { section: 'custom_profiles', key: 'target_lux_range' });
        await postJson(`${apiBase}/reset`, { section: 'custom_profiles', key: 'fixed_brightness' });
      }
      toast('\u5df2\u6062\u5fa9\u9810\u8a2d\u503c');
      renderPanel();
      if (mode === 'manual') setTimeout(() => livePreviewCustom(), 0);
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
    customControlType = customControlType || 'target_lux_range';
    $('#ledAdvancedFields').innerHTML = renderAdvancedFields();
    advancedOriginalPayload = clone(currentCustomPayload());
    advancedDraft = clone(advancedOriginalPayload);
    modal.classList.add('show');
    modal.setAttribute('aria-hidden', 'false');
  }

  function closeAdvancedSettings(options = {}) {
    const modal = $('#ledAdvancedSettingsModal');
    if (!modal) return;
    clearTimeout(advancedPreviewTimer);
    advancedPreviewTimer = null;
    if (options.restore !== false && advancedOriginalPayload && controlMode() === 'manual' && !smartModeOn()) {
      customControlType = advancedOriginalPayload.custom_control_type || customControlType;
      sendDashboardCommand('set_custom_light', advancedOriginalPayload, { silent: true });
      toast('\u5df2\u53d6\u6d88\u9032\u968e\u8a2d\u5b9a\uff0c\u71c8\u5149\u5df2\u56de\u5fa9\u8abf\u6574\u524d\u72c0\u614b');
    }
    advancedDraft = null;
    advancedOriginalPayload = null;
    modal.classList.remove('show');
    modal.setAttribute('aria-hidden', 'true');
    renderPanel();
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
      const slider = event.target.closest('[data-profile-field], [data-advanced-field]');
      if (slider?.type === 'range') {
        updateSliderUi(slider);
        const key = slider.dataset.advancedField || '';
        const colorModeSelect = $('[data-advanced-field="color_mode"]');
        if (['rgb_r', 'rgb_g', 'rgb_b'].includes(key) && colorModeSelect) colorModeSelect.value = 'rgb';
        if (key === 'color_temp_k' && colorModeSelect) colorModeSelect.value = 'cct';
        if (slider.dataset.advancedField) scheduleAdvancedPreview();
      }
    });

    document.addEventListener('change', (event) => {
      const slider = event.target.closest('[data-profile-field], [data-advanced-field]');
      if (slider?.type === 'range') commitSliderPreview(slider);
      if (event.target.matches('[data-advanced-field="custom_control_type"]')) {
        customControlType = event.target.value || 'target_lux_range';
        advancedDraft = {
          ...storedCustomProfile(customControlType),
          color_mode: advancedDraft?.color_mode || storedCustomProfile(customControlType).color_mode,
          color_temp_k: advancedDraft?.color_temp_k ?? storedCustomProfile(customControlType).color_temp_k,
          rgb: advancedDraft?.rgb || storedCustomProfile(customControlType).rgb,
          effect: advancedDraft?.effect || storedCustomProfile(customControlType).effect,
          custom_control_type: customControlType
        };
        $('#ledAdvancedFields').innerHTML = renderAdvancedFields();
        scheduleAdvancedPreview();
        return;
      }
      if (event.target.matches('[data-advanced-field]')) {
        syncAdvancedDraftFromFields();
        scheduleAdvancedPreview();
      }
    });

    ['pointerup', 'mouseup', 'touchend'].forEach((type) => {
      document.addEventListener(type, (event) => {
        const slider = event.target.closest('[data-profile-field], [data-advanced-field]');
        if (slider?.type === 'range') commitSliderPreview(slider);
      }, true);
    });

    document.addEventListener('click', (event) => {
      const advancedButton = event.target.closest('#openLedAdvancedBtn');
      if (advancedButton) {
        event.preventDefault();
        openAdvancedSettings();
        return;
      }
      if (event.target.closest('[data-led-advanced-close]')) {
        event.preventDefault();
        closeAdvancedSettings({ restore: true });
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
      if (event.key === 'Escape') closeAdvancedSettings({ restore: true });
    });

    document.addEventListener('click', (event) => {
      if (event.target.id === 'ledAdvancedSettingsModal') closeAdvancedSettings({ restore: true });
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
