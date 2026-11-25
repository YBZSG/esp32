#pragma once

// ESP32-2432S028R V0.1 官方原理图引脚。
// SHT3x 温湿度传感器（I2C）：SDA=GPIO27，SCL=GPIO22，模块板载上拉。
#define SHT3X_SDA 27
#define SHT3X_SCL 22
#define SHT3X_ADDR 0x44

#define TFT_SCLK 14
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_DC 2
#define TFT_CS 15
#define TFT_BL 21

#define TOUCH_SCLK 25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS 33
#define TOUCH_IRQ 36
