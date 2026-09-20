#if defined(BOARD_HELTEC_V4_3) || defined(BOARD_HELTEC_V4_R8)
// =====================================================================
// Heltec WiFi LoRa 32 V4 - two variants share this board file:
//  - BOARD_HELTEC_V4_3: revision 4.3 (ESP32-S3R2, 2 MB PSRAM);
//  - BOARD_HELTEC_V4_R8: "R8" series (ESP32-S3R8, 8 MB PSRAM), sold
//    later, which differs here only by the pin of its Vext rail
//    (GPIO40 instead of GPIO36).
//
// Common to both: SX1262 (LoRa and OLED pinout identical to the V3),
// 128x64 OLED driven as SSD1306, a single user button (PRG), and a FEM
// (front-end module) KCT8103L between the SX1262 and the antenna, which
// adds ~12 dB on transmit and offers a bypassable LNA on receive.
// Values taken from the MeshCore firmware (variants/heltec_v4{,_r8}).
//
// Note: V4 boards older than 4.3 carry a different FEM (GC1109, driven
// differently) and are not supported: initPower() detects it and blocks
// startup. The TFT and e-ink variants are not supported either.
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

// FEM KCT8103L: supply LDO, CSD (enable) and CTX (routing:
// HIGH = PA on transmit / LNA bypassed on receive, LOW = LNA active).
constexpr uint8_t kPinFemLdo = 7;
constexpr uint8_t kPinFemCsd = 2;
constexpr uint8_t kPinFemCtx = 5;
// PA gain on transmit: MeshCore documents 10 dBm requested from the
// SX1262 for 22 dBm measured at the antenna.
constexpr int8_t kFemTxGainDb = 12;

constexpr uint8_t kPinOledSda = 17;
constexpr uint8_t kPinOledScl = 18;
constexpr uint8_t kPinOledReset = 21;
constexpr uint8_t kPinButtonPrg = 0;  // tied to ground when pressed

// Vext (OLED supply): active LOW across the whole series - the rail is
// switched by a P-channel MOSFET (official HTIT-WB32LAF V4.3 schematic,
// transistor Q2 AO3401A), as on the V3. Do not trust the
// PIN_VEXT_EN_ACTIVE=HIGH of MeshCore's heltec_v4 variant: their display
// is built without any reference to the rail, so that level is never
// applied (and their newer R8 variant does say LOW).
#ifdef BOARD_HELTEC_V4_R8
constexpr char kBoardName[] = "Heltec WiFi LoRa 32 V4 R8";
constexpr uint8_t kPinVext = 40;
#else
constexpr char kBoardName[] = "Heltec WiFi LoRa 32 V4.3";
constexpr uint8_t kPinVext = 36;
#endif
constexpr uint8_t kVextOnLevel = LOW;

const ButtonSpec kButtons[] = {
    {Key::Ok, kPinButtonPrg, /*activeLow=*/true, /*internalPullup=*/true},
};

class HeltecV4Board : public Board {
public:
  const char *name() const override { return kBoardName; }

  void initPower() override {
    pinMode(kPinVext, OUTPUT);
    digitalWrite(kPinVext, kVextOnLevel);  // turn on the Vext rail (OLED)

    // Power the FEM, then identify its part number from the idle level
    // of CSD (trick taken from MeshCore): internal pull-up on the
    // KCT8103L (V4.3 and R8) -> HIGH, pull-down on the GC1109 (V4 <= 4.2)
    // -> LOW. A GC1109 is driven differently: rather than transmitting
    // through a misconfigured FEM, we cut its supply and let the
    // application stop on selfCheckError().
    pinMode(kPinFemLdo, OUTPUT);
    digitalWrite(kPinFemLdo, HIGH);
    delay(1);  // FEM start-up time
    pinMode(kPinFemCsd, INPUT);
    delay(1);
    if (digitalRead(kPinFemCsd) == LOW) {
      digitalWrite(kPinFemLdo, LOW);
      _selfCheckError = "FEM GC1109 (V4<=4.2)";
    } else {
      // Configure the FEM. CSD high = enabled; CTX high initially = PA
      // in the transmit path, LNA bypassed on receive. RX routing is then
      // handled by radioRxMode() according to setFemLna(). Careful when
      // the LNA is enabled: its gain adds to the RSSI measured by the
      // SX1262, precisely the figure this device displays.
      pinMode(kPinFemCsd, OUTPUT);
      digitalWrite(kPinFemCsd, HIGH);
      pinMode(kPinFemCtx, OUTPUT);
      digitalWrite(kPinFemCtx, HIGH);
    }

    delay(150);  // let Vext settle before initializing the OLED
  }

  const char *selfCheckError() const override { return _selfCheckError; }

  void radioTxMode() override { digitalWrite(kPinFemCtx, HIGH); }

  void radioRxMode() override {
    digitalWrite(kPinFemCtx, _femLnaEnabled ? LOW : HIGH);
  }

  bool hasFemLna() const override { return true; }

  void setFemLna(bool enabled) override { _femLnaEnabled = enabled; }

  Display &display() override { return _display; }

  void beginDisplay() override {
    _display.begin();
    _u8g2.setContrast(255);
  }

  RadioTraits radio() const override {
    RadioTraits t;
    t.pins.nss = kPinLoraNss;
    t.pins.dio1 = kPinLoraDio1;
    t.pins.reset = kPinLoraReset;
    t.pins.busy = kPinLoraBusy;
    t.pins.sck = kPinLoraSck;
    t.pins.miso = kPinLoraMiso;
    t.pins.mosi = kPinLoraMosi;
    t.dio2AsRfSwitch = true;
    t.tcxoVoltage = 1.8f;
    t.currentLimitmA = 140;
    t.femTxGainDb = kFemTxGainDb;
    t.femRxPatch = true;
    return t;
  }

  // Same FEM and same RF chain as the 4.3: same 20 dBm ceiling at the
  // antenna for the R8 series (to adjust if its PA is qualified higher).
  int8_t txPowerMaxDbm() const override { return 20; }

  const ButtonSpec *buttons(size_t &count) const override {
    count = sizeof(kButtons) / sizeof(kButtons[0]);
    return kButtons;
  }

private:
  const char *_selfCheckError = nullptr;
  bool _femLnaEnabled = false;
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2{U8G2_R0, kPinOledReset,
                                            kPinOledScl, kPinOledSda};
  U8g2Display _display{_u8g2};
};

}  // namespace

Board &board() {
  static HeltecV4Board instance;
  return instance;
}
#endif  // BOARD_HELTEC_V4_3 || BOARD_HELTEC_V4_R8
