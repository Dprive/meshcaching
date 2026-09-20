#ifdef BOARD_HELTEC_T096
// =====================================================================
// Heltec T096 - nRF52840 + SX1262, 0.96" ST7735 color TFT (160x80), a
// single user button, and the same KCT8103L FEM as the V4 boards
// (bypassable LNA on receive, ~13 dB of PA gain on transmit according
// to MeshCore: 9 dBm requested from the SX1262 for ~22 dBm at the
// antenna).
//
// Pinout taken from the MeshCore firmware (variants/heltec_t096): the
// Arduino variant (variants/Heltec_T096_Board) provides the
// LORA_*/SX126X_*/PIN_TFT_*/PIN_USER_BTN macros, and the default SPI
// bus is the radio's. The TFT is on SPI1.
// =====================================================================
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "../Board.h"

namespace {

// FEM KCT8103L: supply LDO, CSD (enable) and CTX (routing:
// HIGH = PA on transmit / LNA bypassed on receive, LOW = LNA active).
constexpr uint8_t kPinFemLdo = 30;
constexpr uint8_t kPinFemCsd = 12;
constexpr uint8_t kPinFemCtx = 41;
constexpr int8_t kFemTxGainDb = 13;

constexpr uint8_t kPinVext = 26;   // TFT and GPS VDD, active HIGH
constexpr uint8_t kPin3V3En = 38;  // 3.3 V peripheral rail

const ButtonSpec kButtons[] = {
    // external pull-up on the board
    {Key::Ok, PIN_USER_BTN, /*activeLow=*/true, /*internalPullup=*/false},
};

// Display -> ST7735 adapter: frame composed in a 16-bit framebuffer
// (25.6 KB of RAM), logical 128x64 area centered in the 160x80 panel,
// U8g2 fonts rendered by U8g2_for_Adafruit_GFX. "mini 160x80" init
// without color inversion, like the MeshCore T096 driver.
class T096Display : public Display {
public:
  void begin() override {
    // Backlight switched off during init (active low)
    pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
    digitalWrite(PIN_TFT_LEDA_CTL, !PIN_TFT_LEDA_CTL_ACTIVE);
    _tft.initR(INITR_MINI160x80);
    _tft.setRotation(1);  // landscape 160x80
    _fonts.begin(_canvas);
    _fonts.setFontMode(1);  // transparent background
    _fonts.setForegroundColor(kWhite);
    clear();
    send();
    digitalWrite(PIN_TFT_LEDA_CTL, PIN_TFT_LEDA_CTL_ACTIVE);
  }

  void clear() override { _canvas.fillScreen(kBlack); }
  void send() override {
    _tft.drawRGBBitmap(0, 0, _canvas.getBuffer(), kPanelW, kPanelH);
  }

  void setFont(Font font) override {
    switch (font) {
      case Font::kSmall: _fonts.setFont(u8g2_font_6x12_tf); break;
      case Font::kMedium: _fonts.setFont(u8g2_font_helvB12_tr); break;
      case Font::kMenu: _fonts.setFont(u8g2_font_10x20_tf); break;
      case Font::kBig: _fonts.setFont(u8g2_font_logisoso24_tr); break;
    }
  }

  void drawText(int16_t x, int16_t y, const char *utf8) override {
    _fonts.drawUTF8(x + kOffX, y + kOffY, utf8);
  }
  uint16_t textWidth(const char *utf8) override {
    return _fonts.getUTF8Width(utf8);
  }
  void drawBox(int16_t x, int16_t y, int16_t w, int16_t h) override {
    _canvas.fillRect(x + kOffX, y + kOffY, w, h, _ink);
  }
  void drawHLine(int16_t x, int16_t y, int16_t w) override {
    _canvas.drawFastHLine(x + kOffX, y + kOffY, w, _ink);
  }

  void setInkInverted(bool inverted) override {
    _ink = inverted ? kBlack : kWhite;
    // Inverted text rendered in "solid" mode: the library paints the
    // white glyph background itself then its black pixels, instead of
    // the transparent mode on top of the already filled box.
    _fonts.setFontMode(inverted ? 0 : 1);
    _fonts.setForegroundColor(_ink);
    _fonts.setBackgroundColor(inverted ? kWhite : kBlack);
  }
  void invertFrame() override {
    uint16_t *pixels = _canvas.getBuffer();
    for (int32_t i = 0; i < (int32_t)kPanelW * kPanelH; i++) {
      pixels[i] ^= 0xFFFF;
    }
  }

private:
  static constexpr int16_t kPanelW = 160;
  static constexpr int16_t kPanelH = 80;
  static constexpr int16_t kOffX = (kPanelW - 128) / 2;
  static constexpr int16_t kOffY = (kPanelH - 64) / 2;
  static constexpr uint16_t kBlack = 0x0000;
  static constexpr uint16_t kWhite = 0xFFFF;

  Adafruit_ST7735 _tft{&SPI1, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST};
  GFXcanvas16 _canvas{kPanelW, kPanelH};
  U8G2_FOR_ADAFRUIT_GFX _fonts;
  uint16_t _ink = kWhite;
};

class HeltecT096Board : public Board {
public:
  const char *name() const override { return "Heltec T096"; }

  void initPower() override {
    pinMode(kPin3V3En, OUTPUT);
    digitalWrite(kPin3V3En, HIGH);
    pinMode(kPinVext, OUTPUT);
    digitalWrite(kPinVext, HIGH);  // turn on the TFT VDD

    // Power then configure the FEM. CSD high = enabled; CTX high
    // initially = PA in the transmit path, LNA bypassed on receive. RX
    // routing is then handled by radioRxMode() according to setFemLna().
    // Careful when the LNA is enabled: its gain adds to the RSSI measured
    // by the SX1262, precisely the figure this device displays.
    pinMode(kPinFemLdo, OUTPUT);
    digitalWrite(kPinFemLdo, HIGH);
    delay(1);  // FEM start-up time
    pinMode(kPinFemCsd, OUTPUT);
    digitalWrite(kPinFemCsd, HIGH);
    pinMode(kPinFemCtx, OUTPUT);
    digitalWrite(kPinFemCtx, HIGH);

    delay(50);  // let the rails settle before initializing the TFT
  }

  void radioTxMode() override { digitalWrite(kPinFemCtx, HIGH); }

  void radioRxMode() override {
    digitalWrite(kPinFemCtx, _femLnaEnabled ? LOW : HIGH);
  }

  bool hasFemLna() const override { return true; }

  void setFemLna(bool enabled) override { _femLnaEnabled = enabled; }

  Display &display() override { return _display; }

  RadioTraits radio() const override {
    RadioTraits t;
    t.pins.nss = LORA_CS;
    t.pins.dio1 = SX126X_DIO1;
    t.pins.reset = SX126X_RESET;
    t.pins.busy = SX126X_BUSY;
    // sck/miso/mosi left at -1: default SPI bus of the variant
    t.dio2AsRfSwitch = true;
    t.tcxoVoltage = SX126X_DIO3_TCXO_VOLTAGE;
    t.currentLimitmA = 140;
    t.femTxGainDb = kFemTxGainDb;
    return t;
  }

  int8_t txPowerMaxDbm() const override { return 22; }

  const ButtonSpec *buttons(size_t &count) const override {
    count = sizeof(kButtons) / sizeof(kButtons[0]);
    return kButtons;
  }

private:
  bool _femLnaEnabled = false;
  T096Display _display;
};

}  // namespace

Board &board() {
  static HeltecT096Board instance;
  return instance;
}
#endif  // BOARD_HELTEC_T096
