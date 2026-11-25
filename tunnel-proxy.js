// 带 Basic Auth 的本地反向代理：cloudflared -> 本代理 -> ESP32:80。
// 只监听 127.0.0.1，公网无法直接访问这个端口。
const http = require('http');
const crypto = require('crypto');

const TARGET_HOST = process.env.ESP32_HOST;
const TARGET_PORT = Number(process.env.ESP32_PORT || 80);
const LISTEN_PORT = Number(process.env.PROXY_PORT || 8081);
const USER = process.env.PROXY_USER || 'admin';
const PASS = process.env.PROXY_PASS || '';

if (!TARGET_HOST) {
  console.error('缺少 ESP32_HOST，例如 ESP32_HOST=192.168.1.50');
  process.exit(1);
}
if (!PASS || PASS === 'change-me') {
  console.error('必须设置 PROXY_PASS 作为网页登录密码。');
  process.exit(1);
}

const HOP_BY_HOP = new Set([
  'connection', 'keep-alive', 'proxy-authenticate', 'proxy-authorization',
  'te', 'trailer', 'transfer-encoding', 'upgrade'
]);

function safeEqual(a, b) {
  const left = Buffer.from(String(a || ''));
  const right = Buffer.from(String(b || ''));
  return left.length === right.length && crypto.timingSafeEqual(left, right);
}

function authorized(req) {
  const header = req.headers.authorization || '';
  if (!header.startsWith('Basic ')) return false;
  let decoded = '';
  try {
    decoded = Buffer.from(header.slice(6), 'base64').toString('utf8');
  } catch {
    return false;
  }
  const split = decoded.indexOf(':');
  if (split < 0) return false;
  const userOk = safeEqual(decoded.slice(0, split), USER);
  const passOk = safeEqual(decoded.slice(split + 1), PASS);
  return userOk && passOk;
}

const agent = new http.Agent({ keepAlive: true, maxSockets: 4 });

const server = http.createServer((req, res) => {
  if (!authorized(req)) {
    res.writeHead(401, {
      'WWW-Authenticate': 'Basic realm="ESP32", charset="UTF-8"',
      'Content-Type': 'text/plain; charset=utf-8',
      'Cache-Control': 'no-store'
    });
    return res.end('需要登录');
  }

  const headers = {};
  for (const [name, value] of Object.entries(req.headers)) {
    if (!HOP_BY_HOP.has(name) && name !== 'authorization' && name !== 'host') {
      headers[name] = value;
    }
  }
  headers.host = TARGET_HOST;
  headers['x-forwarded-for'] = req.socket.remoteAddress || '';
  headers['x-forwarded-proto'] = 'https';

  const upstream = http.request({
    hostname: TARGET_HOST,
    port: TARGET_PORT,
    path: req.url,
    method: req.method,
    headers,
    agent
  }, (up) => {
    const responseHeaders = {};
    for (const [name, value] of Object.entries(up.headers)) {
      if (!HOP_BY_HOP.has(name)) responseHeaders[name] = value;
    }
    res.writeHead(up.statusCode || 502, responseHeaders);
    up.pipe(res);
  });

  upstream.on('error', (error) => {
    if (!res.headersSent) {
      res.writeHead(502, { 'Content-Type': 'text/plain; charset=utf-8' });
    }
    res.end('无法连接 ESP32：' + error.message);
  });

  req.pipe(upstream);
});

server.listen(LISTEN_PORT, '127.0.0.1', () => {
  console.log(`ESP32 auth proxy: http://127.0.0.1:${LISTEN_PORT} -> http://${TARGET_HOST}:${TARGET_PORT}`);
  console.log(`Login user: ${USER}`);
});
