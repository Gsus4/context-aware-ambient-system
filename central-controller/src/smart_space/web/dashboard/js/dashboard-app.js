'use strict';
const iconSvg={home:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 11.5 12 4l9 7.5"/><path d="M5 10.5V21h5v-6h4v6h5V10.5"/><path d="M4 21h16"/></svg>',clock:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>',calendar:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="4" y="5" width="16" height="15" rx="2"/><path d="M16 3v4M8 3v4M4 10h16"/></svg>',relax:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6 12c0-3 2-5 6-5s6 2 6 5"/><path d="M5 12h14l-1.5 7h-11z"/><path d="M7 19v2M17 19v2"/><path d="M9 7c-1.5-2-3-2-5-1M15 7c1.5-2 3-2 5-1"/></svg>',leaf:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 4C10 4 5 9 5 19c7 0 14-6 15-15Z"/><path d="M5 19c4-5 8-8 15-15"/></svg>',heartPulse:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.6l-1-1a5.5 5.5 0 0 0-7.8 7.8l1 1L12 21l4.2-4.1"/><path d="M3 13h4l2-4 3 8 2-4h3"/></svg>',heart:'<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 21s-7.5-4.5-9.6-9A5.7 5.7 0 0 1 12 5.6 5.7 5.7 0 0 1 21.6 12C19.5 16.5 12 21 12 21Z"/></svg>',drop:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3s6 6.2 6 11a6 6 0 0 1-12 0c0-4.8 6-11 6-11Z"/><path d="M9 15a3 3 0 0 0 4 2.8"/></svg>',fan:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="2"/><path d="M12 10c1.7-4.4 4.3-6 6.4-4.6 2.3 1.5 1.2 5.6-3.8 6.6"/><path d="M13.7 13c4.6.7 6.8 2.8 6.3 5.3-.5 2.8-4.8 2.9-7.1-1.6"/><path d="M10.3 13C7.4 16.6 4.4 17.3 2.8 15.4 1 13.2 3.7 9.8 8.6 11.4"/></svg>',sun:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>',wifi:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><path d="M5 12.5a10 10 0 0 1 14 0"/><path d="M8.5 16a5 5 0 0 1 7 0"/><path d="M12 20h.01"/></svg>',sliders:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 7h16M4 17h16"/><circle cx="8" cy="7" r="2"/><circle cx="16" cy="17" r="2"/></svg>',bot:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="8" width="14" height="10" rx="3"/><path d="M12 8V5"/><path d="M9 12h.01M15 12h.01"/><path d="M8 18v2M16 18v2"/><path d="M3 13h2M19 13h2"/></svg>',tap:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 11V6a2 2 0 1 1 4 0v8"/><path d="M13 10a2 2 0 1 1 4 0v5"/><path d="M17 12a2 2 0 1 1 4 0v4c0 3-2 5-5 5h-4c-2 0-3.4-1-4.5-2.7L5 14.5a1.8 1.8 0 0 1 3-2l1 1.3"/></svg>',book:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 5.5A3.5 3.5 0 0 1 7.5 2H20v17H7.5A3.5 3.5 0 0 0 4 22z"/><path d="M4 5.5V22"/><path d="M10 6h6"/></svg>',moon:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 15.5A8.5 8.5 0 0 1 8.5 4a7 7 0 1 0 11.5 11.5Z"/><path d="M16 4h.01M19 7h.01"/></svg>',gear:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1-2 3.4-.2-.1a1.6 1.6 0 0 0-1.8-.3 1.6 1.6 0 0 0-1 1.5V22h-4v-.6a1.6 1.6 0 0 0-1-1.5 1.6 1.6 0 0 0-1.8.3l-.2.1-2-3.4.1-.1a1.6 1.6 0 0 0 .3-1.8 1.6 1.6 0 0 0-1.5-1H4v-4h.6a1.6 1.6 0 0 0 1.5-1 1.6 1.6 0 0 0-.3-1.8l-.1-.1 2-3.4.2.1a1.6 1.6 0 0 0 1.8.3 1.6 1.6 0 0 0 1-1.5V2h4v.6a1.6 1.6 0 0 0 1 1.5 1.6 1.6 0 0 0 1.8-.3l.2-.1 2 3.4-.1.1a1.6 1.6 0 0 0-.3 1.8 1.6 1.6 0 0 0 1.5 1h.6v4h-.6a1.6 1.6 0 0 0-1.5 1Z"/></svg>',bulb:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18h6"/><path d="M10 22h4"/><path d="M8 14a6 6 0 1 1 8 0c-1 1-1.5 2-1.5 4h-5c0-2-.5-3-1.5-4Z"/></svg>',brightness:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="3"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>',temperature:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M14 14.76V5a2 2 0 1 0-4 0v9.76A4 4 0 1 0 14 14.76Z"/></svg>',thermometer:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><path d="M14 14.76V5a2 2 0 1 0-4 0v9.76A4 4 0 1 0 14 14.76Z"/><path d="M12 17v.01"/></svg>',person:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="5" r="3"/><path d="M12 8v13M8 12h8M9 21l3-8 3 8"/></svg>',device:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2"/><path d="M8 16h8M8 12h4"/></svg>'};
document.querySelectorAll('[data-icon]').forEach(el=>{const i=el.dataset.icon;if(iconSvg[i])el.innerHTML=iconSvg[i]});
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
const modes={vacant:{name:'無人',sub:'離席・關閉・節能',brightness:0,kelvin:2700,fanSpeed:0,color:'#2d3748',text:'無人情境會關閉 LED 輸出。'},sleep:{name:'睡眠',sub:'低照度・暖色・緩慢',brightness:30,kelvin:2700,fanSpeed:3,color:'#ff9463',text:'睡眠情境使用 50 lux 目標照度與 20 lux 容許誤差。'},relax:{name:'放鬆',sub:'舒適・柔和・呼吸',brightness:40,kelvin:3000,fanSpeed:6,color:'#ff8a5c',text:'放鬆情境使用 250 lux 目標照度與 50 lux 容許誤差。'},work:{name:'工作',sub:'明亮・清晰・穩定',brightness:60,kelvin:4500,fanSpeed:5,color:'#fff2a8',text:'工作情境使用 500 lux 目標照度與 75 lux 容許誤差。'},exercise:{name:'運動',sub:'暖色・活力・律動',brightness:60,kelvin:3000,fanSpeed:7,color:'#ff7828',text:'運動情境使用 400 lux 目標照度與 100 lux 容許誤差。'},custom:{name:'自定義模式',sub:'自定義・進階・個人化',brightness:64,kelvin:3900,fanSpeed:5,color:'#9b5cff',text:'目前為自定義模式，可在燈光進階設定中調整 LED 顏色、亮度、呼吸燈與流水燈。'}};
const state={uiState:{controlMode:'scene',emotionMode:'work',smartModeEnabled:true},sensorData:{heartRate:null,spo2:null,aqi:null,temperature:null,humidity:null,comfort:null,pirDetected:null,pirTime:null,onlineDevices:0,totalDevices:5,systemStatus:'等待實機資料',moduleStatus:null,deviceConnectionText:'',cameraSnapshotUrl:null,cameraHlsUrl:null,cameraWebrtcUrl:null,cameraWhepUrl:null,cameraRtspUrl:null,cameraStreamPath:null},deviceState:{ledOn:null,brightness:null,kelvin:null,ledColor:'#2d3748',fanOn:null,fanSpeed:null,breathingEnabled:false,breathingSpeed:'slow',breathingStrength:'medium',flowEnabled:false,flowPreset:'soft_rainbow',flowSpeed:'slow',flowBrightness:null},audioState:{connected:null,playing:false,state:'STOPPED',mode:null,scene:null,autoModeEnabled:true,volume:null,track:null,trackPath:null,outputDevice:null,progressSec:null,durationSec:null,trackIndex:0},trends:{heart:[],spo2:[],aqi:[],temp:[],humidity:[]},weeklyTrends:{heart:[],spo2:[],aqi:[],temp:[],humidity:[]},events:[]};
const clamp=(n,min,max)=>Math.min(Math.max(n,min),max),rw=(v,min,max,s)=>clamp(v+(Math.random()*2-1)*s,min,max),pad=n=>String(n).padStart(2,'0'),time=d=>`${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`,dateText=d=>`${d.getFullYear()}/${pad(d.getMonth()+1)}/${pad(d.getDate())}（${['日','一','二','三','四','五','六'][d.getDay()]}）`;
const audioPlaylist=[
  {track:'Lo-fi 晚間放鬆歌單 － Soft Room Ambience',mode:'環境音樂',durationSec:225},
  {track:'Rainy Night Jazz － 深夜雨聲爵士',mode:'放鬆音樂',durationSec:248},
  {track:'Focus Beats － 專注低頻節奏',mode:'專注音樂',durationSec:210},
  {track:'Ocean Sleep － 海浪助眠白噪音',mode:'睡眠音樂',durationSec:300}
];

const TREND_HISTORY_KEY='smartDashboard.weeklyTrendHistory.v1';
const WEEK_MS=7*24*60*60*1000;
const TREND_LIMITS={heart:{min:30,max:220},spo2:{min:70,max:100},aqi:{min:0,max:1000},temp:{min:-10,max:60},humidity:{min:0,max:100}};
const TREND_META={heart:{label:'心率',unit:' bpm',min:0,max:150,color:'rgb(255,92,117)'},spo2:{label:'血氧',unit:' %',min:80,max:100,color:'rgb(53,169,255)'},aqi:{label:'Raw Value',unit:'',min:0,max:500,color:'rgb(66,232,138)'}};
let trendHistorySaveTimer=null;
function emptyTrendHistory(){return{heart:[],spo2:[],aqi:[],temp:[],humidity:[]}}
function readTrendHistory(){try{const raw=JSON.parse(localStorage.getItem(TREND_HISTORY_KEY)||'{}');const out=emptyTrendHistory();Object.keys(out).forEach(k=>{out[k]=Array.isArray(raw[k])?raw[k].filter(p=>p&&Number.isFinite(Number(p.ts))&&isTrendValueValid(k,p.v)).map(p=>({ts:Number(p.ts),v:+Number(p.v).toFixed(1)})):[]});return out}catch(e){return emptyTrendHistory()}}
function saveTrendHistory(){try{localStorage.setItem(TREND_HISTORY_KEY,JSON.stringify(state.weeklyTrends))}catch(e){console.warn('[Dashboard] 一週趨勢紀錄儲存失敗：',e)}}
function scheduleTrendHistorySave(){clearTimeout(trendHistorySaveTimer);trendHistorySaveTimer=setTimeout(saveTrendHistory,350)}
function isTrendValueValid(k,v){const n=Number(v),r=TREND_LIMITS[k];return Number.isFinite(n)&&(!r||(n>=r.min&&n<=r.max))}
function pruneTrendHistory(){const cutoff=Date.now()-WEEK_MS;Object.keys(state.weeklyTrends).forEach(k=>{state.weeklyTrends[k]=(state.weeklyTrends[k]||[]).filter(p=>p&&p.ts>=cutoff&&isTrendValueValid(k,p.v)).slice(-2500)})}
function dailyKey(ts){const d=new Date(ts);return `${d.getFullYear()}/${pad(d.getMonth()+1)}/${pad(d.getDate())}`}
function statOfPoints(points){const values=(points||[]).map(p=>Number(p.v)).filter(Number.isFinite);if(!values.length)return null;const sum=values.reduce((a,b)=>a+b,0);return{count:values.length,avg:sum/values.length,min:Math.min(...values),max:Math.max(...values)}}
function fmtStat(stat,unit=''){if(!stat)return'--';return `${Math.round(stat.avg)}${unit} / ${Math.round(stat.min)} / ${Math.round(stat.max)}`}
function fmtAudio(sec){sec=Math.max(0,Math.floor(sec||0));return `${pad(Math.floor(sec/60))}:${pad(sec%60)}`}

const MODULE_DEFS=[
  {key:'led',label:'LED系統'},
  {key:'wearable',label:'穿戴式手環'},
  {key:'envComfort',label:'環境舒適度/節能控制節點'},
  {key:'audio',label:'音樂播放服務'},
  {key:'camera',label:'空間影像串流'}
];
function moduleAlias(key){if(['heart','spo2','wearable'].includes(key))return 'wearable';if(['aqi','tempHumidity','pir','fan','env','envComfort'].includes(key))return 'envComfort';if(['audio','music'].includes(key))return 'audio';return key}
function hasValue(v){return v!==null&&v!==undefined&&v!==''&&!(typeof v==='number'&&!Number.isFinite(v));}
function nval(v,dec=0){if(!hasValue(v))return'--';const n=Number(v);if(!Number.isFinite(n))return'--';return dec>0?n.toFixed(dec):String(Math.round(n));}
function textOrDash(v){return hasValue(v)?String(v):'--'}
function audioFileName(a){const raw=a?.track||a?.trackPath;if(!hasValue(raw))return'--';return String(raw).split(/[\\/]/).filter(Boolean).pop()||String(raw)}
function comfortSummary(v){if(!hasValue(v))return{value:'--',unit:''};if(typeof v==='object'){const score=v.score??v.value??v.comfort??v.comfort_score??v.comfortScore;const label=v.label??v.text??v.state??v.comfort_label??v.comfortLabel;const n=Number(score);if(Number.isFinite(n))return{value:String(Math.round(n)),unit:'/100'};if(hasValue(label))return{value:String(label),unit:''};return{value:'--',unit:''}}const n=Number(v);if(Number.isFinite(n))return{value:String(Math.round(n)),unit:'/100'};return{value:String(v),unit:''}}
function statusFromBool(v){if(v===true||v==='online'||v==='connected')return'online';return'offline'}
function moduleMap(){const raw=state.sensorData.moduleStatus||state.sensorData.deviceConnections||{};const map={};const mergeItem=(key,item)=>{const alias=moduleAlias(key);if(!MODULE_DEFS.some(def=>def.key===alias))return;const normalized=item&&typeof item==='object'?{...item,status:statusFromBool(item.status||item.online)}:{key:alias,status:statusFromBool(item)};const prev=map[alias];const online=(prev?.status==='online')||(normalized.status==='online');map[alias]={...prev,...normalized,key:alias,status:online?'online':(normalized.status||prev?.status||'offline')}};if(Array.isArray(raw)){raw.forEach(item=>{if(!item)return;const key=item.key||item.module;if(key)mergeItem(key,item)})}else if(raw&&typeof raw==='object'){Object.entries(raw).forEach(([key,item])=>mergeItem(key,item))}return map}
function renderModuleStatus(){
  const map=moduleMap();
  let online=0;
  MODULE_DEFS.forEach(def=>{
    const item=map[def.key]||{status:'offline'};
    const status=statusFromBool(item.status||item.online);
    if(status==='online')online++;
    const el=document.querySelector(`.module-item[data-module="${def.key}"]`);
    if(el){
      el.classList.toggle('is-online',status==='online');
      el.classList.toggle('is-offline',status!=='online');
      el.classList.remove('is-unknown');
      const deviceName=item.label||item.name||def.label;
      const connectionText=status==='online'?'已連線':'未連線';
      el.setAttribute('aria-label',`${deviceName}：${connectionText}`);
      el.setAttribute('data-tooltip',deviceName);
      el.setAttribute('data-device-name',deviceName);
      el.title=deviceName;
      const em=el.querySelector('em');
      if(em)em.textContent='';
    }
  });
  state.sensorData.onlineDevices=online;
  state.sensorData.totalDevices=MODULE_DEFS.length;
  if($('#moduleStatusSmall'))$('#moduleStatusSmall').textContent=`${online}/${MODULE_DEFS.length} 模組連線`;
  if($('#moduleStatusSummary'))$('#moduleStatusSummary').textContent='';
  return{online,total:MODULE_DEFS.length,map}
}
function isModuleOnline(modules,key){const item=modules?.map?.[key]||{};return statusFromBool(item.status||item.online)==='online'}
function setControlLocked(el,locked){
  if(!el)return;
  el.classList.toggle('module-control-locked',locked);
  el.classList.toggle('disabled',locked);
  el.setAttribute('aria-disabled',String(locked));
  if('disabled' in el)el.disabled=locked;
  if(locked)el.blur?.();
}
function setManyLocked(selector,locked){$$(selector).forEach(el=>setControlLocked(el,locked))}
function applyModuleControlLocks(modules){
  const ledOnline=isModuleOnline(modules,'led');
  const fanOnline=isModuleOnline(modules,'envComfort');
  const audioOnline=isModuleOnline(modules,'audio');
  const cameraOnline=isModuleOnline(modules,'camera');
  const d=state.deviceState||{};
  const smartOn=state.uiState.smartModeEnabled!==false;
  const controlMode=state.uiState.controlMode;
  const ledOn=d.ledOn===true;
  const scene=controlMode==='scene';
  const manual=controlMode==='manual';

  document.body.classList.toggle('led-module-offline',!ledOnline);
  document.body.classList.toggle('fan-module-offline',!fanOnline);
  document.body.classList.toggle('audio-module-offline',!audioOnline);
  document.body.classList.toggle('camera-module-offline',!cameraOnline);

  const smartLocked=smartOn;
  setManyLocked('.mode-card, .switch[data-toggle="led"], #applyBtn, #resetBtn',!ledOnline||smartLocked);
  $$('.small-mode').forEach(el=>setControlLocked(el,!ledOnline||!scene||smartLocked));
  setControlLocked($('#brightnessRange'),!ledOnline||!ledOn||!(scene||manual)||smartLocked);
  setControlLocked($('#kelvinRange'),!ledOnline||!ledOn||!(scene||manual)||smartLocked);
  setControlLocked($('#openAdvancedLightBtn'),!ledOnline||!ledOn||!manual||smartLocked);
  setManyLocked('.color-dot, .advanced-color-dot, #advancedColorPicker, #advancedBrightnessRange, #breathingEnabled, #breathingSpeed, #breathingStrength, #flowEnabled, #flowPreset, #flowSpeed, #applyAdvancedLightBtn',!ledOnline||!ledOn||!manual||smartLocked);

  setControlLocked($('#fanRange'),!fanOnline);
  setManyLocked('.audio-btn, #audioVolumeRange',!audioOnline);
  setControlLocked($('#audioProgressRange'),true);
  // 監視器影像入口固定可點擊：未連線或未設定來源時，改由彈窗內顯示狀態說明。
  setControlLocked($('#openCameraBtn'),false);
}

function fanState(s,on=true){s=Math.round(Number(s)||0);if(!on||s<=0)return'關閉';if(s<=3)return'低速';if(s<=7)return'中速';return'高速'}function controlModeName(mode){return {auto:'情境模式',scene:'情境模式',manual:'自定義模式'}[mode]||'情境模式'}function controlModeIcon(mode){return {auto:'relax',scene:'relax',manual:'tap'}[mode]||'relax'}function controlModeAccent(mode){return {auto:'var(--purple)',scene:'var(--purple)',manual:'var(--blue)'}[mode]||'var(--purple)'}function summaryModeInfo(controlMode,m){if(controlMode==='manual')return{name:'自定義模式',sub:'自定義・進階・個人化'};return{name:m.name,sub:m.sub||'情境・環境光自適應'}}function aqiQ(a){a=Number(a);if(!Number.isFinite(a))return'--';return a<=200?'良好':a<=300?'普通':a<=400?'差':'很差'}function kelvinLabel(k){return k<3600?'暖色':k<5000?'自然':'冷色'}function lightTone(k){return k<3600?'暖黃光':k<5000?'自然白光':'冷白光'}function pct(input){const p=(Number(input.value)-Number(input.min))/(Number(input.max)-Number(input.min))*100;input.style.setProperty('--percent',p+'%')}function toast(msg){const t=$('#toast');t.textContent=msg;t.classList.add('show');clearTimeout(toast.timer);toast.timer=setTimeout(()=>t.classList.remove('show'),2300)}function addEvent(text,icon='device',accent='var(--green)'){const now=Date.now(),last=state.events[0];if(last&&last.text===text&&last._ts&&now-last._ts<3000)return;state.events.unshift({_ts:now,time:time(new Date(now)),text,icon,accent});state.events=state.events.slice(0,100);renderEvents();renderEventHistoryModal()}function eventType(icon){return {fan:'風扇',person:'PIR',leaf:'空品',relax:'控制',bulb:'燈光',device:'系統',bot:'自動',tap:'自定義',temperature:'色溫',music:'音樂',alert:'提醒'}[icon]||'事件'}function eventItemHtml(e,full=false){return `<div class="event-item${full?' event-item-full':''}" style="--accent:${e.accent}"><div>${e.time}</div><div class="event-type">${eventType(e.icon)}</div><div class="event-text">${e.text}</div></div>`}function renderEvents(){const list=$('#eventList');if(list)list.innerHTML=state.events.slice(0,3).map(e=>eventItemHtml(e)).join('')}function renderEventHistoryModal(){const list=$('#eventHistoryList'),count=$('#eventHistoryCount');if(count)count.textContent=String(state.events.length);if(!list)return;list.innerHTML=state.events.length?state.events.slice(0,100).map(e=>eventItemHtml(e,true)).join(''):'<div class="history-empty">尚無事件紀錄</div>'}
function render(){
  if(state.uiState.controlMode==='auto')state.uiState.controlMode='scene';
  const m=modes[state.uiState.emotionMode]||modes.relax;
  const s=state.sensorData||{};
  const d=state.deviceState||{};
  const modules=renderModuleStatus();
  applyModuleControlLocks(modules);
  const onlineDevices=modules.online;
  const totalDevices=modules.total;
  const systemStatus=s.systemStatus||(onlineDevices===totalDevices?'正常運作':onlineDevices>0?'部分連線':'等待實機資料');
  const aq=hasValue(s.aqi)?aqiQ(Number(s.aqi)):'--';
  const fanKnown=hasValue(d.fanSpeed)||hasValue(d.fanOn);
  if(fanKnown){d.fanSpeed=clamp(Math.round(Number(d.fanSpeed)||0),0,10);d.fanOn=d.fanOn===true||d.fanSpeed>0;}
  const fs=fanKnown?fanState(d.fanSpeed,d.fanOn):'--';
  const smartOn=state.uiState.smartModeEnabled!==false;
  document.body.classList.toggle('smart-mode-disabled',!smartOn);
  document.body.classList.toggle('smart-mode-enabled',smartOn);
  const sm=smartOn?summaryModeInfo(state.uiState.controlMode,m):{name:'自定義模式',sub:'智慧模式已關閉'};

  $('#summaryMode').textContent=sm.name;
  $('#summaryModeSub').textContent=sm.sub;
  if($('#summaryAqiQuality'))$('#summaryAqiQuality').textContent=aq;
  if($('#summaryAqi'))$('#summaryAqi').textContent=nval(s.aqi);
  if($('#summaryHeart'))$('#summaryHeart').textContent=nval(s.heartRate);
  if($('#summarySpo2'))$('#summarySpo2').textContent=nval(s.spo2);
  $('#summaryFanState').textContent=fs;
  $('#summaryFanSpeed').textContent=nval(d.fanSpeed);
  const comfort=comfortSummary(s.comfort);
  $('#summaryComfort').textContent=comfort.value;
  if($('#summaryComfortUnit'))$('#summaryComfortUnit').textContent=comfort.unit;
  if($('#onlineCount'))$('#onlineCount').textContent=onlineDevices;
  if($('#onlineTotalText'))$('#onlineTotalText').textContent='實機即時判定';
  if($('#systemStatusText'))$('#systemStatusText').textContent=systemStatus;
  const connectionCard=$('.summary-connection-card');
  if(connectionCard){connectionCard.classList.toggle('is-all-online',onlineDevices===totalDevices);connectionCard.classList.toggle('is-partial-online',onlineDevices>0&&onlineDevices<totalDevices);connectionCard.classList.toggle('is-offline',onlineDevices===0);connectionCard.title=(s.deviceConnectionText||'依各類模組是否收到實機資料即時判定');}

  if($('#brightnessLabel'))$('#brightnessLabel').textContent=(state.uiState.controlMode==='manual')?'亮度':'目標亮度';
  $('#brightnessValue').textContent=nval(d.brightness);
  $('#kelvinValue').textContent=nval(d.kelvin);
  $('#fanValue').textContent=nval(d.fanSpeed);
  $('#fanText').textContent=fs;
  $('#roomLightTone').textContent=hasValue(d.ledOn)?(d.ledOn?lightTone(d.kelvin||3500):'燈光關閉'):'--';
  $('#roomBrightness').textContent=nval(d.brightness);
  $('#roomAqiQuality').textContent=aq;
  $('#roomAqi').textContent=nval(s.aqi);
  $('#roomFanState').textContent=fanKnown?(d.fanOn?fs+'運轉':'風扇關閉'):'--';
  $('#roomFanSpeed').textContent=nval(d.fanSpeed);
  if($('#recommendText'))$('#recommendText').textContent=m.text;
  if($('#recommendMode'))$('#recommendMode').textContent=m.name;

  $('#heartValue').textContent=nval(s.heartRate);
  $('#spo2Value').textContent=nval(s.spo2);
  $('#aqiValue').textContent=nval(s.aqi);
  $('#aqiQuality').textContent=aq;
  $('#tempValue').textContent=nval(s.temperature,1);
  $('#humidityValue').textContent=nval(s.humidity);
  if($('#pirStateText'))$('#pirStateText').textContent=hasValue(s.pirDetected)?(s.pirDetected?'有人活動':'無人活動'):'--';
  $('#pirTime').textContent=textOrDash(s.pirTime);

  if($('#controlStatusMode'))$('#controlStatusMode').textContent=smartOn?'智慧模式開啟':'智慧模式關閉';
  const smartModeSwitch=$('#smartModeSwitch');
  if(smartModeSwitch){smartModeSwitch.classList.toggle('on',smartOn);smartModeSwitch.setAttribute('aria-pressed',String(smartOn));smartModeSwitch.title=smartOn?'智慧模式已開啟':'智慧模式已關閉'}
  if($('#controlStatusLed')){const cm=state.uiState.controlMode==='auto'?'scene':state.uiState.controlMode;const ledModeText=cm==='scene'?m.name:controlModeName(cm);const brightnessText=cm==='scene'?'目標 '+nval(d.brightness)+'%':nval(d.brightness)+'%';$('#controlStatusLed').textContent=hasValue(d.ledOn)?(ledModeText+'・'+(d.ledOn?'開啟':'關閉')+'・'+brightnessText):(ledModeText+'・--');}
  if($('#controlStatusFan'))$('#controlStatusFan').textContent=fanKnown?(d.fanOn?(fs+'・'+nval(d.fanSpeed)+'級'):'關閉・0級'):'--';

  renderAudioState();
  updateTrendStats();
  updateGaugeView();
  const br=$('#brightnessRange');if(br&&hasValue(d.brightness))br.value=d.brightness;
  const kr=$('#kelvinRange');if(kr&&hasValue(d.kelvin))kr.value=d.kelvin;
  const fanRange=$('#fanRange');if(fanRange){if(hasValue(d.fanSpeed))fanRange.value=d.fanSpeed;}
  const fanPowerText=$('#fanPowerText');if(fanPowerText)fanPowerText.textContent=fanKnown?(d.fanOn?`${nval(d.fanSpeed)}級運轉`:'0級關閉'):'--';
  [br,kr,fanRange].filter(Boolean).forEach(pct);
  $$('.switch[data-toggle="led"]').forEach(b=>b.classList.toggle('on',d.ledOn===true));
  $$('.small-mode').forEach(b=>b.classList.toggle('active',b.dataset.mode===state.uiState.emotionMode));
  const visibleControlMode=state.uiState.controlMode==='auto'?'scene':state.uiState.controlMode;
  $$('.mode-card').forEach(b=>b.classList.toggle('active',b.dataset.controlMode===visibleControlMode));
  $$('.color-dot').forEach(b=>b.classList.toggle('active',hasValue(d.ledColor)&&b.dataset.color.toLowerCase()===d.ledColor.toLowerCase()));
  document.documentElement.style.setProperty('--led',d.ledOn===true?(d.ledColor||'#ffb75c'):'#2d3748');
  document.documentElement.style.setProperty('--brightness',String(d.ledOn===true&&hasValue(d.brightness)?d.brightness/100:0));
  document.documentElement.style.setProperty('--fan-speed',String(d.fanOn===true&&hasValue(d.fanSpeed)?d.fanSpeed*10:0));
  scheduleDrawAll();
  if($('#trendHistoryModal')?.classList.contains('show'))renderTrendHistoryModal();
}
function ensureAudioWaveBars(wave,count=44){
  let bars=[...wave.querySelectorAll('i')];
  while(bars.length<count){
    const bar=document.createElement('i');
    wave.appendChild(bar);
    bars.push(bar);
  }
  if(bars.length>count){
    bars.slice(count).forEach(bar=>bar.remove());
    bars=bars.slice(0,count);
  }
  wave.style.setProperty('--wave-count',String(count));
  return bars;
}
function randomizeAudioWave(){
  const wave=$('#audioWave');
  if(!wave)return;
  const a=state.audioState||{};
  const bars=ensureAudioWaveBars(wave,44);

  if(document.body.classList.contains('audio-module-offline')||state.audioState?.connected!==true){
    bars.forEach((bar)=>{
      bar.style.setProperty('--wave-h','5%');
      bar.style.opacity='.18';
    });
    return;
  }

  // V10：改為「隨機跳動」音浪。
  // 不再使用 sin/cos 週期波形，避免視覺上變成規律波浪。
  if(!a.playing){
    bars.forEach((bar)=>{
      const h=10+Math.random()*18;
      bar.style.setProperty('--wave-h',h.toFixed(1)+'%');
      bar.style.opacity='.34';
    });
    return;
  }

  const volumeEnergy=clamp((Number(a.volume)||45)/100,0.18,1);
  bars.forEach((bar)=>{
    const current=parseFloat(bar.style.getPropertyValue('--wave-h'))||32;
    const roll=Math.random();
    let target;

    if(roll<0.16){
      target=8+Math.random()*18;       // 低谷：讓部分音柱突然降低
    }else if(roll>0.78){
      target=72+Math.random()*24;      // 高峰：讓部分音柱突然跳高
    }else{
      target=24+Math.random()*48;      // 一般跳動區間
    }

    target=clamp(target*(0.72+volumeEnergy*.34),8,96);
    const mixed=current*.18+target*.82; // 保留少量慣性，但以跳動感為主
    bar.style.setProperty('--wave-h',mixed.toFixed(1)+'%');
    bar.style.opacity=String((.62+Math.random()*.36).toFixed(2));
  });
}

function renderAudioState(){
  const a=state.audioState;if(!a)return;
  const conn=$('#audioConnectionState'),track=$('#musicMarqueeText'),mode=$('#audioModeText'),vol=$('#audioVolumeText'),bar=$('#audioVolumeBar'),volRange=$('#audioVolumeRange'),playIcon=$('#audioPlayPauseIcon'),cur=$('#audioCurrentTime'),dur=$('#audioDurationTime'),prog=$('#audioProgressBar'),progRange=$('#audioProgressRange');
  const connected=a.connected===true;
  const duration=hasValue(a.durationSec)?Math.max(1,Number(a.durationSec)||1):null;
  if(duration!==null&&hasValue(a.progressSec))a.progressSec=clamp(Number(a.progressSec)||0,0,duration);
  const p=duration!==null&&hasValue(a.progressSec)?clamp(a.progressSec/duration,0,1):0;
  if(conn)conn.textContent=connected?'已連線':'未連線';
  const wave=$('#audioWave');if(wave)wave.classList.toggle('playing',connected&&a.playing===true);
  randomizeAudioWave();
  if(track)track.textContent=audioFileName(a);
  if(mode)mode.textContent=textOrDash(a.mode);
  if(vol)vol.textContent=hasValue(a.volume)?Math.round(a.volume)+'%':'--';
  if(bar){const v=hasValue(a.volume)?clamp(a.volume,0,100):0;bar.style.width=v+'%';bar.parentElement.style.setProperty('--audio-volume',v+'%')}
  if(volRange){const v=hasValue(a.volume)?clamp(Number(a.volume)||0,0,100):0;volRange.value=v;volRange.style.setProperty('--percent',v+'%')}
  if(playIcon)playIcon.innerHTML=(connected&&a.playing===true)?'<path d="M7 5h4v14H7zM13 5h4v14h-4z"/>':'<path d="M8 5v14l11-7z"/>';
  if(cur)cur.textContent=hasValue(a.progressSec)?fmtAudio(a.progressSec):'--';
  if(dur)dur.textContent=hasValue(a.durationSec)?fmtAudio(a.durationSec):'--';
  if(prog){prog.style.width=(p*100).toFixed(1)+'%';prog.parentElement.style.setProperty('--audio-progress',(p*100).toFixed(1)+'%')}
  if(progRange){progRange.max=Math.round(duration||1);progRange.value=Math.round(hasValue(a.progressSec)?a.progressSec:0);progRange.style.setProperty('--percent',(p*100).toFixed(1)+'%')}
}
function audioAction(action){if(document.body.classList.contains('audio-module-offline'))return;const a=state.audioState;const seek=(delta)=>{a.progressSec=clamp((Number(a.progressSec)||0)+delta,0,Number(a.durationSec)||0)};const loadTrack=(idx)=>{const item=audioPlaylist[idx];Object.assign(a,{track:item.track,mode:item.mode,durationSec:item.durationSec,progressSec:0,trackIndex:idx,playing:true});addEvent(`<strong>切換歌曲：</strong>${item.track}`,'music','var(--cyan)')};if(action==='toggle'){a.playing=!a.playing;addEvent(a.playing?'<strong>音樂播放：</strong>已開啟':'<strong>音樂播放：</strong>已暫停','music','var(--cyan)')}if(action==='prev')loadTrack((a.trackIndex-1+audioPlaylist.length)%audioPlaylist.length);if(action==='next')loadTrack((a.trackIndex+1)%audioPlaylist.length);render()}
function updateUiState(o){Object.assign(state.uiState,o||{});render()}
function updateSensorData(o){Object.assign(state.sensorData,o);if(hasValue(o.heartRate))push('heart',Number(o.heartRate));if(hasValue(o.spo2))push('spo2',Number(o.spo2));if(hasValue(o.aqi))push('aqi',Number(o.aqi));if(hasValue(o.temperature))push('temp',Number(o.temperature));if(hasValue(o.humidity))push('humidity',Number(o.humidity));render()}function updateDeviceState(o){Object.assign(state.deviceState,o);render()}function updateMode(mode,defaults=false,options={}){if(!modes[mode])return;state.uiState.emotionMode=mode;if(defaults){const m=modes[mode];Object.assign(state.deviceState,{brightness:m.brightness,kelvin:m.kelvin,ledColor:m.color,ledOn:true})}if(!options.silent)addEvent(`<strong>切換情境模式：</strong>${modes[mode].name}`,'relax','var(--purple)');render()}
function clampSceneAdjustment(field,value){if(state.uiState.controlMode==='scene'&&window.DashboardSceneMemory?.clampSceneValue){return window.DashboardSceneMemory.clampSceneValue(field,value,state.uiState.emotionMode)}if(field==='brightness')return clamp(Math.round(Number(value)||0),0,100);if(field==='kelvin')return Math.round(clamp(Number(value)||2700,2700,6500)/100)*100;return value}
function initTrends(){state.weeklyTrends=readTrendHistory();pruneTrendHistory();Object.keys(state.trends).forEach(k=>{state.trends[k]=(state.weeklyTrends[k]||[]).filter(p=>p.ts>=Date.now()-60*60*1000).map(p=>p.v).slice(-36)});saveTrendHistory()}
function push(k,v){if(!state.trends[k])state.trends[k]=[];if(!state.weeklyTrends[k])state.weeklyTrends[k]=[];if(!isTrendValueValid(k,v))return;const n=+Number(v).toFixed(1);state.trends[k].push(n);if(state.trends[k].length>36)state.trends[k].shift();state.weeklyTrends[k].push({ts:Date.now(),v:n});pruneTrendHistory();scheduleTrendHistorySave()}
function avg(arr){return arr.length?Math.round(arr.reduce((a,b)=>a+b,0)/arr.length):null}
function gaugePercent(v,min,max){if(!Number.isFinite(Number(v)))return 0;return clamp((v-min)/(max-min),0,1)}
function setGauge(id,value,min,max){const g=$('#'+id);if(!g)return;const p=gaugePercent(value,min,max),dash=188.5,rot=-90+180*p;g.classList.toggle('module-status-missing',!hasValue(value));g.style.setProperty('--gauge-pct',p.toFixed(3));g.style.setProperty('--gauge-offset',(dash*(1-p)).toFixed(2));g.style.setProperty('--gauge-deg',(180*p).toFixed(1)+'deg');g.style.setProperty('--needle-rot',rot.toFixed(1)+'deg')}
function updateTrendStats(){const hv=avg(state.trends.heart),sv=avg(state.trends.spo2),av=avg(state.trends.aqi);if($('#heartTrendValue'))$('#heartTrendValue').textContent=hasValue(hv)?hv+' bpm':'--';if($('#spo2TrendValue'))$('#spo2TrendValue').textContent=hasValue(sv)?sv+' %':'--';if($('#aqiTrendValue'))$('#aqiTrendValue').textContent=hasValue(av)?'Raw Value '+av:'--';if($('#aqiTrendQuality'))$('#aqiTrendQuality').textContent=hasValue(av)?aqiQ(av):''}
function updateGaugeView(){setGauge('tempGauge',state.sensorData.temperature,0,40);setGauge('humidityGauge',state.sensorData.humidity,0,100)}
function resize(c){
  // Canvas 必須用原始 layout 尺寸計算，不能用 getBoundingClientRect()。
  // Dashboard 外層會用 CSS transform 做等比例縮放；在瀏覽器縮放 500% 時，
  // getBoundingClientRect() 會拿到已縮放後的尺寸，造成 canvas 內的座標與文字被放大。
  const w=Math.max(10,c.clientWidth||c.offsetWidth||10);
  const h=Math.max(10,c.clientHeight||c.offsetHeight||10);
  const dpr=Math.max(1,Math.min(window.devicePixelRatio||1,3));
  const targetW=Math.floor(w*dpr);
  const targetH=Math.floor(h*dpr);
  if(c.width!==targetW)c.width=targetW;
  if(c.height!==targetH)c.height=targetH;
  const ctx=c.getContext('2d');
  ctx.setTransform(dpr,0,0,dpr,0,0);
  return{ctx,w,h}
}
function line(id,data,{min=Math.min(...data),max=Math.max(...data),color='rgb(53,208,228)',axis=false,footer=false,pad={left:2,right:2,top:5,bottom:4}}={}){const c=$('#'+id);if(!c)return;data=Array.isArray(data)?data.filter(v=>Number.isFinite(Number(v))).map(Number):[];const{ctx,w,h}=resize(c),range=Math.max(1,max-min);ctx.clearRect(0,0,w,h);ctx.strokeStyle='rgba(157,177,213,.12)';ctx.lineWidth=1;for(let i=0;i<3;i++){let y=pad.top+(h-pad.top-pad.bottom)*(i/2);ctx.beginPath();ctx.moveTo(pad.left,y);ctx.lineTo(w-pad.right,y);ctx.stroke()}if(axis){ctx.fillStyle='rgba(198,211,234,.68)';ctx.font='11px Inter,sans-serif';ctx.fillText(String(Math.round(max)),2,pad.top+4);ctx.fillText(String(Math.round((max+min)/2)),2,h/2+4);ctx.fillText(String(Math.round(min)),2,h-pad.bottom+4);ctx.fillText('--:--',pad.left,h-5);ctx.fillText('--:--',w/2-13,h-5);ctx.fillText('--:--',w-pad.right-34,h-5)}if(data.length<2){ctx.fillStyle='rgba(198,211,234,.45)';ctx.font='12px Inter,sans-serif';ctx.fillText('--',Math.max(pad.left,w/2-8),Math.max(pad.top+14,h/2));return;}const pts=data.map((v,i)=>[pad.left+(w-pad.left-pad.right)*(i/(data.length-1||1)),pad.top+(h-pad.top-pad.bottom)*(1-(v-min)/range)]);ctx.beginPath();pts.forEach(([x,y],i)=>i?ctx.lineTo(x,y):ctx.moveTo(x,y));ctx.strokeStyle=color;ctx.lineWidth=2;ctx.shadowColor=color;ctx.shadowBlur=10;ctx.stroke();ctx.shadowBlur=0;ctx.lineTo(w-pad.right,h-pad.bottom);ctx.lineTo(pad.left,h-pad.bottom);ctx.closePath();let g=ctx.createLinearGradient(0,pad.top,0,h-pad.bottom);g.addColorStop(0,color.replace('rgb','rgba').replace(')',',.22)'));g.addColorStop(1,color.replace('rgb','rgba').replace(')',',.02)'));ctx.fillStyle=g;ctx.fill();}
function bars(id,data,{min=Math.min(...data),max=Math.max(...data),color='rgb(66,232,138)'}={}){const c=$('#'+id);if(!c)return;data=Array.isArray(data)?data.filter(v=>Number.isFinite(Number(v))).map(Number):[];const{ctx,w,h}=resize(c);ctx.clearRect(0,0,w,h);if(!data.length){ctx.fillStyle='rgba(198,211,234,.45)';ctx.font='12px Inter,sans-serif';ctx.fillText('--',Math.max(4,w/2-8),Math.max(14,h/2));return;}const range=Math.max(1,max-min),slice=data.slice(-14),gap=5,bw=(w-4-gap*(slice.length-1))/slice.length;slice.forEach((v,i)=>{const bh=8+(h-12)*((v-min)/range),x=2+i*(bw+gap),y=h-4-bh;ctx.fillStyle=color.replace('rgb','rgba').replace(')',i%3===0?',.85)':',.48)');ctx.fillRect(x,y,bw,bh)})}
function drawAll(){line('heartTrend',state.trends.heart,{min:0,max:150,color:'rgb(255,92,117)',axis:true,footer:true,pad:{left:30,right:6,top:7,bottom:22}});line('spo2Trend',state.trends.spo2,{min:80,max:100,color:'rgb(53,169,255)',axis:true,footer:true,pad:{left:30,right:6,top:7,bottom:22}});line('aqiTrend',state.trends.aqi,{min:0,max:500,color:'rgb(66,232,138)',axis:true,footer:true,pad:{left:30,right:6,top:7,bottom:22}});line('heartSmall',state.trends.heart,{min:55,max:105,color:'rgb(255,92,117)'});line('spo2Small',state.trends.spo2,{min:88,max:100,color:'rgb(53,169,255)'});line('tempSmall',state.trends.temp,{min:24,max:29,color:'rgb(255,180,77)'});bars('aqiBars',state.trends.aqi,{min:0,max:500,color:'rgb(66,232,138)'});bars('humidityBars',state.trends.humidity,{min:35,max:75,color:'rgb(53,169,255)'})}
let drawAllPending=false;function scheduleDrawAll(){if(drawAllPending)return;drawAllPending=true;requestAnimationFrame(()=>{drawAllPending=false;drawAll()})}
const DASHBOARD_SCALE_MODE = window.DASHBOARD_CONFIG?.scaleMode || 'fit'; // fit：完整顯示；fill-width：優先填滿寬度；fill-height：優先填滿高度
function resizeDashboard(){
  const baseW=1920, baseH=1080;
  const fit=Math.min(window.innerWidth/baseW, window.innerHeight/baseH);
  const scale=DASHBOARD_SCALE_MODE==='fill-width'?window.innerWidth/baseW:(DASHBOARD_SCALE_MODE==='fill-height'?window.innerHeight/baseH:fit);
  document.documentElement.style.setProperty('--dashboard-scale',String(Math.max(scale,0.1).toFixed(5)));
  requestAnimationFrame(drawAll);
}



function weeklyValues(k){pruneTrendHistory();return (state.weeklyTrends[k]||[]).filter(p=>p.ts>=Date.now()-WEEK_MS&&isTrendValueValid(k,p.v))}
function renderTrendHistoryModal(){
  ['heart','spo2','aqi'].forEach(k=>{
    const points=weeklyValues(k),meta=TREND_META[k],stat=statOfPoints(points),avgEl=$(`#${k}WeeklyAvg`),countEl=$(`#${k}WeeklyCount`);
    if(avgEl)avgEl.textContent=stat?`${Math.round(stat.avg)}${meta.unit}`:'--';
    if(countEl)countEl.textContent=`${stat?stat.count:0} 筆有效資料`;
    line(`${k}WeeklyTrend`,points.map(p=>p.v),{min:meta.min,max:meta.max,color:meta.color,axis:true,footer:true,pad:{left:38,right:8,top:12,bottom:22}});
  });
  const rows=$('#weeklyTrendRows');
  if(rows){
    const days=[];
    for(let i=6;i>=0;i--){const d=new Date(Date.now()-i*24*60*60*1000);days.push(`${d.getFullYear()}/${pad(d.getMonth()+1)}/${pad(d.getDate())}`)}
    rows.innerHTML=days.map(day=>{
      const h=statOfPoints(weeklyValues('heart').filter(p=>dailyKey(p.ts)===day));
      const s=statOfPoints(weeklyValues('spo2').filter(p=>dailyKey(p.ts)===day));
      const a=statOfPoints(weeklyValues('aqi').filter(p=>dailyKey(p.ts)===day));
      const total=(h?.count||0)+(s?.count||0)+(a?.count||0);
      return `<tr><td>${day}</td><td>${fmtStat(h,'')}</td><td>${fmtStat(s,'')}</td><td>${fmtStat(a,'')}</td><td>${total}</td></tr>`;
    }).join('');
  }
}
function openTrendHistoryModal(){const modal=$('#trendHistoryModal');renderTrendHistoryModal();if(modal){modal.classList.add('show');modal.setAttribute('aria-hidden','false');requestAnimationFrame(renderTrendHistoryModal)}}
function closeTrendHistoryModal(){const modal=$('#trendHistoryModal');if(modal){modal.classList.remove('show');modal.setAttribute('aria-hidden','true')}}
function openEventHistoryModal(){const modal=$('#eventHistoryModal');renderEventHistoryModal();if(modal){modal.classList.add('show');modal.setAttribute('aria-hidden','false')}}
function closeEventHistoryModal(){const modal=$('#eventHistoryModal');if(modal){modal.classList.remove('show');modal.setAttribute('aria-hidden','true')}}

function refreshCameraTime(){
  const el=$('#cameraTime');
  if(el)el.textContent=time(new Date());
}
function setCameraPlaceholderMessage(message){
  const placeholder=$('#cameraPlaceholder');
  if(!placeholder)return;
  let msg=placeholder.querySelector('.camera-status-message');
  if(!msg){
    msg=document.createElement('div');
    msg.className='camera-status-message';
    placeholder.appendChild(msg);
  }
  msg.textContent=message||'尚未設定監視器影像來源';
}
function resolveCameraUrl(rawUrl){
  if(!rawUrl)return'';
  try{
    const url=new URL(rawUrl,window.location.href);
    if(['127.0.0.1','localhost','0.0.0.0','::1'].includes(url.hostname)){
      url.hostname=window.location.hostname||url.hostname;
    }
    return url.toString();
  }catch(_error){
    return rawUrl;
  }
}
function getCameraStreamUrl(){
  const streamPath=state.sensorData.cameraStreamPath||window.DASHBOARD_CONFIG?.camera?.streamPath||'';
  if(streamPath)return `/api/camera/hls/${streamPath}/index.m3u8`;
  return state.sensorData.cameraHlsUrl||'/api/camera/hls/space/index.m3u8';
}
function getCameraWebrtcUrl(){
  return resolveCameraUrl(state.sensorData.cameraWebrtcUrl||window.DASHBOARD_CONFIG?.camera?.webrtcUrl||'');
}
function getCameraSourceUrl(){
  return resolveCameraUrl(state.sensorData.cameraSnapshotUrl||window.DASHBOARD_CONFIG?.cameraSnapshotUrl||window.DASHBOARD_CONFIG?.camera?.snapshotUrl||'');
}
let cameraHls=null;
function destroyCameraStream(video, webRtcFrame){
  if(cameraHls){
    cameraHls.destroy();
    cameraHls=null;
  }
  if(video){
    video.pause();
    video.removeAttribute('src');
    video.load();
    video.style.display='none';
  }
  if(webRtcFrame){
    webRtcFrame.removeAttribute('src');
    webRtcFrame.style.display='none';
  }
}
function loadCameraWebrtcFrame(webRtcFrame, streamUrl, onReady){
  if(!webRtcFrame||!streamUrl)return false;
  webRtcFrame.onload=()=>onReady();
  webRtcFrame.src=streamUrl;
  return true;
}
function loadCameraStream(video, streamUrl, onReady, onFailure){
  if(!video||!streamUrl)return false;
  video.controls=false;
  video.disablePictureInPicture=true;
  video.muted=true;
  if(window.Hls&&window.Hls.isSupported()){
    destroyCameraStream(video);
    const hls=new window.Hls({lowLatencyMode:true,backBufferLength:0,maxBufferLength:1,liveSyncDurationCount:1,liveMaxLatencyDurationCount:2,maxLiveSyncPlaybackRate:1.5});
    cameraHls=hls;
    hls.on(window.Hls.Events.MANIFEST_PARSED,()=>{
      onReady();
      video.play().catch(()=>onFailure());
    });
    hls.on(window.Hls.Events.LEVEL_UPDATED,()=>{
      if(!Number.isFinite(video.currentTime)||!Number.isFinite(hls.liveSyncPosition))return;
      if(video.currentTime<hls.liveSyncPosition-1){
        video.currentTime=hls.liveSyncPosition;
      }
    });
    hls.on(window.Hls.Events.ERROR,(_event,data)=>{
      if(data?.fatal){
        onFailure();
      }
    });
    hls.loadSource(streamUrl);
    hls.attachMedia(video);
    return true;
  }
  if(video.canPlayType('application/vnd.apple.mpegurl')){
    destroyCameraStream(video);
    video.onloadedmetadata=()=>{
      onReady();
      video.play().catch(()=>onFailure());
    };
    video.onerror=()=>onFailure();
    video.src=streamUrl;
    return true;
  }
  return false;
}
function openCameraModal(){
  refreshCameraTime();
  const modal=$('#cameraModal');
  const webRtcFrame=$('#cameraWebrtcFrame');
  const video=$('#cameraVideo');
  const img=$('#cameraSnapshot');
  const placeholder=$('#cameraPlaceholder');
  const cameraOffline=document.body.classList.contains('camera-module-offline');
  const webRtcUrl=getCameraWebrtcUrl();
  const streamUrl=getCameraStreamUrl();
  const snapshotUrl=getCameraSourceUrl();
  const showSnapshotFallback=()=>{
    destroyCameraStream(video,webRtcFrame);
    showSnapshot();
  };
  const showPlaceholder=(message)=>{
    if(webRtcFrame)webRtcFrame.style.display='none';
    if(video)video.style.display='none';
    if(img)img.style.display='none';
    if(placeholder)placeholder.style.display='block';
    setCameraPlaceholderMessage(message);
  };
  destroyCameraStream(video,webRtcFrame);
  const showSnapshot=()=>{
    if(!img||!snapshotUrl){
      showPlaceholder(cameraOffline?'監視器尚未連線':'尚未設定監視器影像來源');
      return;
    }
    img.onload=()=>{
      img.style.display='block';
      if(placeholder)placeholder.style.display='none';
    };
    img.onerror=()=>showPlaceholder('影像載入失敗，請確認 Camera URL / 串流來源');
    img.src=snapshotUrl+(snapshotUrl.includes('?')?'&':'?')+'t='+Date.now();
    showPlaceholder('影像載入中...');
  };
  const showWebrtc=()=>{
    if(webRtcFrame)webRtcFrame.style.display='block';
    if(video)video.style.display='none';
    if(img)img.style.display='none';
    if(placeholder)placeholder.style.display='none';
  };
  const showVideo=()=>{
    if(webRtcFrame)webRtcFrame.style.display='none';
    video.style.display='block';
    if(placeholder)placeholder.style.display='none';
  };

  if(loadCameraWebrtcFrame(webRtcFrame,webRtcUrl,showWebrtc)){
    showPlaceholder('影像載入中...');
  }else if(loadCameraStream(video,streamUrl+(streamUrl.includes('?')?'&':'?')+'t='+Date.now(),showVideo,showSnapshotFallback)){
    showPlaceholder('影像載入中...');
  }else if(snapshotUrl){
    showSnapshot();
  }else{
    showPlaceholder(cameraOffline?'監視器尚未連線':'尚未設定監視器影像來源');
  }

  if(modal){
    modal.classList.add('show');
    modal.setAttribute('aria-hidden','false');
  }
  addEvent('<strong>監視器：</strong>已開啟影像視窗','device','var(--cyan)');
}
function closeCameraModal(){
  const modal=$('#cameraModal');
  const webRtcFrame=$('#cameraWebrtcFrame');
  const video=$('#cameraVideo');
  destroyCameraStream(video,webRtcFrame);
  if(modal){
    modal.classList.remove('show');
    modal.setAttribute('aria-hidden','true');
  }
}
function bind(){document.addEventListener('click',e=>{if(e.target.closest('.module-control-locked,.disabled,[disabled]'))return;const m=e.target.closest('.mode-card');if(m){const nextControlMode=m.dataset.controlMode;state.uiState.controlMode=nextControlMode;if(nextControlMode==='scene'||nextControlMode==='manual'){state.uiState.smartModeEnabled=false;}addEvent(`<strong>控制模式：</strong>${controlModeName(nextControlMode)}`,controlModeIcon(nextControlMode),controlModeAccent(nextControlMode));render()}const sm=e.target.closest('.small-mode');if(sm){const nextMode=sm.dataset.mode;state.uiState.controlMode='scene';state.uiState.smartModeEnabled=false;state.uiState.emotionMode=nextMode;if(window.DashboardSceneMemory?.applySceneProfile){window.DashboardSceneMemory.applySceneProfile(nextMode,true);addEvent(`<strong>切換情境模式：</strong>${modes[nextMode]?.name||'情境模式'}`,'relax','var(--purple)')}else{updateMode(nextMode,true,{silent:true})}}const sw=e.target.closest('.switch');if(sw){if(sw.dataset.toggle==='smart-mode'){const enabled=state.uiState.smartModeEnabled===false;if(enabled&&state.uiState.controlMode==='manual'){state.uiState.controlMode='scene';}updateUiState({smartModeEnabled:enabled});addEvent(enabled?'<strong>智慧模式：</strong>已開啟':'<strong>智慧模式：</strong>已關閉，保留自定義設定','bot',enabled?'var(--green)':'var(--yellow)');return}if(sw.dataset.toggle==='led'){updateDeviceState({ledOn:!state.deviceState.ledOn})}}const cd=e.target.closest('.color-dot');if(cd){updateDeviceState({ledColor:cd.dataset.color,ledOn:true})}});$('#brightnessRange')?.addEventListener('input',e=>{const value=clampSceneAdjustment('brightness',e.target.value);e.target.value=value;updateDeviceState({brightness:value})});$('#brightnessRange')?.addEventListener('change',e=>{const value=clampSceneAdjustment('brightness',e.target.value);e.target.value=value;updateDeviceState({brightness:value})});$('#kelvinRange')?.addEventListener('input',e=>{const value=clampSceneAdjustment('kelvin',e.target.value);e.target.value=value;updateDeviceState({kelvin:value})});$('#kelvinRange')?.addEventListener('change',e=>{const value=clampSceneAdjustment('kelvin',e.target.value);e.target.value=value;updateDeviceState({kelvin:value})});$('#fanRange')?.addEventListener('input',e=>{const level=clamp(Math.round(+e.target.value),0,10);updateDeviceState({fanSpeed:level,fanOn:level>0})});$('#fanRange')?.addEventListener('change',()=>{if(!state.deviceState.fanOn)return;addEvent(`<strong>風扇等級調整為</strong> ${fanState(state.deviceState.fanSpeed)}（${Math.round(state.deviceState.fanSpeed)}級）`,'fan','var(--cyan)')});const audioVolumeRange=$('#audioVolumeRange');if(audioVolumeRange){audioVolumeRange.addEventListener('input',e=>{state.audioState.volume=clamp(+e.target.value,0,100);renderAudioState()});audioVolumeRange.addEventListener('change',()=>renderAudioState())}$('#applyBtn')?.addEventListener('click',()=>{if(window.DashboardSceneMemory?.saveCurrentScene?.())return;toast('已套用目前燈光設定。')});$('#resetBtn')?.addEventListener('click',()=>{if(window.DashboardSceneMemory?.resetCurrentModeDefault?.())return;toast('已恢復目前模式預設。')});$('#applyRecommendBtn')?.addEventListener('click',()=>{updateMode(state.uiState.emotionMode,true,{silent:true});toast(`已套用推薦模式：${modes[state.uiState.emotionMode].name}`)});const openCameraBtn=$('#openCameraBtn');if(openCameraBtn)openCameraBtn.addEventListener('click',openCameraModal);['#closeCameraBtn','#closeCameraBtn2'].forEach(sel=>{const btn=$(sel);if(btn)btn.addEventListener('click',closeCameraModal)});const refreshCameraBtn=$('#refreshCameraBtn');if(refreshCameraBtn)refreshCameraBtn.addEventListener('click',openCameraModal);const cameraModal=$('#cameraModal');if(cameraModal)cameraModal.addEventListener('click',e=>{if(e.target.id==='cameraModal')closeCameraModal()});$('#openTrendHistoryBtn')?.addEventListener('click',openTrendHistoryModal);$('#closeTrendHistoryBtn')?.addEventListener('click',closeTrendHistoryModal);$('#openEventHistoryBtn')?.addEventListener('click',openEventHistoryModal);$('#closeEventHistoryBtn')?.addEventListener('click',closeEventHistoryModal);const trendModal=$('#trendHistoryModal');if(trendModal)trendModal.addEventListener('click',e=>{if(e.target.id==='trendHistoryModal')closeTrendHistoryModal()});const eventModal=$('#eventHistoryModal');if(eventModal)eventModal.addEventListener('click',e=>{if(e.target.id==='eventHistoryModal')closeEventHistoryModal()});document.addEventListener('keydown',e=>{if(e.key==='Escape'){closeTrendHistoryModal();closeEventHistoryModal();closeCameraModal()}});addEventListener("resize",()=>{resizeDashboard();scheduleDrawAll();if($('#trendHistoryModal')?.classList.contains('show'))renderTrendHistoryModal()})}
function seedEvents(){state.events=state.events.slice(0,100);renderEvents();renderEventHistoryModal()}function clock(){const n=new Date();$('#clockText').textContent=time(n);$('#dateText').textContent=dateText(n)}
function fakeLoop(){/* V11 實機版：不產生前端假資料 */}
function tickAudioProgress(){
  const a=state.audioState;
  if(!a||!a.playing)return;
  const runtimeMode=window.DASHBOARD_CONFIG?.smartLight?.runtimeMode||window.DASHBOARD_CONFIG?.runtimeMode||'dev';
  const realAudioBackend=runtimeMode!=='dev'&&runtimeMode!=='mock';
  if(realAudioBackend)return;
  const duration=Math.max(1,Number(a.durationSec)||1);
  a.progressSec=(Number(a.progressSec)||0)+1;
  if(a.progressSec>=duration){
    const nextIndex=(Number(a.trackIndex)||0)+1;
    const item=audioPlaylist[nextIndex%audioPlaylist.length];
    Object.assign(a,{track:item.track,mode:item.mode,durationSec:item.durationSec,progressSec:0,trackIndex:nextIndex%audioPlaylist.length});
  }
  renderAudioState();
}
function updateAudioState(o){const previousTrack=state.audioState.track,previousPlaying=state.audioState.playing,previousState=state.audioState.state;Object.assign(state.audioState,o);if(typeof state.audioState.progressSec==='number'&&typeof state.audioState.durationSec==='number')state.audioState.progressSec=clamp(state.audioState.progressSec,0,state.audioState.durationSec);render();const trackChanged=o.track&&o.track!==previousTrack,playbackChanged=(Object.prototype.hasOwnProperty.call(o,'playing')&&o.playing!==previousPlaying)||(Object.prototype.hasOwnProperty.call(o,'state')&&o.state!==previousState);if(trackChanged||playbackChanged){const text=trackChanged&&state.audioState.track?`<strong>目前播放：</strong>${state.audioState.track}`:state.audioState.playing?'<strong>音樂播放：</strong>已開啟':'<strong>音樂播放：</strong>已暫停';addEvent(text,'music','var(--cyan)')}}
function clearEvents(){state.events=[];renderEvents();renderEventHistoryModal()}
window.SmartDashboard={
  getState:()=>JSON.parse(JSON.stringify(state)),
  getModuleState:()=>renderModuleStatus(),
  updateSensorData,
  updateDeviceState,
  updateAudioState,
  updateUiState,
  setAudioVolume:(volume)=>updateAudioState({volume:clamp(Number(volume)||0,0,100)}),
  setAudioProgress:(seconds)=>updateAudioState({progressSec:clamp(Number(seconds)||0,0,state.audioState.durationSec||0)}),
  seekAudio:(deltaSeconds)=>updateAudioState({progressSec:clamp((Number(state.audioState.progressSec)||0)+(Number(deltaSeconds)||0),0,state.audioState.durationSec||0)}),
  audioAction,
  updateMode,
  addEvent,
  clearEvents,
  render,
  stopFakeData:(options={})=>{
    if(window.__SMART_DASHBOARD_FAKE_TIMER__){
      clearInterval(window.__SMART_DASHBOARD_FAKE_TIMER__);
      window.__SMART_DASHBOARD_FAKE_TIMER__=null;
      if(!options.silent)addEvent('<strong>資料更新：</strong>已停止','device','var(--yellow)');
    }
  }
};
function init(){
  resizeDashboard();
  clock();
  setInterval(clock,1000);
  if(!window.__SMART_DASHBOARD_AUDIO_TIMER__){
    window.__SMART_DASHBOARD_AUDIO_TIMER__=setInterval(tickAudioProgress,1000);
  }
  if(!window.__SMART_DASHBOARD_AUDIO_WAVE_TIMER__){
    window.__SMART_DASHBOARD_AUDIO_WAVE_TIMER__=setInterval(randomizeAudioWave,180);
  }
  initTrends();
  seedEvents();
  bind();
  render();

  const config = window.DASHBOARD_CONFIG || {};
  if (config.useFakeData !== false) {
    const intervalMs = Number(config.fakeDataIntervalMs || 2200);
    window.__SMART_DASHBOARD_FAKE_TIMER__ = setInterval(fakeLoop, intervalMs);
  }
}
init();
