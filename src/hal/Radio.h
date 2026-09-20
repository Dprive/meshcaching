#pragma once
#include <RadioLib.h>

#include "Board.h"
#include "RxGain.h"

// SX1262 wrapper (RadioLib): the wiring comes from the board
// description, and TX power is expressed "at the antenna" - the gain of
// any external FEM is subtracted before being passed to the SX1262.
//
// Reception is interrupt-driven: DIO1 raises a flag, consumed at leisure
// in loop() through packetAvailable() (basic rule with RadioLib: never
// do any processing in the ISR itself).
class Radio {
public:
  explicit Radio(Board &board);

  // Configures and starts listening. Returns RADIOLIB_ERR_NONE if all
  // went well.
  int16_t begin(float freqMhz, float bwKhz, uint8_t sf, uint8_t cr,
                int8_t txPowerDbm);

  // Power at the antenna, clamped to the board range.
  void setTxPowerDbm(int8_t antennaDbm);
  int8_t txPowerDbm() const { return _antennaDbm; }

  // Receive gain chain (SX126x internal boost, FEM LNA if the board has
  // one, or nothing); puts the radio back into listening mode.
  void setRxGainMode(RxGainMode mode);

  // One CAD (LBT) cycle: true if the channel is free. Puts the radio
  // back into listening mode and clears only the flag raised by the CAD
  // itself - the retry loop lives in the caller, which can thus handle a
  // real packet that arrived between two cycles instead of losing it.
  bool channelClear();

  // Transmits then goes back to listening.
  int16_t transmit(const uint8_t *data, size_t len);

  // Instantaneous RSSI, read without disturbing the ongoing reception.
  float rssiInstant() { return _lora.getRSSI(false); }

  // true if a packet has arrived since the last call
  bool packetAvailable();

  // Reads the received packet then puts the radio back into listening
  // mode. len is 0 if the packet was empty or too large for buf. rssi is
  // the RssiPkt averaged over the packet (noise included), despreadRssi
  // the SignalRssiPkt estimated after despreading of the LoRa signal.
  int16_t readPacket(uint8_t *buf, size_t maxLen, size_t &len, float &rssi,
                     float &snr, float &despreadRssi);

  int16_t startReceive();

private:
  int8_t chipPowerDbm(int8_t antennaDbm) const;
  static void onDio1Isr();

  static volatile bool s_packetFlag;

  Board &_board;
  RadioTraits _traits;
  SX1262 _lora;
  int8_t _antennaDbm = 0;
};
