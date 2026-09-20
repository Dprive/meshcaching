#ifdef BOARD_HELTEC_V3
// =====================================================================
// Heltec WiFi LoRa 32 V3 - ESP32-S3 + SX1262, SSD1306 128x64 OLED (I2C),
// a single user button (PRG).
// =====================================================================
#include <U8g2lib.h>

#include "../Board.h"
#include "../U8g2Display.h"

namespace {

constexpr uint8_t kPinLoraNss = 8;
constexpr uint8_t kPinLoraSck = 9;
constexpr uint8_t kPinLoraMosi = 10;
constexpr uint8_t kPinLoraMiso = 11;
constexpr uint8_t kPinLoraReset = 12;
constexpr uint8_t kPinLoraBusy = 13;
constexpr uint8_t kPinLoraDio1 = 14;

constexpr uint8_t kPinOledSda = 17;
constexpr uint8_t kPinOledScl = 18;
constexpr uint8_t kPinOledReset = 21;
constexpr uint8_t kPinVext = 36;      // OLED power rail, active low
constexpr uint8_t kPinButtonPrg = 0;  // tied to ground when pressed

const ButtonSpec kButtons[] = {
    {Key::Ok, kPinButtonPrg, /*activeLow=*/true, /*internalPullup=*/true},
};

class HeltecV3Board : public Board {
public:
  const char *name() const override { return "Heltec WiFi LoRa 32 V3"; }

  void initPower() override {
    pinMode(kPinVext, OUTPUT);
    digitalWrite(kPinVext, LOW);  // turn the Vext rail (OLED) on
    delay(150);
  }

  Display &display() override { return _display; }

  RadioTraits radio() const override {
    RadioTraits t;
    t.pins.nss = kPinLoraNss;
    t.pins.dio1 = kPinLoraDio1;
    t.pins.reset = kPinLoraReset;
    t.pins.busy = kPinLoraBusy;
    t.pins.sck = kPinLoraSck;
    t.pins.miso = kPinLoraMiso;
    t.pins.mosi = kPinLoraMosi;
    t.dio2AsRfSwitch = true;  // DIO2 drives the antenna switch
    t.tcxoVoltage = 1.8f;
    t.currentLimitmA = 140;
    return t;
  }

  int8_t txPowerMaxDbm() const override { return 22; }

  const ButtonSpec *buttons(size_t &count) const override {
    count = sizeof(kButtons) / sizeof(kButtons[0]);
    return kButtons;
  }

private:
  // The OLED hardware reset (pin 21) is handled by U8g2 at begin()
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2{U8G2_R0, kPinOledReset,
                                            kPinOledScl, kPinOledSda};
  U8g2Display _display{_u8g2};
};

}  // namespace

Board &board() {
  static HeltecV3Board instance;
  return instance;
}
#endif  // BOARD_HELTEC_V3
