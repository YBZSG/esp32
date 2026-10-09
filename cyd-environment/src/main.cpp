#include <Arduino.h>
#include <DHT.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <SdFat.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>

SoftSpiDriver<19, 23, 18> cardSpi;
SdFs storage;
bool cardReady = false;
bool mikuAssetReady = false;
uint32_t lastCardLog = 0;
FsFile mikuUploadFile;
bool mikuUploadActive = false;
WebSocketsClient qwenSocket;
String qwenKey;
String qwenVoice = "Cherry";
String ttsPendingText;
String ttsStatus = "未配置";
String qwenErrorDetail;
bool qwenSocketEnabled = false;
bool ttsBusy = false;
bool ttsAudioDone = false;
bool ttsError = false;
bool ttsOverflow = false;
char qwenAuthHeader[240]{};
constexpr size_t AUDIO_RING_CAP = 24576; // 约 1 秒的 24 kHz、8 bit DAC 缓冲（配合背压不丢帧）
// Allocated once at audio init (heap is clean then, so a 32 KB contiguous block
// is available) instead of living in static .bss which is over budget.
uint8_t* audioRing = nullptr;
volatile size_t audioHead = 0, audioTail = 0;
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
bool audioReady = false;
TaskHandle_t audioTaskHandle = nullptr;
#include "board_config.h"
#include "display.h"

// Miku animation is stored as miku.rle on the TF card. The original header is
// kept only as the source for tools/make_miku_asset.py and is not compiled.
constexpr uint16_t MIKU_W = 112;
constexpr uint16_t MIKU_H = 122;
constexpr uint16_t MIKU_TRANSPARENT = 0xF81F;
constexpr uint8_t MIKU_FRAME_COUNT = 57;
constexpr uint8_t MIKU_IDLE_START = 0, MIKU_IDLE_COUNT = 6;
constexpr uint8_t MIKU_WAVE_START = 6, MIKU_WAVE_COUNT = 4;
constexpr uint8_t MIKU_JUMP_START = 10, MIKU_JUMP_COUNT = 5;
constexpr uint8_t MIKU_FAILED_START = 15, MIKU_FAILED_COUNT = 8;
constexpr uint8_t MIKU_WAITING_START = 23, MIKU_WAITING_COUNT = 6;
constexpr uint8_t MIKU_WORKING_START = 29, MIKU_WORKING_COUNT = 6;
constexpr uint8_t MIKU_REVIEW_START = 35, MIKU_REVIEW_COUNT = 6;
constexpr uint8_t MIKU_LOOK_A_START = 41, MIKU_LOOK_A_COUNT = 8;
constexpr uint8_t MIKU_LOOK_B_START = 49, MIKU_LOOK_B_COUNT = 8;
constexpr uint16_t MIKU_ASSET_HEADER = 10;
constexpr uint16_t MIKU_ASSET_INDEX_ENTRY = 6;
constexpr uint16_t MIKU_RLE_MAX = 23000;
uint32_t mikuFrameOffsets[MIKU_FRAME_COUNT]{};
uint16_t mikuFrameSizes[MIKU_FRAME_COUNT]{};
uint8_t mikuRle[MIKU_RLE_MAX];

CYDDisplay lcd;
LGFX_Sprite petCanvas(&lcd);
DHT dht(DHT_PIN, DHT_TYPE);
Preferences prefs;
WebServer web(80);
bool webStarted = false;
struct ChatJob { String question; String answer; SemaphoreHandle_t done = nullptr; } chatJob;
bool chatBusy = false;
// Set while the pet sprite is temporarily released so mbedTLS / large WS
// frames can allocate contiguous heap on this no-PSRAM board.
volatile bool petCanvasHeld = false;
// True only when petCanvas holds a valid sprite buffer. The sprite exists only
// while the pet page is active (and not held for a network session).
volatile bool petCanvasReady = false;
// Set after a chat reply; the sprite is rebuilt only once chat AND TTS are both
// idle (so the Qwen TLS session also gets the freed contiguous block).
volatile bool petCanvasNeedsRestore = false;


uint16_t C_BG, C_PANEL, C_BLUE, C_CYAN, C_GREEN, C_YELLOW, C_RED, C_TEXT, C_MUTED, C_HEADER;
bool darkMode = false;

enum Page { DASHBOARD, TREND, PET, WIFI_LIST, KEYBOARD };
Page page = DASHBOARD;

enum PetMotion { PET_AUTO, PET_IDLE, PET_WAVE, PET_JUMP, PET_FAILED, PET_WAITING, PET_WORKING, PET_REVIEW, PET_LOOK };
PetMotion petMotion = PET_AUTO;
void startPetMotion(PetMotion motion, uint32_t duration);
void drawPage();
void drawPetPage();
void drawPetConversation();
void drawPetFrame();
uint16_t petFrameInterval();
bool startQwenTts(const String& text);
void qwenSocketEvent(WStype_t type, uint8_t* payload, size_t length);
void initAudioOutput();
uint8_t petFrame = 0, petLookFrame = 0;
uint32_t petFrameAt = 0, petOverrideUntil = 0;
uint16_t petPixels[MIKU_W * MIKU_H];

struct Sample { float t, h; uint32_t ms; };
constexpr int HISTORY_SIZE = 72;       // 6 分钟，5 秒一个点
Sample history[HISTORY_SIZE]{};
int historyCount = 0, historyHead = 0;
float temperature = NAN, humidity = NAN;
float shownT = NAN, shownH = NAN, animFromT = NAN, animFromH = NAN;
uint32_t animStarted = 0, lastAnimFrame = 0;
float emaT = NAN, emaH = NAN;
float forecastT = NAN, forecastH = NAN;
float meanT = NAN, meanH = NAN, minT = NAN, maxT = NAN, minH = NAN, maxH = NAN;
uint32_t lastRead = 0, lastUi = 0, connectStarted = 0;

String ssids[20];
int rssis[20]{};
int wifiCount = 0, wifiPage = 0;
String selectedSsid, password;
String deepseekKey;
String petReply;
uint8_t keyboardMode = 0;  // 0 小写，1 大写，2 符号
bool connecting = false;
String statusText = "正在初始化";
uint8_t sensorFailCount = 0;

const char WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>环境健康助手</title><style>
*{box-sizing:border-box}body{margin:0;background:#f4f7fb;color:#172033;font-family:system-ui,"Microsoft YaHei",sans-serif}.wrap{max-width:760px;margin:auto;padding:18px}.top{display:flex;justify-content:space-between;align-items:center}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#2ac779;margin-right:6px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin:18px 0}.card{background:#fff;border:2px solid transparent;border-radius:18px;padding:18px;box-shadow:0 8px 30px #26436b12;transition:border-color .35s,color .35s}.label{color:#718096;display:flex;justify-content:space-between}.badge{font-size:13px;font-weight:700}.value{font-size:38px;font-weight:750;margin-top:5px;transition:color .35s}.state{font-size:21px;font-weight:700;color:#157f50}.advice{line-height:1.75}.muted{color:#718096;font-size:14px}canvas{width:100%;height:180px}input,textarea,button{width:100%;padding:11px;border:1px solid #d8dfeb;border-radius:10px;font:inherit;margin-top:9px}button{background:#20a66a;color:white;border:0;font-weight:700;cursor:pointer}textarea{min-height:82px;resize:vertical}.reply{white-space:pre-wrap;line-height:1.65;margin-top:12px}@media(max-width:480px){.wrap{padding:12px}.value{font-size:32px}}
</style></head><body><main class="wrap"><div class="top"><h2>环境健康助手</h2><span class="muted"><i class="dot"></i>设备在线</span></div>
<section class="grid"><div class="card" id="tcard"><div class="label">温度 <b class="badge" id="tl">未就绪</b></div><div class="value" id="tv"><span id="t">--</span> ℃</div></div><div class="card" id="hcard"><div class="label">湿度 <b class="badge" id="hl">未就绪</b></div><div class="value" id="hv"><span id="h">--</span> %</div></div></section>
<section class="card"><div id="state" class="state">正在读取</div><p id="advice" class="advice"></p><div id="extra" class="muted"></div></section><section class="card" style="margin-top:12px"><div class="label">最近趋势</div><canvas id="chart" width="700" height="180"></canvas></section><section class="card" style="margin-top:12px"><h3>Miku · DeepSeek</h3><div class="muted">密钥只保存在这块 ESP32；请仅在可信局域网中填写。</div><input id="key" type="password" autocomplete="off" placeholder="DeepSeek API Key（留空不会覆盖）"><button onclick="saveKey()">保存 API Key</button><textarea id="q" placeholder="和 Miku 聊聊，或询问当前环境"></textarea><button onclick="chat()">发送给 Miku</button><div id="reply" class="reply muted"></div></section><p><a href="/storage">TF卡状态</a> · <a href="/speaker-test">喇叭测试</a></p></main>
<script>const $=id=>document.getElementById(id),cv=$('chart'),cx=cv.getContext('2d');function tm(v){return v<=12?['过低','#2367c9']:v<18?['偏低','#22a6d8']:v<=26?['舒适','#20a66a']:v<30?['偏高','#f59e0b']:['过高','#e5484d']}function hm(v){return v<25?['过低','#ef6c35']:v<35?['偏低','#eab308']:v<=65?['舒适','#218bea']:v<=75?['偏高','#8657d9']:['过高','#c832b9']}function paint(v,m,p){let [l,c]=m(v);$(p+'l').textContent=l;$(p+'l').style.color=c;$(p+'v').style.color=c;$(p+'card').style.borderColor=c}function graph(a,b){cx.clearRect(0,0,700,180);let all=a.concat(b);if(all.length<2)return;let lo=Math.min(...all),hi=Math.max(...all);if(hi-lo<2)hi=lo+2;function line(v,c){cx.beginPath();cx.strokeStyle=c;cx.lineWidth=3;v.forEach((n,i)=>{let x=i*699/(v.length-1),y=170-(n-lo)*155/(hi-lo);i?cx.lineTo(x,y):cx.moveTo(x,y)});cx.stroke()}line(a,'#f59e0b');line(b,'#218bea')}async function load(){try{let d=await fetch('/api',{cache:'no-store'}).then(r=>r.json());$('t').textContent=d.temperature?.toFixed(1)??'--';$('h').textContent=d.humidity?.toFixed(1)??'--';if(d.temperature!=null)paint(d.temperature,tm,'t');if(d.humidity!=null)paint(d.humidity,hm,'h');$('state').textContent=d.level;$('advice').textContent=d.advice;$('extra').textContent=`结露温度 ${d.dewPoint?.toFixed(1)??'--'}℃　10分钟趋势 ${d.forecastT?.toFixed(1)??'--'}℃ / ${d.forecastH?.toFixed(0)??'--'}%`;graph(d.historyT,d.historyH)}catch(e){$('state').textContent='设备暂时离线'}}async function saveKey(){let k=$('key').value.trim();if(!k)return;$('reply').textContent='正在保存…';let r=await fetch('/deepseek/config',{method:'POST',headers:{'Content-Type':'text/plain'},body:k});$('key').value='';$('reply').textContent=await r.text()}async function chat(){let q=$('q').value.trim();if(!q)return;$('reply').textContent='Miku 正在思考…';try{let r=await fetch('/deepseek/chat',{method:'POST',headers:{'Content-Type':'text/plain'},body:q});while(r.status===202){await new Promise(ok=>setTimeout(ok,700));r=await fetch('/deepseek/result',{cache:'no-store'})}$('reply').textContent=await r.text()}catch(e){$('reply').textContent='请求失败，请检查设备连接'}}load();setInterval(load,2000)</script></body></html>
)HTML";

void chineseFont(bool bold = false) {
  lcd.setFont(bold ? &fonts::efontCN_16_b : &fonts::efontCN_16);
  lcd.setTextSize(1);
  lcd.setTextDatum(textdatum_t::top_left);
}

void applyTheme() {
  if (darkMode) {
    C_BG = 0x0841; C_PANEL = 0x18E3; C_HEADER = 0x10C4;
    C_BLUE = 0x249F; C_CYAN = 0x2E7F; C_GREEN = 0x4E8C;
    C_YELLOW = 0xFDC0; C_RED = 0xF9C7; C_TEXT = 0xFFFF; C_MUTED = 0xA514;
    lcd.setBrightness(145);
  } else {
    C_BG = 0xFFFF; C_PANEL = 0xEF5D; C_HEADER = 0xFFFF;
    C_BLUE = 0x3D7F; C_CYAN = 0x251F; C_GREEN = 0x3DA8;
    C_YELLOW = 0xFCE0; C_RED = 0xE9C7; C_TEXT = 0x18E3; C_MUTED = 0x6B4D;
    lcd.setBrightness(230);
  }
}

void text(const String& s, int x, int y, uint16_t color = C_TEXT, bool bold = false) {
  chineseFont(bold);
  lcd.setTextColor(color);
  lcd.drawString(s, x, y);
}

void panel(int x, int y, int w, int h, uint16_t color = C_PANEL) {
  lcd.fillRoundRect(x, y, w, h, 7, color);
}

void button(const String& label, int x, int y, int w, int h, uint16_t color = C_BLUE) {
  lcd.fillRoundRect(x, y, w, h, 6, color);
  chineseFont(true);
  lcd.setTextColor(C_TEXT, color);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.drawString(label, x + w / 2, y + h / 2);
  lcd.setTextDatum(textdatum_t::top_left);
}

float dewPoint(float t, float rh) {
  if (!isfinite(t) || !isfinite(rh) || rh <= 0) return NAN;
  const float a = 17.62f, b = 243.12f;
  float gamma = log(rh / 100.0f) + a * t / (b + t);
  return b * gamma / (a - gamma);
}

void calculateStats() {
  if (!historyCount) return;
  float sumT = 0, sumH = 0;
  minT = minH = 999; maxT = maxH = -999;
  double sx = 0, sxx = 0, sxt = 0, sxh = 0;
  const uint32_t origin = history[(historyHead - historyCount + HISTORY_SIZE) % HISTORY_SIZE].ms;
  for (int i = 0; i < historyCount; ++i) {
    const Sample& s = history[(historyHead - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE];
    sumT += s.t; sumH += s.h;
    minT = min(minT, s.t); maxT = max(maxT, s.t);
    minH = min(minH, s.h); maxH = max(maxH, s.h);
    double x = (s.ms - origin) / 60000.0;
    sx += x; sxx += x * x; sxt += x * s.t; sxh += x * s.h;
  }
  meanT = sumT / historyCount; meanH = sumH / historyCount;
  if (historyCount >= 6) {
    double den = historyCount * sxx - sx * sx;
    double horizon = (history[historyHead ? historyHead - 1 : HISTORY_SIZE - 1].ms - origin) / 60000.0 + 10.0;
    if (fabs(den) > 0.0001) {
      double slopeT = (historyCount * sxt - sx * sumT) / den;
      double slopeH = (historyCount * sxh - sx * sumH) / den;
      forecastT = constrain(meanT + slopeT * (horizon - sx / historyCount), -20.0, 60.0);
      forecastH = constrain(meanH + slopeH * (horizon - sx / historyCount), 0.0, 100.0);
    }
  }
}

void addSample(float t, float h) {
  history[historyHead] = {t, h, millis()};
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  historyCount = min(historyCount + 1, HISTORY_SIZE);
  emaT = isfinite(emaT) ? 0.22f * t + 0.78f * emaT : t;
  emaH = isfinite(emaH) ? 0.22f * h + 0.78f * emaH : h;
  calculateStats();
}

String environmentLevel() {
  if (!isfinite(temperature)) return "传感器未就绪";
  if (temperature >= 30 || temperature <= 12 || humidity >= 75 || humidity <= 25) return "环境风险较高";
  if (temperature > 27 || temperature < 18 || humidity > 65 || humidity < 35) return "环境需要调整";
  if (temperature >= 20 && temperature <= 26 && humidity >= 40 && humidity <= 60) return "环境舒适";
  return "环境基本适宜";
}

String advice() {
  if (!isfinite(temperature)) return "请检查温湿度模块和GPIO27接口";
  if (temperature >= 30 && humidity >= 70) return "闷热潮湿：通风并开启空调除湿，及时补水";
  if (temperature >= 28) return "温度偏高：遮阳、通风或开启制冷，避免久坐闷热处";
  if (temperature <= 15) return "温度偏低：关闭冷风入口并适当供暖，注意保暖";
  if (humidity >= 70) return "湿度过高：开窗换气或除湿，留意墙面和设备凝露";
  if (humidity <= 30) return "空气干燥：适度加湿和补水，避免加湿器直吹";
  if (humidity > 60) return "湿度稍高：增加通风，减少室内晾晒和水汽来源";
  if (humidity < 40) return "湿度稍低：可适度加湿，并保持饮水";
  if (temperature > 26) return "略偏暖：减少热源并加强空气流动";
  if (temperature < 20) return "略偏凉：减少穿堂风并适当增加衣物";
  return "当前舒适：维持通风，继续观察趋势即可";
}

String jsonNumber(float value, int digits = 1) {
  return isfinite(value) ? String(value, digits) : String("null");
}

// The web form accepts the token itself, or a copied "Bearer <token>" value.
// Do not accept the DashScope Base URL here: it is an HTTP endpoint, not a
// credential, and sending it as a bearer token only produces a WebSocket 401.
String normalizeQwenKey(String raw) {
  raw.trim();
  if (raw.startsWith("Authorization:")) {
    raw = raw.substring(strlen("Authorization:"));
    raw.trim();
  }
  if (raw.startsWith("Bearer ") || raw.startsWith("bearer ")) {
    raw = raw.substring(7);
    raw.trim();
  }
  return raw;
}

bool isLikelyQwenKey(const String& key) {
  if (key.length() < 12 || key.length() > 200) return false;
  String lower = key;
  lower.toLowerCase();
  if (lower.startsWith("http://") || lower.startsWith("https://") ||
      lower.indexOf("dashscope.aliyuncs.com") >= 0 || lower.indexOf("/compatible-mode/") >= 0) {
    return false;
  }
  for (size_t i = 0; i < key.length(); ++i) {
    const char c = key[i];
    if (c <= 0x20 || c == '\r' || c == '\n') return false;
  }
  return true;
}

void setQwenTransportError(const uint8_t* payload, size_t length) {
  qwenErrorDetail = "";
  if (payload && length) {
    size_t shown = min(length, (size_t)160);
    qwenErrorDetail.reserve(shown);
    for (size_t i = 0; i < shown; ++i) qwenErrorDetail += (char)payload[i];
    qwenErrorDetail.trim();
  }
  String lower = qwenErrorDetail;
  lower.toLowerCase();
  if (lower.indexOf("401") >= 0) {
    ttsStatus = "API Key 无效或未授权（HTTP 401）";
  } else if (lower.indexOf("403") >= 0) {
    ttsStatus = "API Key 无权限（HTTP 403）";
  } else if (lower.indexOf("handshake") >= 0) {
    ttsStatus = "Qwen WebSocket 握手失败";
  } else {
    ttsStatus = "Qwen WebSocket 连接失败";
  }
  ttsError = true;
}

String telemetryJson() {
  String json;
  json.reserve(2600);
  json = "{\"temperature\":" + jsonNumber(temperature) +
         ",\"humidity\":" + jsonNumber(humidity) +
         ",\"dewPoint\":" + jsonNumber(dewPoint(temperature, humidity)) +
         ",\"forecastT\":" + jsonNumber(forecastT) +
         ",\"forecastH\":" + jsonNumber(forecastH) +
         ",\"level\":\"" + environmentLevel() +
         "\",\"advice\":\"" + advice() + "\",\"historyT\":[";
  for (int i = 0; i < historyCount; ++i) {
    if (i) json += ',';
    json += String(history[(historyHead - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE].t, 1);
  }
  json += "],\"historyH\":[";
  for (int i = 0; i < historyCount; ++i) {
    if (i) json += ',';
    json += String(history[(historyHead - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE].h, 1);
  }
  json += "]}";
  return json;
}

// Allocate the tightly-sized Miku sprite (112x122 ~= 27 KB). Returns false and
// leaves petCanvasReady cleared if no contiguous block is available yet.
bool ensurePetCanvas() {
  if (petCanvasReady) return true;
  if (!petCanvas.createSprite(MIKU_W, MIKU_H)) {
    Serial.printf("PetCanvas alloc FAILED, free heap=%u largest=%u\n",
                  ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return false;
  }
  petCanvas.setSwapBytes(true);
  petCanvasReady = true;
  petFrameAt = 0;  // force the next loop tick to redraw the pet
  return true;
}

// Free the sprite buffer (called for network sessions or when leaving the pet
// page so other pages get the full ~27 KB back).
void discardPetCanvas() {
  if (petCanvasReady) {
    petCanvas.deleteSprite();
    petCanvasReady = false;
  }
}

// Release the sprite to free a contiguous block for the TLS handshake and the
// large TTS audio frames; pet animation is frozen while held (drawPetFrame).
void holdPetCanvas() {
  petCanvasHeld = true;
  delay(30);  // let any in-progress frame draw in loopTask finish
  discardPetCanvas();
  Serial.printf("PetCanvas held, free heap=%u, largest=%u\n",
                ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
void releasePetCanvas() {
  if (ensurePetCanvas()) {
    petCanvasHeld = false;
    petCanvasNeedsRestore = false;
    Serial.printf("PetCanvas restored, free heap=%u\n", ESP.getFreeHeap());
  }
}

String askDeepSeek(const String& question) {
  if (!deepseekKey.length()) return "请先在网页中保存 DeepSeek API Key。";
  if (WiFi.status() != WL_CONNECTED) return "设备尚未联网。";
  // Free the ~47 KB pet sprite so mbedTLS can allocate the contiguous buffers
  // its X.509 handshake needs (otherwise "X509 - Allocation of memory failed").
  holdPetCanvas();
  String result;
  WiFiClientSecure secure;
  secure.setInsecure();
  HTTPClient http;
  http.setTimeout(25000);
  if (!http.begin(secure, "https://api.deepseek.com/chat/completions")) {
    Serial.println("DeepSeek: http.begin failed");
    result = "无法建立 DeepSeek 连接。";
  } else {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + deepseekKey);
    JsonDocument request;
    request["model"] = "deepseek-chat";
    request["temperature"] = 0.7;
    request["max_tokens"] = 500;
    JsonArray messages = request["messages"].to<JsonArray>();
    JsonObject system = messages.add<JsonObject>();
    system["role"] = "system";
    String context = "你是触摸屏宠物Miku，用简短自然的中文陪伴用户。当前温度" + jsonNumber(temperature) +
                     "摄氏度，湿度" + jsonNumber(humidity) + "%，环境状态：" + environmentLevel() +
                     "。回答可参考环境数据，但不要把每个话题都强行转成环境建议。";
    system["content"] = context;
    JsonObject user = messages.add<JsonObject>();
    user["role"] = "user"; user["content"] = question;
    String body; serializeJson(request, body);
    Serial.printf("DeepSeek: POST, free heap=%u\n", ESP.getFreeHeap());
    int code = http.POST(body);
    String response = http.getString();
    char tlsErr[64] = {0};
    secure.lastError(tlsErr, sizeof(tlsErr));
    Serial.printf("DeepSeek: code=%d (%s), respLen=%u, heap=%u, tlsErr=%s\n",
                  code, http.errorToString(code).c_str(), (unsigned)response.length(),
                  ESP.getFreeHeap(), tlsErr);
    http.end();
    if (code != 200) {
      result = "DeepSeek 请求失败（HTTP " + String(code) + "），请检查密钥或额度。";
    } else {
      JsonDocument doc;
      if (deserializeJson(doc, response)) {
        result = "DeepSeek 返回内容无法解析。";
      } else {
        const char* answer = doc["choices"][0]["message"]["content"] | "";
        result = strlen(answer) ? String(answer) : String("DeepSeek 没有返回内容。");
      }
    }
  }
  // Keep the sprite released through the Qwen TTS session that follows: its
  // TLS handshake and audio frames need the same contiguous block. The sprite
  // is rebuilt from loop() once both chat and TTS are idle.
  petCanvasNeedsRestore = true;
  return result;
}

size_t audioRingCount() {
  size_t count;
  portENTER_CRITICAL(&audioMux);
  count = audioHead >= audioTail ? audioHead - audioTail : AUDIO_RING_CAP - audioTail + audioHead;
  portEXIT_CRITICAL(&audioMux);
  return count;
}

void audioRingClear() {
  portENTER_CRITICAL(&audioMux);
  audioHead = audioTail = 0;
  portEXIT_CRITICAL(&audioMux);
}

size_t audioRingPush(const uint8_t* data, size_t length) {
  // Real-time back-pressure: when the ring is full, yield to the audio task
  // (higher priority) until it drains space instead of dropping samples. This
  // runs in loopTask (the WebSocket callback); blocking here only pauses the
  // next frame read, and TCP flow control holds the rest without data loss.
  size_t pushed = 0;
  uint32_t start = millis();
  while (pushed < length) {
    portENTER_CRITICAL(&audioMux);
    while (pushed < length) {
      size_t next = (audioHead + 1) % AUDIO_RING_CAP;
      if (next == audioTail) break;  // ring full
      audioRing[audioHead] = data[pushed++];
      audioHead = next;
    }
    portEXIT_CRITICAL(&audioMux);
    if (pushed >= length) break;
    if (millis() - start > 3000) { ttsOverflow = true; break; }
    vTaskDelay(1);
  }
  return pushed;
}

size_t audioRingPop(uint8_t* data, size_t maxLength) {
  size_t popped = 0;
  portENTER_CRITICAL(&audioMux);
  while (popped < maxLength && audioTail != audioHead) {
    data[popped++] = audioRing[audioTail];
    audioTail = (audioTail + 1) % AUDIO_RING_CAP;
  }
  portEXIT_CRITICAL(&audioMux);
  return popped;
}

void audioOutputTask(void*) {
  uint8_t samples8[512];
  // Built-in DAC mode uses stereo frames (L then R) as in the official CYD MP3
  // demo; identical sample in both slots so DAC1(GPIO25)/DAC2(GPIO26) match.
  uint16_t samples16[1024];
  for (;;) {
    // Always feed a full 512-sample frame at the real-time rate. Any samples not
    // available in the ring are filled with the DAC midpoint (silence), so the
    // DMA stream never underruns — that is what removed the choppy gaps during
    // network bursts. i2s_write blocks until DMA accepts, pacing the task.
    size_t count = audioRingPop(samples8, sizeof(samples8));
    for (size_t i = count; i < sizeof(samples8); ++i) samples8[i] = 128;
    for (size_t i = 0; i < sizeof(samples8); ++i) {
      uint16_t v = (uint16_t)samples8[i] << 8;
      samples16[i * 2] = v;      // L slot -> DAC1 / GPIO25
      samples16[i * 2 + 1] = v;  // R slot -> DAC2 / GPIO26 -> SC8002B IN+
    }
    size_t written = 0;
    i2s_write(I2S_NUM_0, samples16, sizeof(samples16), &written, pdMS_TO_TICKS(100));
  }
}

void initAudioOutput() {
  // Allocate the audio ring early, while the heap is still unfragmented.
  if (!audioRing) {
    audioRing = static_cast<uint8_t*>(heap_caps_malloc(AUDIO_RING_CAP, MALLOC_CAP_8BIT));
  }
  if (!audioRing) { Serial.println("Audio I2S: ring alloc failed"); return; }
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  config.sample_rate = 24000;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  // Mirrors the official CYD MP3 demo (ESP8266Audio AudioOutputI2S for the
  // built-in DAC): stereo frames, both DAC pads enabled, MSB-aligned standard
  // format, and the audio PLL enabled on rev>0 silicon for a clean DAC clock.
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  // 8 buffers x 256 stereo frames = ~85 ms of audio in hardware DMA (vs ~42 ms
  // with 8x128, which stuttered), while keeping the DMA memory footprint at
  // 8 KB: 16 KB of DMA buffers fragmented the heap enough that the WebSockets
  // library failed to malloc ~21 KB for incoming TTS audio frames and dropped
  // the connection before any audio arrived.
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = true;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) {
    Serial.println("Audio I2S: unavailable");
    return;
  }
  i2s_set_pin(I2S_NUM_0, nullptr);
  // Both built-in DACs (GPIO25/DAC1, GPIO26/DAC2); the SC8002B IN+ is coupled
  // to the DAC output and the amp shutdown pin is tied to 3.3V in hardware, so
  // no GPIO pinMode is needed (the official demo does not touch them either).
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
  i2s_zero_dma_buffer(I2S_NUM_0);
  // Priority above Arduino loopTask (1) so WiFi/UI cannot starve the DAC DMA.
  audioReady = xTaskCreate(audioOutputTask, "audio-output", 4096, nullptr, 5, &audioTaskHandle) == pdPASS;
  Serial.printf("Audio I2S: %s\n", audioReady ? "ready: built-in DAC stereo (GPIO25/26), APLL on" : "task failed");
}

void qwenSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  // Keep protocol diagnostics on the serial port only. Never print the
  // Authorization header or any stored API key.
  if (type == WStype_TEXT && payload && length) {
    // Audio delta frames are large base64 blobs: log their length only instead
    // of flooding the UART (which would starve the real-time audio path).
    if (strstr(reinterpret_cast<const char*>(payload), "response.audio.delta") != nullptr) {
      Serial.printf("Qwen WS audio delta: %u bytes\n", (unsigned)length);
    } else {
      size_t shown = min(length, (size_t)512);
      Serial.printf("Qwen WS text (%u): %.*s\n", (unsigned)length, (int)shown,
                    reinterpret_cast<const char*>(payload));
    }
  } else if (type == WStype_ERROR || type == WStype_DISCONNECTED) {
    size_t shown = payload ? min(length, (size_t)160) : 0;
    Serial.printf("Qwen WS event=%d len=%u reason=%.*s | free heap=%u largest=%u\n", (int)type,
                  (unsigned)length, (int)shown,
                  payload ? reinterpret_cast<const char*>(payload) : "",
                  ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (type == WStype_ERROR) setQwenTransportError(payload, length);
  }
  if (type == WStype_CONNECTED) {
    Serial.println("Qwen WS connected");
    ttsStatus = "Qwen 已连接，正在合成";
    JsonDocument session;
    session["event_id"] = String("cyd-") + String(millis());
    session["type"] = "session.update";
    JsonObject config = session["session"].to<JsonObject>();
    config["voice"] = qwenVoice;
    config["mode"] = "commit";
    config["language_type"] = "Chinese";
    config["response_format"] = "pcm";
    config["sample_rate"] = 24000;
    String message;
    serializeJson(session, message);
    qwenSocket.sendTXT(message);

    JsonDocument append;
    append["event_id"] = String("cyd-text-") + String(millis());
    append["type"] = "input_text_buffer.append";
    append["text"] = ttsPendingText;
    message = "";
    serializeJson(append, message);
    qwenSocket.sendTXT(message);

    JsonDocument commit;
    commit["event_id"] = String("cyd-commit-") + String(millis());
    commit["type"] = "input_text_buffer.commit";
    message = "";
    serializeJson(commit, message);
    qwenSocket.sendTXT(message);
    return;
  }
  if (type == WStype_DISCONNECTED) {
    if (!ttsAudioDone && !ttsError) {
      ttsError = true;
      ttsStatus = "Qwen 连接断开";
    }
    return;
  }
  if (type == WStype_ERROR) {
    // setQwenTransportError() above has already translated the transport
    // reason (401/403/handshake) into a user-facing status.
    return;
  }
  if (type != WStype_TEXT || !payload || !length) return;

  // Large audio frames: locate the base64 delta in place and stream-decode it
  // in small chunks straight into the audio ring. No large intermediate buffer
  // is allocated (a 24 KB heap block here fragmented the heap enough that the
  // WebSocket library could not malloc the ~21 KB incoming frame and dropped
  // the connection). Base64 decodes 4 chars -> 3 bytes; each chunk length must
  // be a multiple of 4.
  char* frame = reinterpret_cast<char*>(payload);
  if (strstr(frame, "response.audio.delta") != nullptr) {
    const char* marker = strstr(frame, "\"delta\":\"");
    if (!marker) return;
    const char* b64 = marker + 9;
    const char* b64End = b64;
    while (*b64End && *b64End != '"') ++b64End;
    size_t b64Len = b64End - b64;
    uint8_t tmp[768];  // 1024 base64 chars -> up to 768 PCM bytes -> 384 samples
    const char* p = b64;
    size_t remain = b64Len;
    while (remain >= 4) {
      size_t chunk = min(remain, (size_t)1024);
      chunk -= chunk % 4;  // keep each chunk on a base64 quantum boundary
      size_t outLen = 0;
      int rc = mbedtls_base64_decode(tmp, sizeof(tmp), &outLen,
                                     (const unsigned char*)p, chunk);
      if (rc == 0 && outLen >= 2) {
        // Signed 16-bit LE mono -> 8-bit DAC (high byte + midpoint), with a
        // fixed ~+4 dB gain and hard clipping. Compress in place: write index
        // i always trails the read index 2i, so unread bytes are preserved.
        size_t samples = outLen / 2;
        for (size_t i = 0; i < samples; ++i) {
          int16_t sample = (int16_t)((uint16_t)tmp[i * 2] | ((uint16_t)tmp[i * 2 + 1] << 8));
          int v = sample >> 8;
          v = (v * 3) / 2;
          if (v > 127) v = 127; else if (v < -128) v = -128;
          tmp[i] = (uint8_t)(v + 128);
        }
        audioRingPush(tmp, samples);
      }
      p += chunk;
      remain -= chunk;
    }
    return;
  }

  JsonDocument event;
  if (deserializeJson(event, payload, length)) return;
  const char* eventType = event["type"] | "";
  if (!strcmp(eventType, "response.audio.done")) {
    ttsAudioDone = true;
    ttsStatus = "正在播放";
  } else if (!strcmp(eventType, "error")) {
    const char* message = event["error"]["message"] | "";
    const char* code = event["error"]["code"] | "";
    if (message && strlen(message)) {
      ttsStatus = String("Qwen 返回错误：") + String(message).substring(0, 96);
    } else if (code && strlen(code)) {
      ttsStatus = String("Qwen 返回错误（") + code + ")";
    } else {
      ttsStatus = "Qwen 返回错误";
    }
    ttsError = true;
  }
}

bool startQwenTts(const String& text) {
  if (!qwenKey.length() || !isLikelyQwenKey(qwenKey)) {
    ttsStatus = "请填写有效的百炼 API Key（不是 Base URL）";
    return false;
  }
  if (!audioReady || WiFi.status() != WL_CONNECTED || !text.length()) return false;
  if (ttsBusy) return false;
  audioRingClear();
  ttsPendingText = text;
  ttsAudioDone = false;
  ttsError = false;
  ttsOverflow = false;
  qwenErrorDetail = "";
  ttsBusy = true;
  ttsStatus = "连接 Qwen";
  // WebSocketsClient appends the terminating CRLF itself.  Keeping it out of
  // the stored header avoids an extra blank line in the handshake.
  snprintf(qwenAuthHeader, sizeof(qwenAuthHeader), "Authorization: Bearer %s", qwenKey.c_str());
  qwenSocketEnabled = true;
  qwenSocket.setExtraHeaders(qwenAuthHeader);
  qwenSocket.setReconnectInterval(10000);
  qwenSocket.beginSSL("dashscope.aliyuncs.com", 443,
                      "/api-ws/v1/realtime?model=qwen3-tts-flash-realtime");
  return true;
}

void serviceQwenTts() {
  if (qwenSocketEnabled) qwenSocket.loop();
  if (!ttsBusy) return;
  // Safety net: if the server drops the connection without the library firing
  // a disconnect callback (e.g. frame-too-big internal reset), ttsBusy would
  // stay true forever and suspend touch. Force-clean after a hard timeout.
  static uint32_t ttsStartedAt = 0;
  if (ttsStartedAt == 0) ttsStartedAt = millis();
  if (millis() - ttsStartedAt > 45000 && !ttsAudioDone) {
    Serial.println("Qwen TTS: hard timeout, forcing cleanup");
    ttsError = true;
    ttsStatus = "Qwen 响应超时";
  }
  if (ttsError) {
    qwenSocketEnabled = false;
    qwenSocket.disconnect();
    ttsBusy = false;
    ttsStartedAt = 0;
    return;
  }
  if (ttsAudioDone && audioRingCount() == 0) {
    qwenSocketEnabled = false;
    qwenSocket.disconnect();
    ttsBusy = false;
    ttsStartedAt = 0;
    ttsStatus = ttsOverflow ? "播放完成（网络过快，有丢帧）" : "播放完成";
  }
}

// Push a block of 8-bit samples into the audio ring with back-pressure so a
// diagnostic sequence longer than the ring capacity can stream through the
// same I2S path as Qwen audio without a second big buffer.
static size_t pushAudioBlock(const uint8_t* block, size_t count) {
  size_t written = 0;
  while (written < count) {
    while (audioRingCount() > AUDIO_RING_CAP - 512 - 1) delay(1);
    size_t pushed = audioRingPush(block + written, min((size_t)512, count - written));
    written += pushed;
    if (pushed == 0) delay(1);
  }
  return written;
}

bool queueSpeakerTestTone() {
  if (!audioReady || ttsBusy) return false;
  audioRingClear();
  constexpr size_t SR = 24000;
  constexpr size_t total = SR * 3 / 2;  // 1.5 s clean 880 Hz tone
  size_t off = 0;
  while (off < total) {
    size_t n = min(total - off, (size_t)256);
    uint8_t b[256];
    for (size_t i = 0; i < n; ++i) {
      float pos = (float)(off + i) / (float)total;
      float fade = min(pos * 20.0f, (1.0f - pos) * 20.0f);
      fade = constrain(fade, 0.0f, 1.0f);
      float w = sinf(2.0f * PI * 880.0f * (float)(off + i) / (float)SR);
      b[i] = (uint8_t)constrain(128.0f + 124.0f * fade * w, 0.0f, 255.0f);
    }
    pushAudioBlock(b, n);
    off += n;
  }
  return true;
}

void startWebServer() {
  if (webStarted) return;
  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html; charset=utf-8", WEB_PAGE); });
  web.on("/speaker-test", HTTP_GET, [] {
    web.send(200, "text/html; charset=utf-8", "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><h3>喇叭硬件测试</h3><p>播放 1.5 秒 880Hz 纯音。已按官方 MP3 demo 配置：立体声帧、双内置 DAC、标准 MSB 对齐、音频 PLL(APLL) 开启；播放时自动暂停触摸。</p><form method='post'><button>播放测试音</button></form>");
  });
  web.on("/speaker-test", HTTP_POST, [] {
    if (ttsBusy) { web.send(409, "text/plain; charset=utf-8", "请等待当前语音播放结束后再测试。"); return; }
    if (!queueSpeakerTestTone()) { web.send(503, "text/plain; charset=utf-8", "I2S 音频输出未就绪。"); return; }
    web.send(200, "text/plain; charset=utf-8", "测试音已输出；若仍只有沙沙声，请断电检查 P4 两个功放输出脚和喇叭插头。");
  });
  web.on("/storage", HTTP_GET, [] {
    if (!cardReady) { web.send(200, "text/plain; charset=utf-8", "TF卡未挂载，请断电后检查卡片。"); return; }
    String status = "TF卡已挂载；environment.csv 每分钟记录温湿度。";
    status += mikuAssetReady ? " Miku动画已就绪。" : " Miku动画文件缺失或损坏。";
    web.send(200, "text/plain; charset=utf-8", status);
  });
  web.on("/miku-upload", HTTP_GET, [] {
    web.send(200, "text/html; charset=utf-8", "<!doctype html><meta charset='utf-8'><h3>更新 Miku 动画</h3><form method='post' enctype='multipart/form-data'><input type='file' name='miku' accept='.rle' required><button>写入 TF 卡</button></form><p>文件名必须为 miku.rle，写入后重启设备。</p>");
  });
  web.on("/miku-upload", HTTP_POST, [] {
    if (mikuUploadFile) mikuUploadFile.close();
    mikuUploadActive = false;
    web.send(cardReady ? 200 : 503, "text/plain; charset=utf-8", cardReady ? "Miku 动画已写入 TF 卡，请重启设备后生效。" : "TF 卡未挂载，无法写入。");
  }, [] {
    HTTPUpload& upload = web.upload();
    if (!cardReady) return;
    if (upload.status == UPLOAD_FILE_START) {
      if (mikuUploadFile) mikuUploadFile.close();
      mikuUploadFile = storage.open("miku.rle", O_WRONLY | O_CREAT | O_TRUNC);
      mikuUploadActive = (bool)mikuUploadFile;
      Serial.printf("Miku upload start: %s\n", upload.filename.c_str());
    } else if (upload.status == UPLOAD_FILE_WRITE && mikuUploadActive) {
      mikuUploadFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END && mikuUploadActive) {
      mikuUploadFile.flush();
      mikuUploadFile.close();
      mikuUploadActive = false;
      Serial.printf("Miku upload complete: %u bytes\n", upload.totalSize);
    }
  });
  web.on("/api", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json; charset=utf-8", telemetryJson());
  });
  web.on("/deepseek/config", HTTP_POST, [] {
    if (chatBusy) { web.send(409, "text/plain; charset=utf-8", "请等待对话结束后修改配置"); return; }
    String key = web.arg("plain"); key.trim();
    if (key.length() < 12 || key.length() > 180) { web.send(400, "text/plain; charset=utf-8", "API Key 格式不正确。"); return; }
    deepseekKey = key;
    prefs.putString("deepseek", deepseekKey);
    web.send(200, "text/plain; charset=utf-8", "API Key 已保存在设备中，页面不会回显。");
  });
  web.on("/qwen-config", HTTP_GET, [] {
    String html = "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><h3>Qwen TTS 配置</h3><p>密钥只写入 ESP32，不会回显。</p><p>这里填写百炼控制台创建的 API Key（通常以 <code>sk-</code> 开头），不要填写 Base URL。截图中的 <code>https://dashscope.aliyuncs.com/compatible-mode/v1</code> 仅用于 OpenAI 兼容 HTTP 请求；实时语音地址已内置。</p><form method='post'><label>百炼 API Key<br><input name='key' type='password' autocomplete='off' placeholder='留空表示保持原密钥'></label><br><label>音色 ID<br><input name='voice' value='" + qwenVoice + "' placeholder='例如 Cherry'></label><br><button>保存配置</button></form><p>保存后，在主网页发送 DeepSeek 对话即可播放语音。</p>";
    web.send(200, "text/html; charset=utf-8", html);
  });
  web.on("/qwen-config", HTTP_POST, [] {
    String key = normalizeQwenKey(web.arg("key"));
    String voice = web.arg("voice"); voice.trim();
    if (key.length() && !isLikelyQwenKey(key)) {
      web.send(400, "text/plain; charset=utf-8", "请填写百炼 API Key（通常以 sk- 开头），不要填写 Base URL 或带空格的内容。"); return;
    }
    if (voice.length() < 1 || voice.length() > 64) {
      web.send(400, "text/plain; charset=utf-8", "音色 ID 长度不正确。"); return;
    }
    if (chatBusy || ttsBusy) { web.send(409, "text/plain; charset=utf-8", "请等待当前对话和语音播放结束。"); return; }
    if (key.length()) { qwenKey = key; prefs.putString("qwen", qwenKey); }
    qwenVoice = voice;
    prefs.putString("qwenVoice", qwenVoice);
    qwenErrorDetail = "";
    ttsStatus = qwenKey.length() ? "已配置，等待测试" : "未配置";
    web.send(200, "text/plain; charset=utf-8", "Qwen TTS 配置已保存，密钥不会回显。");
  });
  web.on("/qwen-say", HTTP_POST, [] {
    String text = web.arg("plain"); text.trim();
    if (!qwenKey.length()) { web.send(400, "text/plain; charset=utf-8", "请先打开 /qwen-config 保存 Qwen API Key。"); return; }
    if (!text.length() || text.length() > 800) { web.send(400, "text/plain; charset=utf-8", "请输入 1～800 字文本。"); return; }
    if (!startQwenTts(text)) { web.send(409, "text/plain; charset=utf-8", "语音忙碌、未联网或音频输出不可用。"); return; }
    web.send(202, "text/plain; charset=utf-8", "Qwen TTS 已开始流式播放。");
  });
  web.on("/tts/status", HTTP_GET, [] {
    web.send(200, "text/plain; charset=utf-8", ttsBusy ? "正在播放：" + ttsStatus : ttsStatus);
  });
  web.on("/deepseek/chat", HTTP_POST, [] {
    Serial.printf("CHAT req: chatBusy=%d ttsBusy=%d len=%d heap=%u largest=%u\n",
                  (int)chatBusy, (int)ttsBusy, web.arg("plain").length(),
                  ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (chatBusy || ttsBusy) { web.send(409, "text/plain; charset=utf-8", "请等待上一条回复或语音播放结束"); return; }
    String question = web.arg("plain"); question.trim();
    if (!question.length() || question.length() > 600) { web.send(400, "text/plain; charset=utf-8", "请输入 1～600 字的问题。"); return; }
    page = PET;
    petReply = "Miku：正在思考…";
    startPetMotion(PET_WORKING, 30000);
    drawPage();
    chatJob.question = question;
    chatJob.answer = "";
    if (!chatJob.done) chatJob.done = xSemaphoreCreateBinary();
    chatBusy = true;
    BaseType_t started = chatJob.done ? xTaskCreate([](void*) {
      chatJob.answer = askDeepSeek(chatJob.question);
      xSemaphoreGive(chatJob.done);
      vTaskDelete(nullptr);
    }, "ai-request", 12288, nullptr, 1, nullptr) : pdFAIL;
    if (started != pdPASS) {
      chatBusy = false;
      petReply = "内存不足，请稍后重试";
      drawPetConversation();
      web.send(503, "text/plain; charset=utf-8", petReply);
      return;
    }
    web.send(202, "text/plain; charset=utf-8", "Miku 正在思考…");
  });
  web.onNotFound([] { web.send(404, "text/plain; charset=utf-8", "页面不存在"); });
  web.on("/deepseek/result", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "no-store");
    web.send(chatBusy ? 202 : 200, "text/plain; charset=utf-8", chatBusy ? String("Thinking...") : chatJob.answer);
  });
  web.begin();
  webStarted = true;
  Serial.printf("Web dashboard: http://%s/\n", WiFi.localIP().toString().c_str());
}

uint16_t levelColor() {
  String l = environmentLevel();
  if (l.indexOf("风险") >= 0) return C_RED;
  if (l.indexOf("调整") >= 0) return C_YELLOW;
  return C_GREEN;
}

void drawHeaderStatus() {
  // 状态、联网指示和主题按钮是三个独立刷新区。状态文字绝不能
  // 写入 x=188 之后，否则后续传感器刷新会盖住联网圆点。
  lcd.fillRect(92, 0, 96, 34, C_HEADER);
  lcd.fillCircle(97, 17, 2, C_MUTED);
  String level = environmentLevel();
  if (level == "环境风险较高") level = "环境风险";
  else if (level == "环境需要调整") level = "需调整";
  else if (level == "环境基本适宜") level = "基本适宜";
  else if (level == "环境舒适") level = "舒适";
  else if (level == "传感器未就绪") level = "未就绪";
  lcd.setClipRect(103, 0, 83, 34);
  text(level, 105, 8, levelColor(), true);
  lcd.clearClipRect();
  // 此函数也会被周期性单独调用，因此每次都恢复联网圆点。
  lcd.fillCircle(195, 17, 4, WiFi.status() == WL_CONNECTED ? C_GREEN : C_MUTED);
}

void drawHeader(const String& title) {
  lcd.fillScreen(C_BG);
  lcd.fillRect(0, 0, 240, 34, C_HEADER);
  text(title, 10, 8, C_TEXT, true);
  if (title == "环境数据") drawHeaderStatus();
  lcd.fillCircle(195, 17, 4, WiFi.status() == WL_CONNECTED ? C_GREEN : C_MUTED);
  button(darkMode ? "日" : "夜", 202, 4, 31, 26, C_PANEL);
}

void drawNav() {
  button("环境", 3, 282, 56, 34, page == DASHBOARD ? C_CYAN : C_PANEL);
  button("趋势", 62, 282, 56, 34, page == TREND ? C_CYAN : C_PANEL);
  button("宠物", 121, 282, 56, 34, page == PET ? C_CYAN : C_PANEL);
  button("设置", 180, 282, 57, 34, page == WIFI_LIST ? C_CYAN : C_PANEL);
}

PetMotion automaticPetMotion() {
  if (!isfinite(temperature)) return PET_WAITING;
  String level = environmentLevel();
  if (level.indexOf("风险") >= 0) return PET_FAILED;
  if (level.indexOf("调整") >= 0) return PET_WAITING;
  return PET_IDLE;
}

void petRange(PetMotion motion, uint8_t& start, uint8_t& count) {
  if (motion == PET_AUTO) motion = automaticPetMotion();
  switch (motion) {
    case PET_WAVE: start = MIKU_WAVE_START; count = MIKU_WAVE_COUNT; break;
    case PET_JUMP: start = MIKU_JUMP_START; count = MIKU_JUMP_COUNT; break;
    case PET_FAILED: start = MIKU_FAILED_START; count = MIKU_FAILED_COUNT; break;
    case PET_WAITING: start = MIKU_WAITING_START; count = MIKU_WAITING_COUNT; break;
    case PET_WORKING: start = MIKU_WORKING_START; count = MIKU_WORKING_COUNT; break;
    case PET_REVIEW: start = MIKU_REVIEW_START; count = MIKU_REVIEW_COUNT; break;
    default: start = MIKU_IDLE_START; count = MIKU_IDLE_COUNT; break;
  }
}

uint16_t petFrameInterval() {
  PetMotion motion = petMotion == PET_AUTO ? automaticPetMotion() : petMotion;
  if (motion == PET_IDLE) return 180;
  if (motion == PET_WAITING || motion == PET_REVIEW || motion == PET_FAILED) return 175;
  if (motion == PET_WAVE || motion == PET_JUMP) return 150;
  return 145;
}

uint16_t le16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool loadMikuAsset() {
  mikuAssetReady = false;
  if (!cardReady) return false;
  FsFile file = storage.open("miku.rle", O_RDONLY);
  if (!file) return false;
  uint8_t header[MIKU_ASSET_HEADER];
  bool valid = file.read(header, sizeof(header)) == sizeof(header) &&
               memcmp(header, "MKU1", 4) == 0 && le16(header + 4) == MIKU_W &&
               le16(header + 6) == MIKU_H && le16(header + 8) == MIKU_FRAME_COUNT;
  if (valid) {
    for (uint8_t i = 0; i < MIKU_FRAME_COUNT; ++i) {
      uint8_t entry[MIKU_ASSET_INDEX_ENTRY];
      if (file.read(entry, sizeof(entry)) != sizeof(entry)) { valid = false; break; }
      mikuFrameOffsets[i] = le32(entry);
      mikuFrameSizes[i] = le16(entry + 4);
      if (!mikuFrameSizes[i] || mikuFrameSizes[i] > MIKU_RLE_MAX) { valid = false; break; }
    }
  }
  file.close();
  mikuAssetReady = valid;
  return valid;
}

void decodeMikuFrame(uint8_t index) {
  uint16_t size = 0;
  if (mikuAssetReady && index < MIKU_FRAME_COUNT) {
    FsFile file = storage.open("miku.rle", O_RDONLY);
    if (file && file.seekSet(mikuFrameOffsets[index])) {
      size_t got = file.read(mikuRle, mikuFrameSizes[index]);
      if (got == mikuFrameSizes[index]) size = mikuFrameSizes[index];
    }
    if (file) file.close();
  }
  uint16_t out = 0;
  for (uint16_t i = 0; i + 2 < size && out < MIKU_W * MIKU_H; i += 3) {
    uint8_t run = mikuRle[i];
    uint16_t color = mikuRle[i + 1] | (mikuRle[i + 2] << 8);
    while (run-- && out < MIKU_W * MIKU_H) petPixels[out++] = color;
  }
  while (out < MIKU_W * MIKU_H) petPixels[out++] = MIKU_TRANSPARENT;
}

void drawPetFrame() {
  if (!petCanvasReady) return;  // sprite released (network session / other page)
  uint8_t index;
  if (petMotion == PET_LOOK && millis() < petOverrideUntil) {
    index = petLookFrame < 8 ? MIKU_LOOK_A_START + petLookFrame : MIKU_LOOK_B_START + petLookFrame - 8;
  } else {
    if (petMotion != PET_AUTO && millis() >= petOverrideUntil) { petMotion = PET_AUTO; petFrame = 0; }
    uint8_t start, count; petRange(petMotion, start, count);
    index = start + petFrame % count;
  }
  decodeMikuFrame(index);
  int jumpLift = 0;
  if (petMotion == PET_JUMP) {
    const int8_t lift[] = {0, 10, 22, 10, 0};
    jumpLift = lift[petFrame % 5];
  }
  // Sprite is tightly sized to Miku (112x122) and pushed with transparency, so
  // erase the on-screen pet area (including the full jump range) first to
  // avoid leaving pixels from the previous frame.
  lcd.fillRect(64, 113, MIKU_W, MIKU_H + 22, C_PANEL);
  petCanvas.fillSprite(MIKU_TRANSPARENT);
  for (int sy = 0; sy < MIKU_H; ++sy) {
    for (int sx = 0; sx < MIKU_W; ++sx) {
      uint16_t color = petPixels[sy * MIKU_W + sx];
      if (color != MIKU_TRANSPARENT) petCanvas.drawPixel(sx, sy, color);
    }
  }
  petCanvas.pushSprite(64, 135 - jumpLift, MIKU_TRANSPARENT);
}

String petStatus() {
  PetMotion motion = petMotion == PET_AUTO ? automaticPetMotion() : petMotion;
  if (motion == PET_WAVE) return "你好呀！";
  if (motion == PET_JUMP) return "摸摸收到，很开心";
  if (motion == PET_FAILED) return "环境不太舒服，请看看建议";
  if (motion == PET_WAITING) return isfinite(temperature) ? "环境需要调整，我在等你行动" : "等待温湿度数据";
  if (motion == PET_WORKING) return "正在帮你分析环境";
  if (motion == PET_REVIEW) return "正在认真检查趋势";
  if (motion == PET_LOOK) return "我看到你滑动的方向啦";
  return "当前环境不错，我会继续陪你观察";
}

void drawPetConversation() {
  lcd.fillRect(10, 42, 220, 65, C_PANEL);
  if (!petReply.length()) return;
  uint16_t bubble = darkMode ? 0x3186 : 0xFFFF;
  lcd.fillRoundRect(10, 43, 220, 56, 9, bubble);
  lcd.fillTriangle(105, 98, 121, 98, 114, 106, bubble);
  lcd.drawRoundRect(10, 43, 220, 56, 9, darkMode ? C_MUTED : 0xCE79);
  lcd.setClipRect(18, 48, 204, 45);
  lcd.setFont(&fonts::efontCN_14);
  lcd.setTextSize(1);
  lcd.setTextColor(C_TEXT, bubble);
  lcd.setCursor(18, 49);
  lcd.setTextWrap(true, false);
  lcd.print(petReply);
  lcd.clearClipRect();
}

void drawPetPage() {
  drawHeader("Miku");
  panel(6, 41, 228, 232);
  drawPetFrame();
  drawPetConversation();
  lcd.setFont(&fonts::efontCN_14); lcd.setTextSize(1); lcd.setTextColor(C_MUTED);
  lcd.setTextDatum(textdatum_t::top_center);
  lcd.drawString("点按·长按·滑动", 120, 264);
  lcd.setTextDatum(textdatum_t::top_left);
  drawNav();
}

void startPetMotion(PetMotion motion, uint32_t duration = 1800) {
  petMotion = motion; petFrame = 0; petFrameAt = 0;
  petOverrideUntil = millis() + duration;
  if (page == PET) drawPetFrame();
}

void handlePetGesture(int sx, int sy, int ex, int ey, uint32_t held) {
  if (sx < 28 || sx > 212 || sy < 102 || sy > 264) return;
  int dx = ex - sx, dy = ey - sy;
  if (abs(dx) > 22 || abs(dy) > 22) {
    float angle = atan2f((float)dx, (float)-dy) * 180.0f / PI;
    if (angle < 0) angle += 360.0f;
    petLookFrame = ((int)roundf(angle / 22.5f)) & 15;
    startPetMotion(PET_LOOK, 1300);
  } else if (held >= 650) startPetMotion(PET_JUMP, 1900);
  else startPetMotion(PET_WAVE, 1500);
}

uint16_t temperatureColor(float value) {
  if (!isfinite(value)) return C_MUTED;
  if (value <= 12) return 0x1979;   // 过低：深蓝
  if (value < 18) return 0x2DDF;    // 偏低：青蓝
  if (value <= 26) return 0x35A8;   // 舒适：绿色
  if (value < 30) return 0xFD20;    // 偏高：橙色
  return 0xE947;                    // 过高：红色
}

uint16_t humidityColor(float value) {
  if (!isfinite(value)) return C_MUTED;
  if (value < 25) return 0xF9C0;    // 过低：橙红
  if (value < 35) return 0xFDE0;    // 偏低：黄色
  if (value <= 65) return 0x251F;   // 舒适：蓝色
  if (value <= 75) return 0x8B9F;   // 偏高：紫色
  return 0xC81F;                    // 过高：洋红
}

String temperatureLevel(float value) {
  if (!isfinite(value)) return "未就绪";
  if (value <= 12) return "过低";
  if (value < 18) return "偏低";
  if (value <= 26) return "舒适";
  if (value < 30) return "偏高";
  return "过高";
}

String humidityLevel(float value) {
  if (!isfinite(value)) return "未就绪";
  if (value < 25) return "过低";
  if (value < 35) return "偏低";
  if (value <= 65) return "舒适";
  if (value <= 75) return "偏高";
  return "过高";
}

void drawLiveNumbers() {
  lcd.fillRect(12, 72, 98, 38, C_PANEL);
  lcd.fillRect(130, 72, 98, 38, C_PANEL);
  lcd.setFont(&fonts::Font4);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(temperatureColor(temperature), C_PANEL);
  lcd.drawString(isfinite(shownT) ? String(shownT, 1) + " C" : "--", 61, 91);
  lcd.setTextColor(humidityColor(humidity), C_PANEL);
  lcd.drawString(isfinite(shownH) ? String(shownH, 1) + " %" : "--", 179, 91);
  lcd.setTextDatum(textdatum_t::top_left);
}

void drawDashboardValues() {
  panel(6, 41, 110, 76); panel(124, 41, 110, 76);
  uint16_t tc = temperatureColor(temperature), hc = humidityColor(humidity);
  lcd.drawRoundRect(6, 41, 110, 76, 7, tc);
  lcd.drawRoundRect(124, 41, 110, 76, 7, hc);
  lcd.fillRoundRect(16, 111, 90, 3, 2, tc);
  lcd.fillRoundRect(134, 111, 90, 3, 2, hc);
  text("温度", 13, 49, C_MUTED); text("湿度", 131, 49, C_MUTED);
  text(temperatureLevel(temperature), 67, 49, tc, true);
  text(humidityLevel(humidity), 185, 49, hc, true);
  drawLiveNumbers();
  panel(6, 124, 228, 149);
  lcd.setFont(&fonts::efontCN_14); lcd.setTextSize(1);
  lcd.setTextColor(C_MUTED); lcd.setCursor(14, 133); lcd.print("结露温度");
  lcd.setTextColor(C_TEXT); lcd.setCursor(105, 133);
  lcd.print(isfinite(temperature) ? String(dewPoint(temperature, humidity), 1) + " C" : String("--"));
  lcd.setTextColor(C_MUTED); lcd.setCursor(14, 158); lcd.print("10分钟预测");
  lcd.setTextColor(C_TEXT); lcd.setCursor(105, 158);
  lcd.print(isfinite(forecastT) ? String(forecastT, 1) + " C / " + String(forecastH, 0) + "%" : String("采集中"));
  lcd.drawFastHLine(14, 186, 212, darkMode ? 0x31A6 : 0xD69A);
  text("当前建议", 14, 194, levelColor(), true);
  String a = advice();
  lcd.setClipRect(12, 218, 216, 49);
  lcd.setFont(&fonts::efontCN_14); lcd.setTextSize(1); lcd.setTextColor(C_TEXT);
  lcd.setCursor(14, 219); lcd.setTextWrap(true, false); lcd.print(a);
  lcd.clearClipRect();
}

void drawDashboard() {
  drawHeader("环境数据");
  drawDashboardValues();
  drawNav();
}

void graphLine(bool tempGraph, int x, int y, int w, int h, uint16_t color) {
  if (historyCount < 2) return;
  float lo = tempGraph ? minT - 1 : minH - 3;
  float hi = tempGraph ? maxT + 1 : maxH + 3;
  if (hi - lo < 2) hi = lo + 2;
  int px = x, py = y + h;
  for (int i = 0; i < historyCount; ++i) {
    const Sample& s = history[(historyHead - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE];
    float v = tempGraph ? s.t : s.h;
    int nx = x + (historyCount == 1 ? 0 : i * (w - 1) / (historyCount - 1));
    int ny = y + h - constrain((int)((v - lo) * h / (hi - lo)), 0, h);
    if (i) lcd.drawLine(px, py, nx, ny, color);
    px = nx; py = ny;
  }
}

void drawTrend() {
  drawHeader("环境趋势");
  panel(6, 41, 228, 232);
  lcd.drawRect(15, 75, 210, 130, 0x4208);
  graphLine(true, 16, 76, 208, 128, C_YELLOW);
  graphLine(false, 16, 76, 208, 128, C_CYAN);
  text("温度", 17, 47, C_YELLOW); text("湿度", 76, 47, C_CYAN);
  text("均值 " + (isfinite(meanT) ? String(meanT,1) + "C  " + String(meanH,0) + "%" : String("--")), 13, 216);
  lcd.setClipRect(10, 239, 220, 30);
  text("范围 " + (isfinite(minT) ? String(minT,1) + "~" + String(maxT,1) + "C  " + String(minH,0) + "~" + String(maxH,0) + "%" : String("--")), 13, 241, C_MUTED);
  lcd.clearClipRect();
  drawNav();
}

void scanWifi() {
  drawHeader("正在扫描附近网络...");
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true);
  wifiCount = min(n, 20);
  wifiPage = 0;
  for (int i = 0; i < wifiCount; ++i) { ssids[i] = WiFi.SSID(i); rssis[i] = WiFi.RSSI(i); }
  WiFi.scanDelete();
  statusText = n < 0 ? "扫描失败，点击刷新"
                     : (WiFi.status() == WL_CONNECTED ? "http://" + WiFi.localIP().toString() + "/"
                                                       : "点击网络并输入密码");
}

void drawWifiList() {
  drawHeader("无线网络设置");
  text(statusText, 8, 39, connecting ? C_YELLOW : C_MUTED);
  int first = wifiPage * 6;
  int shown = min(max(0, wifiCount - first), 6);
  for (int i = 0; i < shown; ++i) {
    panel(6, 67 + i * 29, 228, 26, C_PANEL);
    int index = first + i;
    String name = ssids[index].length() > 14 ? ssids[index].substring(0, 14) : ssids[index];
    text(name, 12, 70 + i * 29);
    text(String(rssis[index]), 192, 70 + i * 29, C_MUTED);
  }
  button("上页", 6, 248, 70, 27, C_PANEL);
  button("刷新", 84, 248, 70, 27, C_BLUE);
  button("下页", 162, 248, 72, 27, C_PANEL);
  drawNav();
}

const char* lowerRows[] = {"1234567890", "qwertyuiop", "asdfghjkl_", "zxcvbnm.-@"};
const char* symbolRows[] = {"!@#$%^&*()", "[]{}<>?/\\", "`~+=_-:;\"", "',. 012345"};
char keyboardChar(int row, int col) {
  char c = keyboardMode == 2 ? symbolRows[row][col] : lowerRows[row][col];
  if (keyboardMode == 1 && c >= 'a' && c <= 'z') c -= 32;
  return c;
}

void drawCharacterKey(int row, int col, bool pressed = false) {
  char c = keyboardChar(row, col);
  int keyX = 2 + col * 24, keyY = 76 + row * 43;
  uint16_t keyBg = pressed ? C_CYAN : (darkMode ? 0x2945 : 0xFFFF);
  button(String(c), keyX, keyY, 22, 37, keyBg);
  lcd.drawRoundRect(keyX, keyY, 22, 37, 5,
                    pressed ? C_BLUE : (darkMode ? C_MUTED : 0xBDF7));
}

void drawKeyboardAction(int index, bool pressed = false) {
  const int xs[] = {3, 61, 119, 177};
  const int ws[] = {55, 55, 55, 60};
  String label = index == 0 ? (keyboardMode == 0 ? "大写" : (keyboardMode == 1 ? "符号" : "小写"))
                            : (index == 1 ? "退格" : (index == 2 ? "取消" : "连接"));
  uint16_t color = pressed ? C_CYAN : (index == 3 ? C_GREEN : C_PANEL);
  button(label, xs[index], 251, ws[index], 56, color);
}

void drawPasswordField() {
  panel(6, 39, 228, 30);
  String masked;
  for (unsigned i = 0; i < password.length(); ++i) masked += '*';
  text(masked.length() ? masked : "请输入WiFi密码", 12, 45, masked.length() ? C_TEXT : C_MUTED);
}

void drawKeyboard() {
  drawHeader("连接 " + selectedSsid.substring(0, 10));
  drawPasswordField();
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 10; ++col) {
      drawCharacterKey(row, col);
    }
  }
  for (int i = 0; i < 4; ++i) drawKeyboardAction(i);
}

void drawPage() {
  // The 27 KB pet sprite is allocated only while the pet page is shown (and no
  // network session is holding it); every other page gives that memory back.
  if (page == PET) {
    if (!petCanvasHeld && !ensurePetCanvas()) petCanvasNeedsRestore = true;
  } else {
    discardPetCanvas();
  }
  if (page == DASHBOARD) drawDashboard();
  else if (page == TREND) drawTrend();
  else if (page == PET) drawPetPage();
  else if (page == WIFI_LIST) drawWifiList();
  else drawKeyboard();
}

void startConnect() {
  if (!selectedSsid.length()) return;
  WiFi.disconnect();
  WiFi.setAutoReconnect(true);
  WiFi.begin(selectedSsid.c_str(), password.c_str());
  connecting = true; connectStarted = millis();
  statusText = "正在连接 " + selectedSsid;
  page = WIFI_LIST;
  drawPage();
}

void handleTouch(int x, int y) {
  if (y < 34 && x >= 205) {
    darkMode = !darkMode;
    applyTheme();
    drawPage();
    return;
  }
  if (page != KEYBOARD && y >= 282) {
    if (x < 60) page = DASHBOARD;
    else if (x < 120) page = TREND;
    else if (x < 179) page = PET;
    else { page = WIFI_LIST; if (!wifiCount) scanWifi(); }
    drawPage(); return;
  }
  if (page == WIFI_LIST) {
    if (y >= 67 && y < 241) {
      int i = (y - 67) / 29;
      int index = wifiPage * 6 + i;
      if (index < wifiCount && i < 6) { selectedSsid = ssids[index]; password = ""; page = KEYBOARD; drawPage(); }
    } else if (y >= 244 && y < 281) {
      if (x < 80 && wifiPage > 0) --wifiPage;
      else if (x < 159) scanWifi();
      else if ((wifiPage + 1) * 6 < wifiCount) ++wifiPage;
      drawPage();
    }
  } else if (page == KEYBOARD) {
    if (y >= 76 && y < 248) {
      int row = (y - 76) / 43, col = x / 24;
      if (row < 4 && col < 10 && password.length() < 63) {
        drawCharacterKey(row, col, true);
        delay(65);
        char c = keyboardChar(row, col);
        password += c;
        drawPasswordField();
        drawCharacterKey(row, col);
      }
    } else if (y >= 249) {
      int action = x < 60 ? 0 : (x < 118 ? 1 : (x < 176 ? 2 : 3));
      drawKeyboardAction(action, true);
      delay(65);
      if (action == 0) {
        keyboardMode = (keyboardMode + 1) % 3;
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 10; ++col) drawCharacterKey(row, col);
        drawKeyboardAction(0);
      }
      else if (action == 1) {
        if (password.length()) password.remove(password.length() - 1);
        drawPasswordField();
        drawKeyboardAction(1);
      }
      else if (action == 2) { page = WIFI_LIST; drawPage(); }
      else startConnect();
    }
  }
}

void restoreWifi() {
  prefs.begin("env-assistant", false);
  deepseekKey = prefs.isKey("deepseek") ? prefs.getString("deepseek", "") : String();
  qwenKey = prefs.isKey("qwen") ? normalizeQwenKey(prefs.getString("qwen", "")) : String();
  qwenVoice = prefs.isKey("qwenVoice") ? prefs.getString("qwenVoice", "Cherry") : String("Cherry");
  ttsStatus = qwenKey.length() ? (isLikelyQwenKey(qwenKey) ? "已配置，等待测试" : "配置无效，请重新填写 API Key") : "未配置";
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  if (ssid.length()) {
    selectedSsid = ssid;
    password = pass;  // 保留已保存密码，避免自动连接后被空字符串覆盖。
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(selectedSsid.c_str(), password.c_str());
    statusText = "正在自动连接 " + selectedSsid;
    connecting = true;
    connectStarted = millis();
  } else {
    statusText = "请选择网络，成功后自动保存";
  }
}

void setup() {
  Serial.begin(115200);
  // Register the event dispatcher before any Qwen TTS request can start.
  // Without this, WebSocketsClient receives frames but qwenSocketEvent() is
  // never called, leaving the status stuck at "连接 Qwen".
  qwenSocket.onEvent(qwenSocketEvent);
  lcd.init(); lcd.setRotation(0);
  petCanvas.setColorDepth(16);
  // The sprite is allocated on demand (pet page only) via ensurePetCanvas().
  applyTheme();
  uint16_t cal[] = {239, 3926, 233, 265, 3856, 3896, 3714, 308};
  lcd.setTouchCalibrate(cal);
  dht.begin();
  initAudioOutput();
  cardReady = storage.begin(SdSpiConfig(5, DEDICATED_SPI, SD_SCK_MHZ(4), &cardSpi));
  Serial.printf("TF card: %s\n", cardReady ? "mounted" : "unavailable");
  Serial.printf("Miku asset: %s\n", loadMikuAsset() ? "miku.rle ready" : "missing or invalid");
  restoreWifi();
  drawPage();
}

void loop() {
  serviceQwenTts();
  // Rebuild the pet sprite only after chat AND TTS are both idle, so the Qwen
  // TLS session has released its blocks; retry periodically until it fits.
  // On other pages the sprite stays deleted — drawPage() recreates it when the
  // user returns to the pet page.
  static uint32_t lastCanvasRetry = 0;
  if (petCanvasNeedsRestore && !chatBusy && !ttsBusy &&
      millis() - lastCanvasRetry > 1500) {
    lastCanvasRetry = millis();
    if (page == PET) {
      releasePetCanvas();
      if (petCanvasReady) drawPetFrame();
    } else {
      petCanvasHeld = false;
      petCanvasNeedsRestore = false;
    }
  }
  if (chatBusy && xSemaphoreTake(chatJob.done, 0) == pdTRUE) {
    chatBusy = false;
    petReply = chatJob.answer;
    startPetMotion(PET_REVIEW, 2200);
    if (page == PET) drawPetConversation();
    if (qwenKey.length() && !chatJob.answer.startsWith("DeepSeek 请求失败") &&
        !chatJob.answer.startsWith("请先在网页")) {
      startQwenTts(chatJob.answer);
    }
  }
  uint16_t x, y;
  static bool wasTouched = false;
  static uint16_t touchStartX = 0, touchStartY = 0, touchLastX = 0, touchLastY = 0;
  static uint32_t touchStartedAt = 0;
  // GPIO25 is shared by the XPT2046 touch clock (TP_CLK) and the SC8002B amp
  // audio input (DAC1). Polling touch drives SPI clocks onto the same pad and
  // corrupts the analog audio with audible clicks, so suspend touch reads
  // while audio (test tone or TTS) is actually playing. Touch auto-resumes
  // once the ring drains.
  bool audioActive = ttsBusy || audioRingCount() > 0;
  bool touched = audioActive ? false : lcd.getTouch(&x, &y);
  if (touched && !wasTouched) {
    touchStartX = touchLastX = x; touchStartY = touchLastY = y; touchStartedAt = millis();
    if (page != PET || y >= 282 || y < 34) handleTouch(x, y);
  } else if (touched) {
    touchLastX = x; touchLastY = y;
  } else if (!touched && wasTouched && page == PET && touchStartY >= 34 && touchStartY < 282) {
    handlePetGesture(touchStartX, touchStartY, touchLastX, touchLastY, millis() - touchStartedAt);
  }
  wasTouched = touched;

  if (connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      connecting = false;
      statusText = "已保存  http://" + WiFi.localIP().toString();
      prefs.putString("ssid", WiFi.SSID()); prefs.putString("pass", password);
      startWebServer();
      drawPage();
    } else if (millis() - connectStarted > 15000) {
      connecting = false; WiFi.disconnect(); statusText = "连接失败，请检查密码"; drawPage();
    }
  }
  if (webStarted) web.handleClient();

  if (millis() - lastRead >= 5000 || lastRead == 0) {
    lastRead = millis();
    float h = dht.readHumidity(), t = dht.readTemperature();
    if (isfinite(t) && isfinite(h)) {
      PetMotion previousAutoMotion = automaticPetMotion();
      sensorFailCount = 0;
      animFromT = isfinite(shownT) ? shownT : t;
      animFromH = isfinite(shownH) ? shownH : h;
      temperature = t; humidity = h;
      if (!isfinite(shownT)) { shownT = t; shownH = h; }
      animStarted = millis();
      addSample(t, h);
      if (cardReady && (!lastCardLog || millis() - lastCardLog >= 60000)) {
        auto log = storage.open("environment.csv", O_WRONLY | O_CREAT | O_APPEND);
        if (log) {
          if (!log.fileSize()) log.println("uptime_ms,temperature_c,humidity_percent");
          log.printf("%lu,%.1f,%.1f\n", (unsigned long)millis(), t, h);
          log.close();
        }
        lastCardLog = millis();
      }
      Serial.printf("DHT22 T=%.1fC H=%.1f%%\n", t, h);
      if (page == DASHBOARD) { drawHeaderStatus(); drawDashboardValues(); }
      else if (page == PET && petMotion == PET_AUTO && previousAutoMotion != automaticPetMotion()) { petFrame = 0; drawPetFrame(); }
    }
    else {
      sensorFailCount = (uint8_t)min((int)sensorFailCount + 1, 10);
      Serial.printf("DHT22 read timeout (%u)\n", sensorFailCount);
      // 网络页只显示网络状态；偶发的 DHT 时序失败不覆盖连接地址。
      if (sensorFailCount >= 3 && page == DASHBOARD && !isfinite(temperature)) {
        drawDashboardValues();
      }
    }
  }
  if (page == DASHBOARD && animStarted && millis() - lastAnimFrame >= 40) {
    lastAnimFrame = millis();
    float p = min(1.0f, (millis() - animStarted) / 600.0f);
    float eased = 1.0f - powf(1.0f - p, 3.0f);
    shownT = animFromT + (temperature - animFromT) * eased;
    shownH = animFromH + (humidity - animFromH) * eased;
    drawLiveNumbers();
    if (p >= 1.0f) animStarted = 0;
  }
  // Freeze Miku animation while audio is playing: every redraw does a 13k-pixel
  // loop plus an SPI sprite transfer in loopTask, which delays qwenSocket.loop()
  // and starves the audio ring (choppy speech). Resume automatically once idle.
  if (page == PET && petMotion != PET_AUTO && millis() >= petOverrideUntil) {
    if (audioRingCount() == 0 && !ttsBusy) { petMotion = PET_AUTO; petFrame = 0; drawPetFrame(); }
  }
  if (page == PET && petMotion != PET_LOOK && audioRingCount() == 0 && !ttsBusy &&
      millis() - petFrameAt >= petFrameInterval()) {
    petFrameAt = millis();
    if (petMotion != PET_LOOK) ++petFrame;
    drawPetFrame();
  }
  delay(15);
}
