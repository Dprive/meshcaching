#pragma once
#ifdef BOARD_TDECK

#include <Adafruit_ST7789.h>
#include <lvgl.h>
#include "../../hal/Board.h"
#include "../../hal/Buttons.h"
#include "../../hal/Settings.h"
#include "../StatusScreen.h"

// Forward declaration of HAL functions in BoardTDeck.cpp
Adafruit_ST7789* getTDeckTFT();
bool getTDeckTouch(int16_t &x, int16_t &y);
uint8_t getTDeckKey();
void initTDeckTouch();

class TDeckUI {
public:
  explicit TDeckUI(Board &board);
  ~TDeckUI();

  void setup();
  void tick(); // call lv_timer_handler
  void showSplash(const char *version);
  void refreshMain(const MainView &view);

  // Settings Menu API
  bool isOpen() const { return _inSettings; }
  void open(const AppSettings &current);
  const AppSettings& result() const { return _settings; }
  
  bool wantsOpenSettings() const { return _wantsOpenSettings; }
  void clearWantsOpenSettings() { _wantsOpenSettings = false; }
  
  bool didClose() { 
    bool c = _closedByTouch; 
    _closedByTouch = false; 
    return c; 
  }

  void updateTrackball(const ButtonEvent &event);

  // Returns true if the menu was closed
  bool handleEvent(const ButtonEvent &event);
  bool tickTimeout();

private:
  static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
  static void indev_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data);
  static void indev_keypad_read(lv_indev_drv_t *drv, lv_indev_data_t *data);
  static void indev_trackball_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

  void buildDashboard();
  void buildSettings();
  void applyStyles();
  void updateSettingsUI();
  uint8_t rxGainChoices(RxGainMode out[3]) const;

  // LVGL Callbacks for Settings Widgets
  static void onTargetChanged(lv_event_t *e);
  static void onPowerChanged(lv_event_t *e);
  static void onRxGainChanged(lv_event_t *e);
  static void onDisplayChanged(lv_event_t *e);
  static void onBackClicked(lv_event_t *e);

  Board &_board;
  Adafruit_ST7789 *_tft;

  bool _inSettings;
  AppSettings _settings;
  AppSettings _backup;

  bool _wantsOpenSettings = false;
  bool _closedByTouch = false;

  uint32_t _lastActivityMs;
  static constexpr uint32_t kTimeoutMs = 30000;

  // LVGL Buffers and Screens
  static constexpr uint32_t kBufSize = 320 * 24; // ~15KB
  lv_color_t *_buf1;
  lv_disp_draw_buf_t _draw_buf;
  lv_disp_drv_t _disp_drv;
  lv_group_t *_group;

  // Trackball state
  int16_t _tb_x = 160;
  int16_t _tb_y = 120;
  bool _tb_pressed = false;

  lv_obj_t *_scrDashboard;
  lv_obj_t *_scrSettings;

  // Dashboard Widgets
  lv_obj_t *_lblRepeater;
  lv_obj_t *_lblNoise;
  lv_obj_t *_lblRssiTitle;
  lv_obj_t *_lblRssi;
  lv_obj_t *_lblDespreadTitle;
  lv_obj_t *_lblDespread;
  lv_obj_t *_lblSnr;
  lv_obj_t *_badgeTx;
  lv_obj_t *_lblTx;
  lv_obj_t *_barCooldown;

  // Settings Widgets
  lv_obj_t *_taTarget;
  lv_obj_t *_taPower;
  lv_obj_t *_ddRxGain;
  lv_obj_t *_ddDisplay;
  lv_obj_t *_btnBack;

  // Styles
  lv_style_t _style_screen;
  lv_style_t _style_title;
  lv_style_t _style_value;
  lv_style_t _style_badge;
};

#endif // BOARD_TDECK
