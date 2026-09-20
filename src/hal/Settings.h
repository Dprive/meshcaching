#pragma once
#include <stdint.h>

#include "RxGain.h"

// Choice of the signal measurements shown on the main screen
enum class RssiDisplayMode : uint8_t {
  kBoth = 0,      // average RSSI and despreader side by side
  kRssiOnly = 1,  // average RSSI alone, in large type - default
  kDespreadOnly = 2,  // despreader RSSI alone, in large type
};

// =====================================================================
// Persisted application configuration, editable from the menu.
// Storage: NVS (Preferences) on ESP32, internal LittleFS on nRF52.
// Factory defaults are composed by the application (AppConfig + Board).
// =====================================================================
struct AppSettings {
  uint8_t targetPrefix[2];  // public key prefix of the target repeater
  int8_t txPowerDbm;        // transmit power "at the antenna"
  RxGainMode rxGainMode;
  RssiDisplayMode rssiDisplay;
};

bool settingsEqual(const AppSettings &a, const AppSettings &b);

// false when no valid config is stored (first boot, incompatible
// version): the caller then starts from the factory defaults.
bool settingsLoad(AppSettings &out);
void settingsSave(const AppSettings &s);
