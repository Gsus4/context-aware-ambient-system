// Dashboard 實機版設定｜SmartLight MQTT 對接模式
// ------------------------------------------------------------
// 實機版需要：
// 1. 燈光 MQTT Broker: 192.168.39.100:1883
// 2. 心律/血氧 MQTT Broker: 192.168.69.168:1883
// 3. 環境監測 MQTT Broker: 192.168.69.107:1883
// 4. SmartLight NODE：bedroom01
// 3. FastAPI Backend：smart_space.main:app
// 4. WebSocket：/ws/dashboard
window.DASHBOARD_CONFIG = {
  // dev / mqtt
  // mqtt：實機版，透過 FastAPI Backend → MQTT Broker → SmartLight NODE。
  runtimeMode: 'mqtt',

  // fit：完整顯示；fill-width：優先填滿寬度；fill-height：優先填滿高度
  scaleMode: 'fit',

  // 實機版不使用前端假資料循環。
  // 所有模組資料皆以實機回傳為準；尚未收到數值時，Dashboard 顯示 --。
  useFakeData: false,
  fakeDataIntervalMs: 2200,

  dataSource: {
    mode: 'websocket', // none | rest | websocket

    rest: {
      enabled: false,
      url: '/api/dashboard',
      intervalMs: 1000
    },

    websocket: {
      enabled: true,
      url: `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws/dashboard`
    }
  },

  smartLight: {
    enabled: true,
    runtimeMode: 'mqtt',
    commandUrl: '/api/smartlight/command',
    nodeId: 'bedroom01',
    brokerHost: '192.168.39.100',
    brokerPort: 1883,
    brightness: {
      min: 0,
      max: 100,
      step: 1
    },
    kelvin: {
      min: 2700,
      max: 6500,
      step: 100
    },
    modeMap: {
      vacant: 'vacant',
      sleep: 'sleep',
      relax: 'relax',
      work: 'work',
      exercise: 'exercise'
    },
    sceneProfiles: {
      vacant: { label: '無人', scene: 'vacant', targetLux: null, toleranceLux: null, targetLuxMin: null, targetLuxMax: null, colorMode: 'off', kelvinMin: 2700, kelvinMax: 6500, kelvinDefault: 2700, brightnessMin: 0, brightnessMax: 0, brightnessDefault: 0, minOutput: 0, maxOutput: 0, adaptiveProfile: 'off' },
      sleep: { label: '睡眠', scene: 'sleep', targetLux: 50, toleranceLux: 20, targetLuxMin: 30, targetLuxMax: 70, colorMode: 'cct', kelvinMin: 2700, kelvinMax: 6500, kelvinDefault: 2700, brightnessMin: 0, brightnessMax: 30, brightnessDefault: 30, minOutput: 5, maxOutput: 30, adaptiveProfile: 'slow' },
      relax: { label: '放鬆', scene: 'relax', targetLux: 250, toleranceLux: 50, targetLuxMin: 200, targetLuxMax: 300, colorMode: 'cct', kelvinMin: 2700, kelvinMax: 6500, kelvinDefault: 3000, brightnessMin: 5, brightnessMax: 70, brightnessDefault: 40, minOutput: 5, maxOutput: 70, adaptiveProfile: 'soft' },
      work: { label: '工作', scene: 'work', targetLux: 500, toleranceLux: 75, targetLuxMin: 425, targetLuxMax: 575, colorMode: 'cct', kelvinMin: 2700, kelvinMax: 6500, kelvinDefault: 4500, brightnessMin: 0, brightnessMax: 100, brightnessDefault: 60, minOutput: 0, maxOutput: 100, adaptiveProfile: 'normal' },
      exercise: { label: '運動', scene: 'exercise', targetLux: 400, toleranceLux: 100, targetLuxMin: 300, targetLuxMax: 500, colorMode: 'rgb', kelvinMin: 2700, kelvinMax: 6500, kelvinDefault: 3000, brightnessMin: 10, brightnessMax: 85, brightnessDefault: 60, minOutput: 10, maxOutput: 85, adaptiveProfile: 'rgb_slow', rgb: { r: 255, g: 120, b: 40 } }
    }
  }
};
