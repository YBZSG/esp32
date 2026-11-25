const $ = id => document.getElementById(id);
const points = [];
let lastReceived = 0;

function number(value, digits = 1) {
  return Number.isFinite(Number(value)) ? Number(value).toFixed(digits) : '--';
}

function update(sample) {
  if (!sample) return;
  const received = Date.parse(sample.receivedAt);
  if (!Number.isFinite(received) || received <= lastReceived) return;
  lastReceived = received;

  $('temperature').textContent = number(sample.temperature);
  $('humidity').textContent = number(sample.humidity);
  $('margin').textContent = number(sample.condensationMargin);
  $('dew').textContent = number(sample.dewPoint) + ' °C';
  $('absolute').textContent = number(sample.absoluteHumidity) + ' g/m³';
  $('mold').textContent = sample.moldMinutes > 0
    ? `${sample.moldRisk || '注意'} · ${sample.moldMinutes} 分钟`
    : '低';
  $('event').textContent = sample.event || '--';
  $('updated').textContent = `更新于 ${new Date(sample.receivedAt).toLocaleTimeString('zh-CN', { hour12: false })}`;
  $('device').textContent = `设备：${sample.deviceId || 'ESP32 CYD'}`;

  const alert = $('alert');
  if (sample.alertActive && sample.alertText) {
    $('alertText').textContent = sample.alertText;
    alert.hidden = false;
  } else {
    alert.hidden = true;
  }

  points.push(sample);
  if (points.length > 180) points.shift();
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
  for (let y = 20; y < h - 20; y += (h - 40) / 4) { ctx.beginPath(); ctx.moveTo(35, y); ctx.lineTo(w - 10, y); ctx.stroke(); }
  if (points.length < 2) return;

  const plot = (key, color, min, max) => {
    ctx.beginPath(); ctx.strokeStyle = color; ctx.lineWidth = 2.5;
    points.forEach((point, index) => {
      const x = 35 + index * (w - 50) / Math.max(points.length - 1, 1);
      const y = h - 20 - (point[key] - min) / (max - min) * (h - 40);
      index ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.stroke();
  };

  const temps = points.map(p => Number(p.temperature));
  const hums = points.map(p => Number(p.humidity));
  plot('temperature', '#ffb65c', Math.min(...temps) - 2, Math.max(...temps) + 2);
  plot('humidity', '#43d7c7', Math.max(0, Math.min(...hums) - 5), Math.min(100, Math.max(...hums) + 5));
}

function pollStatus() {
  fetch('/api/status', { cache: 'no-store' })
    .then(response => {
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      return response.json();
    })
    .then(data => update(data.latest))
    .catch(() => {});
}

fetch('/api/status', { cache: 'no-store' }).then(response => response.json()).then(data => {
  points.push(...data.history.slice(-180));
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
