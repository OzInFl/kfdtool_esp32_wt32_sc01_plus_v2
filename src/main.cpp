#include <Arduino.h>
#include "container_model.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include "ui.h"

// ------------------------------------------------------------------
// LovyanGFX config for WT32-SC01-PLUS (ESP32-S3, 8-bit parallel ST7796)
// ------------------------------------------------------------------

class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Panel_ST7796   _panel_instance;   // ST7796U
  lgfx::Bus_Parallel8  _bus_instance;     // 8-bit MCU8080
  lgfx::Light_PWM      _light_instance;
  lgfx::Touch_FT5x06   _touch_instance;

  LGFX(void) {
    // 8-bit parallel bus config
    {
      auto cfg = _bus_instance.config();

      // ESP32-S3 WT32-SC01-PLUS uses I2S0 (port 0) for 8-bit parallel
      cfg.port       = 0;
      cfg.freq_write = 40000000;   // 40 MHz is fine for this panel

      cfg.pin_wr = 47;
      cfg.pin_rd = -1;
      cfg.pin_rs = 0;   // D/C

      // LCD data interface, 8bit MCU (8080)
      cfg.pin_d0 = 9;
      cfg.pin_d1 = 46;
      cfg.pin_d2 = 3;
      cfg.pin_d3 = 8;
      cfg.pin_d4 = 18;
      cfg.pin_d5 = 17;
      cfg.pin_d6 = 16;
      cfg.pin_d7 = 15;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    // Panel config
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = -1;
      cfg.pin_rst          = 4;
      cfg.pin_busy         = -1;

      // Panel & memory geometry: 320x480 portrait
      cfg.memory_width     = 320;
      cfg.memory_height    = 480;
      cfg.panel_width      = 320;
      cfg.panel_height     = 480;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;

      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;

      // Color tuning
      cfg.invert           = true;   // start with non-inverted
      cfg.rgb_order        = false;    // ST7796 on this board is BGR

      cfg.dlen_16bit       = false;   // 8-bit parallel
      cfg.bus_shared       = false;   // panel has its own bus

      _panel_instance.config(cfg);
    }

    // Backlight
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl      = 45;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    // Touch (FT5x06 / FT6336U)
    {
      auto cfg = _touch_instance.config();
      cfg.x_min          = 0;
      cfg.x_max          = 319;
      cfg.y_min          = 0;
      cfg.y_max          = 479;

      cfg.pin_int        = -1;     // we poll I2C
      cfg.bus_shared     = false;  // separate I2C bus
      cfg.offset_rotation = 0;

      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda  = 6;
      cfg.pin_scl  = 5;
      cfg.freq     = 400000;      // 400 kHz I2C

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

static LGFX lcd;

// ------------------------------------------------------------------
// LVGL glue
// ------------------------------------------------------------------

static lv_disp_draw_buf_t draw_buf;
static lv_color_t         lv_buf1[320 * 40];

static void lvgl_flush_cb(lv_disp_drv_t* disp,
                          const lv_area_t* area,
                          lv_color_t* color_p) {
  int32_t x1 = area->x1;
  int32_t y1 = area->y1;
  int32_t w  = area->x2 - area->x1 + 1;
  int32_t h  = area->y2 - area->y1 + 1;

  if (w <= 0 || h <= 0) {
    lv_disp_flush_ready(disp);
    return;
  }

  lcd.startWrite();
  lcd.setAddrWindow(x1, y1, w, h);

  // LV_COLOR_DEPTH must be 16 for this direct cast to be valid.
  lcd.pushPixels((lgfx::rgb565_t*)&color_p->full, w * h);

  lcd.endWrite();
  lv_disp_flush_ready(disp);
}

static bool    last_pressed = false;
static int16_t last_x = 0;
static int16_t last_y = 0;

static void lvgl_touch_read(lv_indev_drv_t* indev_drv, lv_indev_data_t* data) {
  (void)indev_drv;

  uint16_t x, y;
  bool pressed = lcd.getTouch(&x, &y);

  if (pressed) {
    int16_t tx = x;
    int16_t ty = y;

    data->state   = LV_INDEV_STATE_PRESSED;
    data->point.x = tx;
    data->point.y = ty;

    if (!last_pressed || tx != last_x || ty != last_y) {
      Serial.printf("LVGL touch DOWN: (%d, %d)\n", tx, ty);
      last_x = tx;
      last_y = ty;
      last_pressed = true;
    }
  } else {
    if (last_pressed) {
      Serial.println("LVGL touch UP");
    }
    data->state = LV_INDEV_STATE_RELEASED;
    last_pressed = false;
  }
}

static void setup_lvgl() {
  lv_init();

  lv_disp_draw_buf_init(&draw_buf, lv_buf1, nullptr, 320 * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = 320;
  disp_drv.ver_res  = 480;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lvgl_touch_read;
  lv_indev_drv_register(&indev_drv);
}

// ------------------------------------------------------------------
// Arduino setup/loop
// ------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Keyloader UI boot (LVGL, LittleFS, persistence)...");

  lcd.init();
  lcd.setColorDepth(16);  // make sure LGFX is in 16-bit mode
  lcd.setRotation(0);     // portrait: 320x480
  lcd.setBrightness(200);

  setup_lvgl();

  // Mount storage + load containers from LittleFS (or defaults)
  ContainerModel& model = ContainerModel::instance();
  model.loadDefaults();  // safe defaults first
  model.load();          // try to override from persistent storage

  ui_init();
}

void loop() {
  lv_timer_handler();

  // simple timing / debouncing
  static uint32_t last = millis();
  uint32_t now = millis();
  if (now - last > 5) {
    last = now;
  }

  static int32_t x, y;
  if (lcd.getTouch(&x, &y)) {
    delay(50);
  } else {
    delay(5);
  }
}
