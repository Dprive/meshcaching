#include "Radio.h"

#include <SPI.h>

#if defined(ESP32)
#define RADIO_ISR_ATTR IRAM_ATTR
#else
#define RADIO_ISR_ATTR
#endif

volatile bool Radio::s_packetFlag = false;

void RADIO_ISR_ATTR Radio::onDio1Isr() {
  s_packetFlag = true;
}

Radio::Radio(Board &board)
    : _board(board),
      _traits(board.radio()),
      _lora(new Module(_traits.pins.nss, _traits.pins.dio1,
                       _traits.pins.reset, _traits.pins.busy)) {}

int8_t Radio::chipPowerDbm(int8_t antennaDbm) const {
  int chip = antennaDbm - _traits.femTxGainDb;
  return (int8_t)constrain(chip, kTxPowerMinDbm, 22);  // SX1262 range
}

void Radio::setTxPowerDbm(int8_t antennaDbm) {
  _antennaDbm = (int8_t)constrain(antennaDbm, _board.txPowerMinDbm(),
                                  _board.txPowerMaxDbm());
  _lora.setOutputPower(chipPowerDbm(_antennaDbm));
}

void Radio::setRxGainMode(RxGainMode mode) {
  if (mode == RxGainMode::kFemLna && !_board.hasFemLna()) {
    mode = RxGainMode::kSxBoost;
  }
  _board.setFemLna(mode == RxGainMode::kFemLna);
  _lora.setRxBoostedGainMode(mode == RxGainMode::kSxBoost);
  startReceive();  // re-applies the FEM RX routing
}

int16_t Radio::begin(float freqMhz, float bwKhz, uint8_t sf, uint8_t cr,
                     int8_t txPowerDbm) {
#if defined(ESP32)
  if (_traits.pins.sck >= 0) {
    SPI.begin(_traits.pins.sck, _traits.pins.miso, _traits.pins.mosi,
              (int8_t)_traits.pins.nss);
  } else {
    SPI.begin();
  }
#else
  SPI.begin();  // pins fixed by the variant
#endif

  _antennaDbm = (int8_t)constrain(txPowerDbm, _board.txPowerMinDbm(),
                                  _board.txPowerMaxDbm());
  int16_t state = _lora.begin(freqMhz, bwKhz, sf, cr,
                              RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                              chipPowerDbm(_antennaDbm), 8,
                              _traits.tcxoVoltage);
  if (state != RADIOLIB_ERR_NONE) {
    return state;
  }

  if (_traits.dio2AsRfSwitch) {
    _lora.setDio2AsRfSwitch(true);
  }
  if (_traits.pins.rxEn != RADIOLIB_NC || _traits.pins.txEn != RADIOLIB_NC) {
    _lora.setRfSwitchPins(_traits.pins.rxEn, _traits.pins.txEn);
  }
  _lora.setCurrentLimit(_traits.currentLimitmA);
  // The RX gain chain (SX126x boost / FEM LNA) is applied afterwards by
  // the application through setRxGainMode(), per the persisted config.

  if (_traits.femRxPatch) {
    // Undocumented register 0x8B5: "improved RX" for the Heltec V4
    // boards with a FEM, on par with the MeshCore firmware (Heltec
    // recipe). Without a valid read we do not write: writing 0x01 alone
    // would clobber the other bits of the register.
    uint8_t value = 0;
    if (_lora.readRegister(0x8B5, &value, 1) == RADIOLIB_ERR_NONE) {
      value |= 0x01;
      _lora.writeRegister(0x8B5, &value, 1);
    } else {
      Serial.println(F("RX patch 0x8B5 not applied (register read failed)"));
    }
  }

  _lora.setDio1Action(onDio1Isr);
  return startReceive();
}

bool Radio::channelClear() {
  // A scan error is treated as a busy channel.
  int16_t state = _lora.scanChannel();
  // The CAD-done has just raised DIO1: clear it now, then go back to
  // listening. A later RX-done (packet received while the caller waits)
  // will raise the flag again and be seen by packetAvailable() before
  // the next CAD cycle.
  s_packetFlag = false;
  startReceive();
  return state == RADIOLIB_CHANNEL_FREE;
}

int16_t Radio::transmit(const uint8_t *data, size_t len) {
  _board.radioTxMode();
  int16_t state = _lora.transmit(data, len);
  // transmit() also raises a "transmission done" DIO1 interrupt, which
  // may have armed the flag by mistake: clear it before going back to
  // listening, so that nothing is handled as a received packet.
  s_packetFlag = false;
  startReceive();
  return state;
}

bool Radio::packetAvailable() {
  if (!s_packetFlag) {
    return false;
  }
  s_packetFlag = false;
  return true;
}

int16_t Radio::readPacket(uint8_t *buf, size_t maxLen, size_t &len,
                          float &rssi, float &snr, float &despreadRssi) {
  len = _lora.getPacketLength();
  if (len == 0 || len > maxLen) {
    len = 0;
    startReceive();
    return RADIOLIB_ERR_NONE;
  }
  int16_t state = _lora.readData(buf, len);
  // GetPacketStatus (LoRa) returns [RssiPkt, SnrPkt, SignalRssiPkt],
  // ordered here from the high byte down to the low byte. Careful:
  // RadioLib's getRSSI(packet) reads the low byte, hence SignalRssiPkt -
  // the three bytes are decoded explicitly so they are not confused.
  uint32_t status = _lora.getPacketStatus();
  rssi = -0.5f * (float)((status >> 16) & 0xFF);
  despreadRssi = -0.5f * (float)(status & 0xFF);
  snr = _lora.getSNR();
  startReceive();
  return state;
}

int16_t Radio::startReceive() {
  _board.radioRxMode();
  return _lora.startReceive();
}
