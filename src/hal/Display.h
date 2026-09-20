#pragma once
#include <stdint.h>

// =====================================================================
// Display abstraction of the application: a logical 128x64 monochrome
// frame (white on black), whatever the actual panel is - an OLED driven
// by U8g2 (see U8g2Display) or a color TFT (adapter in the relevant
// board file, e.g. the ST7735 of the Heltec T096).
// The screens (ui/) only know about this interface.
// =====================================================================

// Logical font set, mapped by each adapter.
enum class Font : uint8_t {
  kSmall,   // 6x12 - body text, header bar, hints
  kMedium,  // ~12 px bold - splash title, medium Z of the logo
  kMenu,    // 10x20 - menu values
  kBig,     // ~24 px - RSSI, hex digits, large Z
};

class Display {
public:
  virtual ~Display() {}

  virtual void begin() = 0;

  // Dimensions of the logical frame (adapters for larger panels
  // center this area).
  uint16_t width() const { return 128; }
  uint16_t height() const { return 64; }

  virtual void clear() = 0;  // clears the frame being composed
  virtual void send() = 0;   // pushes the frame to the panel

  virtual void setFont(Font font) = 0;
  virtual void drawText(int16_t x, int16_t y, const char *utf8) = 0;
  virtual uint16_t textWidth(const char *utf8) = 0;
  virtual void drawBox(int16_t x, int16_t y, int16_t w, int16_t h) = 0;
  virtual void drawHLine(int16_t x, int16_t y, int16_t w) = 0;

  // Inverted ink: draws in the background color (text of a solid badge)
  virtual void setInkInverted(bool inverted) = 0;
  // Inverts the whole frame being composed (receive flash)
  virtual void invertFrame() = 0;
};
