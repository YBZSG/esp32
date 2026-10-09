const $ = id => document.getElementById(id);
const points = [];
let lastReceived = 0;

function duration(seconds) {
  if (!Number.isFinite(seconds)) return '--';
  const h = Math.floor(seconds / 3600);
  const m = Math.floor(seconds % 3600 / 60);
  return `${h}小时 ${m}分钟`;
}

function update(sample) {
  if (!sample) return;
  const received = Date.parse(sample.receivedAt);
  if (!Number.isFinite(received) || received <= lastReceived) return;
  lastReceived = received;
  $('temperature').textContent = Number(sample.temperature).toFixed(1);
  $('humidity').textContent = Number(sample.humidity).toFixed(1);
  $('rssi').textContent = sample.rssi ?? '---';
  $('updated').textContent = `更新于 ${new Date(sample.receivedAt).toLocaleTimeString()}`;
  $('device').textContent = `设备：${sample.deviceId}`;
  $('uptime').textContent = `运行：${duration(sample.uptime)}`;
  points.push(sample);
  if (points.length > 120) points.shift();
  draw();
}

function draw() {
  const canvas = $('chart');
  const dpr = devicePixelRatio || 1;
  const w = canvas.clientWidth, h = canvas.clientHeight;
  canvas.width = w * dpr; canvas.height = h * dpr;
  const ctx = canvas.getContext('2d'); ctx.scale(dpr, dpr);
  ctx.clearRect(0, 0, w, h);
  ctx.strokeStyle = '#24344a'; ctx.lineWidth = 1;
  for (let y = 20; y < h - 20; y += (h - 40) / 4) { ctx.beginPath(); ctx.moveTo(35,y); ctx.lineTo(w-10,y); ctx.stroke(); }
  if (points.length < 2) return;
  const plot = (key, color, min, max) => {
    ctx.beginPath(); ctx.strokeStyle = color; ctx.lineWidth = 2.5;
    points.forEach((p,i) => {
      const x = 35 + i * (w - 50) / Math.max(points.length - 1, 1);
      const y = h - 20 - (p[key] - min) / (max - min) * (h - 40);
      i ? ctx.lineTo(x,y) : ctx.moveTo(x,y);
    }); ctx.stroke();
  };
  const temps = points.map(p => p.temperature), hums = points.map(p => p.humidity);
  plot('temperature','#ffb65c',Math.min(...temps)-2,Math.max(...temps)+2);
  plot('humidity','#43d7c7',Math.max(0,Math.min(...hums)-5),Math.min(100,Math.max(...hums)+5));
}

function pollStatus() {
  fetch('/api/status', { cache: 'no-store' })
    .then(r => {
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      return r.json();
    })
    .then(data => update(data.latest))
    .catch(() => {});
}

fetch('/api/status', { cache: 'no-store' }).then(r => r.json()).then(data => {
  points.push(...data.history.slice(-120));
  if (data.latest) update(data.latest); else draw();
});
new EventSource('/api/events').onmessage = event => update(JSON.parse(event.data));
setInterval(pollStatus, 5000);
setInterval(() => {
  const online = Date.now() - lastReceived < 20000;
  $('state').textContent = online ? '设备在线' : '设备离线';
  $('state').className = `state${online ? '' : ' offline'}`;
}, 1000);
addEventListener('resize', draw);
