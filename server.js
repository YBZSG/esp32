const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const HOST = process.env.MONITOR_HOST || '0.0.0.0';
const PORT = Number(process.env.MONITOR_PORT || 8080);
const API_TOKEN = process.env.MONITOR_TOKEN || 'change-me-before-public-access';
const VIEW_TOKEN = process.env.MONITOR_VIEW_TOKEN || '';
const HISTORY_LIMIT = 720;
const publicDir = path.join(__dirname, 'public');
const assistantDir = path.join(__dirname, 'cyd-environment', 'web');
const clients = new Set();
const history = [];
let latest = null;

// 只读中继模式：设置 ESP32_URL 后，由电脑主动轮询设备的 /api，ESP32 无需改固件。
const ESP32_URL = (process.env.ESP32_URL || '').replace(/\/+$/, '');
const POLL_MS = Math.max(2000, Number(process.env.POLL_MS || 5000));
// 企业微信 / 钉钉群机器人都接受这个 text 消息格式。
const NOTIFY_WEBHOOK = process.env.NOTIFY_WEBHOOK || '';
const DINGTALK_SECRET = process.env.DINGTALK_SECRET || '';
const PUSHPLUS_TOKEN = process.env.PUSHPLUS_TOKEN || '';
const PUSHPLUS_API = process.env.PUSHPLUS_API || 'https://www.pushplus.plus/send';
const NOTIFY_RECOVERY = process.env.NOTIFY_RECOVERY !== '0';
const ALERT_FALLBACK = process.env.ALERT_FALLBACK !== '0';
const numEnv = (name, fallback) => {
  const value = Number(process.env[name]);
  return Number.isFinite(value) ? value : fallback;
};
const ALERT_T_MIN = numEnv('ALERT_T_MIN', 20);
const ALERT_T_MAX = numEnv('ALERT_T_MAX', 26);
const ALERT_H_MIN = numEnv('ALERT_H_MIN', 40);
const ALERT_H_MAX = numEnv('ALERT_H_MAX', 60);
const ALERT_HOLD_MIN = Math.max(0, numEnv('ALERT_HOLD_MIN', 10));
const ALERT_HOLD_MS = ALERT_HOLD_MIN * 60000;

function json(res, status, body) {
  const data = JSON.stringify(body);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(data),
    'Cache-Control': 'no-store'
  });
  res.end(data);
}

function safeEqual(a, b) {
  const left = Buffer.from(String(a || ''));
  const right = Buffer.from(String(b || ''));
  return left.length === right.length && crypto.timingSafeEqual(left, right);
}

function viewAuthorized(req) {
  const remote = req.socket.remoteAddress || '';
  const loopback = remote === '127.0.0.1' || remote === '::1' || remote === '::ffff:127.0.0.1';
  if (loopback && !req.headers['cf-connecting-ip']) return true;
  if (!VIEW_TOKEN) return true;
  const cookie = req.headers.cookie || '';
  const match = cookie.match(/(?:^|;\s*)monitor_view=([^;]+)/);
  return match && safeEqual(decodeURIComponent(match[1]), VIEW_TOKEN);
}

function numberOrNull(value) {
  if (value == null) return null;
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function buildSample(source) {
  if (source.temperature == null || source.humidity == null) return null;
  const temperature = Number(source.temperature);
  const humidity = Number(source.humidity);
  if (!Number.isFinite(temperature) || !Number.isFinite(humidity) ||
      temperature < -40 || temperature > 80 || humidity < 0 || humidity > 100) {
    return null;
  }
  return {
    deviceId: String(source.deviceId || 'esp32-cyd').slice(0, 64),
    temperature: Math.round(temperature * 10) / 10,
    humidity: Math.round(humidity * 10) / 10,
    dewPoint: numberOrNull(source.dewPoint),
    absoluteHumidity: numberOrNull(source.absoluteHumidity),
    enthalpy: numberOrNull(source.enthalpy),
    condensationMargin: numberOrNull(source.condensationMargin),
    moldMinutes: Math.max(0, Math.round(numberOrNull(source.moldMinutes) || 0)),
    moldRisk: String(source.moldRisk || '').slice(0, 16),
    level: String(source.level || '').slice(0, 64),
    advice: String(source.advice || '').slice(0, 300),
    event: String(source.event || '').slice(0, 200),
    alertEnabled: source.alertEnabled === true,
    alertActive: source.alertActive === true,
    alertText: String(source.alertText || '').slice(0, 500),
    rssi: numberOrNull(source.rssi),
    uptime: numberOrNull(source.uptime),
    receivedAt: new Date().toISOString()
  };
}

let fallbackOutSince = 0;
let alertNotified = false;
let lastPollLogAt = 0;

// A live relay can be restarted with its own /api/status snapshot. Preserve
// the original sample times and avoid replaying old measurements as alerts.
if (process.env.MONITOR_RESTORE_FILE) {
  const snapshot = JSON.parse(fs.readFileSync(process.env.MONITOR_RESTORE_FILE, 'utf8'));
  history.push(...snapshot.history.slice(-HISTORY_LIMIT));
  latest = history.at(-1) || null;
  if (latest && Date.now() - Date.parse(latest.receivedAt) < 2 * POLL_MS + 10000) {
    alertNotified = latest.alertActive;
    if (ALERT_FALLBACK && !latest.alertEnabled) {
      for (const s of history) {
        const outside = s.temperature < ALERT_T_MIN || s.temperature > ALERT_T_MAX ||
          s.humidity < ALERT_H_MIN || s.humidity > ALERT_H_MAX;
        fallbackOutSince = outside ? fallbackOutSince || Date.parse(s.receivedAt) : 0;
      }
    }
  }
}

function fallbackAlertText(sample) {
  const reasons = [];
  if (sample.temperature < ALERT_T_MIN) reasons.push('温度偏低');
  if (sample.temperature > ALERT_T_MAX) reasons.push('温度偏高');
  if (sample.humidity < ALERT_H_MIN) reasons.push('湿度偏低');
  if (sample.humidity > ALERT_H_MAX) reasons.push('湿度偏高');
  const holdText = ALERT_HOLD_MIN > 0 ? `已连续 ${ALERT_HOLD_MIN} 分钟超出` : '当前超出';
  return `${reasons.join('、') || '环境异常'}：当前 ${sample.temperature.toFixed(1)}℃、` +
    `${sample.humidity.toFixed(1)}%RH，${holdText} ` +
    `${ALERT_T_MIN}~${ALERT_T_MAX}℃ / ${ALERT_H_MIN}~${ALERT_H_MAX}%。`;
}

function alertMessage(text) {
  return `【ESP32 环境提醒】\n${text}\n时间：${new Date().toLocaleString('zh-CN', { hour12: false })}`;
}

function recoveryMessage(sample) {
  return `【ESP32 环境恢复】\n当前 ${sample.temperature.toFixed(1)}℃、` +
    `${sample.humidity.toFixed(1)}%RH，已回到预设区间。\n` +
    `时间：${new Date().toLocaleString('zh-CN', { hour12: false })}`;
}

async function sendRobot(text) {
  let url = NOTIFY_WEBHOOK;
  if (DINGTALK_SECRET) {
    const timestamp = Date.now();
    const sign = crypto.createHmac('sha256', DINGTALK_SECRET)
      .update(`${timestamp}\n${DINGTALK_SECRET}`)
      .digest('base64');
    url += `${url.includes('?') ? '&' : '?'}timestamp=${timestamp}&sign=${encodeURIComponent(sign)}`;
  }
  try {
    const response = await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ msgtype: 'text', text: { content: text } }),
      signal: AbortSignal.timeout(8000)
    });
    const reply = await response.text();
    console.log(`[notify] HTTP ${response.status} ${reply.slice(0, 200)}`);
  } catch (error) {
    console.error('[notify] 发送失败：' + error.message);
  }
}

async function sendPushPlus(text) {
  const title = text.includes('环境恢复') ? 'ESP32 环境恢复' : 'ESP32 环境提醒';
  try {
    const response = await fetch(PUSHPLUS_API, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ token: PUSHPLUS_TOKEN, title, content: text, template: 'txt' }),
      signal: AbortSignal.timeout(8000)
    });
    const reply = await response.text();
    let ok = response.ok;
    try {
      const data = JSON.parse(reply);
      if (Number(data.code) !== 200) ok = false;
    } catch {}
    if (ok) {
      console.log(`[pushplus] 已推送：${title}`);
    } else {
      console.error(`[pushplus] 推送失败：${reply.slice(0, 200)}`);
    }
  } catch (error) {
    console.error('[pushplus] 发送失败：' + error.message);
  }
}

async function sendNotification(text) {
  const jobs = [];
  if (NOTIFY_WEBHOOK) jobs.push(sendRobot(text));
  if (PUSHPLUS_TOKEN) jobs.push(sendPushPlus(text));
  if (!jobs.length) {
    console.warn('[notify] 未配置 NOTIFY_WEBHOOK 或 PUSHPLUS_TOKEN，跳过通知：' + text.split('\n')[1]);
    return;
  }
  await Promise.allSettled(jobs);
}

function evaluateAlerts(sample) {
  if (!sample.alertEnabled && !ALERT_FALLBACK) {
    if (alertNotified && NOTIFY_RECOVERY) sendNotification(recoveryMessage(sample));
    alertNotified = false;
    fallbackOutSince = 0;
    return;
  }

  let active = false;
  let text = '';
  if (sample.alertEnabled) {
    // 设备端 /alert-config 已启用：完全以设备计算结果为准。
    fallbackOutSince = 0;
    active = sample.alertActive;
    text = sample.alertText || '环境超出设备预设区间';
  } else {
    const out = sample.temperature < ALERT_T_MIN || sample.temperature > ALERT_T_MAX ||
      sample.humidity < ALERT_H_MIN || sample.humidity > ALERT_H_MAX;
    if (out) {
      if (!fallbackOutSince) fallbackOutSince = Date.now();
      if (Date.now() - fallbackOutSince >= ALERT_HOLD_MS) {
        active = true;
        text = fallbackAlertText(sample);
      }
    } else {
      fallbackOutSince = 0;
    }
  }

  if (active) {
    sample.alertActive = true;
    sample.alertText = text;
  }

  if (active && !alertNotified) {
    alertNotified = true;
    sendNotification(alertMessage(text));
  } else if (!active && alertNotified) {
    alertNotified = false;
    if (NOTIFY_RECOVERY) sendNotification(recoveryMessage(sample));
  }
}

function acceptSample(source) {
  const sample = buildSample(source);
  if (!sample) return null;
  evaluateAlerts(sample);
  latest = sample;
  history.push(sample);
  if (history.length > HISTORY_LIMIT) history.shift();
  broadcast(sample);
  return sample;
}

function broadcast(sample) {
  const line = `data: ${JSON.stringify(sample)}\n\n`;
  for (const res of clients) res.write(line);
}

function localAssistant(req) {
  const ip = (req.socket.remoteAddress || '').replace(/^::ffff:/, '');
  return !!ESP32_URL && !req.headers['cf-connecting-ip'] && !req.headers['x-forwarded-for'] &&
    (ip === '::1' || /^(127\.|10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)/.test(ip) || /^(fd|fc|fe80:)/i.test(ip));
}

async function assistantRequest(req, res, url) {
  const plain = (code, msg) => { res.writeHead(code, {'Content-Type':'text/plain; charset=utf-8','Cache-Control':'no-store'}); res.end(msg); };
  if (!viewAuthorized(req) || !localAssistant(req)) return plain(403, '智能助手需要从已连接 ESP32 的本机或局域网上位机打开。');
  if (req.method === 'POST' && req.headers.origin && req.headers.origin !== 'http://' + req.headers.host &&
      req.headers.origin !== 'https://' + req.headers.host) return plain(403, '请从本机上位机页面发送。');
  const options = {method:req.method, signal:AbortSignal.timeout(16000), redirect:'error'};
  let target = url.pathname;
  try {
    if (req.method === 'POST') {
      const chunks = [];
      let length = 0;
      for await (const chunk of req) {
        length += chunk.length;
        if (length > 10000) return plain(413, '消息超过设备的上下文容量。');
        chunks.push(chunk);
      }
      options.body = Buffer.concat(chunks);
      options.headers = {'Content-Type': url.pathname === '/deepseek/chat' ? 'application/json' : 'application/x-www-form-urlencoded'};
      if (url.pathname === '/deepseek/chat') target += '?turn=1';
    } else if (url.pathname === '/deepseek/result') {
      target += '?id=' + encodeURIComponent(url.searchParams.get('id') || '');
    }
    const response = await fetch(ESP32_URL + target, options);
    if (response.ok && target === '/assistant/state') {
      return json(res, 200, {...await response.json(), deviceUrl: ESP32_URL});
    }
    plain(response.status, await response.text());
  } catch {
    plain(503, '上位机暂时无法连接 ESP32，请检查设备网络。');
  }
}

function serveStatic(req, res) {
  const pathname = new URL(req.url, 'http://localhost').pathname;
  const requested = pathname === '/' ? '/index.html' : pathname;
  const assistantAsset = pathname === '/assistant' ? 'assistant.html' :
    ['/pets.png', '/leaf.png'].includes(pathname) ? pathname.slice(1) : null;
  const full = assistantAsset ? path.join(assistantDir, assistantAsset) : path.resolve(publicDir, `.${requested}`);
  if (!assistantAsset && !full.startsWith(publicDir + path.sep)) return json(res, 403, { error: 'forbidden' });
  fs.readFile(full, (error, data) => {
    if (error) return json(res, 404, { error: 'not found' });
    const ext = path.extname(full);
    const type = ext === '.html' ? 'text/html; charset=utf-8'
      : ext === '.css' ? 'text/css; charset=utf-8'
      : ext === '.js' ? 'application/javascript; charset=utf-8'
      : ext === '.png' ? 'image/png'
      : 'application/octet-stream';
    res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-cache' });
    res.end(data);
  });
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');

  if (req.method === 'GET' && url.pathname === '/' && url.searchParams.has('key')) {
    const key = url.searchParams.get('key');
    if (!safeEqual(key, VIEW_TOKEN)) return json(res, 401, { error: 'invalid view key' });
    res.writeHead(302, {
      Location: '/',
      'Set-Cookie': `monitor_view=${encodeURIComponent(key)}; HttpOnly; SameSite=Lax; Path=/; Max-Age=2592000`,
      'Cache-Control': 'no-store'
    });
    return res.end();
  }

  if (req.method === 'GET' && url.pathname === '/api/status') {
    if (!viewAuthorized(req)) return json(res, 401, { error: 'view authorization required' });
    return json(res, 200, { latest, history, assistant: localAssistant(req), serverTime: new Date().toISOString() });
  }
  if ((req.method === 'GET' && ['/assistant/state', '/deepseek/result'].includes(url.pathname)) ||
      (req.method === 'POST' && ['/assistant/settings', '/deepseek/chat'].includes(url.pathname))) {
    return assistantRequest(req, res, url);
  }

  if (req.method === 'GET' && url.pathname === '/api/events') {
    if (!viewAuthorized(req)) return json(res, 401, { error: 'view authorization required' });
    res.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache',
      Connection: 'keep-alive'
    });
    res.write(': connected\n\n');
    clients.add(res);
    req.on('close', () => clients.delete(res));
    return;
  }

  if (req.method === 'POST' && url.pathname === '/api/telemetry') {
    if (req.headers['cf-connecting-ip']) return json(res, 403, { error: 'tunneled telemetry is disabled' });
    if (!safeEqual(req.headers['x-api-token'], API_TOKEN)) {
      return json(res, 401, { error: 'invalid token' });
    }
    let raw = '';
    req.on('data', chunk => {
      raw += chunk;
      if (raw.length > 4096) req.destroy();
    });
    req.on('end', () => {
      try {
        const body = JSON.parse(raw);
        const sample = acceptSample(body);
        if (!sample) return json(res, 400, { error: 'invalid telemetry values' });
        json(res, 200, { ok: true, receivedAt: sample.receivedAt });
      } catch {
        json(res, 400, { error: 'invalid JSON' });
      }
    });
    return;
  }

  if (req.method === 'GET') return serveStatic(req, res);
  json(res, 405, { error: 'method not allowed' });
});

server.listen(PORT, HOST, () => {
  const interfaces = require('os').networkInterfaces();
  const addresses = Object.values(interfaces).flat().filter(i => i && i.family === 'IPv4' && !i.internal);
  console.log(`ESP32 monitor: http://localhost:${PORT}`);
  for (const item of addresses) console.log(`LAN address:    http://${item.address}:${PORT}`);
  if (API_TOKEN === 'change-me-before-public-access') {
    console.warn('WARNING: telemetry token is still the default; tunneled telemetry is blocked.');
  }
  if (!VIEW_TOKEN) console.warn('WARNING: dashboard view authentication is disabled.');
});

async function pollEsp32() {
  if (!ESP32_URL) return;
  try {
    const response = await fetch(`${ESP32_URL}/api`, {
      cache: 'no-store',
      signal: AbortSignal.timeout(5000)
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    const sample = acceptSample(data);
    if (!sample) throw new Error('invalid telemetry');
    if (Date.now() - lastPollLogAt >= 60000) {
      lastPollLogAt = Date.now();
      console.log(`[poll] ${sample.temperature}℃ ${sample.humidity}% ${sample.level || ''}`);
    }
  } catch (error) {
    console.warn(`[poll] ${ESP32_URL}: ${error.message}`);
  }
}

if (ESP32_URL) {
  console.log(`Relay mode: polling ${ESP32_URL}/api every ${POLL_MS} ms`);
  pollEsp32();
  setInterval(pollEsp32, POLL_MS);
} else {
  console.log('Push mode: waiting for POST /api/telemetry');
}
if (!NOTIFY_WEBHOOK && !PUSHPLUS_TOKEN) {
  console.warn('WARNING: NOTIFY_WEBHOOK / PUSHPLUS_TOKEN 未配置；区间提醒只会打印到控制台。');
} else {
  if (PUSHPLUS_TOKEN) console.log(`PushPlus: configured (${PUSHPLUS_TOKEN.slice(0, 4)}***)`);
  if (NOTIFY_WEBHOOK) console.log('Robot webhook: configured');
}
