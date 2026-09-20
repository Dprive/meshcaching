#pragma once
#include <stddef.h>

// Noise floor measured in cycles of 64 instantaneous RSSI samples: on
// every completed cycle the median becomes the current value and the
// next cycle starts right away (continuous evaluation). The median
// naturally rejects the samples taken while a packet was going
// through, without having to detect them.
class NoiseFloor {
public:
  void addSample(float rssiDbm);

  bool hasValue() const { return _hasValue; }
  float valueDbm() const { return _medianDbm; }

private:
  static constexpr size_t kSamplesPerCycle = 64;

  float _samples[kSamplesPerCycle];
  size_t _count = 0;
  float _medianDbm = 0;
  bool _hasValue = false;
};
