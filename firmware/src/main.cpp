#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include "config.h"

constexpr uint8_t DHT_PIN = 13;
constexpr uint8_t DHT_TYPE = DHT22;
constexpr uint8_t LCD_SCL = 18;
constexpr uint8_t LCD_SDA = 23;
constexpr uint8_t LCD_RS = 27;
constexpr uint8_t LCD_CS = 25;
constexpr uint8_t LCD_RST = 26;
constexpr unsigned long SEND_INTERVAL_MS = 5000;
DHT dht(DHT_PIN, DHT_TYPE);
unsigned long lastSend = 0;

class UC1701LCD9648 : public Adafruit_GFX {
 public:
  UC1701LCD9648() : Adafruit_GFX(96, 48) {}
  void begin() {
    pinMode(LCD_RST, OUTPUT); pinMode(LCD_RS, OUTPUT); pinMode(LCD_CS, OUTPUT);
    digitalWrite(LCD_CS, HIGH); digitalWrite(LCD_RS, HIGH);
    digitalWrite(LCD_RST, HIGH); delay(10);
    digitalWrite(LCD_RST, LOW); delay(10);
    digitalWrite(LCD_RST, HIGH); delay(10);
    SPI.begin(LCD_SCL, -1, LCD_SDA, LCD_CS);
    const uint8_t init[] = {0xE2, 0xC8, 0xA0, 0x2F, 0x26, 0x81, 0x10, 0xAF};
    for (uint8_t command : init) writeCommand(command);
    clearDisplay(); display();
  }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= 96 || y >= 48) return;
    uint8_t &cell = buffer[x + (y >> 3) * 96];
    const uint8_t mask = 1U << (y & 7);
    if (color) cell |= mask; else cell &= ~mask;
  }
  void clearDisplay() { memset(buffer, 0, sizeof(buffer)); }
  void display() {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
    for (uint8_t page = 0; page < 6; ++page) {
      commandRaw(0xB0 + page); commandRaw(0x00); commandRaw(0x10);
      digitalWrite(LCD_RS, HIGH); digitalWrite(LCD_CS, LOW);
      SPI.writeBytes(buffer + page * 96, 96);
      digitalWrite(LCD_CS, HIGH);
    }
    SPI.endTransaction();
  }
 private:
  uint8_t buffer[96 * 6] = {};
  void commandRaw(uint8_t value) {
    digitalWrite(LCD_RS, LOW); digitalWrite(LCD_CS, LOW);
    SPI.transfer(value); digitalWrite(LCD_CS, HIGH);
  }
  void writeCommand(uint8_t value) {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
    commandRaw(value); SPI.endTransaction();
  }
};

UC1701LCD9648 lcd;

void drawLcd(float temperature, float humidity, bool valid) {
  lcd.clearDisplay();
  lcd.drawRect(0, 0, 96, 48, 1);
  lcd.setTextColor(1); lcd.setTextSize(1); lcd.setCursor(4, 3);
  lcd.print("DHT22");
  lcd.print(WiFi.status() == WL_CONNECTED ? " OK" : " --");
  if (valid) {
    lcd.setTextSize(2);
    lcd.setCursor(3, 15); lcd.print(temperature, 1); lcd.print("C");
    lcd.setCursor(3, 31); lcd.print(humidity, 1); lcd.print("%");
  } else {
    lcd.setCursor(7, 27); lcd.print("SENSOR ERROR");
  }
  lcd.display();
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi OK, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi connection failed; will retry.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  lcd.begin();
  lcd.clearDisplay();
  lcd.drawRect(0, 0, 96, 48, 1);
  lcd.drawLine(0, 0, 95, 47, 1);
  lcd.drawLine(95, 0, 0, 47, 1);
  lcd.setTextColor(1); lcd.setTextSize(1); lcd.setCursor(14, 20);
  lcd.print("UC1701 LCD9648");
  lcd.display();
  dht.begin();
  connectWiFi();
}

void loop() {
  connectWiFi();
  if (millis() - lastSend < SEND_INTERVAL_MS) { delay(50); return; }
  lastSend = millis();

  const float humidity = dht.readHumidity();
  const float temperature = dht.readTemperature();
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("DHT22 read failed. Check DATA=GPIO13, VCC and GND.");
    drawLcd(0, 0, false);
    return;
  }

  drawLcd(temperature, humidity, true);

  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setTimeout(3000);
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Token", API_TOKEN);
  const String payload = String("{\"deviceId\":\"") + DEVICE_ID +
    "\",\"temperature\":" + String(temperature, 1) +
    ",\"humidity\":" + String(humidity, 1) +
    ",\"rssi\":" + String(WiFi.RSSI()) +
    ",\"uptime\":" + String(millis() / 1000) + "}";
  const int status = http.POST(payload);
  Serial.printf("T=%.1fC H=%.1f%% RSSI=%ddBm HTTP=%d\n", temperature, humidity, WiFi.RSSI(), status);
  http.end();
}
