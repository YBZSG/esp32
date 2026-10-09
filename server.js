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
const clients = new Set();
const history = [];
let latest = null;

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

function broadcast(sample) {
  const line = `data: ${JSON.stringify(sample)}\n\n`;
  for (const res of clients) res.write(line);
}

function serveStatic(req, res) {
  const pathname = new URL(req.url, 'http://localhost').pathname;
  const requested = pathname === '/' ? '/index.html' : pathname;
  const full = path.resolve(publicDir, `.${requested}`);
  if (!full.startsWith(publicDir + path.sep)) return json(res, 403, { error: 'forbidden' });
  fs.readFile(full, (error, data) => {
    if (error) return json(res, 404, { error: 'not found' });
    const ext = path.extname(full);
    const type = ext === '.html' ? 'text/html; charset=utf-8'
      : ext === '.css' ? 'text/css; charset=utf-8'
      : ext === '.js' ? 'application/javascript; charset=utf-8'
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
    return json(res, 200, { latest, history, serverTime: new Date().toISOString() });
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
        const temperature = Number(body.temperature);
        const humidity = Number(body.humidity);
        if (!Number.isFinite(temperature) || !Number.isFinite(humidity) ||
            temperature < -40 || temperature > 80 || humidity < 0 || humidity > 100) {
          return json(res, 400, { error: 'invalid DHT22 values' });
        }
        latest = {
          deviceId: String(body.deviceId || 'esp32-dht22').slice(0, 64),
          temperature: Math.round(temperature * 10) / 10,
          humidity: Math.round(humidity * 10) / 10,
          rssi: Number.isFinite(Number(body.rssi)) ? Number(body.rssi) : null,
          uptime: Number.isFinite(Number(body.uptime)) ? Number(body.uptime) : null,
          receivedAt: new Date().toISOString()
        };
        history.push(latest);
        if (history.length > HISTORY_LIMIT) history.shift();
        broadcast(latest);
        json(res, 200, { ok: true, receivedAt: latest.receivedAt });
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
