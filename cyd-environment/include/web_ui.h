#pragma once

const char WEB_STYLE[] PROGMEM = R"CSS(
:root{color-scheme:light;--bg:#f4f7fb;--panel:#fff;--ink:#172a36;--muted:#536674;--line:#dce5e9;--green:#12734f;--soft:#edf7f1;--blue:#2368b1;--orange:#a85712}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.65 system-ui,"Microsoft YaHei",sans-serif;accent-color:var(--green)}
::selection{background:#c4e8d4;color:#123e2b}a{color:var(--green);text-underline-offset:4px}a:hover{color:#0a5136}button,input,textarea,select{font:inherit}button,a,input,textarea,summary,select{-webkit-tap-highlight-color:transparent}button:focus-visible,a:focus-visible,input:focus-visible,textarea:focus-visible,summary:focus-visible,select:focus-visible{outline:3px solid #2368b1;outline-offset:3px}button{cursor:pointer;min-height:44px;border:0;border-radius:10px;padding:10px 18px;background:var(--green);color:white;font-weight:650;transition:background .15s}button:hover{background:#0b583c}button:disabled{cursor:wait;opacity:.55}button.secondary{background:var(--soft);color:var(--green)}button.secondary:hover{background:#dcefe4}input,textarea,select{width:100%;border:1px solid #afbdc6;border-radius:10px;background:var(--panel);color:var(--ink);padding:11px 12px;caret-color:var(--green)}input::placeholder,textarea::placeholder{color:#61717e}textarea{resize:vertical;min-height:110px}label{display:block;font-weight:600;margin-bottom:6px}h1,h2,h3,p{margin-top:0}h1{font-size:27px;letter-spacing:-.025em;line-height:1.3;margin-bottom:5px}h2{font-size:18px;line-height:1.4;margin-bottom:14px}h3{font-size:15px}p{margin-bottom:12px}small,.muted{color:var(--muted);font-size:13px}.wrap{max-width:1140px;margin:auto;padding:32px 28px}.top{display:flex;justify-content:space-between;align-items:center;gap:20px;margin-bottom:24px}.top p{margin:0}.connection{text-align:right;flex-shrink:0}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:7px;background:#7b8791}.online .dot{background:var(--green)}.offline{color:#a93439}.offline .dot{background:#b53c40}.layout{display:grid;grid-template-columns:minmax(0,1.65fr) minmax(290px,1fr);gap:20px}.panel{min-width:0;background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:24px}.section-head{display:flex;justify-content:space-between;align-items:center;gap:12px}.section-head h2{margin-bottom:0}.section-head{margin-bottom:20px}.metrics{display:grid;grid-template-columns:1fr 1fr;gap:24px;margin:16px 0 22px}.metric+.metric{padding-left:24px;border-left:1px solid var(--line)}.metric-label{display:flex;align-items:center;justify-content:space-between;gap:8px;color:var(--muted)}.badge{font-size:12px;color:var(--green);font-weight:650}.reading{font-size:46px;font-weight:650;letter-spacing:-.035em;line-height:1.35;font-variant-numeric:tabular-nums}.unit{font-size:19px;font-weight:450;letter-spacing:0;color:var(--muted);margin-left:4px}.advice{background:var(--soft);border-radius:10px;padding:16px 18px}.advice strong{display:block;color:var(--green);margin-bottom:5px}.advice p{margin:0;font-size:14px;line-height:1.8}.facts{display:flex;gap:12px 24px;flex-wrap:wrap;margin-top:14px;font-size:12px;color:var(--muted)}.facts b{font-weight:550;color:var(--ink)}.weather-reading{font-size:32px;font-weight:650;line-height:1.4;font-variant-numeric:tabular-nums}.weather-reading span+span{font-size:20px;margin-left:14px;font-weight:500}.weather-note{font-size:13px;color:var(--muted);margin:8px 0 18px}.weather-action{border-top:1px solid var(--line);padding-top:16px}.weather-action b{display:block;margin-bottom:5px}.weather-action p{font-size:14px}.actions{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:12px;margin-top:18px}.actions a{font-size:13px}.actions button{font-size:13px}.chart-label{display:flex;justify-content:space-between;font-size:13px;color:var(--muted);margin:12px 0 4px}.chart-label b{color:var(--orange);font-weight:550}.chart-label.humidity b{color:var(--blue)}canvas{display:block;width:100%;height:108px}.chart-axis{display:flex;justify-content:space-between;font-size:11px;color:var(--muted)}.empty{padding:22px 0;color:var(--muted);text-align:center;font-size:14px}.audio-status{display:flex;align-items:center;gap:10px;margin-bottom:10px}.audio-indicator{width:32px;height:32px;display:grid;place-items:center;background:var(--soft);color:var(--green);border-radius:9px;flex-shrink:0}.audio-indicator svg{width:20px;height:20px}.audio-status strong{font-size:15px}.stats{display:flex;gap:20px;border-top:1px solid var(--line);padding-top:14px;margin:18px 0}.stats div{font-size:12px;color:var(--muted)}.stats b{display:block;color:var(--ink);font-size:22px;font-weight:600;font-variant-numeric:tabular-nums}.chat-panel{grid-column:1/-1;display:grid;grid-template-columns:minmax(210px,1fr) minmax(0,2fr);gap:32px}.chat-panel p{font-size:14px}.chat-panel h2{margin-bottom:8px}.chat-panel details{margin-top:20px}summary{cursor:pointer;min-height:44px;padding:9px 0;color:var(--green);font-size:14px}.config-form{margin-top:8px}.config-form label{font-size:13px}.config-form button{margin-top:10px;width:100%}.chat-controls{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-top:10px}.chat-controls small{font-size:12px}.reply{white-space:pre-wrap;overflow-wrap:anywhere;line-height:1.85;background:#f5f8fa;padding:16px;border-radius:10px;margin-top:18px;font-size:14px}.feedback{font-size:13px;margin:10px 0 0;color:var(--muted);overflow-wrap:anywhere}.feedback.error{color:#a93439}.alert{display:flex;justify-content:space-between;align-items:center;gap:20px;margin-bottom:20px;padding:16px 20px;background:#fff1ed;color:#90371d;border:1px solid #efc9b9;border-radius:12px}.alert p{margin:0}.alert button{white-space:nowrap;background:#90371d}.notice{margin-bottom:20px;padding:12px 16px;background:#fff1ed;color:#90371d;border-radius:10px;font-size:14px}.footer{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;padding:24px 0 0;font-size:13px;color:var(--muted)}.footer nav{display:flex;flex-wrap:wrap;gap:10px 20px}[hidden]{display:none!important}
body.settings-page{max-width:680px;margin:32px auto;padding:24px;background:var(--panel);border:1px solid var(--line);border-radius:14px}body.settings-page input,body.settings-page select{min-height:44px}body.settings-page input[type=checkbox]{width:auto;min-height:auto}body.settings-page button{margin:12px 0}body.settings-page h3{font-size:20px;margin:24px 0 12px}body.settings-page form{margin:18px 0}body.settings-page .back{display:inline-flex;align-items:center;min-height:44px;margin-bottom:8px;font-size:14px}body.settings-page .tools{display:flex;gap:18px;flex-wrap:wrap}
@media(max-width:760px){.wrap{padding:24px 16px}.layout{grid-template-columns:1fr}.chat-panel{grid-template-columns:1fr;gap:20px}.top{align-items:flex-start}.top h1{font-size:23px}.panel{padding:20px}.connection{font-size:12px}.connection small{font-size:11px}.metrics{gap:16px}.metric+.metric{padding-left:16px}.reading{font-size:42px}.footer{padding-bottom:12px}}
@media(max-width:380px){.wrap{padding:20px 12px}.panel{padding:16px}.top{gap:10px}.top h1{font-size:21px}.reading{font-size:36px}.metric-label{font-size:13px}.badge{font-size:11px}.chat-controls{align-items:flex-start;flex-direction:column}.chat-controls button{width:100%}.alert{align-items:flex-start;flex-direction:column}body.settings-page{margin:0;border:0;border-radius:0;padding:18px}}
.mobile-nav{display:none}
@media(max-width:760px){.wrap{padding-bottom:calc(94px + env(safe-area-inset-bottom))}.layout{gap:16px}.top{margin-bottom:20px}.metrics{margin:12px 0 16px}.section-head{margin-bottom:16px}.weather-action{padding-top:12px}.weather-note{margin:6px 0 12px}.weather-action p{margin-bottom:0}.actions{margin-top:12px}.feedback:empty{display:none}.mobile-nav{position:fixed;bottom:0;left:0;right:0;z-index:10;display:grid;grid-template-columns:repeat(4,1fr);padding:6px 12px calc(6px + env(safe-area-inset-bottom));background:var(--panel);border-top:1px solid var(--line)}.mobile-nav button{display:flex;flex-direction:column;align-items:center;gap:3px;background:transparent;color:var(--muted);padding:8px 4px;font-size:12px;line-height:1.4;min-height:56px}.mobile-nav button[aria-current=page]{background:var(--soft);color:var(--green)}.mobile-nav svg{width:21px;height:21px}.footer[data-view=device]{padding:20px;background:var(--panel);border:1px solid var(--line);border-radius:14px;margin-top:16px;display:block}.footer nav{display:flex;flex-direction:column;gap:0;margin-top:12px}.footer nav a{display:flex;align-items:center;min-height:48px;text-decoration:none;border-top:1px solid var(--line)}body:not([data-tab=environment]) [data-view=environment],body:not([data-tab=trend]) [data-view=trend],body:not([data-tab=miku]) [data-view=miku],body:not([data-tab=device]) [data-view=device]{display:none}.chat-panel textarea{min-height:130px}input,textarea,select{font-size:16px}}
@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto!important;transition:none!important}}
)CSS";

const char WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#f4f7fb"><title>环境健康助手</title><link rel="stylesheet" href="/ui.css"></head>
<body data-tab="environment"><main class="wrap">
<header class="top"><div><h1>环境健康助手</h1><p class="muted">室内外环境，一眼掌握。</p></div><div class="connection"><div id="connection" role="status"><i class="dot" aria-hidden="true"></i><span id="connectionText">正在连接</span></div><small id="updated">等待设备数据</small></div></header>
<div id="offline" class="notice" role="status" hidden>设备暂时离线。以下为最后一次读数，连接恢复后自动更新。</div>
<div id="alert" class="alert" role="alert" hidden><p id="alertText"></p><button id="dismiss" type="button">知道了</button></div>
<div class="layout">
<section class="panel" data-view="environment" aria-labelledby="indoorTitle"><div class="section-head"><h2 id="indoorTitle">室内环境</h2><span class="muted">实时监测</span></div>
<div class="metrics"><div class="metric"><div class="metric-label">温度 <span class="badge" id="tl">未就绪</span></div><div class="reading"><span id="t">--</span><span class="unit">℃</span></div></div><div class="metric"><div class="metric-label">湿度 <span class="badge" id="hl">未就绪</span></div><div class="reading"><span id="h">--</span><span class="unit">%</span></div></div></div>
<div class="advice"><strong id="state">正在读取环境</strong><p id="advice">连接设备后，这里会显示当前环境建议。</p></div><div class="facts"><span>露点 <b id="dew">--</b></span><span>结露余量 <b id="cm">--</b></span><span>绝对湿度 <b id="ah">--</b></span><span>焓值 <b id="eh">--</b></span><span>霉变风险 <b id="mold">--</b></span><span>10 分钟趋势 <b id="forecast">--</b></span><span>最近事件 <b id="event">--</b></span></div></section>
<section class="panel" data-view="environment" aria-labelledby="outdoorTitle"><div class="section-head"><h2 id="outdoorTitle">室外与节能建议</h2><span id="op" class="muted">未获取</span></div><div class="weather-reading"><span id="ot">-- ℃</span><span id="oh">-- %</span></div><p class="weather-note" id="om">等待室外天气</p><div class="weather-action"><b id="oa">等待数据</b><p id="or" class="muted">结合室内外温湿度，给出通风和空调建议。</p></div><div class="actions"><a href="/weather-config">设置城市 / 坐标</a><button class="secondary" id="refreshWeather" type="button">刷新天气</button></div><p class="feedback" id="weatherFeedback" role="status"></p></section>
<section class="panel" data-view="trend" aria-labelledby="trendTitle"><div class="section-head"><h2 id="trendTitle">最近趋势</h2><span class="muted">最多 6 分钟</span></div><div id="chartEmpty" class="empty">正在积累读数，稍后显示趋势。</div><div id="charts" hidden><div class="chart-label"><b>温度 · ℃</b><span id="tRange"></span></div><canvas id="tChart" role="img" aria-label="室内温度变化趋势"></canvas><div class="chart-label humidity"><b>湿度 · %</b><span id="hRange"></span></div><canvas id="hChart" role="img" aria-label="室内湿度变化趋势"></canvas><div class="chart-axis"><span id="historyStart"></span><span>现在</span></div></div>
<div class="section-head" style="margin-top:26px"><h2>过去 24 小时</h2><span class="muted">TF 卡记录 · 5 分钟聚合</span></div><div id="dayEmpty" class="empty">正在读取历史记录。</div><div id="dayCharts" hidden><div class="chart-label"><b>温度 · ℃</b><span id="t24Range"></span></div><canvas id="tChart24" role="img" aria-label="过去 24 小时温度趋势"></canvas><div class="chart-label humidity"><b>湿度 · %</b><span id="h24Range"></span></div><canvas id="hChart24" role="img" aria-label="过去 24 小时湿度趋势"></canvas><div class="chart-axis"><span>24 小时前</span><span>现在</span></div><p class="muted" id="dayStats" style="margin:10px 0 0"></p></div></section>
<section class="panel" data-view="device" aria-labelledby="audioTitle"><div class="section-head"><h2 id="audioTitle">喇叭与语音</h2><a class="muted" href="/qwen-config">语音设置</a></div><div class="audio-status"><span class="audio-indicator" aria-hidden="true"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M11 5 6 9H3v6h3l5 4V5Z"/><path d="M15 8a6 6 0 0 1 0 8m3-11a10 10 0 0 1 0 14"/></svg></span><strong id="audioState">正在读取状态</strong></div><p class="muted" id="ttsState">语音状态将在连接后显示。</p><div class="stats"><div>缓冲中断<b id="underruns">--</b></div><div>写入未完成<b id="shortWrites">--</b></div></div><div class="actions"><button class="secondary" id="testTone" type="button" disabled>播放测试音</button><a href="/speaker-test">查看测试说明</a></div><p class="feedback" id="audioFeedback" role="status"></p></section>
<section class="panel chat-panel" data-view="miku" aria-labelledby="chatTitle"><div><h2 id="chatTitle">和 Miku 聊聊</h2><p class="muted">询问当前环境，或聊聊今天的日常。<br>由 DeepSeek 回复，配置语音后可朗读。</p><details><summary>DeepSeek 密钥设置</summary><form id="keyForm" class="config-form"><label for="key">API Key</label><input id="key" type="password" autocomplete="off" placeholder="留空保留原密钥" maxlength="180"><button id="saveKey" type="submit" class="secondary">保存密钥</button><p class="muted">密钥只保存在这块 ESP32；请仅在可信局域网中填写。</p><p id="keyFeedback" class="feedback" role="status"></p></form></details></div><div><form id="chatForm"><label for="q">发送消息</label><textarea id="q" maxlength="600" placeholder="现在适合开窗通风吗？" required></textarea><div class="chat-controls"><small>最多 600 字节（约 200 个汉字）</small><button id="sendChat" type="submit">发送给 Miku</button></div></form><p id="chatFeedback" class="feedback" role="status"></p><div id="reply" class="reply" aria-live="polite" hidden></div></div></section>
</div><footer class="footer" data-view="device"><span>ESP32 · 设备设置</span><nav aria-label="设备设置"><a href="/alert-config">区间提醒</a><a href="/storage">TF 卡状态</a><a href="/miku-upload">更新 Miku 动画</a></nav></footer>
</main><nav class="mobile-nav" aria-label="手机页面">
<button type="button" data-tab-target="environment" aria-current="page"><svg aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="m3 10 9-7 9 7M5 9v12h14V9M9 21v-8h6v8"/></svg>环境</button>
<button type="button" data-tab-target="trend"><svg aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M4 3v17h17M7 14l4-5 4 3 5-7"/></svg>趋势</button>
<button type="button" data-tab-target="miku"><svg aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M20 15a3 3 0 0 1-3 3H9l-5 3V6a3 3 0 0 1 3-3h10a3 3 0 0 1 3 3v9ZM8 8h8M8 12h5"/></svg>Miku</button>
<button type="button" data-tab-target="device"><svg aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="2" width="14" height="20" rx="3"/><path d="M9 6h6M11 18h2"/></svg>设备</button>
</nav><script>
const $=id=>document.getElementById(id);
let snapshot=null,alertShown='',chatPending=false,tonePending=false,deviceBusy=false,pollTimer,daySnapshot=null,dayLoadedAt=0,dayLoading=false;
const number=(v,d=1)=>Number.isFinite(v)?v.toFixed(d):'--';
function feedback(id,text,error=false){$(id).textContent=text;$(id).classList.toggle('error',error)}
async function request(url,options={}){
  const response=await fetch(url,{cache:'no-store',...options,signal:AbortSignal.timeout(15000)});
  if(!response.ok)throw new Error(await response.text()||`设备返回错误 ${response.status}`);
  return response;
}
function message(e){return e.name==='TimeoutError'?'设备响应超时，请稍后重试。':e instanceof TypeError?'连接失败，请检查设备网络。':e.message}
function paint(v,type){
  let label='未就绪',color='var(--muted)';
  if(Number.isFinite(v)){
    if(type==='t')[label,color]=v<=12?['过低','#2368b1']:v<18?['偏低','#2368b1']:v<=26?['舒适','#12734f']:v<30?['偏高','#a85712']:['过高','#b3363b'];
    else [label,color]=v<25?['过低','#a85712']:v<35?['偏低','#a85712']:v<=65?['舒适','#2368b1']:v<=75?['偏高','#7954a2']:['过高','#a33178'];
  }
  $(type+'l').textContent=label;$(type+'l').style.color=color;
}
function graph(values,id,color,unit){
  const cv=$(id),cx=cv.getContext('2d'),w=cv.clientWidth,h=108,dpr=window.devicePixelRatio||1;
  if(!w)return;
  cv.width=Math.round(w*dpr);cv.height=Math.round(h*dpr);cx.scale(dpr,dpr);
  const lo=Math.floor(Math.min(...values)-1),hi=Math.ceil(Math.max(...values)+1),left=32,right=w-8;
  cx.font='11px system-ui';cx.fillStyle='#536674';cx.lineWidth=1;
  for(let i=0;i<3;i++){const y=10+i*40;cx.fillText(number(hi-(hi-lo)*i/2,0),0,y+4);cx.beginPath();cx.strokeStyle='#e5ebef';cx.moveTo(left,y);cx.lineTo(right,y);cx.stroke()}
  cx.beginPath();cx.strokeStyle=color;cx.lineWidth=2;
  values.forEach((v,i)=>{const x=left+i*(right-left)/(values.length-1),y=90-(v-lo)*80/(hi-lo);i?cx.lineTo(x,y):cx.moveTo(x,y)});cx.stroke();
  cv.setAttribute('aria-label',`${unit}趋势，最低 ${number(Math.min(...values))}，最高 ${number(Math.max(...values))}`);
}
function charts(d){
  const available=d.historyT.length>=2&&d.historyH.length>=2;
  $('chartEmpty').hidden=available;$('charts').hidden=!available;if(!available)return;
  graph(d.historyT,'tChart','#a85712','温度');graph(d.historyH,'hChart','#2368b1','湿度');
  $('tRange').textContent=`${number(Math.min(...d.historyT))}–${number(Math.max(...d.historyT))} ℃`;
  $('hRange').textContent=`${number(Math.min(...d.historyH))}–${number(Math.max(...d.historyH))} %`;
  const seconds=(d.historyT.length-1)*5;$('historyStart').textContent=seconds>=60?`${number(seconds/60)} 分钟前`:`${seconds} 秒前`;
}
function renderDay(d){
  const has=d&&d.historyT&&d.historyT.length>=2;
  $('dayEmpty').hidden=has;$('dayCharts').hidden=!has;
  if(!has){$('dayEmpty').textContent=d&&d.samples?`只有 ${d.samples} 个样本，继续记录后显示 24 小时曲线。`:'TF 卡暂无历史记录（需插入 TF 卡并运行一段时间）。';dayLoadedAt=Date.now();return}
  graph(d.historyT,'tChart24','#a85712','24小时温度');
  graph(d.historyH,'hChart24','#2368b1','24小时湿度');
  $('t24Range').textContent=`${number(d.minT)}–${number(d.maxT)} ℃`;
  $('h24Range').textContent=`${number(d.minH,0)}–${number(d.maxH,0)} %`;
  $('dayStats').textContent=`共 ${d.samples} 个样本 · 温度 >28℃ ${d.hotMinutes} 分钟 · 湿度 >70% ${d.humidMinutes} 分钟`;
  daySnapshot=d;dayLoadedAt=Date.now();
}
async function loadDay(){
  if(dayLoading)return;
  dayLoading=true;
  try{const d=await(await request('/api/history24h')).json();renderDay(d)}
  catch(e){$('dayEmpty').hidden=false;$('dayCharts').hidden=true;$('dayEmpty').textContent='读取历史记录失败：'+message(e)}
  finally{dayLoading=false}
}
function render(d){
  $('t').textContent=number(d.temperature);$('h').textContent=number(d.humidity);paint(d.temperature,'t');paint(d.humidity,'h');
  $('state').textContent=d.level;$('advice').textContent=d.advice;$('dew').textContent=number(d.dewPoint)+' ℃';$('forecast').textContent=`${number(d.forecastT)} ℃ / ${number(d.forecastH,0)} %`;
  $('cm').textContent=number(d.condensationMargin)+' ℃';$('ah').textContent=number(d.absoluteHumidity)+' g/m³';$('eh').textContent=number(d.enthalpy,0)+' kJ/kg';
  $('mold').textContent=d.moldMinutes>0?`${d.moldRisk} · ${d.moldMinutes} 分钟`:'低';$('event').textContent=d.event||'--';
  $('op').textContent=d.weatherPlace;$('ot').textContent=number(d.outTemperature)+' ℃';$('oh').textContent=number(d.outHumidity,0)+' %';
  $('om').textContent=`${d.outWeather} · 体感 ${number(d.outFeels)} ℃ · 露点 ${number(d.outDewPoint)} ℃`;
  $('oa').textContent=d.action;$('or').textContent=d.actionReason;
  if(d.alertActive&&d.alertWeb&&d.alertText){if(alertShown!==d.alertText){alertShown=d.alertText;$('alertText').textContent=d.alertText;$('alert').hidden=false}}
  else{$('alert').hidden=true;alertShown=''}
  const a=d.audio;deviceBusy=d.chatBusy||a.busy||a.running||a.buffered>0;
  $('audioState').textContent=!a.ready?'音频输出未就绪':a.running?'喇叭播放中':a.busy?'正在准备语音':'喇叭已静音';
  $('ttsState').textContent=a.status;$('underruns').textContent=a.underruns;$('shortWrites').textContent=a.shortWrites;
  $('testTone').disabled=tonePending||deviceBusy||!a.ready;$('sendChat').disabled=chatPending||deviceBusy;
  charts(d);
}
async function load(){
  clearTimeout(pollTimer);
  try{const d=await(await request('/api')).json();render(d);snapshot=d;$('connection').className='online';$('connectionText').textContent='设备在线';$('updated').textContent='更新于 '+new Date().toLocaleTimeString('zh-CN',{hour12:false});$('offline').hidden=true}
  catch(e){$('connection').className='offline';$('connectionText').textContent='连接中断';$('offline').hidden=false;if(!snapshot)$('offline').textContent='暂时无法连接设备，正在自动重试。';$('testTone').disabled=true;$('sendChat').disabled=true}
  finally{pollTimer=setTimeout(load,document.hidden?10000:2500)}
}
$('dismiss').onclick=()=>{$('alert').hidden=true};
$('refreshWeather').onclick=async()=>{const b=$('refreshWeather');b.disabled=true;b.textContent='正在刷新…';try{const r=await request('/weather/refresh',{method:'POST'});feedback('weatherFeedback',await r.text())}catch(e){feedback('weatherFeedback',message(e),true)}finally{b.disabled=false;b.textContent='刷新天气'}};
$('keyForm').onsubmit=async e=>{e.preventDefault();const key=$('key').value.trim();if(!key){feedback('keyFeedback','请先填写 API Key。',true);return}const b=$('saveKey');b.disabled=true;try{const r=await request('/deepseek/config',{method:'POST',headers:{'Content-Type':'text/plain'},body:key});$('key').value='';feedback('keyFeedback',await r.text())}catch(e){feedback('keyFeedback',message(e),true)}finally{b.disabled=false}};
$('chatForm').onsubmit=async e=>{
  e.preventDefault();if(chatPending)return;const q=$('q').value.trim();if(!q)return;
  if(new TextEncoder().encode(q).length>600){feedback('chatFeedback','消息过长，请缩短至 600 字节以内（约 200 个汉字）。',true);return}
  chatPending=true;$('sendChat').disabled=true;$('sendChat').textContent='正在回复…';feedback('chatFeedback','Miku 正在思考…');
  try{let r=await request('/deepseek/chat',{method:'POST',headers:{'Content-Type':'text/plain'},body:q});const deadline=Date.now()+180000;
    while(r.status===202){if(Date.now()>deadline)throw new Error('回复等待超时，请稍后重新打开页面查看。');await new Promise(resolve=>setTimeout(resolve,1200));r=await request('/deepseek/result')}
    $('reply').textContent=await r.text();$('reply').hidden=false;feedback('chatFeedback','已收到回复。');
  }catch(e){feedback('chatFeedback',message(e),true)}finally{chatPending=false;$('sendChat').disabled=deviceBusy;$('sendChat').textContent='发送给 Miku'}
};
$('testTone').onclick=async()=>{if(tonePending)return;tonePending=true;$('testTone').disabled=true;try{const r=await request('/speaker-test',{method:'POST'});feedback('audioFeedback',await r.text())}catch(e){feedback('audioFeedback',message(e),true)}finally{tonePending=false;$('testTone').disabled=deviceBusy}};
function showTab(){
  const tab=['environment','trend','miku','device'].includes(location.hash.slice(1))?location.hash.slice(1):'environment';
  document.body.dataset.tab=tab;
  document.querySelectorAll('[data-tab-target]').forEach(b=>{if(b.dataset.tabTarget===tab)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current')});
  if(snapshot)charts(snapshot);
  if((tab==='trend'||matchMedia('(min-width:761px)').matches)&&Date.now()-dayLoadedAt>300000)loadDay();
}
document.querySelectorAll('[data-tab-target]').forEach(b=>b.onclick=()=>{location.hash=b.dataset.tabTarget;window.scrollTo(0,0)});
window.addEventListener('hashchange',showTab);showTab();
window.addEventListener('resize',()=>{if(matchMedia('(min-width:761px)').matches&&Date.now()-dayLoadedAt>300000)loadDay()});
new ResizeObserver(()=>{if(snapshot)charts(snapshot);if(daySnapshot&&(document.body.dataset.tab==='trend'||matchMedia('(min-width:761px)').matches))renderDay(daySnapshot)}).observe($('tChart').parentElement);
load();
</script></body></html>
)HTML";

const char SPEAKER_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>喇叭测试 · 环境健康助手</title><link rel="stylesheet" href="/ui.css"></head><body class="settings-page"><a class="back" href="/">返回环境健康助手</a><h1>喇叭测试</h1><p>播放 1.5 秒、880 Hz 的本地测试音。测试不经过网络语音服务，可用于比较本地声音与语音播放的区别。</p><p class="muted">播放完成后，DAC 自动关闭并保持静音。若测试音清晰而语音断续，可在首页查看缓冲中断计数。</p><button id="play" type="button">播放测试音</button><p id="result" role="status"></p><div class="tools"><a href="/qwen-config">语音设置</a><a href="/">查看播放状态</a></div><script>
document.getElementById('play').onclick=async function(){this.disabled=true;const result=document.getElementById('result');result.textContent='正在发送测试…';try{const r=await fetch('/speaker-test',{method:'POST',signal:AbortSignal.timeout(15000)});result.textContent=await r.text()}catch(e){result.textContent='设备连接失败，请稍后重试。'}finally{this.disabled=false}};
</script></body></html>
)HTML";
