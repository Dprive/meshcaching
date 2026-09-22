#include "TDeckUI.h"
#ifdef BOARD_TDECK

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static TDeckUI* g_uiInstance = nullptr;

namespace {
  const lv_color_t kColorBg = lv_color_hex(0x0F172A);
  const lv_color_t kColorNavBg = lv_color_hex(0x1E293B);
  const lv_color_t kColorAccent = lv_color_hex(0x10B981);
  const lv_color_t kColorText = lv_color_hex(0xFFFFFF);
  const lv_color_t kColorDimText = lv_color_hex(0x94A3B8);
  const lv_color_t kColorWarning = lv_color_hex(0xEF4444);
  const lv_color_t kColorHighlight = lv_color_hex(0x334155);
}

TDeckUI::TDeckUI(Board &board) 
  : _board(board), 
    _tft(getTDeckTFT()),
    _inSettings(false),
    _lastActivityMs(0) 
{
  g_uiInstance = this;
}

TDeckUI::~TDeckUI() {
  if (_buf1) free(_buf1);
}

void TDeckUI::disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  if (g_uiInstance && g_uiInstance->_tft) {
    g_uiInstance->_tft->drawRGBBitmap(area->x1, area->y1, (uint16_t*)color_p, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
  }
  lv_disp_flush_ready(disp);
}

void TDeckUI::indev_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  int16_t x = 0, y = 0;
  if (getTDeckTouch(x, y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void TDeckUI::indev_keypad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  static uint32_t last_key = 0;
  uint8_t k = getTDeckKey();
  if (k > 0) {
    data->state = LV_INDEV_STATE_PR;
    if (k == 0x08 || k == 0x7F) last_key = LV_KEY_BACKSPACE;
    else if (k == '\r' || k == '\n') last_key = LV_KEY_ENTER;
    else last_key = k;
    if (g_uiInstance) g_uiInstance->_lastActivityMs = millis();
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  data->key = last_key;
}

void TDeckUI::indev_trackball_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  if (g_uiInstance) {
    data->point.x = g_uiInstance->_tb_x;
    data->point.y = g_uiInstance->_tb_y;
    data->state = g_uiInstance->_tb_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    g_uiInstance->_tb_pressed = false; // auto release
  }
}

void TDeckUI::setup() {
  lv_init();
  initTDeckTouch();

  _buf1 = (lv_color_t*)malloc(kBufSize * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&_draw_buf, _buf1, NULL, kBufSize);

  lv_disp_drv_init(&_disp_drv);
  _disp_drv.hor_res = 320;
  _disp_drv.ver_res = 240;
  _disp_drv.flush_cb = disp_flush;
  _disp_drv.draw_buf = &_draw_buf;
  lv_disp_drv_register(&_disp_drv);

  static lv_indev_drv_t indev_touch;
  lv_indev_drv_init(&indev_touch);
  indev_touch.type = LV_INDEV_TYPE_POINTER;
  indev_touch.read_cb = indev_touch_read;
  lv_indev_drv_register(&indev_touch);

  static lv_indev_drv_t indev_keypad;
  lv_indev_drv_init(&indev_keypad);
  indev_keypad.type = LV_INDEV_TYPE_KEYPAD;
  indev_keypad.read_cb = indev_keypad_read;
  lv_indev_t *keypad_dev = lv_indev_drv_register(&indev_keypad);

  static lv_indev_drv_t indev_trackball;
  lv_indev_drv_init(&indev_trackball);
  indev_trackball.type = LV_INDEV_TYPE_POINTER;
  indev_trackball.read_cb = indev_trackball_read;
  lv_indev_t *trackball_dev = lv_indev_drv_register(&indev_trackball);

  lv_obj_t * cursor_obj = lv_obj_create(lv_layer_sys());
  lv_obj_set_size(cursor_obj, 12, 12);
  lv_obj_set_style_bg_color(cursor_obj, kColorAccent, 0);
  lv_obj_set_style_radius(cursor_obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(cursor_obj, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(cursor_obj, 2, 0);
  lv_indev_set_cursor(trackball_dev, cursor_obj);

  _group = lv_group_create();
  lv_group_set_default(_group);
  lv_indev_set_group(keypad_dev, _group);

  applyStyles();
  
  _scrDashboard = lv_obj_create(NULL);
  lv_obj_add_style(_scrDashboard, &_style_screen, 0);
  buildDashboard();
  
  _scrSettings = lv_obj_create(NULL);
  lv_obj_add_style(_scrSettings, &_style_screen, 0);
  buildSettings();
}

void TDeckUI::tick() {
  lv_timer_handler();
}

void TDeckUI::applyStyles() {
  lv_style_init(&_style_screen);
  lv_style_set_bg_color(&_style_screen, kColorBg);
  lv_style_set_text_color(&_style_screen, kColorText);

  lv_style_init(&_style_title);
  lv_style_set_text_color(&_style_title, kColorAccent);

  lv_style_init(&_style_value);
  lv_style_set_text_color(&_style_value, kColorText);

  lv_style_init(&_style_badge);
  lv_style_set_bg_color(&_style_badge, kColorNavBg);
  lv_style_set_radius(&_style_badge, 8);
}

void TDeckUI::buildDashboard() {
  // Same as before
  lv_obj_t *header = lv_obj_create(_scrDashboard);
  lv_obj_set_size(header, 300, 40);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_add_style(header, &_style_badge, 0);
  lv_obj_set_style_border_width(header, 0, 0);

  _lblRepeater = lv_label_create(header);
  lv_obj_align(_lblRepeater, LV_ALIGN_LEFT_MID, 10, 0);

  _lblNoise = lv_label_create(header);
  lv_obj_align(_lblNoise, LV_ALIGN_LEFT_MID, 120, 0);
  lv_obj_set_style_text_color(_lblNoise, kColorDimText, 0);

  _badgeTx = lv_obj_create(header);
  lv_obj_set_size(_badgeTx, 60, 24);
  lv_obj_align(_badgeTx, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_bg_color(_badgeTx, kColorAccent, 0);
  lv_obj_set_style_radius(_badgeTx, 6, 0);
  lv_obj_set_style_border_width(_badgeTx, 0, 0);
  
  _lblTx = lv_label_create(_badgeTx);
  lv_obj_center(_lblTx);
  lv_obj_set_style_text_color(_lblTx, kColorBg, 0);
  lv_label_set_text(_lblTx, "");
  lv_obj_add_flag(_badgeTx, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *valContainer = lv_obj_create(_scrDashboard);
  lv_obj_set_size(valContainer, 300, 100);
  lv_obj_align(valContainer, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_opa(valContainer, 0, 0);
  lv_obj_set_style_border_width(valContainer, 0, 0);

  _lblRssiTitle = lv_label_create(valContainer);
  lv_obj_add_style(_lblRssiTitle, &_style_title, 0);
  lv_label_set_text(_lblRssiTitle, "RSSI");

  _lblRssi = lv_label_create(valContainer);
  lv_obj_add_style(_lblRssi, &_style_value, 0);
  lv_label_set_text(_lblRssi, "--");

  _lblDespreadTitle = lv_label_create(valContainer);
  lv_obj_add_style(_lblDespreadTitle, &_style_title, 0);
  lv_label_set_text(_lblDespreadTitle, "DESPREAD");

  _lblDespread = lv_label_create(valContainer);
  lv_obj_add_style(_lblDespread, &_style_value, 0);
  lv_label_set_text(_lblDespread, "--");

  _lblSnr = lv_label_create(valContainer);
  lv_obj_align(_lblSnr, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_text_color(_lblSnr, kColorDimText, 0);
  lv_label_set_text(_lblSnr, "SNR -- dB");

  _barCooldown = lv_bar_create(_scrDashboard);
  lv_obj_set_size(_barCooldown, 320, 4);
  lv_obj_align(_barCooldown, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_bg_color(_barCooldown, kColorBg, 0);
  lv_obj_set_style_bg_color(_barCooldown, kColorAccent, LV_PART_INDICATOR);
  lv_bar_set_range(_barCooldown, 0, 100);
  lv_bar_set_value(_barCooldown, 0, LV_ANIM_OFF);

  lv_obj_t *navbar = lv_obj_create(_scrDashboard);
  lv_obj_set_size(navbar, 320, 40);
  lv_obj_align(navbar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(navbar, kColorNavBg, 0);
  lv_obj_set_style_border_width(navbar, 0, 0);

  lv_obj_t *btnDash = lv_btn_create(navbar);
  lv_obj_set_size(btnDash, 140, 40);
  lv_obj_align(btnDash, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_opa(btnDash, 0, 0);
  lv_obj_t *lblDash = lv_label_create(btnDash);
  lv_label_set_text(lblDash, "DASHBOARD");
  lv_obj_center(lblDash);
  lv_obj_set_style_text_color(lblDash, kColorAccent, 0);

  lv_obj_t *btnSet = lv_btn_create(navbar);
  lv_obj_set_size(btnSet, 140, 40);
  lv_obj_align(btnSet, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(btnSet, 0, 0);
  lv_obj_t *lblSet = lv_label_create(btnSet);
  lv_label_set_text(lblSet, "PARAMETRES");
  lv_obj_center(lblSet);
  lv_obj_set_style_text_color(lblSet, kColorDimText, 0);

  lv_obj_add_event_cb(btnSet, [](lv_event_t *e){
    TDeckUI* ui = (TDeckUI*)lv_event_get_user_data(e);
    ui->_wantsOpenSettings = true;
  }, LV_EVENT_CLICKED, this);
}

void TDeckUI::onTargetChanged(lv_event_t *e) {
  TDeckUI* ui = (TDeckUI*)lv_event_get_user_data(e);
  lv_obj_t * ta = lv_event_get_target(e);
  const char * txt = lv_textarea_get_text(ta);
  int len = strlen(txt);
  
  if (len >= 2) {
    char h[3] = {txt[0], txt[1], 0};
    ui->_settings.targetPrefix[0] = strtol(h, NULL, 16);
  } else {
    ui->_settings.targetPrefix[0] = 0;
  }
  
  if (len >= 4) {
    char h[3] = {txt[2], txt[3], 0};
    ui->_settings.targetPrefix[1] = strtol(h, NULL, 16);
  } else {
    ui->_settings.targetPrefix[1] = 0;
  }
}

void TDeckUI::onPowerChanged(lv_event_t *e) {
  TDeckUI* ui = (TDeckUI*)lv_event_get_user_data(e);
  lv_obj_t * ta = lv_event_get_target(e);
  const char * txt = lv_textarea_get_text(ta);
  if (strlen(txt) > 0) {
    int p = atoi(txt);
    if (p > 22) {
      p = 22;
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", p);
      lv_textarea_set_text(ta, buf);
    }
    ui->_settings.txPowerDbm = p;
  }
}

void TDeckUI::onRxGainChanged(lv_event_t *e) {
  TDeckUI* ui = (TDeckUI*)lv_event_get_user_data(e);
  lv_obj_t * dd = lv_event_get_target(e);
  uint16_t sel = lv_dropdown_get_selected(dd);
  RxGainMode choices[3];
  ui->rxGainChoices(choices);
  ui->_settings.rxGainMode = choices[sel];
}

void TDeckUI::onDisplayChanged(lv_event_t *e) {
  TDeckUI* ui = (TDeckUI*)lv_event_get_user_data(e);
  lv_obj_t * dd = lv_event_get_target(e);
  ui->_settings.rssiDisplay = (RssiDisplayMode)lv_dropdown_get_selected(dd);
}

void TDeckUI::onBackClicked(lv_event_t *e) {
  TDeckUI* ui = (TDeckUI*)lv_event_get_user_data(e);
  
  // Force parse final values in case event was missed
  const char * txt = lv_textarea_get_text(ui->_taTarget);
  int len = strlen(txt);
  if (len >= 2) { char h[3] = {txt[0], txt[1], 0}; ui->_settings.targetPrefix[0] = strtol(h, NULL, 16); }
  if (len >= 4) { char h[3] = {txt[2], txt[3], 0}; ui->_settings.targetPrefix[1] = strtol(h, NULL, 16); }
  
  const char * pwrTxt = lv_textarea_get_text(ui->_taPower);
  if (strlen(pwrTxt) > 0) {
    int p = atoi(pwrTxt);
    if (p > 22) p = 22;
    ui->_settings.txPowerDbm = p;
  }
  
  RxGainMode choices[3];
  ui->rxGainChoices(choices);
  ui->_settings.rxGainMode = choices[lv_dropdown_get_selected(ui->_ddRxGain)];
  ui->_settings.rssiDisplay = (RssiDisplayMode)lv_dropdown_get_selected(ui->_ddDisplay);
  
  ui->_inSettings = false;
  ui->_closedByTouch = true;
  lv_scr_load(ui->_scrDashboard);
}

uint8_t TDeckUI::rxGainChoices(RxGainMode out[3]) const {
  out[0] = RxGainMode::kNone;
  out[1] = RxGainMode::kSxBoost;
  out[2] = RxGainMode::kFemLna;
  return _board.hasFemLna() ? 3 : 2;
}

void TDeckUI::buildSettings() {
  lv_obj_t *title = lv_label_create(_scrSettings);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_style(title, &_style_title, 0);
  lv_label_set_text(title, "PARAMETRES");

  lv_obj_t *cont = lv_obj_create(_scrSettings);
  lv_obj_set_size(cont, 300, 180);
  lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -50);
  lv_obj_set_style_bg_opa(cont, 0, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  // Target Prefix
  lv_obj_t *lbl = lv_label_create(cont);
  lv_label_set_text(lbl, "Repeteur Cible (Hex)");
  _taTarget = lv_textarea_create(cont);
  lv_textarea_set_one_line(_taTarget, true);
  lv_textarea_set_max_length(_taTarget, 4);
  lv_textarea_set_accepted_chars(_taTarget, "0123456789ABCDEFabcdef");
  lv_obj_set_width(_taTarget, lv_pct(100));
  lv_obj_add_event_cb(_taTarget, onTargetChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_group_add_obj(_group, _taTarget);

  // Power
  lbl = lv_label_create(cont);
  lv_label_set_text(lbl, "Puissance TX (Max 22)");
  _taPower = lv_textarea_create(cont);
  lv_textarea_set_one_line(_taPower, true);
  lv_textarea_set_max_length(_taPower, 2);
  lv_textarea_set_accepted_chars(_taPower, "0123456789");
  lv_obj_set_width(_taPower, lv_pct(100));
  lv_obj_add_event_cb(_taPower, onPowerChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_group_add_obj(_group, _taPower);

  // Gain
  lbl = lv_label_create(cont);
  lv_label_set_text(lbl, "Gain RX");
  _ddRxGain = lv_dropdown_create(cont);
  lv_obj_set_width(_ddRxGain, lv_pct(100));
  lv_obj_add_event_cb(_ddRxGain, onRxGainChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_group_add_obj(_group, _ddRxGain);

  // Display
  lbl = lv_label_create(cont);
  lv_label_set_text(lbl, "Affichage");
  _ddDisplay = lv_dropdown_create(cont);
  lv_dropdown_set_options(_ddDisplay, "R+D\nRSSI\nDESPREAD");
  lv_obj_set_width(_ddDisplay, lv_pct(100));
  lv_obj_add_event_cb(_ddDisplay, onDisplayChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_group_add_obj(_group, _ddDisplay);

  // Save/Back
  _btnBack = lv_btn_create(cont);
  lv_obj_set_width(_btnBack, lv_pct(100));
  lv_obj_t *lblBack = lv_label_create(_btnBack);
  lv_label_set_text(lblBack, "Enregistrer & Retour");
  lv_obj_center(lblBack);
  lv_obj_add_event_cb(_btnBack, onBackClicked, LV_EVENT_CLICKED, this);
  lv_group_add_obj(_group, _btnBack);

  lv_obj_t *navbar = lv_obj_create(_scrSettings);
  lv_obj_set_size(navbar, 320, 40);
  lv_obj_align(navbar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(navbar, kColorNavBg, 0);
  lv_obj_set_style_border_width(navbar, 0, 0);

  lv_obj_t *btnDash = lv_btn_create(navbar);
  lv_obj_set_size(btnDash, 140, 40);
  lv_obj_align(btnDash, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_opa(btnDash, 0, 0);
  lv_obj_t *lblDash = lv_label_create(btnDash);
  lv_label_set_text(lblDash, "DASHBOARD");
  lv_obj_center(lblDash);
  lv_obj_set_style_text_color(lblDash, kColorDimText, 0);

  lv_obj_t *btnSet = lv_btn_create(navbar);
  lv_obj_set_size(btnSet, 140, 40);
  lv_obj_align(btnSet, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(btnSet, 0, 0);
  lv_obj_t *lblSet = lv_label_create(btnSet);
  lv_label_set_text(lblSet, "PARAMETRES");
  lv_obj_center(lblSet);
  lv_obj_set_style_text_color(lblSet, kColorAccent, 0);

  lv_obj_add_event_cb(btnDash, [](lv_event_t *e){
    TDeckUI* ui = (TDeckUI*)lv_event_get_user_data(e);
    
    // Auto-save when switching back to Dashboard
    const char * txt = lv_textarea_get_text(ui->_taTarget);
    int len = strlen(txt);
    if (len >= 2) { char h[3] = {txt[0], txt[1], 0}; ui->_settings.targetPrefix[0] = strtol(h, NULL, 16); }
    if (len >= 4) { char h[3] = {txt[2], txt[3], 0}; ui->_settings.targetPrefix[1] = strtol(h, NULL, 16); }
    
    const char * pwrTxt = lv_textarea_get_text(ui->_taPower);
    if (strlen(pwrTxt) > 0) {
      int p = atoi(pwrTxt);
      if (p > 22) p = 22;
      ui->_settings.txPowerDbm = p;
    }
    
    RxGainMode choices[3];
    ui->rxGainChoices(choices);
    ui->_settings.rxGainMode = choices[lv_dropdown_get_selected(ui->_ddRxGain)];
    ui->_settings.rssiDisplay = (RssiDisplayMode)lv_dropdown_get_selected(ui->_ddDisplay);

    ui->_inSettings = false;
    ui->_closedByTouch = true;
    lv_scr_load(ui->_scrDashboard);
  }, LV_EVENT_CLICKED, this);
}

void TDeckUI::showSplash(const char *version) {
  lv_obj_t *splash = lv_obj_create(NULL);
  lv_obj_add_style(splash, &_style_screen, 0);
  
  lv_obj_t *lbl = lv_label_create(splash);
  lv_obj_center(lbl);
  lv_obj_add_style(lbl, &_style_title, 0);
  lv_label_set_text(lbl, "MESHCACHING");

  lv_obj_t *lblV = lv_label_create(splash);
  lv_obj_align(lblV, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_style_text_color(lblV, kColorDimText, 0);
  lv_label_set_text(lblV, version);

  lv_scr_load(splash);
  
  for(int i=0; i<10; i++) {
    lv_timer_handler();
    delay(5);
  }
}

void TDeckUI::refreshMain(const MainView &v) {
  if (_inSettings) return;
  if (lv_scr_act() != _scrDashboard) lv_scr_load(_scrDashboard);

  char buf[32];
  snprintf(buf, sizeof(buf), "RPT %02X%02X", v.pubkeyPrefix[0], v.prefixLen >= 2 ? v.pubkeyPrefix[1] : 0);
  lv_label_set_text(_lblRepeater, buf);

  if (v.noiseValid) {
    snprintf(buf, sizeof(buf), "NF: %d dBm", (int)lroundf(v.noiseDbm));
    lv_label_set_text(_lblNoise, buf);
  }

  if (v.txBadge != nullptr) {
    lv_obj_clear_flag(_badgeTx, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(_lblTx, v.txBadge);
    lv_obj_set_style_bg_color(_badgeTx, strcmp(v.txBadge, "OCCUPE") == 0 ? kColorWarning : kColorAccent, 0);
  } else {
    lv_obj_add_flag(_badgeTx, LV_OBJ_FLAG_HIDDEN);
  }

  if (v.rssiDisplay == RssiDisplayMode::kBoth) {
    lv_obj_clear_flag(_lblRssiTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_lblRssi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_lblDespreadTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_lblDespread, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_align(_lblRssiTitle, LV_ALIGN_TOP_MID, -70, 0);
    lv_obj_align(_lblRssi, LV_ALIGN_TOP_MID, -70, 25);
    
    lv_obj_align(_lblDespreadTitle, LV_ALIGN_TOP_MID, 70, 0);
    lv_obj_align(_lblDespread, LV_ALIGN_TOP_MID, 70, 25);
  } else if (v.rssiDisplay == RssiDisplayMode::kRssiOnly) {
    lv_obj_clear_flag(_lblRssiTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_lblRssi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_lblDespreadTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_lblDespread, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_align(_lblRssiTitle, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_align(_lblRssi, LV_ALIGN_TOP_MID, 0, 25);
  } else {
    lv_obj_add_flag(_lblRssiTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_lblRssi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_lblDespreadTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_lblDespread, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_align(_lblDespreadTitle, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_align(_lblDespread, LV_ALIGN_TOP_MID, 0, 25);
  }

  if (v.rssiValid) {
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(v.rssi));
    lv_label_set_text(_lblRssi, buf);
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(v.despreadRssi));
    lv_label_set_text(_lblDespread, buf);
    int snr10 = (int)lroundf(v.snr * 10.0f);
    snprintf(buf, sizeof(buf), "SNR %s%d.%c dB", snr10 < 0 ? "-" : "", abs(snr10) / 10, (char)('0' + abs(snr10) % 10));
    lv_label_set_text(_lblSnr, buf);
  } else {
    lv_label_set_text(_lblRssi, "--");
    lv_label_set_text(_lblDespread, "--");
    lv_label_set_text(_lblSnr, "EN VEILLE");
  }

  if (v.cooldownRemainingMs > 0 && v.cooldownTotalMs > 0) {
    uint32_t pct = (100 * v.cooldownRemainingMs) / v.cooldownTotalMs;
    lv_bar_set_value(_barCooldown, pct, LV_ANIM_OFF);
  } else {
    lv_bar_set_value(_barCooldown, 0, LV_ANIM_OFF);
  }
}

void TDeckUI::open(const AppSettings &current) {
  _settings = current;
  _backup = current;
  _inSettings = true;
  _lastActivityMs = millis();
  
  updateSettingsUI();
  lv_scr_load(_scrSettings);
}

void TDeckUI::updateSettingsUI() {
  char buf[32];
  snprintf(buf, sizeof(buf), "%02X%02X", _settings.targetPrefix[0], _settings.targetPrefix[1]);
  lv_textarea_set_text(_taTarget, buf);

  snprintf(buf, sizeof(buf), "%d", _settings.txPowerDbm);
  lv_textarea_set_text(_taPower, buf);

  // RX Gain options
  RxGainMode choices[3];
  uint8_t count = rxGainChoices(choices);
  String opts = "";
  int selGain = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (choices[i] == RxGainMode::kNone) opts += "AUCUN";
    else if (choices[i] == RxGainMode::kSxBoost) opts += "RX BOOST";
    else opts += "FEM LNA";
    if (i < count - 1) opts += "\n";
    if (choices[i] == _settings.rxGainMode) selGain = i;
  }
  lv_dropdown_set_options(_ddRxGain, opts.c_str());
  lv_dropdown_set_selected(_ddRxGain, selGain);

  lv_dropdown_set_selected(_ddDisplay, (uint16_t)_settings.rssiDisplay);
}

bool TDeckUI::tickTimeout() {
  if (_inSettings && millis() - _lastActivityMs >= kTimeoutMs) {
    _inSettings = false;
    _settings = _backup; // cancel modifications
    lv_scr_load(_scrDashboard);
    return true;
  }
  return false;
}

void TDeckUI::updateTrackball(const ButtonEvent &event) {
  _lastActivityMs = millis();
  const int step = 15;
  switch (event.key) {
    case Key::Up: _tb_y = max(0, _tb_y - step); break;
    case Key::Down: _tb_y = min(239, _tb_y + step); break;
    case Key::Left: _tb_x = max(0, _tb_x - step); break;
    case Key::Right: _tb_x = min(319, _tb_x + step); break;
    case Key::Ok: _tb_pressed = true; break;
    case Key::Back: break;
  }
}

bool TDeckUI::handleEvent(const ButtonEvent &event) {
  // Navigation is handled natively by LVGL and updateTrackball
  return false;
}

#endif // BOARD_TDECK
