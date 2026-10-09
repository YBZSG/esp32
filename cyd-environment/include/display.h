#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board_config.h"

class CYDDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_XPT2046 touch_;

 public:
  CYDDisplay() {
    {
      auto c = bus_.config();
      c.spi_host = HSPI_HOST;
      c.spi_mode = 0;
      c.freq_write = 40000000;
      c.freq_read = 16000000;
      c.spi_3wire = false;
      c.use_lock = true;
      c.dma_channel = 1;
      c.pin_sclk = TFT_SCLK;
      c.pin_mosi = TFT_MOSI;
      c.pin_miso = TFT_MISO;
      c.pin_dc = TFT_DC;
      bus_.config(c);
      panel_.setBus(&bus_);
    }
    {
      auto c = panel_.config();
      c.pin_cs = TFT_CS;
      c.pin_rst = -1;
      c.pin_busy = -1;
      c.memory_width = 240;
      c.memory_height = 320;
      c.panel_width = 240;
      c.panel_height = 320;
      c.offset_x = 0;
      c.offset_y = 0;
      c.offset_rotation = 0;
      c.readable = true;
      c.invert = false;
      c.rgb_order = false;
      c.dlen_16bit = false;
      c.bus_shared = true;
      panel_.config(c);
    }
    {
      auto c = light_.config();
      c.pin_bl = TFT_BL;
      c.invert = false;
      c.freq = 12000;
      c.pwm_channel = 7;
      light_.config(c);
      panel_.setLight(&light_);
    }
    {
      auto c = touch_.config();
      c.x_min = 0;
      c.x_max = 239;
      c.y_min = 0;
      c.y_max = 319;
      c.pin_int = TOUCH_IRQ;
      c.bus_shared = false;
      c.offset_rotation = 0;
      c.spi_host = VSPI_HOST;
      c.freq = 2500000;
      c.pin_sclk = TOUCH_SCLK;
      c.pin_mosi = TOUCH_MOSI;
      c.pin_miso = TOUCH_MISO;
      c.pin_cs = TOUCH_CS;
      touch_.config(c);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }
};
