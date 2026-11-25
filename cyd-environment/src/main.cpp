#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
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
const char* const skinFiles[] = {"miku.rle", "rabbit.rle", "fox.rle", "penguin.rle"};
const char* const skinNames[] = {"Miku", "月兔", "赤狐", "围巾企鹅"};
constexpr unsigned SKIN_COUNT = sizeof(skinFiles) / sizeof(*skinFiles);
uint8_t selectedSkin = 0, availableSkins = 0;
bool speakReplies = true;
bool readingReply = false;
int uploadSkin = -1;
String uploadError;
bool readPetIndex(const char* path, bool activate = false, bool checkPixels = false);
void drawPetFrame();
WebSocketsClient qwenSocket;
String qwenKey;
String qwenVoice = "Cherry";
String ttsPendingText;
String ttsStatus = "未配置";
String qwenErrorDetail;
bool qwenSocketEnabled = false;
volatile bool ttsBusy = false;
volatile bool ttsAudioDone = false;
bool ttsError = false;
bool ttsOverflow = false;
char qwenAuthHeader[240]{};
// 16 kHz 长语音在此设备上仍出现断流，使用已验证的 8 kHz 配置。
constexpr uint32_t TTS_SAMPLE_RATE = 8000;
// I2S/DAC 以 2 倍采样率输出，音频任务里做线性插值。8 kHz 输出时 880 Hz 测试音
// 每周期只有 9 个样本，阶梯波和 7 kHz 附近的镜像会听成“噼啪”；16 kHz 输出让
// 每周期有 18 个样本，镜像也被推到小喇叭自然衰减的频段。
constexpr uint32_t DAC_SAMPLE_RATE = TTS_SAMPLE_RATE * 2;
constexpr size_t AUDIO_RING_CAP = 24576; // PCM16 约 1.5 秒，给网络帧间隔留余量
constexpr size_t AUDIO_PRIME_BYTES = TTS_SAMPLE_RATE; // 约 0.5 秒预缓冲
constexpr size_t AUDIO_DMA_COUNT = 8, AUDIO_DMA_FRAMES = 256;
// DMA 排空时间 + 64 ms 淡出 + 余量。只按 DMA 时长等待时，淡出数据还没播完
// 就 i2s_stop，尾巴被硬切，听感上是一声“啪”。
constexpr uint32_t AUDIO_DMA_DRAIN_MS =
    (AUDIO_DMA_COUNT * AUDIO_DMA_FRAMES * 1000 + DAC_SAMPLE_RATE - 1) / DAC_SAMPLE_RATE + 80;
// 诊断计数：TTS 播放期间环形缓冲断粮次数、i2s_write 未写完整次数。
volatile uint32_t ttsUnderruns = 0;
volatile uint32_t ttsShortWrites = 0;
volatile bool audioPrimed = false;
volatile bool audioDacRunning = false;
volatile bool testToneBusy = false;
// 语音数字增益：Qwen 返回的 PCM 振幅偏小，2 倍（+6 dB）后限幅，
// 只作用于 TTS 数据，本地测试音保持原振幅。
constexpr int32_t TTS_DIGITAL_GAIN = 2;
// 当前音频数据来自 TTS 语音（true）还是本地测试音（false）。
volatile bool voicePlayback = false;
// 本次 TTS 解码后的原始峰值，用于在串口确认增益是否合适。
volatile uint16_t ttsPeak = 0;
uint32_t ttsStartedAt = 0;
bool ttsGotAudio = false;
// 启动时一次分配，避免 TLS 请求期间再申请连续内存。
uint8_t* audioRing = nullptr;
volatile size_t audioHead = 0, audioTail = 0;
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
bool audioReady = false;
TaskHandle_t audioTaskHandle = nullptr;
#include "board_config.h"
#include "display.h"
#include "web_ui.h"
#include "bubble_layout.h"
#include "leaf_mark.h"
#include <StreamString.h>
extern const char assistantPage[] asm("_binary_web_assistant_html_start");
extern const char assistantEnd[] asm("_binary_web_assistant_html_end");
extern const char petPreviews[] asm("_binary_web_pets_png_start");
extern const char petPreviewsEnd[] asm("_binary_web_pets_png_end");
extern const char leafPng[] asm("_binary_web_leaf_png_start");
extern const char leafEnd[] asm("_binary_web_leaf_png_end");
#include <driver/dac.h>

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
Adafruit_SHT31 sht(&Wire);
Preferences prefs;
WebServer web(80);
bool webStarted = false;
struct ChatJob {
  String question, answer, id;
  JsonDocument history;
  bool ok = false, scoped = false;
  SemaphoreHandle_t done = nullptr;
} chatJob;
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

// 室外天气（Open-Meteo，无需 API Key）。TLS 请求放在独立任务中，避免卡住界面和触摸。
String weatherPlace = "上海";
float weatherLat = 31.2304f, weatherLon = 121.4737f;
float outT = NAN, outH = NAN, outFeels = NAN, outDew = NAN;
int outCode = -1;
uint32_t weatherAt = 0, weatherTriedAt = 0;
volatile bool weatherBusy = false;
volatile bool weatherUiDirty = false;
bool dashboardDirty = false;
// 固定缓冲区：后台任务写、loopTask 读，避免 String 跨任务重分配时崩溃。
char weatherStatus[48] = "未获取";


uint16_t C_BG, C_PANEL, C_BLUE, C_CYAN, C_GREEN, C_YELLOW, C_RED, C_TEXT, C_MUTED, C_HEADER;
bool darkMode = false;

enum Page { DASHBOARD, TREND, PET, OUTDOOR, WIFI_LIST, KEYBOARD };
Page page = PET;

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
size_t audioRingCount();
void requestWeatherFetch();
bool outdoorValid();
String weatherText();
enum class EnvAction { WAIT, HOLD, VENT, AC_COOL, AC_DRY, AC_HEAT, HUMIDIFY };
EnvAction recommendAction();
String actionLabel(EnvAction action);
uint16_t actionColor(EnvAction action);
String actionReason(EnvAction action);
uint8_t petFrame = 0, petLookFrame = 0;
uint32_t petFrameAt = 0, petOverrideUntil = 0;

struct Sample { float t, h; uint32_t ms; };
constexpr int HISTORY_SIZE = 72;       // 6 分钟，5 秒一个点
Sample history[HISTORY_SIZE]{};
int historyCount = 0, historyHead = 0;
// 1 Hz 原始采样窗口：识别吹气、开窗、加湿等短时事件。
struct FastSample { float t, h; uint32_t ms; };
constexpr int FAST_WINDOW = 300;       // 5 分钟
FastSample fastWindow[FAST_WINDOW]{};
int fastCount = 0, fastHead = 0;
uint32_t lastFastRead = 0;
uint32_t lastBlowAt = 0, lastHumidEventAt = 0, lastTempEventAt = 0;
String lastEventText;
uint32_t lastEventAt = 0;
// 霉变风险：湿度持续偏高的连续分钟数，回到低湿后清零。
uint16_t moldMinutes = 0;
uint32_t lastMoldTick = 0;
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

// ---- 可选的区间提醒（默认关闭）------------------------------------------
// 温湿度超出舒适区、且偏离超过设定百分比，连续保持设定时长后提醒一次；
// 回到区间后重新武装。默认值面向普通居室，可改成吉他保养等专用区间。
bool alertEnabled = false;
float alertTMin = 20.0f, alertTMax = 26.0f;
float alertHMin = 40.0f, alertHMax = 60.0f;
uint8_t alertTolPct = 5;
uint16_t alertHoldMin = 10;
bool alertVoice = true;
bool alertWeb = true;
bool alertActive = false;
uint32_t alertOutSince = 0;
String alertText;



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

// ---- 高精度环境指标 ------------------------------------------------------
// 绝对湿度（g/m³）：比相对湿度更能反映空气里实际含多少水。
float absoluteHumidity(float t, float rh) {
  if (!isfinite(t) || !isfinite(rh) || rh <= 0) return NAN;
  const float es = 6.112f * expf(17.62f * t / (243.12f + t));  // 饱和水汽压 hPa
  return 216.7f * (rh / 100.0f) * es / (273.15f + t);
}

// 焓值（kJ/kg 干空气）：空调负荷和闷热感的综合指标。
float enthalpy(float t, float rh) {
  if (!isfinite(t) || !isfinite(rh) || rh <= 0) return NAN;
  const float es = 6.112f * expf(17.62f * t / (243.12f + t));
  const float pv = (rh / 100.0f) * es;
  const float w = 0.62198f * pv / (1013.25f - pv);
  return 1.006f * t + w * (2501.0f + 1.86f * t);
}

// 结露余量：室温减去露点。小于约 2℃ 时，窗玻璃和金属件就可能结露。
float condensationMargin() {
  const float dp = dewPoint(temperature, humidity);
  return isfinite(dp) ? temperature - dp : NAN;
}

String moldRiskText() {
  if (moldMinutes < 60) return "低";
  if (moldMinutes < 180) return "注意";
  if (moldMinutes < 360) return "较高";
  return "高";
}

void serviceMoldRisk() {
  if (!lastMoldTick) { lastMoldTick = millis(); return; }
  if (millis() - lastMoldTick < 60000) return;
  lastMoldTick = millis();
  if (!isfinite(temperature) || !isfinite(humidity)) return;
  if (humidity >= 70 && temperature >= 18 && temperature <= 32) {
    if (moldMinutes < 9999) ++moldMinutes;
  } else if (humidity < 65) {
    moldMinutes = 0;
  }
}

// ---- 短时事件识别 --------------------------------------------------------
void pushFastSample(float t, float h) {
  fastWindow[fastHead] = {t, h, millis()};
  fastHead = (fastHead + 1) % FAST_WINDOW;
  fastCount = min(fastCount + 1, FAST_WINDOW);
}

const FastSample* fastAgo(uint32_t msAgo) {
  if (!fastCount) return nullptr;
  const uint32_t target = millis() - msAgo;
  for (int i = 1; i <= fastCount; ++i) {
    const FastSample& s = fastWindow[(fastHead - i + FAST_WINDOW) % FAST_WINDOW];
    if ((int32_t)(s.ms - target) <= 0) return &s;
  }
  return &fastWindow[(fastHead - fastCount + FAST_WINDOW) % FAST_WINDOW];
}

void pushEnvEvent(const String& text) {
  lastEventText = text;
  lastEventAt = millis();
  Serial.printf("EVENT %s\n", text.c_str());
}

void detectEnvEvents() {
  if (fastCount < 4) return;
  const FastSample& now = fastWindow[(fastHead + FAST_WINDOW - 1) % FAST_WINDOW];
  const FastSample* s3 = fastAgo(3000);
  const FastSample* s180 = fastAgo(180000);
  // 吹气：3 秒内湿度骤升 8% 以上，比加湿器和洗澡都快。
  if (s3 && millis() - s3->ms >= 2000 && now.h - s3->h >= 8.0f &&
      (!lastBlowAt || millis() - lastBlowAt >= 60000)) {
    lastBlowAt = millis();
    pushEnvEvent("检测到快速湿度脉冲，可能有人对着传感器吹气");
    if (page == PET && !ttsBusy && !audioDacRunning && audioRingCount() == 0 &&
        petMotion == PET_AUTO) startPetMotion(PET_JUMP, 1900);
    return;
  }
  if (!s180 || millis() - s180->ms < 150000) return;
  const uint32_t cooldown = 10UL * 60 * 1000;
  if (now.h - s180->h >= 12.0f && (!lastHumidEventAt || millis() - lastHumidEventAt >= cooldown)) {
    lastHumidEventAt = millis();
    pushEnvEvent("湿度快速上升，可能加湿、洗澡或晾晒");
  } else if (s180->h - now.h >= 12.0f && (!lastHumidEventAt || millis() - lastHumidEventAt >= cooldown)) {
    lastHumidEventAt = millis();
    pushEnvEvent("湿度快速下降，可能通风或开启除湿");
  } else if (s180->t - now.t >= 1.5f && (!lastTempEventAt || millis() - lastTempEventAt >= cooldown)) {
    lastTempEventAt = millis();
    pushEnvEvent("温度快速下降，可能开窗或开启制冷");
  } else if (now.t - s180->t >= 1.5f && (!lastTempEventAt || millis() - lastTempEventAt >= cooldown)) {
    lastTempEventAt = millis();
    pushEnvEvent("温度快速上升，可能关窗或开启制热");
  }
}

String environmentLevel() {
  if (!isfinite(temperature)) return "传感器未就绪";
  if (temperature >= 30 || temperature <= 12 || humidity >= 75 || humidity <= 25) return "环境风险较高";
  if (temperature > 27 || temperature < 18 || humidity > 65 || humidity < 35) return "环境需要调整";
  if (temperature >= 20 && temperature <= 26 && humidity >= 40 && humidity <= 60) return "环境舒适";
  return "环境基本适宜";
}

String advice() {
  if (!isfinite(temperature)) return "请检查温湿度模块和I2C接口（SDA27/SCL22）";
  if (moldMinutes >= 180) return "霉变风险偏高：湿度已连续 " + String(moldMinutes / 60) + " 小时偏高，请除湿并通风。";
  const float margin = condensationMargin();
  if (isfinite(margin) && margin < 2.0f) return "凝露风险：室温已接近露点，窗面和设备可能结露，请除湿。";
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

void serviceAlert() {
  if (!alertEnabled || !isfinite(temperature) || !isfinite(humidity)) return;
  const float tol = alertTolPct / 100.0f;
  const float tLo = alertTMin * (1.0f - tol), tHi = alertTMax * (1.0f + tol);
  const float hLo = alertHMin * (1.0f - tol), hHi = alertHMax * (1.0f + tol);
  const bool out = temperature < tLo || temperature > tHi || humidity < hLo || humidity > hHi;
  if (!out) {
    alertOutSince = 0;
    alertActive = false;
    alertText = "";
    return;
  }
  if (!alertOutSince) { alertOutSince = millis(); return; }
  if (alertActive) return;
  if (millis() - alertOutSince < (uint32_t)alertHoldMin * 60000UL) return;
  alertActive = true;
  const String why = temperature < tLo ? "温度偏低" : temperature > tHi ? "温度偏高"
                    : humidity < hLo ? "湿度偏低" : "湿度偏高";
  alertText = why + "：当前 " + String(temperature, 1) + "℃、" + String(humidity, 0) + "%RH，已持续 " +
              String(alertHoldMin) + " 分钟超出舒适区 " + String(alertTMin, 0) + "~" + String(alertTMax, 0) +
              "℃ / " + String(alertHMin, 0) + "~" + String(alertHMax, 0) + "%。";
  Serial.printf("ALERT %s\n", alertText.c_str());
  if (alertVoice && qwenKey.length() && WiFi.status() == WL_CONNECTED && !ttsBusy && !chatBusy) {
    startQwenTts("环境提醒，" + alertText + "请及时处理。");
  }
}

String telemetryJson() {
  String json;
  json.reserve(4200);
  const EnvAction action = recommendAction();
  json = "{\"temperature\":" + jsonNumber(temperature) +
         ",\"humidity\":" + jsonNumber(humidity) +
         ",\"dewPoint\":" + jsonNumber(dewPoint(temperature, humidity)) +
         ",\"absoluteHumidity\":" + jsonNumber(absoluteHumidity(temperature, humidity)) +
         ",\"enthalpy\":" + jsonNumber(enthalpy(temperature, humidity)) +
         ",\"condensationMargin\":" + jsonNumber(condensationMargin()) +
         ",\"moldMinutes\":" + String(moldMinutes) +
         ",\"moldRisk\":\"" + moldRiskText() + "\"" +
         ",\"event\":\"" + lastEventText + "\"" +
         ",\"eventAge\":" + String(lastEventAt ? (millis() - lastEventAt) / 1000 : 0) +
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
  json += "],\"outTemperature\":" + jsonNumber(outdoorValid() ? outT : NAN) +
          ",\"outHumidity\":" + jsonNumber(outdoorValid() ? outH : NAN) +
          ",\"outDewPoint\":" + jsonNumber(outdoorValid() ? outDew : NAN) +
          ",\"outFeels\":" + jsonNumber(outdoorValid() ? outFeels : NAN) +
          ",\"outWeather\":\"" + weatherText() + "\"" +
          ",\"weatherPlace\":\"" + weatherPlace + "\"" +
          ",\"weatherAge\":" + String(weatherAt ? (millis() - weatherAt) / 1000 : 0) +
          ",\"weatherStatus\":\"" + String(weatherStatus) + "\"" +
          ",\"action\":\"" + actionLabel(action) + "\"" +
          ",\"actionReason\":\"" + actionReason(action) + "\"" +
          ",\"alertEnabled\":" + String(alertEnabled ? "true" : "false") +
          ",\"alertActive\":" + String(alertActive ? "true" : "false") +
          ",\"alertWeb\":" + String(alertWeb ? "true" : "false") +
          ",\"alertText\":\"" + alertText + "\"";
  JsonDocument audio;
  audio["ready"] = audioReady;
  audio["busy"] = ttsBusy;
  audio["running"] = audioDacRunning;
  audio["buffered"] = audioRingCount();
  audio["status"] = ttsStatus;
  audio["underruns"] = ttsUnderruns;
  audio["shortWrites"] = ttsShortWrites;
  json += ",\"chatBusy\":" + String(chatBusy ? "true" : "false") + ",\"audio\":";
  String audioJson;
  serializeJson(audio, audioJson);
  json += audioJson;
  json += '}';
  return json;
}

// 24 小时历史：读取 TF 卡 environment.csv，只取最后 1440 条，按 5 分钟聚合。
String history24Json() {
  constexpr uint32_t DAY_MINUTES = 24 * 60;
  constexpr uint32_t BUCKET_MINUTES = 5;
  constexpr int DAY_BUCKETS = DAY_MINUTES / BUCKET_MINUTES;  // 288 个点
  static float sumT[DAY_BUCKETS], sumH[DAY_BUCKETS];
  static uint16_t bucketCount[DAY_BUCKETS];
  memset(bucketCount, 0, sizeof(bucketCount));
  int buckets = 0;
  uint32_t samples = 0, hotMinutes = 0, humidMinutes = 0;
  float minT = NAN, maxT = NAN, minH = NAN, maxH = NAN;

  if (cardReady) {
    FsFile file = storage.open("environment.csv", O_RDONLY);
    if (file) {
      char line[64];
      // 只读取文件尾部约 64 KB，避免 CSV 长期追加后整文件扫描阻塞界面。
      const uint32_t size = file.fileSize();
      const uint32_t offset = size > 64UL * 1024 ? size - 64UL * 1024 : 0;
      uint32_t lines = 0;
      file.seekSet(offset);
      if (offset) file.fgets(line, sizeof(line));  // 丢弃可能被截断的首行
      while (file.fgets(line, sizeof(line)) > 0) ++lines;
      const uint32_t dataLines = offset ? lines : (lines ? lines - 1 : 0);
      const uint32_t skip = dataLines > DAY_MINUTES ? dataLines - DAY_MINUTES : 0;
      file.seekSet(offset);
      file.fgets(line, sizeof(line));  // offset=0 时跳过表头，否则丢弃截断行
      for (uint32_t i = 0; i < skip && file.fgets(line, sizeof(line)) > 0; ++i) {}
      int inBucket = 0;
      while (file.fgets(line, sizeof(line)) > 0) {
        unsigned long stamp;
        float t, h;
        if (sscanf(line, "%lu,%f,%f", &stamp, &t, &h) != 3) continue;
        if (!isfinite(t) || !isfinite(h)) continue;
        if (!samples) { minT = maxT = t; minH = maxH = h; }
        minT = min(minT, t); maxT = max(maxT, t);
        minH = min(minH, h); maxH = max(maxH, h);
        if (t > 28.0f) ++hotMinutes;
        if (h > 70.0f) ++humidMinutes;
        ++samples;
        if (buckets >= DAY_BUCKETS) continue;
        if (!inBucket) { sumT[buckets] = 0; sumH[buckets] = 0; bucketCount[buckets] = 0; }
        sumT[buckets] += t; sumH[buckets] += h; ++bucketCount[buckets];
        if (++inBucket >= (int)BUCKET_MINUTES) { ++buckets; inBucket = 0; }
      }
      if (inBucket && buckets < DAY_BUCKETS) ++buckets;
      file.close();
    }
  }

  String json;
  json.reserve(4600);
  json = "{\"samples\":" + String(samples) +
         ",\"minT\":" + jsonNumber(minT) +
         ",\"maxT\":" + jsonNumber(maxT) +
         ",\"minH\":" + jsonNumber(minH) +
         ",\"maxH\":" + jsonNumber(maxH) +
         ",\"hotMinutes\":" + String(hotMinutes) +
         ",\"humidMinutes\":" + String(humidMinutes) +
         ",\"historyT\":[";
  for (int i = 0; i < buckets; ++i) {
    if (i) json += ',';
    json += String(sumT[i] / bucketCount[i], 1);
  }
  json += "],\"historyH\":[";
  for (int i = 0; i < buckets; ++i) {
    if (i) json += ',';
    json += String(sumH[i] / bucketCount[i], 1);
  }
  json += "]}";
  return json;
}

// Allocate the tightly-sized Miku sprite (112x122 ~= 27 KB). Returns false and
// leaves petCanvasReady cleared if no contiguous block is available yet.
// 互斥锁：后台任务（对话 / 天气）会释放画布给 TLS 用，而 loopTask 可能正在
// 解码/推送同一帧。没有串行化时 deleteSprite() 可能发生在 pushSprite() 中间，
// 造成偶发死机（表现就是画面定住、触摸无响应）。
SemaphoreHandle_t petCanvasMux = nullptr;

void ensurePetCanvasMux() {
  if (!petCanvasMux) petCanvasMux = xSemaphoreCreateMutex();
}

bool ensurePetCanvas() {
  ensurePetCanvasMux();
  if (!petCanvasMux) return false;
  xSemaphoreTake(petCanvasMux, portMAX_DELAY);
  if (!petCanvasReady) {
    if (petCanvas.createSprite(MIKU_W, MIKU_H)) {
      petCanvas.setSwapBytes(true);
      petCanvasReady = true;
      petFrameAt = 0;  // force the next loop tick to redraw the pet
    } else {
      Serial.printf("PetCanvas alloc FAILED, free heap=%u largest=%u\n",
                    ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
  }
  bool ok = petCanvasReady;
  xSemaphoreGive(petCanvasMux);
  return ok;
}

// Free the sprite buffer (called for network sessions or when leaving the pet
// page so other pages get the full ~27 KB back).
void discardPetCanvas() {
  ensurePetCanvasMux();
  if (!petCanvasMux) return;
  xSemaphoreTake(petCanvasMux, portMAX_DELAY);
  if (petCanvasReady) {
    petCanvas.deleteSprite();
    petCanvasReady = false;
  }
  xSemaphoreGive(petCanvasMux);
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
  // HTTPClient configures the socket timeout, but this ESP32 core leaves
  // Stream's chunk-header timeout at 1 s. Longer completions need both.
  secure.Stream::setTimeout(60000);
  http.setTimeout(60000);
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
    String context = "你是 Miku，室内环境与节能助手。先回答用户当前问题，结合实测值说明判断依据，给出可执行的步骤和复测条件。"
                     "区分实测、短时趋势和推测。结露余量是室温减露点，设备未测窗面或墙温；评估结露时说明表面温度与露点的关系。"
                     "null 表示无数据。上下文不足时说明缺少哪项信息。"
                     "日常闲聊自然简短。用简体中文和短段落回答。当前室内温度(℃) " + jsonNumber(temperature) +
                     "，相对湿度(%) " + jsonNumber(humidity) + "，露点(℃) " + jsonNumber(dewPoint(temperature, humidity)) +
                     "，结露余量(℃) " + jsonNumber(condensationMargin()) + "，湿度风险持续分钟 " + String(moldMinutes) +
                     "，十分钟温湿度趋势估计 " + jsonNumber(forecastT) + "℃ / " + jsonNumber(forecastH) + "%。";
    if (outdoorValid()) context += "室外 " + weatherPlace + " " + jsonNumber(outT) + "℃ / " + jsonNumber(outH) +
                                  "%，更新于 " + String((millis() - weatherAt) / 60000) + " 分钟前。";
    if (lastEventText.length() && lastEventAt && millis() - lastEventAt < 1800000UL)
      context += "近期环境事件：" + lastEventText;
    system["content"] = context;
    for (JsonObject turn : chatJob.history.as<JsonArray>()) messages.add(turn);
    chatJob.history.clear();
    JsonObject user = messages.add<JsonObject>();
    user["role"] = "user"; user["content"] = question;
    String body; serializeJson(request, body);
    request.clear();
    Serial.printf("DeepSeek: POST, free heap=%u\n", ESP.getFreeHeap());
    int code = http.POST(body);
    body = String();
    StreamString response;
    int received = code == 200 ? http.writeToStream(&response) : 0;
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
      if (received < 0) {
        result = "回复接收中断：" + http.errorToString(received);
      } else if (deserializeJson(doc, response.c_str())) {
        result = "DeepSeek 返回内容无法解析。";
      } else {
        const char* answer = doc["choices"][0]["message"]["content"] | "";
        chatJob.ok = strlen(answer) > 0;
        result = chatJob.ok ? String(answer) : String("DeepSeek 没有返回内容。");
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
  // 临界区只更新读写指针，memcpy 放在锁外：一帧音频可能有好几 KB，关中断
  // 复制会延迟 I2S DMA / Wi-Fi 中断，正是播放“噼啪”的来源之一。
  size_t pushed = 0;
  uint32_t start = millis();
  while (pushed < length) {
    size_t space = 0, head = 0;
    portENTER_CRITICAL(&audioMux);
    head = audioHead;
    space = (audioTail + AUDIO_RING_CAP - head - 1) % AUDIO_RING_CAP;
    portEXIT_CRITICAL(&audioMux);
    if (space > length - pushed) space = length - pushed;
    space -= space % 2; // PCM16 样本不可拆成半个暴露给音频任务。
    if (space) {
      size_t first = min(space, AUDIO_RING_CAP - head);
      memcpy(audioRing + head, data + pushed, first);
      if (space > first) memcpy(audioRing, data + pushed + first, space - first);
      // 数据写完后再发布 head，消费端不会读到未完成的区域。
      portENTER_CRITICAL(&audioMux);
      audioHead = (head + space) % AUDIO_RING_CAP;
      portEXIT_CRITICAL(&audioMux);
    }
    pushed += space;
    if (pushed >= length) break;
    if (millis() - start > 3000) { ttsOverflow = true; break; }
    vTaskDelay(1);
  }
  return pushed;
}

size_t audioRingPop(uint8_t* data, size_t maxLength) {
  size_t tail = 0, available = 0;
  portENTER_CRITICAL(&audioMux);
  tail = audioTail;
  available = audioHead >= tail ? audioHead - tail : AUDIO_RING_CAP - tail + audioHead;
  portEXIT_CRITICAL(&audioMux);
  const size_t popped = min(available, maxLength);
  if (popped) {
    size_t first = min(popped, AUDIO_RING_CAP - tail);
    memcpy(data, audioRing + tail, first);
    if (popped > first) memcpy(data + first, audioRing, popped - first);
    // 复制完成后才推进 tail；生产端只填 head 之后的空间，不会覆盖这里。
    portENTER_CRITICAL(&audioMux);
    audioTail = (tail + popped) % AUDIO_RING_CAP;
    portEXIT_CRITICAL(&audioMux);
  }
  return popped;
}

// 把一段数据完整写进 I2S DMA。i2s_write 可能只接收一部分（WiFi/TLS 抢占时
// 尤其明显），未写完的样本以前被直接丢弃，听起来就是吞字和卡顿。
static size_t i2sWriteAll(const uint8_t* data, size_t bytes) {
  size_t written = 0;
  const uint32_t start = millis();
  while (written < bytes && millis() - start < 1500) {
    size_t w = 0;
    if (i2s_write(I2S_NUM_0, data + written, bytes - written, &w, pdMS_TO_TICKS(100)) != ESP_OK) break;
    if (!w) break;
    written += w;
  }
  return written;
}

// 从中点渐降至 0，排空 DMA 后关闭 DAC 模拟输出，再由 GPIO 保持低电平。
// 等待时间跟随实际 DAC 采样率；i2s_stop 本身不会关闭 DAC 的模拟电路。
static void muteAudioDac(uint16_t* frames, size_t frameCount) {
  for (size_t i = 0; i < frameCount; ++i) {
    uint16_t v = (uint16_t)((128 - (int)(i * 128 / (frameCount - 1))) << 8);
    frames[i * 2] = v;
    frames[i * 2 + 1] = v;
  }
  i2sWriteAll(reinterpret_cast<const uint8_t*>(frames), frameCount * 4);
  vTaskDelay(pdMS_TO_TICKS(AUDIO_DMA_DRAIN_MS));
  i2s_stop(I2S_NUM_0);
  dac_output_disable(DAC_CHANNEL_2);
  pinMode(26, OUTPUT);
  digitalWrite(26, LOW);
}

void audioOutputTask(void*) {
  int16_t pcm[128];
  // Built-in DAC mode uses stereo frames (L then R) as in the official CYD MP3
  // demo; both slots carry the same sample so the output is correct no matter
  // which slot DAC2 (GPIO26) latches. DAC1 (GPIO25) stays disabled because that
  // pad is the touch SPI clock.
  uint16_t samples16[1024];
  int32_t previousSample = 0;
  int16_t prevPcm = 0; // 2x 插值状态，跨缓冲保持连续
  int32_t dacError = 0; // 一阶噪声整形的量化误差状态
  bool lastVoiceMode = false;
  bool underrunLogged = false;
  uint32_t lastReal = millis();
  for (;;) {
    const size_t pending = audioRingCount();
    const bool sourceActive = (ttsBusy && !ttsAudioDone) || testToneBusy;
    const bool priming = !audioPrimed && sourceActive && pending < AUDIO_PRIME_BYTES;
    if (!priming && pending == 0) {
      if (audioPrimed && ttsBusy && !ttsAudioDone && !underrunLogged) {
        underrunLogged = true;
        ++ttsUnderruns;
        Serial.println("Audio: ring underrun");
      }
      // 空档先平滑回到中点；等待一段预缓冲再恢复，避免连续断粮时反复跳变。
      if (audioDacRunning && previousSample != 0) {
        for (size_t i = 0; i < 512; ++i) {
          int32_t sample = previousSample * (511 - (int)i) / 512;
          uint16_t v = (uint16_t)constrain((sample + 32768 + 128) / 256, 0, 255) << 8;
          samples16[i * 2] = samples16[i * 2 + 1] = v;
        }
        i2sWriteAll(reinterpret_cast<const uint8_t*>(samples16), sizeof(samples16));
        previousSample = 0;
      }
      // 会话还在进行时（sourceActive）短暂断粮只补中点静音，DAC 保持运行。
      // 以前 1.2 秒没数据就关 DAC，网络稍有抖动就会走一遍
      // “淡出→关 DAC→重新预缓冲→淡入”，听起来就是一次明显的断音加咔哒。
      // 只有会话结束或连续 5 秒没有新数据才真正关闭模拟输出。
      if (audioDacRunning && (!sourceActive || millis() - lastReal > 5000)) {
        muteAudioDac(samples16, sizeof(samples16) / (2 * sizeof(samples16[0])));
        audioDacRunning = false;
        audioPrimed = false;
        prevPcm = 0;
        dacError = 0;
        Serial.println("Audio I2S: idle, DAC muted");
      } else if (audioDacRunning) {
        for (size_t i = 0; i < sizeof(samples16) / sizeof(samples16[0]); i += 2) {
          samples16[i] = samples16[i + 1] = 0x8000;
        }
        i2sWriteAll(reinterpret_cast<const uint8_t*>(samples16), sizeof(samples16));
      }
      vTaskDelay(pdMS_TO_TICKS(audioDacRunning ? 4 : 20));
      continue;
    }
    underrunLogged = false;
    if (priming) {
      // 预缓冲期间保持 DMA 有数据，防止硬件欠载把 DAC 拉到 0 产生咔哒声。
      if (audioDacRunning && millis() - lastReal > 5000) {
        muteAudioDac(samples16, 512);
        audioDacRunning = false;
        audioPrimed = false;
        prevPcm = 0;
        dacError = 0;
      } else if (audioDacRunning) {
        for (size_t i = 0; i < sizeof(samples16) / sizeof(samples16[0]); i += 2) {
          samples16[i] = samples16[i + 1] = 0x8000;
        }
        i2sWriteAll(reinterpret_cast<const uint8_t*>(samples16), sizeof(samples16));
      }
      vTaskDelay(pdMS_TO_TICKS(4));
      continue;
    }
    if (!audioDacRunning) {
      i2s_zero_dma_buffer(I2S_NUM_0);
      i2s_start(I2S_NUM_0);
      // 静音时 GPIO26 被当作普通输出拉低，这里显式让 DAC2 重新接管引脚。
      dac_output_enable(DAC_CHANNEL_2);
      audioDacRunning = true;
      // 从静音低电平缓慢升到 PCM 中点，避免每次开口的直流阶跃。
      for (size_t i = 0; i < 512; ++i) {
        uint16_t v = (uint16_t)(i * 128 / 511) << 8;
        samples16[i * 2] = samples16[i * 2 + 1] = v;
      }
      i2sWriteAll(reinterpret_cast<const uint8_t*>(samples16), sizeof(samples16));
      previousSample = 0;
      prevPcm = 0;
      dacError = 0;
      Serial.println("Audio I2S: DAC resumed");
    }
    audioPrimed = true;
    const size_t count = audioRingPop(reinterpret_cast<uint8_t*>(pcm), sizeof(pcm)) / sizeof(pcm[0]);
    if (!count) continue;
    lastReal = millis();
    const bool voice = voicePlayback;
    if (voice != lastVoiceMode) {
      lastVoiceMode = voice;
      dacError = 0;
      prevPcm = 0;
    }
    // 只写实际取到的样本。每个 8 kHz 样本插值成两个 16 kHz 帧：
    // (prev+cur)/2 和 cur。跨缓冲用 prevPcm 保持连续，不产生边界跳变。
    // 语音额外加数字增益，并用一阶误差反馈把 8-bit 量化误差推向高频，
    // 降低安静段可闻的量化毛刺；测试音保持原始振幅和直接量化。
    for (size_t i = 0; i < count; ++i) {
      int32_t raw = pcm[i];
      if (voice) raw = constrain(raw * TTS_DIGITAL_GAIN, -32768, 32767);
      const int16_t cur = (int16_t)raw;
      const int16_t mid = (int16_t)(((int32_t)prevPcm + cur) / 2);
      for (size_t k = 0; k < 2; ++k) {
        const int32_t s = k ? cur : mid;
        int32_t q;
        if (voice) {
          const int32_t shaped = s + dacError;
          q = constrain((shaped + 32896) / 256, 0, 255);
          dacError = shaped - ((q - 128) * 256);
          dacError = constrain(dacError, -128, 128);
        } else {
          q = constrain((s + 32896) / 256, 0, 255);
        }
        const size_t frame = i * 2 + k;
        const uint16_t v = (uint16_t)q << 8;
        samples16[frame * 2] = samples16[frame * 2 + 1] = v;
      }
      prevPcm = cur;
    }
    previousSample = pcm[count - 1];
    const size_t bytes = count * 2 * 4;
    if (i2sWriteAll(reinterpret_cast<const uint8_t*>(samples16), bytes) < bytes) ++ttsShortWrites;
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
  config.sample_rate = DAC_SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  // Mirrors the official CYD MP3 demo (ESP8266Audio AudioOutputI2S for the
  // built-in DAC): stereo frames, only DAC2 enabled, MSB-aligned standard
  // format, and the audio PLL enabled on rev>0 silicon for a clean DAC clock.
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  // 8 kHz 下约 256 ms DMA，使用 8 KB 内存；静音前按实际时长排空。
  config.dma_buf_count = AUDIO_DMA_COUNT;
  config.dma_buf_len = AUDIO_DMA_FRAMES;
  config.use_apll = true;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) {
    Serial.println("Audio I2S: unavailable");
    return;
  }
  i2s_set_pin(I2S_NUM_0, nullptr);
  // Only DAC2 (GPIO26) feeds the SC8002B amp. DAC1 (GPIO25) must stay disabled:
  // on the CYD, GPIO25 is the XPT2046 touch clock (TP_CLK). Enabling DAC1 turns
  // that pad into an analog output, so the touch SPI clock never reaches the
  // controller and the screen stops responding to touch entirely — which is
  // exactly what BOTH_EN caused at boot.
  i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
  // Belt and braces: i2s_driver_install() may have enabled both built-in DACs
  // before the mode above was applied, and dac_i2s_enable() only turns channels
  // on — it never turns the unselected one off. Explicitly release DAC1/GPIO25
  // so the pad returns to digital mode and the touch SPI clock can drive it.
  dac_output_disable(DAC_CHANNEL_1);
  i2s_zero_dma_buffer(I2S_NUM_0);
  // 上电即关闭 DAC 模拟输出，GPIO26 保持低电平。
  i2s_stop(I2S_NUM_0);
  dac_output_disable(DAC_CHANNEL_2);
  pinMode(26, OUTPUT);
  digitalWrite(26, LOW);
  // Priority above Arduino loopTask (1) so WiFi/UI cannot starve the DAC DMA.
  // 固定在 loopTask 所在的 core 1，避免任务在双核间迁移带来的调度抖动。
  audioReady = xTaskCreatePinnedToCore(audioOutputTask, "audio-output", 4096, nullptr, 6,
                                       &audioTaskHandle, 1) == pdPASS;
  Serial.printf("Audio I2S: %s\n", audioReady ? "ready: built-in DAC2 (GPIO26), touch CLK GPIO25 kept free" : "task failed");
}

void qwenSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  // Keep protocol diagnostics on the serial port only. Never print the
  // Authorization header or any stored API key.
  if (type == WStype_TEXT && payload && length) {
    // Audio delta frames are large base64 blobs: log their length only instead
    // of flooding the UART (which would starve the real-time audio path).
    if (strstr(reinterpret_cast<const char*>(payload), "response.audio.delta") != nullptr) {
      // 每 16 帧才打印一行：串口输出会阻塞 loopTask，播放期间只会添乱。
      static uint16_t audioDeltas = 0;
      if ((++audioDeltas & 0x0F) == 1) {
        Serial.printf("Qwen WS audio delta: %u bytes\n", (unsigned)length);
      }
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
    config["sample_rate"] = TTS_SAMPLE_RATE;
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
    alignas(2) uint8_t tmp[768];  // 1024 base64 chars -> up to 768 PCM bytes -> 384 samples
    const char* p = b64;
    size_t remain = b64Len;
    while (remain >= 4) {
      size_t chunk = min(remain, (size_t)1024);
      chunk -= chunk % 4;  // keep each chunk on a base64 quantum boundary
      size_t outLen = 0;
      int rc = mbedtls_base64_decode(tmp, sizeof(tmp), &outLen,
                                     (const unsigned char*)p, chunk);
      if (rc == 0 && outLen >= 2) {
        ttsGotAudio = true;
        // 记录原始峰值：串口日志可判断 2x 增益是否足够、会不会削波。
        const int16_t* decoded = reinterpret_cast<const int16_t*>(tmp);
        for (size_t i = 0; i < outLen / 2; ++i) {
          const int32_t a = decoded[i] < 0 ? -decoded[i] : decoded[i];
          if (a > ttsPeak) ttsPeak = (uint16_t)min<int32_t>(a, 32767);
        }
        audioRingPush(tmp, outLen - outLen % 2);
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
  if (ttsBusy || audioDacRunning || audioRingCount()) return false;
  readingReply = false;
  // 释放约 27 KB 的宠物精灵，给 TLS 握手和较大的音频帧腾出连续堆空间。
  // 不管当前在哪一页都释放：碎片化的堆会让 WebSocket 帧 malloc 失败（1011）。
  // 播放结束后由 loop() 在 ttsBusy 变回 false 时自动重建。
  if (!petCanvasHeld) {
    holdPetCanvas();
    petCanvasNeedsRestore = true;
  }
  audioRingClear();
  audioPrimed = false;
  ttsPendingText = text;
  ttsAudioDone = false;
  ttsError = false;
  ttsOverflow = false;
  ttsGotAudio = false;
  ttsUnderruns = 0;
  ttsShortWrites = 0;
  ttsPeak = 0;
  voicePlayback = true;
  ttsStartedAt = millis();
  qwenErrorDetail = "";
  ttsBusy = true;
  ttsStatus = "连接 Qwen";
  Serial.printf("TTS start: %u chars\n", (unsigned)text.length());
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
  // 播放阶段的总时限 + “迟迟没有音频”的快速失败：两者都用于尽快恢复触摸。
  // 之前只有 45 秒总超时，网络异常时触摸会一直失效到超时才恢复。
  const uint32_t elapsed = millis() - ttsStartedAt;
  if (!ttsGotAudio && elapsed > 12000) {
    Serial.println("Qwen TTS: no audio within 12s");
    ttsError = true;
    ttsStatus = "语音服务无响应";
  } else if (elapsed > 180000) {
    Serial.println("Qwen TTS: hard timeout, forcing cleanup");
    ttsError = true;
    ttsStatus = "Qwen 响应超时";
  }
  if (ttsError) {
    qwenSocketEnabled = false;
    qwenSocket.disconnect();
    ttsBusy = false;
    ttsStatus = ttsStatus.length() ? ttsStatus : "语音失败";
    audioPrimed = true;
    return;
  }
  if (ttsAudioDone && audioRingCount() == 0) {
    qwenSocketEnabled = false;
    qwenSocket.disconnect();
    ttsBusy = false;
    ttsStatus = ttsOverflow ? "播放完成（网络过快，有丢帧）" : "播放完成";
    Serial.printf("Qwen TTS done: underruns=%u shortWrites=%u peak=%u\n",
                  (unsigned)ttsUnderruns, (unsigned)ttsShortWrites, (unsigned)ttsPeak);
  }
}

// Push a block of PCM16 bytes into the audio ring with back-pressure so a
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

bool queueSpeakerTestTone(uint8_t volume = 60) {
  if (!audioReady || ttsBusy || audioDacRunning || audioRingCount()) return false;
  voicePlayback = false;
  testToneBusy = true;
  audioRingClear();
  audioPrimed = false;
  constexpr size_t SR = TTS_SAMPLE_RATE;
  constexpr size_t total = SR * 3 / 2;  // 1.5 s clean 880 Hz tone
  size_t off = 0;
  while (off < total) {
    size_t n = min(total - off, (size_t)256);
    int16_t b[256];
    for (size_t i = 0; i < n; ++i) {
      float pos = (float)(off + i) / (float)total;
      float fade = min(pos * 20.0f, (1.0f - pos) * 20.0f);
      fade = constrain(fade, 0.0f, 1.0f);
      float w = sinf(2.0f * PI * 880.0f * (float)(off + i) / (float)SR);
      b[i] = (int16_t)(31744.0f * volume / 100.0f * fade * w);
    }
    pushAudioBlock(reinterpret_cast<const uint8_t*>(b), n * sizeof(b[0]));
    off += n;
  }
  testToneBusy = false;
  return true;
}

void startWebServer() {
  if (webStarted) return;
  web.on("/ui.css", HTTP_GET, [] { web.send_P(200, "text/css; charset=utf-8", WEB_STYLE); });
  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html; charset=utf-8", WEB_PAGE); });
  web.on("/speaker-test", HTTP_GET, [] {
    web.send_P(200, "text/html; charset=utf-8", SPEAKER_PAGE);
  });
  web.on("/speaker-test", HTTP_POST, [] {
    if (chatBusy || ttsBusy || audioDacRunning || audioRingCount()) { web.send(409, "text/plain; charset=utf-8", "请等待当前对话或声音播放结束后再测试。"); return; }
    if (!queueSpeakerTestTone()) { web.send(503, "text/plain; charset=utf-8", "I2S 音频输出未就绪。"); return; }
    web.send(200, "text/plain; charset=utf-8", "测试音已开始，播放结束后自动静音。");
  });
  web.on("/storage", HTTP_GET, [] {
    if (!cardReady) { web.send(200, "text/plain; charset=utf-8", "TF卡未挂载，请断电后检查卡片。"); return; }
    String status = "TF卡已挂载；environment.csv 每分钟记录温湿度。";
    status += mikuAssetReady ? String(" 当前外观：") + skinNames[selectedSkin] : String(" 当前外观文件缺失或损坏。");
    web.send(200, "text/plain; charset=utf-8", status);
  });
  web.on("/assistant", HTTP_GET, [] {
    web.send_P(200, "text/html; charset=utf-8", assistantPage, assistantEnd - assistantPage);
  });
  web.on("/pets.png", HTTP_GET, [] {
    web.send_P(200, "image/png", petPreviews, petPreviewsEnd - petPreviews);
  });
  web.on("/leaf.png", HTTP_GET, [] {
    web.send_P(200, "image/png", leafPng, leafEnd - leafPng);
  });
  web.on("/assistant/state", HTTP_GET, [] {
    JsonDocument state;
    state["temperature"] = jsonNumber(temperature);
    state["humidity"] = jsonNumber(humidity);
    state["level"] = environmentLevel();
    state["keyReady"] = deepseekKey.length() > 0;
    state["voiceReady"] = isLikelyQwenKey(qwenKey);
    state["speak"] = speakReplies;
    state["skin"] = selectedSkin;
    state["available"] = availableSkins;
    state["card"] = cardReady;
    state["busy"] = chatBusy || ttsBusy || weatherBusy || audioDacRunning || audioRingCount();
    state["playing"] = ttsBusy || (voicePlayback && audioDacRunning);
    String body; serializeJson(state, body);
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", body);
  });
  web.on("/assistant/settings", HTTP_POST, [] {
    if (web.hasArg("speak") && (web.arg("speak") == "1" || web.arg("speak") == "0")) {
      const bool enabled = web.arg("speak") == "1";
      if (speakReplies != enabled && !prefs.putBool("speakReplies", enabled)) {
        web.send(500, "text/plain", "朗读设置保存失败"); return;
      }
      speakReplies = enabled;
      if (!enabled && readingReply && voicePlayback && (ttsBusy || audioRingCount())) {
        ttsError = true;
        qwenSocketEnabled = false;
        qwenSocket.disconnect();
        audioRingClear();
        ttsBusy = false;
        readingReply = false;
        ttsStatus = "已停止朗读";
      }
    } else if (web.hasArg("skin")) {
      unsigned slot = 0;
      while (slot < SKIN_COUNT && web.arg("skin") != String(slot)) ++slot;
      if (slot == SKIN_COUNT || !(availableSkins & (1U << slot))) {
        web.send(400, "text/plain", "请先将此外观安装到 TF 卡"); return;
      }
      if (!readPetIndex(skinFiles[slot], true)) {
        web.send(422, "text/plain", "外观文件不可读"); return;
      }
      if (selectedSkin != slot && !prefs.putUChar("petSkin", slot)) {
        readPetIndex(skinFiles[selectedSkin], true);
        web.send(500, "text/plain", "外观设置保存失败"); return;
      }
      selectedSkin = slot;
      mikuAssetReady = true;
      petFrame = 0;
      if (page == PET && !petCanvasHeld) drawPetPage();
    } else { web.send(400, "text/plain", "未知设置"); return; }
    web.send(200, "text/plain", "已保存");
  });
  web.on("/miku-upload", HTTP_GET, [] {
    web.send(200, "text/html; charset=utf-8", "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>安装宠物外观</title><link rel=stylesheet href=/ui.css><body class=settings-page><a class=back href=/assistant>返回智能助手</a><h1>安装宠物外观</h1><p>选择 miku.rle、rabbit.rle、fox.rle 或 penguin.rle。文件校验成功后即可切换外观。</p><form method=post enctype=multipart/form-data><input type=file name=pet accept=.rle required><button>写入 TF 卡</button></form>");
  });
  web.on("/miku-upload", HTTP_POST, [] {
    if (!cardReady || uploadError.length() || uploadSkin < 0) {
      web.send(cardReady ? 422 : 503, "text/plain; charset=utf-8", cardReady && uploadError.length() ? uploadError : String("缺少有效上传或 TF 卡")); return;
    }
    uploadSkin = -1;
    web.send(200, "text/html; charset=utf-8", "<meta charset=utf-8><p>外观已安装。<a href=/assistant>返回智能助手选择外观</a></p>");
  }, [] {
    HTTPUpload& u = web.upload();
    if (!cardReady) return;
    if (u.status == UPLOAD_FILE_START) {
      uploadSkin = -1; uploadError = "";
      for (unsigned i = 0; i < SKIN_COUNT; ++i) if (u.filename == skinFiles[i]) uploadSkin = i;
      if (uploadSkin < 0) { uploadError = "不支持此外观文件名"; return; }
      mikuUploadFile = storage.open("pet.tmp", O_WRONLY | O_CREAT | O_TRUNC);
      if (!mikuUploadFile) uploadError = "无法在 TF 卡创建临时文件";
    } else if (u.status == UPLOAD_FILE_WRITE && mikuUploadFile && !uploadError.length()) {
      if (u.totalSize + u.currentSize > MIKU_ASSET_HEADER + MIKU_FRAME_COUNT * (MIKU_RLE_MAX + MIKU_ASSET_INDEX_ENTRY) ||
          mikuUploadFile.write(u.buf, u.currentSize) != u.currentSize) uploadError = "文件过大或 TF 卡写入失败";
    } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
      if (mikuUploadFile && !mikuUploadFile.close()) uploadError = "TF 卡文件关闭失败";
      if (u.status == UPLOAD_FILE_ABORTED) uploadError = "上传中断";
      if (!uploadError.length() && !readPetIndex("pet.tmp", false, true)) uploadError = "动画文件校验失败";
      if (!uploadError.length() && uploadSkin >= 0) {
        String name = skinFiles[uploadSkin], backup = name + ".bak";
        if (storage.exists(backup) && (!storage.exists(name) || !storage.remove(backup))) uploadError = "上次安装尚待恢复";
        if (!uploadError.length() && storage.exists(name) && !storage.rename(name, backup)) uploadError = "无法备份原动画";
        if (!uploadError.length() && !storage.rename("pet.tmp", name)) {
          if (storage.exists(backup)) storage.rename(backup, name);
          uploadError = "替换动画文件失败";
        }
        if (!uploadError.length()) {
          availableSkins |= 1U << uploadSkin;
          if (uploadSkin == selectedSkin) mikuAssetReady = readPetIndex(name.c_str(), true);
        }
      }
      if (storage.exists("pet.tmp")) storage.remove("pet.tmp");
    }
  });
  web.on("/api", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json; charset=utf-8", telemetryJson());
  });
  web.on("/api/history24h", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json; charset=utf-8", history24Json());
  });
  web.on("/weather/refresh", HTTP_POST, [] {
    requestWeatherFetch();
    web.send(202, "text/plain; charset=utf-8", String(weatherStatus));
  });
  web.on("/weather-config", HTTP_GET, [] {
    String html = "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>室外天气设置</title>"
      "<style>body{font-family:system-ui,'Microsoft YaHei',sans-serif;max-width:520px;margin:24px auto;padding:0 14px;line-height:1.8}"
      "input,select,button{width:100%;padding:10px;border:1px solid #ccd5e3;border-radius:10px;font:inherit;margin:6px 0 12px}"
      "button{background:#20a66a;color:#fff;border:0;font-weight:700}</style>"
      "<link rel='stylesheet' href='/ui.css'><body class='settings-page'><a class='back' href='/'>返回环境健康助手</a><h3>室外天气设置</h3><p>使用 Open-Meteo 免费接口（无需 API Key），每 30 分钟自动更新；也可点下方按钮立即刷新。</p><form method='post'>"
      "<label>显示名称<input name='place' maxlength='20' value='" + weatherPlace + "'></label>"
      "<label>常用城市<select onchange=\"let o=this.options[this.selectedIndex],f=this.form;if(o.dataset.lat){f.lat.value=o.dataset.lat;f.lon.value=o.dataset.lon;f.place.value=o.value}\">"
      "<option value=''>-- 选择自动填充 --</option>"
      "<option value='北京' data-lat='39.9042' data-lon='116.4074'>北京</option>"
      "<option value='上海' data-lat='31.2304' data-lon='121.4737'>上海</option>"
      "<option value='广州' data-lat='23.1291' data-lon='113.2644'>广州</option>"
      "<option value='深圳' data-lat='22.5431' data-lon='114.0579'>深圳</option>"
      "<option value='杭州' data-lat='30.2741' data-lon='120.1551'>杭州</option>"
      "<option value='南京' data-lat='32.0603' data-lon='118.7969'>南京</option>"
      "<option value='苏州' data-lat='31.2989' data-lon='120.5853'>苏州</option>"
      "<option value='成都' data-lat='30.5728' data-lon='104.0668'>成都</option>"
      "<option value='重庆' data-lat='29.5630' data-lon='106.5516'>重庆</option>"
      "<option value='武汉' data-lat='30.5928' data-lon='114.3055'>武汉</option>"
      "<option value='西安' data-lat='34.3416' data-lon='108.9398'>西安</option>"
      "<option value='长沙' data-lat='28.2282' data-lon='112.9388'>长沙</option>"
      "<option value='天津' data-lat='39.3434' data-lon='117.3616'>天津</option>"
      "<option value='青岛' data-lat='36.0671' data-lon='120.3826'>青岛</option>"
      "<option value='沈阳' data-lat='41.8057' data-lon='123.4315'>沈阳</option>"
      "<option value='昆明' data-lat='25.0389' data-lon='102.7183'>昆明</option>"
      "<option value='厦门' data-lat='24.4798' data-lon='118.0894'>厦门</option>"
      "<option value='福州' data-lat='26.0745' data-lon='119.2965'>福州</option>"
      "<option value='南昌' data-lat='28.6820' data-lon='115.8579'>南昌</option>"
      "</select></label>"
      "<button type='button' onclick='autoLocate()' style='background:#2b6cb0'>自动定位当前城市</button>"
      "<p id='geoMsg' style='color:#556;margin:0 0 10px'>按手机出口 IP 估算到城市级，不需要授权；结果会填入下面三项，确认后点“保存并刷新”。<br>若手机用流量、开了 VPN，或宽带是跨省出口，可能定位到别的城市，此时请直接选上方城市或手填经纬度。</p>"
      "<label>纬度<input name='lat' type='number' step='0.0001' min='-90' max='90' value='" + String(weatherLat, 4) + "'></label>"
      "<label>经度<input name='lon' type='number' step='0.0001' min='-180' max='180' value='" + String(weatherLon, 4) + "'></label>"
      "<button>保存并刷新</button></form><p><a href='/'>返回首页</a></p>"
      "<script>"
      "async function autoLocate(){"
      "var f=document.forms[0],m=document.getElementById('geoMsg');m.textContent='正在获取定位…';"
      "function apply(a,b,n){"
      "f.lat.value=(+a).toFixed(4);f.lon.value=(+b).toFixed(4);"
      "if(n){var s=n;while(new TextEncoder().encode(s).length>20)s=s.slice(0,-1);f.place.value=s}"
      "m.textContent='已定位到 '+(n||'当前位置')+'，确认后点“保存并刷新”';"
      "}"
      "async function cnCity(){"
      "try{var r=await fetch('https://ip.useragentinfo.com/json');var j=await r.json();"
      "var n=j.city||j.province;if(n)return n.replace(/市$/,'')}catch(e){}"
      "try{var r=await fetch('https://api.vore.top/api/IPdata');var j=await r.json();"
      "if(j.ipdata&&(j.ipdata.info3||j.ipdata.info2))return(j.ipdata.info3||j.ipdata.info2).replace(/市$/,'')}catch(e){}"
      "return '';"
      "}"
      "async function geocode(n){"
      "var r=await fetch('https://geocoding-api.open-meteo.com/v1/search?name='+encodeURIComponent(n)+'&count=1&language=zh&format=json');"
      "var j=await r.json();if(!j.results||!j.results[0])throw new Error('geo');"
      "return{lat:j.results[0].latitude,lon:j.results[0].longitude};"
      "}"
      "async function globalGeo(){"
      "try{var r=await fetch('https://ipwho.is/');var j=await r.json();"
      "if(j.success&&j.latitude)return{lat:j.latitude,lon:j.longitude,name:j.city||j.region||''}}catch(e){}"
      "var r2=await fetch('https://ipapi.co/json/');var j2=await r2.json();"
      "if(!j2.latitude)throw new Error('ip');"
      "return{lat:j2.latitude,lon:j2.longitude,name:j2.city||j2.region||''};"
      "}"
      "try{"
      "var n=await cnCity();"
      "if(n){var g=await geocode(n);apply(g.lat,g.lon,n);return}"
      "var g2=await globalGeo();var nm=g2.name;"
      "try{var rr=await fetch('https://api.bigdatacloud.net/data/reverse-geocode-client?latitude='+g2.lat+'&longitude='+g2.lon+'&localityLanguage=zh');"
      "var gj=await rr.json();nm=gj.city||gj.locality||gj.principalSubdivision||nm}catch(e){}"
      "apply(g2.lat,g2.lon,nm);"
      "}catch(e){m.textContent='自动定位失败：请确认手机能上网，或手动选择常用城市'}"
      "}"
      "</script>";
    web.send(200, "text/html; charset=utf-8", html);
  });
  web.on("/weather-config", HTTP_POST, [] {
    String place = web.arg("place"); place.trim();
    String latText = web.arg("lat"), lonText = web.arg("lon");
    float lat = latText.toFloat(), lon = lonText.toFloat();
    if (!latText.length() || !lonText.length() || !isfinite(lat) || !isfinite(lon) ||
        lat < -90 || lat > 90 || lon < -180 || lon > 180) {
      web.send(400, "text/html; charset=utf-8", "<meta charset='utf-8'><p>经纬度不正确，<a href='/weather-config'>返回修改</a></p>");
      return;
    }
    if (!place.length() || place.length() > 20) place = "自定义";
    place.replace("\"", "'");
    place.replace("'", " ");
    weatherPlace = place; weatherLat = lat; weatherLon = lon;
    prefs.putString("wplace", weatherPlace); prefs.putFloat("wlat", lat); prefs.putFloat("wlon", lon);
    weatherAt = 0;
    requestWeatherFetch();
    web.send(200, "text/html; charset=utf-8", "<meta charset='utf-8'><p>已保存，正在刷新室外天气。<a href='/'>返回首页</a></p>");
  });
  web.on("/alert-config", HTTP_GET, [] {
    String html = "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>区间提醒设置</title>"
      "<style>body{font-family:system-ui,'Microsoft YaHei',sans-serif;max-width:520px;margin:24px auto;padding:0 14px;line-height:1.7}"
      "input,button{width:100%;padding:10px;border:1px solid #ccd5e3;border-radius:10px;font:inherit;margin:6px 0 12px}"
      "input[type=checkbox]{width:auto;margin-right:8px}button{background:#20a66a;color:#fff;border:0;font-weight:700}"
      "button.alt{background:#2b6cb0}.row{display:flex;gap:10px}.row>*{flex:1}</style>"
      "<link rel='stylesheet' href='/ui.css'><body class='settings-page'><a class='back' href='/'>返回环境健康助手</a><h3>区间提醒设置</h3><p>温湿度超出舒适区、并偏离超过设定百分比，持续达到设定时长后提醒一次；回到区间后重新计时。可用于吉他保养等场景。</p>"
      "<div class='row'><button class='alt' type='button' onclick='preset(20,26,45,55,5,10)'>吉他保养预设</button>"
      "<button class='alt' type='button' onclick='preset(20,26,40,60,5,10)'>居家舒适预设</button></div>"
      "<form method='post'><label><input type='checkbox' name='en'" + String(alertEnabled ? " checked" : "") + ">启用区间提醒</label>"
      "<div class='row'><label>温度下限 ℃<input name='tmin' type='number' step='0.5' min='-20' max='60' value='" + String(alertTMin, 1) + "'></label>"
      "<label>温度上限 ℃<input name='tmax' type='number' step='0.5' min='-20' max='60' value='" + String(alertTMax, 1) + "'></label></div>"
      "<div class='row'><label>湿度下限 %<input name='hmin' type='number' step='1' min='0' max='100' value='" + String(alertHMin, 0) + "'></label>"
      "<label>湿度上限 %<input name='hmax' type='number' step='1' min='0' max='100' value='" + String(alertHMax, 0) + "'></label></div>"
      "<div class='row'><label>允许偏差 %<input name='tol' type='number' step='1' min='0' max='50' value='" + String((unsigned)alertTolPct) + "'></label>"
      "<label>持续时长 分钟<input name='hold' type='number' step='1' min='0' max='1440' value='" + String((unsigned)alertHoldMin) + "'></label></div>"
      "<label><input type='checkbox' name='voice'" + String(alertVoice ? " checked" : "") + ">语音提醒（需已配置百炼 Key）</label>"
      "<label><input type='checkbox' name='web'" + String(alertWeb ? " checked" : "") + ">网页弹窗提醒（需保持网页打开）</label>"
      "<button>保存设置</button></form><p><a href='/'>返回首页</a></p>"
      "<script>function preset(a,b,c,d,e,f){var x=document.forms[0];x.tmin.value=a;x.tmax.value=b;x.hmin.value=c;x.hmax.value=d;x.tol.value=e;x.hold.value=f;x.en.checked=true}</script>";
    web.send(200, "text/html; charset=utf-8", html);
  });
  web.on("/alert-config", HTTP_POST, [] {
    float tmin = web.arg("tmin").toFloat(), tmax = web.arg("tmax").toFloat();
    float hmin = web.arg("hmin").toFloat(), hmax = web.arg("hmax").toFloat();
    int tol = web.arg("tol").toInt(), hold = web.arg("hold").toInt();
    if (!isfinite(tmin) || !isfinite(tmax) || !isfinite(hmin) || !isfinite(hmax) ||
        tmin < -20 || tmax > 60 || tmin >= tmax || hmin < 0 || hmax > 100 || hmin >= hmax ||
        tol < 0 || tol > 50 || hold < 0 || hold > 1440) {
      web.send(400, "text/html; charset=utf-8",
               "<meta charset='utf-8'><p>参数不正确：温度需在 -20~60℃ 且下限小于上限，湿度需在 0~100% 且下限小于上限。<a href='/alert-config'>返回修改</a></p>");
      return;
    }
    alertEnabled = web.hasArg("en");
    alertTMin = tmin; alertTMax = tmax;
    alertHMin = hmin; alertHMax = hmax;
    alertTolPct = (uint8_t)tol;
    alertHoldMin = (uint16_t)hold;
    alertVoice = web.hasArg("voice");
    alertWeb = web.hasArg("web");
    prefs.putBool("aEn", alertEnabled);
    prefs.putFloat("aTMin", alertTMin); prefs.putFloat("aTMax", alertTMax);
    prefs.putFloat("aHMin", alertHMin); prefs.putFloat("aHMax", alertHMax);
    prefs.putUChar("aTol", alertTolPct); prefs.putUShort("aHold", alertHoldMin);
    prefs.putBool("aVoice", alertVoice); prefs.putBool("aWeb", alertWeb);
    alertOutSince = 0; alertActive = false; alertText = "";
    web.send(200, "text/html; charset=utf-8",
             "<meta charset='utf-8'><p>已保存。条件满足时提醒一次，回到区间后重新计时。<a href='/'>返回首页</a></p>");
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
    String html = "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>语音设置 · 环境健康助手</title><link rel='stylesheet' href='/ui.css'><body class='settings-page'><a class='back' href='/'>返回环境健康助手</a><h1>语音设置</h1><p>密钥只写入 ESP32，不会回显。</p><p>填写百炼控制台创建的 API Key（通常以 <code>sk-</code> 开头）和音色 ID。实时语音服务地址已内置，无需填写 Base URL。</p><form method='post'><label>百炼 API Key<br><input name='key' type='password' autocomplete='off' placeholder='留空表示保持原密钥'></label><br><label>音色 ID<br><input name='voice' value='" + qwenVoice + "' placeholder='例如 Cherry'></label><br><button>保存配置</button></form><p>保存后，在主网页发送 DeepSeek 对话即可播放语音。</p><h3>测试朗读</h3><form method='post' action='/qwen-say'><label>文本<br><input name='plain' value='你好，我是环境管家，现在可以说话了'></label><br><button>朗读这段文字</button></form>";
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
    if (chatBusy || ttsBusy || weatherBusy || audioDacRunning || audioRingCount()) { web.send(409, "text/plain; charset=utf-8", "设备正在处理上一项任务，请稍后重发。"); return; }
    if (!deepseekKey.length()) { web.send(422, "text/plain; charset=utf-8", "请先在设备页保存 DeepSeek 密钥。"); return; }
    String question;
    const bool scoped = web.hasArg("turn");
    JsonDocument payload;
    if (scoped) {
      if (web.arg("plain").length() > 10000 || deserializeJson(payload, web.arg("plain")) ||
          !payload["history"].is<JsonArray>() || !payload["question"].is<String>()) {
        web.send(400, "text/plain", "消息格式无效"); return;
      }
      JsonArray history = payload["history"];
      size_t i = 0, bytes = 0;
      bool valid = history.size() <= 8 && history.size() % 2 == 0;
      for (JsonObject turn : history) {
        const char* content = turn["content"] | "";
        bytes += strlen(content);
        if (!*content || turn.size() != 2 || turn["role"] != (i++ % 2 ? "assistant" : "user")) valid = false;
      }
      if (!valid || bytes > 3600) { web.send(400, "text/plain", "上下文超过窗口或轮次不完整"); return; }
      question = payload["question"].as<String>();
    } else question = web.arg("plain");
    question.trim();
    if (!question.length() || question.length() > 600) { web.send(400, "text/plain; charset=utf-8", "问题限 1 至 600 字节。"); return; }
    chatJob.history.set(payload["history"]);
    chatJob.scoped = scoped;
    chatJob.id = String(esp_random(), HEX) + "-" + String(esp_random(), HEX);
    chatJob.ok = false;
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
    web.send(202, "text/plain; charset=utf-8", scoped ? chatJob.id : String("Miku 正在思考…"));
  });
  web.onNotFound([] { web.send(404, "text/plain; charset=utf-8", "页面不存在"); });
  web.on("/deepseek/result", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "no-store");
    if ((chatJob.scoped || web.hasArg("id")) && web.arg("id") != chatJob.id) {
      web.send(410, "text/plain; charset=utf-8", "这条回复已过期，请重新发送。"); return;
    }
    web.send(chatBusy ? 202 : (chatJob.ok ? 200 : 502), "text/plain; charset=utf-8", chatBusy ? String("Thinking...") : chatJob.answer);
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
  for (int y = 0; y < 23; ++y) {
    for (int x = 0; x < 23; ++x) {
      const char ink = leafMark[y][x];
      if (ink != ' ') lcd.drawPixel(4 + x, 5 + y, ink == '+' ? 0xB628 : darkMode ? 0x7D70 : 0x2267);
    }
  }
  text(title, 29, 8, C_TEXT, true);
  if (title == "环境数据") drawHeaderStatus();
  lcd.fillCircle(195, 17, 4, WiFi.status() == WL_CONNECTED ? C_GREEN : C_MUTED);
  button(darkMode ? "日" : "夜", 202, 4, 31, 26, C_PANEL);
}

void drawNav() {
  const char* labels[5] = {"环境", "趋势", "宠物", "室外", "设置"};
  const Page targets[5] = {DASHBOARD, TREND, PET, OUTDOOR, WIFI_LIST};
  for (int i = 0; i < 5; ++i) {
    button(labels[i], 3 + i * 47, 282, 45, 34, page == targets[i] ? C_CYAN : C_PANEL);
  }
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

bool readPetIndex(const char* path, bool activate, bool checkPixels) {
  FsFile file = storage.open(path, O_RDONLY);
  uint8_t header[MIKU_ASSET_HEADER];
  if (!file || file.read(header, sizeof(header)) != sizeof(header) || memcmp(header, "MKU1", 4) ||
      le16(header + 4) != MIKU_W || le16(header + 6) != MIKU_H || le16(header + 8) != MIKU_FRAME_COUNT) return false;
  uint16_t sizes[MIKU_FRAME_COUNT];
  uint32_t offset = MIKU_ASSET_HEADER + MIKU_FRAME_COUNT * MIKU_ASSET_INDEX_ENTRY;
  for (auto& size : sizes) {
    uint8_t entry[MIKU_ASSET_INDEX_ENTRY];
    if (file.read(entry, sizeof(entry)) != sizeof(entry) || le32(entry) != offset) return false;
    size = le16(entry + 4);
    if (!size || size > MIKU_RLE_MAX || size % 3) return false;
    offset += size;
  }
  if (offset != file.fileSize()) return false;
  if (checkPixels) {
    for (auto size : sizes) {
      if (file.read(mikuRle, size) != size) return false;
      uint32_t pixels = 0;
      for (int i = 0; i < size; i += 3) pixels += mikuRle[i];
      if (pixels != MIKU_W * MIKU_H) return false;
      yield();
    }
  }
  if (activate) {
    for (int i = MIKU_FRAME_COUNT - 1; i >= 0; --i) {
      offset -= sizes[i];
      mikuFrameOffsets[i] = offset;
      mikuFrameSizes[i] = sizes[i];
    }
  }
  return true;
}

// RLE 帧直接从 TF 卡解码进宠物画布，不再经过整帧 RAM 缓冲（petPixels 曾占
// 27 KB 静态内存，是这块无 PSRAM 板子上最大的一笔可省开销）。
void decodeMikuFrame(uint8_t index) {
  uint16_t size = 0;
  if (mikuAssetReady && index < MIKU_FRAME_COUNT) {
    FsFile file = storage.open(skinFiles[selectedSkin], O_RDONLY);
    if (file && file.seekSet(mikuFrameOffsets[index])) {
      size_t got = file.read(mikuRle, mikuFrameSizes[index]);
      if (got == mikuFrameSizes[index]) size = mikuFrameSizes[index];
    }
    if (file) file.close();
  }
  petCanvas.fillSprite(MIKU_TRANSPARENT);
  uint16_t out = 0;
  int sx = 0, sy = 0;
  for (uint16_t i = 0; i + 2 < size && out < MIKU_W * MIKU_H; i += 3) {
    uint8_t run = mikuRle[i];
    uint16_t color = mikuRle[i + 1] | (mikuRle[i + 2] << 8);
    while (run-- && out < MIKU_W * MIKU_H) {
      if (color != MIKU_TRANSPARENT) petCanvas.drawPixel(sx, sy, color);
      if (++sx == MIKU_W) { sx = 0; ++sy; }
      ++out;
    }
  }
}

void drawPetFrame() {
  if (!petCanvasReady || !mikuAssetReady) return;  // sprite released (network session / other page)
  ensurePetCanvasMux();
  if (!petCanvasMux || xSemaphoreTake(petCanvasMux, pdMS_TO_TICKS(200)) != pdTRUE) return;
  if (!petCanvasReady) { xSemaphoreGive(petCanvasMux); return; }
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
  petCanvas.pushSprite(64, 135 - jumpLift, MIKU_TRANSPARENT);
  xSemaphoreGive(petCanvasMux);
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
  lcd.setTextDatum(textdatum_t::top_left);
  BubbleLine lines[3];
  int count = layoutBubble(petReply.c_str(), 204, lines, [](const char* glyph) { return lcd.textWidth(glyph); });
  for (int i = 0; i < count; ++i) {
    String line = petReply.substring(lines[i].start, lines[i].end);
    if (lines[i].ellipsis) line += "…";
    lcd.drawString(line, 18, 49 + i * 14);
  }
  lcd.clearClipRect();
}

void drawPetPage() {
  drawHeader(skinNames[selectedSkin]);
  panel(6, 41, 228, 232);
  if (mikuAssetReady) drawPetFrame();
  else text(cardReady ? "请在网页安装宠物外观" : "请检查 TF 卡", 38, 174, C_MUTED);
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
  lcd.setTextColor(C_MUTED); lcd.setCursor(14, 133); lcd.print("露点/余量");
  lcd.setTextColor(C_TEXT); lcd.setCursor(105, 133);
  lcd.print(isfinite(temperature) ? String(dewPoint(temperature, humidity), 1) + " / " +
                                       String(condensationMargin(), 1) + "C" : String("--"));
  lcd.setTextColor(C_MUTED); lcd.setCursor(14, 158); lcd.print("10分钟预测");
  lcd.setTextColor(C_TEXT); lcd.setCursor(105, 158);
  lcd.print(isfinite(forecastT) ? String(forecastT, 1) + " C / " + String(forecastH, 0) + "%" : String("采集中"));
  lcd.drawFastHLine(14, 186, 212, darkMode ? 0x31A6 : 0xD69A);
  const EnvAction action = recommendAction();
  const bool havePlan = action != EnvAction::WAIT;
  text(havePlan ? "建议：" + actionLabel(action) : String("当前建议"),
       14, 194, havePlan ? actionColor(action) : levelColor(), true);
  String a = havePlan ? actionReason(action) : advice();
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

// ---------------- 室外天气与“空调还是开窗”建议 ----------------

bool outdoorValid() {
  return isfinite(outT) && isfinite(outH) && weatherAt != 0 &&
         millis() - weatherAt < 90UL * 60 * 1000;
}

String weatherText() {
  if (outCode < 0) return "--";
  if (outCode == 0) return "晴";
  if (outCode <= 2) return "多云";
  if (outCode == 3) return "阴";
  if (outCode <= 48) return "雾";
  if (outCode <= 57) return "小雨";
  if (outCode <= 67) return "雨";
  if (outCode <= 77) return "雪";
  if (outCode <= 82) return "阵雨";
  if (outCode <= 86) return "阵雪";
  if (outCode >= 95) return "雷雨";
  return "阴";
}

bool outdoorRaining() { return outCode >= 51; }

// 核心决策：先看室内是否舒适，再用室外温度与室内外露点差判断
// “开窗换气”是否真的更划算，否则给出空调模式与节能设定。
EnvAction recommendAction() {
  if (!isfinite(temperature) || !isfinite(humidity)) return EnvAction::WAIT;
  const bool comfort = temperature >= 20 && temperature <= 26 && humidity >= 40 && humidity <= 60;
  if (!outdoorValid()) {
    if (temperature > 28) return EnvAction::AC_COOL;
    if (temperature < 18) return EnvAction::AC_HEAT;
    if (humidity > 70) return EnvAction::AC_DRY;
    if (humidity < 30) return EnvAction::HUMIDIFY;
    return comfort ? EnvAction::HOLD : EnvAction::WAIT;
  }
  const float dewIn = dewPoint(temperature, humidity);
  const bool rain = outdoorRaining();
  const bool cooler = outT <= temperature - 1.5f;
  const bool warmer = outT >= temperature + 1.5f;
  const bool drier = isfinite(dewIn) && outDew <= dewIn - 1.5f;
  const bool wetter = isfinite(dewIn) && outDew >= dewIn + 1.5f;
  const bool mild = outT >= 16 && outT <= 28;

  if (comfort) return EnvAction::HOLD;
  if (temperature > 26) {
    if (!rain && cooler && outT <= 30 && !wetter) return EnvAction::VENT;
    return EnvAction::AC_COOL;
  }
  if (humidity > 65) {
    if (!rain && drier && outT <= temperature + 2) return EnvAction::VENT;
    return EnvAction::AC_DRY;
  }
  if (temperature < 20) {
    if (!rain && warmer && outT <= 26) return EnvAction::VENT;
    return EnvAction::AC_HEAT;
  }
  if (humidity < 35) {
    if (!rain && outH >= humidity + 8 && mild) return EnvAction::VENT;
    return EnvAction::HUMIDIFY;
  }
  return EnvAction::HOLD;
}

String actionLabel(EnvAction action) {
  switch (action) {
    case EnvAction::HOLD: return "维持现状";
    case EnvAction::VENT: return "开窗通风";
    case EnvAction::AC_COOL: return "开空调制冷";
    case EnvAction::AC_DRY: return "开空调除湿";
    case EnvAction::AC_HEAT: return "关窗制热";
    case EnvAction::HUMIDIFY: return "加湿补水";
    default: return "等待数据";
  }
}

uint16_t actionColor(EnvAction action) {
  switch (action) {
    case EnvAction::HOLD:
    case EnvAction::VENT: return C_GREEN;
    case EnvAction::AC_COOL:
    case EnvAction::AC_DRY:
    case EnvAction::AC_HEAT: return C_BLUE;
    case EnvAction::HUMIDIFY: return C_CYAN;
    default: return C_MUTED;
  }
}

String actionReason(EnvAction action) {
  if (action == EnvAction::WAIT) {
    return outdoorValid() ? "室内数据未就绪，请检查温湿度模块。"
                          : "室外数据未获取，请在网页设置城市后刷新。";
  }
  if (!outdoorValid()) {
    switch (action) {
      case EnvAction::AC_COOL: return "室内偏热：关窗开空调制冷，设定 26~27C，每调高 1C 约省 7% 电。";
      case EnvAction::AC_DRY: return "室内潮湿：关窗开除湿，目标湿度 50%~60%。";
      case EnvAction::AC_HEAT: return "室内偏冷：关窗制热，设定 20C 左右更省电。";
      case EnvAction::HUMIDIFY: return "室内偏干：加湿并多饮水，制热会加重干燥。";
      default: return "室内舒适，无需开空调；早晚可短时开窗换气。";
    }
  }
  switch (action) {
    case EnvAction::HOLD:
      return "室内处于舒适区，无需空调。可在室外更干爽时短时开窗。";
    case EnvAction::VENT:
      return "室外更低或更干爽，开窗对流 15~20 分钟比开空调更省电。";
    case EnvAction::AC_COOL:
      return "室外更热或更闷，开窗无效。关窗制冷 26~27C，每调高 1C 省电约 7%。";
    case EnvAction::AC_DRY:
      return "室外露点更高，开窗更潮。关窗除湿，目标湿度 50%~60%。";
    case EnvAction::AC_HEAT:
      return "室外更冷，开窗会流失热量。关窗制热 20C 左右更省电。";
    case EnvAction::HUMIDIFY:
      return "室外同样干燥，开窗无效。建议加湿并多饮水。";
    default:
      return "保持当前设置。";
  }
}

String weatherAge() {
  if (!weatherAt) return weatherStatus;
  uint32_t sec = (millis() - weatherAt) / 1000;
  if (sec < 60) return "刚刚更新";
  if (sec < 3600) return String(sec / 60) + "分钟前";
  return String(sec / 3600) + "小时前";
}

// TLS 握手需要连续堆内存：宠物页会先释放 27 KB 画布，取完由 loop 重建。
void weatherTask(void*) {
  float t = NAN, h = NAN, feels = NAN;
  int code = -1;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(weatherLat, 4) +
               "&longitude=" + String(weatherLon, 4) +
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code&timezone=auto";
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure secure;
    secure.setInsecure();
    HTTPClient http;
    http.setTimeout(12000);
    if (http.begin(secure, url)) {
      int status = http.GET();
      if (status == 200) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
          JsonObject current = doc["current"];
          t = current["temperature_2m"] | NAN;
          h = current["relative_humidity_2m"] | NAN;
          feels = current["apparent_temperature"] | NAN;
          code = current["weather_code"] | -1;
        }
      }
      http.end();
    }
  }
  weatherTriedAt = millis();
  if (isfinite(t) && isfinite(h)) {
    outT = t; outH = h; outFeels = feels;
    outDew = dewPoint(t, h);
    outCode = code;
    weatherAt = millis();
    snprintf(weatherStatus, sizeof(weatherStatus), "已更新");
    Serial.printf("Weather: %.1fC %.0f%% code=%d heap=%u\n", t, h, code, ESP.getFreeHeap());
  } else {
    snprintf(weatherStatus, sizeof(weatherStatus), "获取失败");
    Serial.printf("Weather: fetch failed, heap=%u\n", ESP.getFreeHeap());
  }
  weatherBusy = false;
  weatherUiDirty = true;
  vTaskDelete(nullptr);
}

void requestWeatherFetch() {
  if (weatherBusy) return;
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(weatherStatus, sizeof(weatherStatus), "设备未联网");
    weatherUiDirty = true;
    return;
  }
  if (chatBusy || ttsBusy || audioDacRunning || audioRingCount() > 0) {
    snprintf(weatherStatus, sizeof(weatherStatus), "稍后刷新");
    weatherUiDirty = true;
    return;
  }
  weatherBusy = true;
  weatherTriedAt = millis();
  snprintf(weatherStatus, sizeof(weatherStatus), "正在获取");
  if (page == PET) { holdPetCanvas(); petCanvasNeedsRestore = true; }
  if (xTaskCreate(weatherTask, "weather", 12288, nullptr, 1, nullptr) != pdPASS) {
    weatherBusy = false;
    snprintf(weatherStatus, sizeof(weatherStatus), "内存不足");
    if (page == PET) petCanvasNeedsRestore = true;
  }
  weatherUiDirty = true;
}

void serviceWeather() {
  if (weatherBusy || chatBusy || ttsBusy || audioDacRunning || audioRingCount() > 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  const uint32_t now = millis();
  const bool due = weatherAt == 0 || now - weatherAt >= 30UL * 60 * 1000;
  if (due && (weatherTriedAt == 0 || now - weatherTriedAt >= 3UL * 60 * 1000)) requestWeatherFetch();
}

void drawOutdoorValues() {
  const bool valid = outdoorValid();
  const uint16_t tc = valid ? temperatureColor(outT) : C_MUTED;
  const uint16_t hc = valid ? humidityColor(outH) : C_MUTED;
  panel(6, 41, 110, 76); panel(124, 41, 110, 76);
  lcd.drawRoundRect(6, 41, 110, 76, 7, tc);
  lcd.drawRoundRect(124, 41, 110, 76, 7, hc);
  lcd.fillRoundRect(16, 111, 90, 3, 2, tc);
  lcd.fillRoundRect(134, 111, 90, 3, 2, hc);
  text("室外温度", 13, 49, C_MUTED);
  text("室外湿度", 131, 49, C_MUTED);
  lcd.setFont(&fonts::Font4);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(tc, C_PANEL);
  lcd.drawString(valid ? String(outT, 1) + " C" : "--", 61, 91);
  lcd.setTextColor(hc, C_PANEL);
  lcd.drawString(valid ? String(outH, 0) + " %" : "--", 179, 91);
  lcd.setTextDatum(textdatum_t::top_left);

  panel(6, 124, 228, 149);
  lcd.setFont(&fonts::efontCN_14); lcd.setTextSize(1);
  lcd.setTextColor(C_MUTED); lcd.setCursor(14, 132);
  lcd.print(String("天气 ") + weatherText());
  lcd.setTextColor(C_TEXT); lcd.setCursor(148, 132);
  lcd.print(valid ? "露点" + String(outDew, 0) + "C" : String("--"));
  lcd.setTextColor(C_MUTED); lcd.setCursor(14, 156);
  lcd.print(valid && isfinite(outFeels) ? "体感 " + String(outFeels, 1) + "C" : String("体感 --"));
  lcd.setTextColor(C_MUTED); lcd.setCursor(148, 156);
  lcd.print(weatherBusy ? String("正在获取")
                        : (strcmp(weatherStatus, "已更新") != 0 ? String(weatherStatus) : weatherAge()));
  lcd.drawFastHLine(14, 180, 212, darkMode ? 0x31A6 : 0xD69A);
  const EnvAction action = recommendAction();
  text(String("建议：") + actionLabel(action), 14, 186, actionColor(action), true);
  lcd.setClipRect(12, 212, 216, 58);
  lcd.setFont(&fonts::efontCN_14); lcd.setTextSize(1); lcd.setTextColor(C_TEXT);
  lcd.setCursor(14, 212); lcd.setTextWrap(true, false);
  lcd.print(actionReason(action));
  lcd.clearClipRect();
}

void drawOutdoor() {
  drawHeader("室外与节能");
  drawOutdoorValues();
  drawNav();
}

bool wifiScanRunning = false;
uint32_t wifiScanStartedAt = 0;
// 异步扫描：旧实现用阻塞式 scanNetworks()，进入“设置”页时界面会僵住 2~5 秒，
// 触摸也跟着失效。这里改成发起扫描后在 loop 里轮询结果，界面始终可响应。
void beginWifiScan() {
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  wifiCount = 0;
  wifiPage = 0;
  wifiScanRunning = true;
  wifiScanStartedAt = millis();
  statusText = "正在扫描附近网络…";
  WiFi.scanNetworks(true, true);
}

void serviceWifiScan() {
  if (!wifiScanRunning) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - wifiScanStartedAt < 20000) return;
    WiFi.scanDelete();
    wifiScanRunning = false;
    statusText = "扫描超时，点击刷新";
  } else if (n < 0) {
    wifiScanRunning = false;
    statusText = "扫描失败，点击刷新";
  } else {
    wifiScanRunning = false;
    wifiCount = min(n, 20);
    wifiPage = 0;
    for (int i = 0; i < wifiCount; ++i) { ssids[i] = WiFi.SSID(i); rssis[i] = WiFi.RSSI(i); }
    WiFi.scanDelete();
    statusText = WiFi.status() == WL_CONNECTED ? "http://" + WiFi.localIP().toString() + "/"
                                               : "点击网络并输入密码";
  }
  if (page == WIFI_LIST) drawPage();
}

void drawWifiList() {
  drawHeader("无线网络设置");
  text(statusText, 8, 39, (connecting || wifiScanRunning) ? C_YELLOW : C_MUTED);
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
  else if (page == OUTDOOR) drawOutdoor();
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
    const Page targets[5] = {DASHBOARD, TREND, PET, OUTDOOR, WIFI_LIST};
    int slot = (x < 3) ? 0 : (x - 3) / 47;
    slot = constrain(slot, 0, 4);
    page = targets[slot];
    if (page == WIFI_LIST && !wifiCount && !wifiScanRunning) beginWifiScan();
    drawPage(); return;
  }
  if (page == OUTDOOR && y >= 41 && y <= 117) {
    requestWeatherFetch();
    return;
  }
  if (page == WIFI_LIST) {
    if (y >= 67 && y < 241) {
      int i = (y - 67) / 29;
      int index = wifiPage * 6 + i;
      if (index < wifiCount && i < 6) { selectedSsid = ssids[index]; password = ""; page = KEYBOARD; drawPage(); }
    } else if (y >= 244 && y < 281) {
      if (x < 80 && wifiPage > 0) --wifiPage;
      else if (x < 159 && !wifiScanRunning) beginWifiScan();
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
  weatherPlace = prefs.isKey("wplace") ? prefs.getString("wplace", "上海") : String("上海");
  weatherLat = prefs.isKey("wlat") ? prefs.getFloat("wlat", 31.2304f) : 31.2304f;
  weatherLon = prefs.isKey("wlon") ? prefs.getFloat("wlon", 121.4737f) : 121.4737f;
  alertEnabled = prefs.getBool("aEn", false);
  alertTMin = prefs.isKey("aTMin") ? prefs.getFloat("aTMin", 20.0f) : 20.0f;
  alertTMax = prefs.isKey("aTMax") ? prefs.getFloat("aTMax", 26.0f) : 26.0f;
  alertHMin = prefs.isKey("aHMin") ? prefs.getFloat("aHMin", 40.0f) : 40.0f;
  alertHMax = prefs.isKey("aHMax") ? prefs.getFloat("aHMax", 60.0f) : 60.0f;
  alertTolPct = prefs.getUChar("aTol", 5);
  alertHoldMin = prefs.getUShort("aHold", 10);
  alertVoice = prefs.getBool("aVoice", true);
  alertWeb = prefs.getBool("aWeb", true);
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
  // Bring the I2S/DAC path up *before* the display and touch bus. Installed in
  // the opposite order, the DAC setup ran after lcd.init() and could claim
  // GPIO25 (XPT2046 TP_CLK), killing touch. With this order the touch SPI pins
  // are configured last and always win.
  initAudioOutput();
  lcd.init(); lcd.setRotation(0);
  petCanvas.setColorDepth(16);
  // The sprite is allocated on demand (pet page only) via ensurePetCanvas().
  applyTheme();
  uint16_t cal[] = {239, 3926, 233, 265, 3856, 3896, 3714, 308};
  lcd.setTouchCalibrate(cal);
  Wire.begin(SHT3X_SDA, SHT3X_SCL);
  Wire.setClock(100000);
  Serial.printf("SHT3x: %s\n", sht.begin(SHT3X_ADDR) ? "ready" : "not found");
  cardReady = storage.begin(SdSpiConfig(5, DEDICATED_SPI, SD_SCK_MHZ(4), &cardSpi));
  Serial.printf("TF card: %s\n", cardReady ? "mounted" : "unavailable");
  restoreWifi();
  speakReplies = prefs.getBool("speakReplies", true);
  selectedSkin = prefs.getUChar("petSkin", 0);
  if (cardReady) {
    for (unsigned i = 0; i < SKIN_COUNT; ++i) {
      String backup = String(skinFiles[i]) + ".bak";
      if (!storage.exists(skinFiles[i]) && storage.exists(backup)) storage.rename(backup, skinFiles[i]);
      if (readPetIndex(skinFiles[i])) availableSkins |= 1U << i;
    }
  }
  if (selectedSkin >= SKIN_COUNT || !(availableSkins & (1U << selectedSkin))) {
    selectedSkin = 0;
    if (availableSkins) while (!(availableSkins & (1U << selectedSkin))) ++selectedSkin;
  }
  mikuAssetReady = availableSkins && readPetIndex(skinFiles[selectedSkin], true);
  Serial.printf("Pet skins: mask=%u selected=%u\n", availableSkins, selectedSkin);
  drawPage();
}

void loop() {
  // 串口联调：发送 "say" 或 "say <文本>" 直接触发一次朗读；发送 "heap" 查看内存。
  // 这样不用手机网页也能复现语音问题，便于抓日志。
  static String serialCmd;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (serialCmd.length()) {
        String cmd = serialCmd;
        serialCmd = "";
        cmd.trim();
        if (cmd == "say" || cmd.startsWith("say ")) {
          String text = cmd.length() > 4 ? cmd.substring(4) : String("你好，我是环境管家，现在可以说话了");
          Serial.printf("Serial TTS: \"%s\"\n", text.c_str());
          if (ttsBusy) Serial.println("Serial TTS: busy");
          else if (!startQwenTts(text)) Serial.println("Serial TTS: start failed");
        } else if (cmd == "heap") {
          Serial.printf("Heap: free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        } else if (cmd == "status") {
          Serial.println(telemetryJson());
        } else if (cmd == "screen" && !ttsBusy && !audioDacRunning) {
          // Binary PPM from display RAM, one scanline at a time.
          uint8_t row[240 * 3];
          Serial.print("P6\n240 320\n255\n");
          for (int y = 0; y < 320; ++y) {
            lcd.readRectRGB(0, y, 240, 1, row);
            Serial.write(row, sizeof(row));
          }
        } else if (cmd == "day") {
          Serial.println(history24Json());
        } else if (cmd == "quiettone") {
          if (chatBusy || ttsBusy || weatherBusy || audioDacRunning || audioRingCount()) {
            Serial.println("Quiet tone: wait until device is idle");
          } else {
            // 本地隔离测试：暂停无线和背光负载，播放后恢复，凭据不变。
            WiFi.mode(WIFI_OFF);
            lcd.setBrightness(0);
            delay(200);
            Serial.println(queueSpeakerTestTone(60) ? "Quiet tone queued" : "Quiet tone failed");
            const uint32_t started = millis();
            while ((audioDacRunning || audioRingCount()) && millis() - started < 4000) delay(10);
            lcd.setBrightness(darkMode ? 145 : 230);
            WiFi.mode(WIFI_STA);
            WiFi.begin(selectedSsid.c_str(), password.c_str());
            connecting = true;
            connectStarted = millis();
            Serial.println("Quiet tone: WiFi and backlight restored");
          }
        } else if (cmd == "tone" || cmd.startsWith("tone ")) {
          // 本地 880 Hz 纯音，不经过网络：用来区分“DAC/功放链路本身的杂声”
          // 和“WiFi 串流时电源/射频耦合造成的杂声”。默认 60% 振幅，
          // 满幅时小功放很容易削波，听感就是噼啪。
          int volume = cmd.length() > 5 ? cmd.substring(5).toInt() : 60;
          if (volume < 1 || volume > 100) Serial.println("Tone volume must be 1..100");
          else Serial.println(queueSpeakerTestTone(volume) ? "Tone queued" : "Tone failed (busy?)");
        }
      }
    } else if (serialCmd.length() < 240) {
      serialCmd += c;
    }
  }
  serviceQwenTts();
  // Rebuild the pet sprite only after chat, TTS and the weather fetch are all
  // idle, so TLS sessions get the freed contiguous block; retry periodically.
  // On other pages the sprite stays deleted — drawPage() recreates it when the
  // user returns to the pet page.
  static uint32_t lastCanvasRetry = 0;
  if (petCanvasNeedsRestore && !chatBusy && !ttsBusy && !audioDacRunning && !weatherBusy &&
      millis() - lastCanvasRetry > 1500) {
    lastCanvasRetry = millis();
    if (page == PET) {
      releasePetCanvas();
      if (petCanvasReady) drawPetPage();
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
    if (chatJob.ok && speakReplies && qwenKey.length()) {
      readingReply = startQwenTts(chatJob.answer);
    }
  }
  uint16_t x, y;
  static bool wasTouched = false;
  static uint16_t touchStartX = 0, touchStartY = 0, touchLastX = 0, touchLastY = 0;
  static uint32_t touchStartedAt = 0;
  bool audioActive = ttsBusy || audioDacRunning || audioRingCount() > 0;
  // Audio uses DAC2/GPIO26 only, and the XPT2046 keeps its IRQ line (GPIO36):
  // the driver only clocks the touch SPI while a finger is actually down, so
  // GPIO25 stays quiet when idle and does not hiss into the adjacent amp input.
  bool touched = lcd.getTouch(&x, &y);
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

  // 关闭 Wi-Fi 省电模式：默认 modem sleep 会让 TLS / WebSocket 出现周期性的
  // 100~300 ms 停顿，听感上就是“喇叭很卡”。代价是功耗略增。断线重连后
  // ESP-IDF 会恢复默认 modem sleep，所以每次从断开变为已连接都要重新设置。
  static wl_status_t lastWifiStatus = WL_IDLE_STATUS;
  static bool wifiPowerLow = false;
  const wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus == WL_CONNECTED && lastWifiStatus != WL_CONNECTED) {
    WiFi.setSleep(false);
    wifiPowerLow = false;
  }
  lastWifiStatus = wifiStatus;
  // 播放期间降低发射功率：Wi-Fi 射频突发会让 3.3V 电源瞬时跌落，直接耦合
  // 进 DAC/功放，听感是随机“噼啪”。近距离 15 dBm 足够；信号弱时保持全功率，
  // 避免丢包反而加重卡顿。
  const bool wantLowPower = audioActive && wifiStatus == WL_CONNECTED && WiFi.RSSI() > -75;
  if (wantLowPower && !wifiPowerLow) {
    WiFi.setTxPower(WIFI_POWER_15dBm);
    wifiPowerLow = true;
  } else if (!wantLowPower && wifiPowerLow) {
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    wifiPowerLow = false;
  }
  serviceWifiScan();
  if (!connecting) serviceWeather();
  if (weatherUiDirty) {
    weatherUiDirty = false;
    if (page == OUTDOOR) drawOutdoorValues();
    if (page == DASHBOARD) dashboardDirty = true;
  }
  if (dashboardDirty && !audioActive) {
    dashboardDirty = false;
    if (page == DASHBOARD) { drawHeaderStatus(); drawDashboardValues(); }
  }

  // 1 Hz 原始采样：事件识别需要比界面刷新更快的数据。
  if (millis() - lastFastRead >= 1000 || lastFastRead == 0) {
    lastFastRead = millis();
    float t, h;
    if (sht.readBoth(&t, &h)) {
      sensorFailCount = 0;
      pushFastSample(t, h);
      detectEnvEvents();
      temperature = t;
      humidity = h;
    } else {
      sensorFailCount = (uint8_t)min((int)sensorFailCount + 1, 10);
      if (sensorFailCount == 3 || sensorFailCount == 10) {
        Serial.printf("SHT3x read failed (%u)\n", sensorFailCount);
        // 网络页只显示网络状态；偶发的 I2C 读取失败不覆盖连接地址。
        if (page == DASHBOARD && !isfinite(temperature)) drawDashboardValues();
      }
    }
  }
  serviceMoldRisk();

  if (millis() - lastRead >= 5000 || lastRead == 0) {
    lastRead = millis();
    if (isfinite(temperature) && isfinite(humidity)) {
      PetMotion previousAutoMotion = automaticPetMotion();
      animFromT = isfinite(shownT) ? shownT : temperature;
      animFromH = isfinite(shownH) ? shownH : humidity;
      if (!isfinite(shownT)) { shownT = temperature; shownH = humidity; }
      animStarted = millis();
      addSample(temperature, humidity);
      // 播放期间不写 SD 卡：软件 SPI 写卡会让电源出现几十毫秒的跌落，
      // 直接串进 DAC/功放形成“噼啪”。播放结束后下个周期再补写。
      if (cardReady && !audioActive && (!lastCardLog || millis() - lastCardLog >= 60000)) {
        auto log = storage.open("environment.csv", O_WRONLY | O_CREAT | O_APPEND);
        if (log) {
          if (!log.fileSize()) log.println("uptime_ms,temperature_c,humidity_percent");
          log.printf("%lu,%.1f,%.1f\n", (unsigned long)millis(), temperature, humidity);
          log.close();
        }
        lastCardLog = millis();
      }
      Serial.printf("SHT3x T=%.1fC H=%.1f%%\n", temperature, humidity);
      serviceAlert();
      if (page == DASHBOARD) {
        if (audioActive) dashboardDirty = true;  // 播放期间不重绘，避免抢音频任务的时间片
        else { drawHeaderStatus(); drawDashboardValues(); }
      }
      else if (page == PET && petMotion == PET_AUTO && previousAutoMotion != automaticPetMotion()) { petFrame = 0; drawPetFrame(); }
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
    if (audioRingCount() == 0 && !ttsBusy && !audioDacRunning) { petMotion = PET_AUTO; petFrame = 0; drawPetFrame(); }
  }
  if (page == PET && petMotion != PET_LOOK && audioRingCount() == 0 && !ttsBusy && !audioDacRunning &&
      millis() - petFrameAt >= petFrameInterval()) {
    petFrameAt = millis();
    if (petMotion != PET_LOOK) ++petFrame;
    drawPetFrame();
  }
  delay(15);
}
