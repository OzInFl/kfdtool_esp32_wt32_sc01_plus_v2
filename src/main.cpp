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
    { // 8-bit parallel bus config
      auto cfg = _bus_instance.config();
      cfg.freq_write = 20000000;

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

    { // panel config
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = -1;
      cfg.pin_rst          = 4;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 320;
      cfg.panel_height     = 480;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = true;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;

      _panel_instance.config(cfg);
    }

    { // backlight
      auto cfg = _light_instance.config();
      cfg.pin_bl      = 45;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    { // touch (FT5x06 / FT6336U)
      auto cfg = _touch_instance.config();
      cfg.x_min      = 0;
      cfg.x_max      = 319;
      cfg.y_min      = 0;
      cfg.y_max      = 479;

      // Poll I2C, ignore INT pin
      cfg.pin_int    = -1;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;

      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda  = 6;
      cfg.pin_scl  = 5;
      cfg.freq     = 100000;   // 100 kHz

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
static lv_color_t lv_buf1[320 * 40];

// Flush callback: LVGL -> LovyanGFX
static void lvgl_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  (void)disp;
  int32_t w = (area->x2 - area->x1 + 1);
  int32_t h = (area->y2 - area->y1 + 1);

  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  lcd.pushPixels((lgfx::rgb565_t*)&color_p->full, w * h);
  lcd.endWrite();

  lv_disp_flush_ready(disp);
}

// Touch callback: LovyanGFX -> LVGL
static void lvgl_touch_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  (void)indev_driver;
  static bool last_pressed = false;
  uint16_t tx, ty;

  if (lcd.getTouch(&tx, &ty)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = tx;
    data->point.y = ty;

    if (!last_pressed) {
      Serial.printf("LVGL touch DOWN: (%u, %u)\n", tx, ty);
    }
    last_pressed = true;
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
  disp_drv.hor_res = 320;
  disp_drv.ver_res = 480;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lvgl_touch_read;
  lv_indev_drv_register(&indev_drv);
}

// ------------------------------------------------------------------
// Arduino setup/loop
// ------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Keyloader UI boot (LVGL, stable keepalive)...");

  lcd.init();
  lcd.setRotation(0);       // portrait
  lcd.setBrightness(255);
  lcd.fillScreen(0x0000);

  setup_lvgl();

  // Mount storage + load containers from SPIFFS/SD (or defaults)
    ContainerModel::instance().load();

  // Build Motorola-style UI (roles + containers)
  ui_init();
}

void loop() {
  // LVGL core
  lv_timer_handler();

  // --- Touch keepalive: poll controller directly too ---
  static int32_t x, y;
  if (lcd.getTouch(&x, &y)) {
    // Optional: comment out fillRect if you don't want dots:
    // lcd.fillRect(x - 1, y - 1, 3, 3, 0xFFFF);

    // Optional: comment out logs if too noisy:
    // Serial.printf("RAW keepalive touch: (%d, %d)\n", x, y);

    delay(50);
  } else {
    delay(5);
  }
}
