const $ = id => document.getElementById(id);
const points = [];
let lastReceived = 0;
let loadedHistory = false;
const number = (v, digits=1) => Number.isFinite(v) ? v.toFixed(digits) : '—';
const clock = v => new Date(v).toLocaleTimeString('zh-CN',{hour12:false});

function update(sample) {
  if (!sample) return;
  const received = Date.parse(sample.receivedAt);
  if (!Number.isFinite(received) || received <= lastReceived) return;
  lastReceived = received;
  connection();
  $('temperature').textContent = number(sample.temperature);
  $('humidity').textContent = number(sample.humidity);
  $('margin').textContent = number(sample.condensationMargin);
  $('dew').textContent = number(sample.dewPoint) + ' ℃';
  $('absolute').textContent = number(sample.absoluteHumidity) + ' g/m³';
  $('mold').textContent = sample.moldMinutes > 0 ? (sample.moldRisk || '注意')+' · '+sample.moldMinutes+' 分钟' : sample.moldRisk || '—';
  $('event').textContent = sample.event || '暂无';
  $('advice').textContent = [sample.level,sample.advice].filter(Boolean).join(' · ');
  $('advice').hidden = !$('advice').textContent;
  $('updated').textContent = '更新于 '+clock(received);
  $('device').textContent = '设备 · '+sample.deviceId;
  $('alertText').textContent = sample.alertText;
  $('alert').hidden = !(sample.alertActive && sample.alertText);
  points.push(sample);
  if (points.length>180) points.shift();
  draw();
}

function draw() {
  const canvas = $('chart');
  canvas.hidden = points.length < 2;
  $('chartEmpty').hidden = !canvas.hidden;
  if (canvas.hidden) return;
  const w=canvas.clientWidth,h=canvas.clientHeight,dpr=window.devicePixelRatio||1;
  canvas.width=w*dpr; canvas.height=h*dpr;
  const ctx=canvas.getContext('2d');ctx.scale(dpr,dpr);ctx.font='12px system-ui';
  const start=Date.parse(points[0].receivedAt), end=Date.parse(points.at(-1).receivedAt);
  $('period').textContent=clock(start)+' – '+clock(end);
  function plot(key,label,color,top) {
    const values=points.map(p=>p[key]), min=Math.floor(Math.min(...values))-1, max=Math.ceil(Math.max(...values))+1;
    const bottom=top+h/2-9, y0=top+24;
    ctx.fillStyle=color;ctx.fillText(label,0,top+13);
    for(const tick of [min,(min+max)/2,max]){
      const y=bottom-(tick-min)/(max-min)*(bottom-y0);
      ctx.fillStyle='#516456';ctx.fillText(String(tick),0,y+3);
      ctx.strokeStyle='#dbe1d6';ctx.beginPath();ctx.moveTo(34,y);ctx.lineTo(w-4,y);ctx.stroke();
    }
    ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();
    points.forEach((p,i)=>{
      const x=34+(Date.parse(p.receivedAt)-start)/Math.max(1,end-start)*(w-38);
      const y=bottom-(p[key]-min)/(max-min)*(bottom-y0);
      if(i)ctx.lineTo(x,y);else ctx.moveTo(x,y);
    });
    ctx.stroke();ctx.lineWidth=1;
  }
  plot('temperature','温度 · ℃','#9d571d',0);
  plot('humidity','相对湿度 · %','#2c7084',h/2);
}
async function pollStatus() {
  try{
    const r=await fetch('/api/status',{cache:'no-store'});
    if(!r.ok){if(r.status===401)$('updated').textContent='请用带查看密码的链接打开';return}
    const data=await r.json();
    $('assistantLink').hidden=!data.assistant;
    if(!loadedHistory && data.latest){
      const before=points.length?points[0].receivedAt:data.latest.receivedAt;
      points.unshift(...data.history.filter(s=>s.receivedAt<before).slice(-Math.max(1,179-points.length)));
      loadedHistory=true;
      draw();
    }
    update(data.latest);
  }catch{$('updated').textContent='上位机连接中断，正在重试'}
}
pollStatus();
new EventSource('/api/events').onmessage=e=>update(JSON.parse(e.data));
setInterval(pollStatus,5000);
function connection(){
  const online=Date.now()-lastReceived<20000;
  $('state').textContent=online?'设备在线':lastReceived?'设备离线':'等待设备';
  $('state').className='state'+(online?'':' offline');
}
setInterval(connection,1000);
addEventListener('resize',draw);
