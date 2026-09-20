#ifdef BOARD_WIO_TRACKER_L1
// =====================================================================
// Seeed Wio Tracker L1 Pro - nRF52840 + SX1262, SH1106 128x64 OLED (I2C),
// directional pad + menu button.
//
// The pins (P_LORA_*, SX126X_*, PIN_BUTTON*, JOYSTICK_*) come from
// variants/Seeed_Wio_Tracker_L1/variant.h, taken from the MeshCore
// firmware. SPI and I2C use the default buses set by the variant.
// =====================================================================
#include <U8g2lib.h>
#include <Wire.h>

#include "../Board.h"
#include "../U8g2Display.h"

namespace {

const ButtonSpec kButtons[] = {
    // The joystick and its directions have pull-up resistors on the
    // board; only the menu button uses the internal pull-up.
    {Key::Ok, JOYSTICK_PRESS, /*activeLow=*/true, /*internalPullup=*/false},
    {Key::Back, PIN_BUTTON1, /*activeLow=*/true, /*internalPullup=*/true},
    {Key::Up, JOYSTICK_UP, /*activeLow=*/true, /*internalPullup=*/false},
    {Key::Down, JOYSTICK_DOWN, /*activeLow=*/true, /*internalPullup=*/false},
    {Key::Left, JOYSTICK_LEFT, /*activeLow=*/true, /*internalPullup=*/false},
    {Key::Right, JOYSTICK_RIGHT, /*activeLow=*/true, /*internalPullup=*/false},
};

class WioTrackerL1Board : public Board {
public:
  const char *name() const override { return "Seeed Wio Tracker L1 Pro"; }

  // No Vext rail and no dedicated reset: the display is powered
  // permanently, so the default (empty) initPower() is enough.

  Display &display() override { return _display; }

  void beginDisplay() override {
    // Nominal address 0x3D (see variant.h); some SH1106 modules answer
    // at 0x3C, so probe the bus before initializing.
    Wire.begin();
    uint8_t addr = DISPLAY_ADDRESS;
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) {
      addr = 0x3C;
    }
    _u8g2.setI2CAddress(addr << 1);  // U8g2 expects the 8-bit address
    _display.begin();
  }

  RadioTraits radio() const override {
    RadioTraits t;
    t.pins.nss = P_LORA_NSS;
    t.pins.dio1 = P_LORA_DIO_1;
    t.pins.reset = P_LORA_RESET;
    t.pins.busy = P_LORA_BUSY;
    // sck/miso/mosi left at -1: default SPI bus of the variant
    t.pins.rxEn = SX126X_RXEN;  // RadioLib must drive RXEN on this board
    t.pins.txEn = SX126X_TXEN;
    t.dio2AsRfSwitch = SX126X_DIO2_AS_RF_SWITCH;
    // Without declaring the 1.8V TCXO on DIO3, radio init fails (the
    // crystal never starts).
    t.tcxoVoltage = SX126X_DIO3_TCXO_VOLTAGE;
    t.currentLimitmA = 140;
    return t;
  }

  int8_t txPowerMaxDbm() const override { return 22; }

  const ButtonSpec *buttons(size_t &count) const override {
    count = sizeof(kButtons) / sizeof(kButtons[0]);
    return kButtons;
  }

private:
  U8G2_SH1106_128X64_NONAME_F_HW_I2C _u8g2{U8G2_R0, U8X8_PIN_NONE};
  U8g2Display _display{_u8g2};
};

}  // namespace

Board &board() {
  static WioTrackerL1Board instance;
  return instance;
}
#endif  // BOARD_WIO_TRACKER_L1
