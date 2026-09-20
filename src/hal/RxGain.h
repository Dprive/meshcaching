#pragma once
#include <stdint.h>

// Receive gain chain. The modes are mutually exclusive.
enum class RxGainMode : uint8_t {
  kNone = 0,     // everything off
  kSxBoost = 1,  // SX126x internal "RX boosted gain" (~+2 dB) - default
  kFemLna = 2,   // external FEM LNA (Heltec V4.3 only)
};
