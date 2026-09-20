#ifdef BOARD_TBEAM_SUPREME
// =====================================================================
// LilyGo T-Beam Supreme ("T-Beam S3 Supreme"), SX1262 868 MHz variant -
// ESP32-S3 + SX1262, SH1106 128x64 OLED (I2C), a single user button
// (GPIO0, in the middle). The LR1121 / 2.4 GHz variants are not
// supported.
//
// Of note: every peripheral rail is switched by an AXP2101 PMU (I2C,
// Wire1 bus on GPIO42/41) - without it, neither the radio nor the display
// is powered. Pinout and rail assignment taken from the MeshCore firmware
// (helpers/esp32/TBeamBoard.*) and from the LilyGo examples
// (LilyGo-LoRa-Series, utilities.h / LoRaBoards.cpp), checked against the
// LilyGo schematics (T-Beam-S3-Core, T-Beam Supreme V3.0 and V3.1).
// =====================================================================
#include <U8g2lib.h>
#include <Wire.h>
#include <XPowersLib.h>

#include "../Board.h"
#include "../U8g2Display.h"

namespace {

constexpr uint8_t kPinLoraNss = 10;
constexpr uint8_t kPinLoraSck = 12;
constexpr uint8_t kPinLoraMosi = 11;
constexpr uint8_t kPinLoraMiso = 13;
constexpr uint8_t kPinLoraReset = 5;
constexpr uint8_t kPinLoraBusy = 4;
constexpr uint8_t kPinLoraDio1 = 1;

// Main I2C bus (Wire): OLED, magnetometer and BME280 (only the OLED is
// of use here). Its pull-ups sit on ALDO1, like the OLED.
constexpr uint8_t kPinOledSda = 17;
constexpr uint8_t kPinOledScl = 18;
// Secondary I2C bus (Wire1): AXP2101 PMU (+ PCF8563 RTC, unused)
constexpr uint8_t kPinPmuSda = 42;
constexpr uint8_t kPinPmuScl = 41;
constexpr uint8_t kPmuI2cAddress = 0x34;
constexpr uint8_t kPinButtonUser = 0;  // tied to ground when pressed

// Possible I2C addresses of the OLED. It changes with the board
// revision, depending on the magnetometer that shares the bus (LilyGo doc
// t_beam_supreme_hw.md): QMC6310N at 0x3C -> OLED at 0x3D; QMC6310U
// (0x1C, V3.0) or QMC6309 (0x7C, V3.1) -> OLED at 0x3C.
constexpr uint8_t kOledAddrPrimary = 0x3D;
constexpr uint8_t kOledAddrAlternate = 0x3C;

const ButtonSpec kButtons[] = {
    {Key::Ok, kPinButtonUser, /*activeLow=*/true, /*internalPullup=*/true},
};

// Frees an I2C bus where a slave holds SDA low (transaction interrupted
// by a reset): clock SCL until it releases the line, then issue a STOP.
// To be done before Wire takes over the pins.
void recoverI2cBus(uint8_t sda, uint8_t scl) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, OUTPUT_OPEN_DRAIN);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  for (int i = 0; i < 9 && digitalRead(sda) == LOW; i++) {
    digitalWrite(scl, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
  }
  pinMode(sda, OUTPUT_OPEN_DRAIN);  // STOP: SDA low -> high, SCL high
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);
  delayMicroseconds(5);
  pinMode(sda, INPUT);
  pinMode(scl, INPUT);
}

bool i2cAck(TwoWire &bus, uint8_t addr) {
  bus.beginTransmission(addr);
  return bus.endTransmission() == 0;
}

// Reads one byte after writing 0x00: on an SH1106/SSD1306 OLED this is
// the status byte (bit 6 = display off, bit 7 = busy), on a QMC6310 its
// identifier (0x80). Returns -1 when there is no answer. Diagnostics only.
int i2cReadByte0(TwoWire &bus, uint8_t addr) {
  bus.beginTransmission(addr);
  bus.write((uint8_t)0x00);
  if (bus.endTransmission(false) != 0) return -1;
  if (bus.requestFrom(addr, (uint8_t)1) != 1) return -1;
  return bus.read();
}

void describeOledStatus(int status, char *buf, size_t len) {
  if (status < 0) {
    snprintf(buf, len, "no response");
  } else {
    snprintf(buf, len, "0x%02X (display %s%s)", status,
             (status & 0x40) ? "off" : "on",
             (status & 0x80) ? ", busy" : "");
  }
}

// Timestamp (ms since boot) at the head of the diagnostic lines, to see
// where time goes at startup.
void logTs() { Serial.printf("[%6lu] ", (unsigned long)millis()); }

const char *wireErrorName(uint8_t err) {
  switch (err) {
    case 0: return "OK";
    case 2: return "NACK on address";
    case 3: return "NACK on data";
    case 5: return "timeout";
    default: return "error";
  }
}

// Sends the NOP command (0xE3, SSD1306 as well as SH1106): checks that
// the OLED still accepts commands. Returns the Wire error code.
uint8_t i2cOledNop(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0xE3);
  return Wire.endTransmission();
}

struct KnownI2cDevice {
  uint8_t addr;
  const char *what;
};
// What the schematics put on each bus, depending on the revision
const KnownI2cDevice kWireDevices[] = {
    {0x1C, "QMC6310U"}, {0x3C, "OLED or QMC6310N"}, {0x3D, "OLED"},
    {0x76, "BME280"},   {0x77, "BME280"},           {0x7C, "QMC6309"},
};
const KnownI2cDevice kWire1Devices[] = {{0x34, "AXP2101"}, {0x51, "PCF8563"}};

// Inventory of the known addresses on a bus (diagnostics)
void logI2cScan(const char *label, TwoWire &bus, const KnownI2cDevice *devices,
                size_t count) {
  logTs();
  Serial.printf("I2C %s:", label);
  bool any = false;
  for (size_t i = 0; i < count; i++) {
    if (!i2cAck(bus, devices[i].addr)) continue;
    Serial.printf(" 0x%02X (%s)", devices[i].addr, devices[i].what);
    any = true;
  }
  Serial.println(any ? "" : " no known device");
}

// Actual duration of a few elementary operations, to tell apart a starved
// task, a wrong clock, a slow I2C bus and a blocking serial port.
void logTimingProbes() {
  uint32_t t0 = micros();
  delay(100);
  uint32_t dDelay = micros() - t0;
  t0 = micros();
  volatile uint32_t acc = 0;
  for (uint32_t i = 0; i < 1000000; i++) acc += i;
  uint32_t dLoop = micros() - t0;
  t0 = micros();
  Serial.println("Timing: reference line of eighty characters, used to time one serial write");
  uint32_t dSerial = micros() - t0;
  t0 = micros();
  i2cAck(Wire, 0x08);  // free address: NACK expected
  uint32_t dNack = micros() - t0;
  t0 = micros();
  i2cAck(Wire1, kPmuI2cAddress);  // the PMU: ACK expected
  uint32_t dAck = micros() - t0;
  logTs();
  Serial.printf("Timing: delay(100) %lu us, 1M loop %lu us, serial line %lu us, "
                "I2C NACK %lu us, I2C ACK %lu us\n",
                (unsigned long)dDelay, (unsigned long)dLoop, (unsigned long)dSerial,
                (unsigned long)dNack, (unsigned long)dAck);
}

void logRail(XPowersLibInterface &pmu, const char *name, uint8_t channel,
             const char *role) {
  if (pmu.isPowerChannelEnable(channel)) {
    Serial.printf(" | %s ON %u mV%s", name,
                  (unsigned)pmu.getPowerChannelVoltage(channel), role);
  } else {
    Serial.printf(" | %s off%s", name, role);
  }
}

class TBeamSupremeBoard : public Board {
public:
  const char *name() const override { return "LilyGo T-Beam Supreme"; }

  void initPower() override {
    if (!_pmu.init(Wire1, kPinPmuSda, kPinPmuScl, kPmuI2cAddress)) {
      _selfCheckError = "PMU AXP2101 absent";
      Wire.begin(kPinOledSda, kPinOledScl);  // to try to show the error
      return;  // without the PMU nothing is powered: the app stops here
    }
    logTs();
    Serial.println("PMU AXP2101 found");
    // Rails are driven through the generic interface (XPOWERS_* channels),
    // the only public path XPowersLib offers for these operations.
    XPowersLibInterface &pmu = _pmu;

    // Useful rails: ALDO3 powers the SX1262; ALDO1 the OLED (along with
    // its bus pull-ups and the BME280), ALDO2 the magnetometer - MeshCore
    // and LilyGo both turn the two on for the display, so do we. Both
    // LDOs are switched off then back on at cold boot (LilyGo recipe,
    // adopted by MeshCore) to start from a reset OLED.
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
      pmu.disablePowerOutput(XPOWERS_ALDO1);
      pmu.disablePowerOutput(XPOWERS_ALDO2);
      delay(250);
    }
    pmu.setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
    pmu.enablePowerOutput(XPOWERS_ALDO3);
    pmu.setPowerChannelVoltage(XPOWERS_ALDO1, 3300);
    pmu.enablePowerOutput(XPOWERS_ALDO1);
    pmu.setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
    pmu.enablePowerOutput(XPOWERS_ALDO2);

    // Rails useless here, cut off for battery life: GNSS (ALDO4), SD card
    // (BLDO1), expansion and M.2 connectors (BLDO2, DCDC3..5 - nothing
    // else is wired to them according to the schematics), RTC backup
    // (VBACKUP). DCDC1 powers the ESP32: never touch it.
    pmu.disablePowerOutput(XPOWERS_ALDO4);
    pmu.disablePowerOutput(XPOWERS_BLDO1);
    pmu.disablePowerOutput(XPOWERS_BLDO2);
    pmu.disablePowerOutput(XPOWERS_DCDC2);
    pmu.disablePowerOutput(XPOWERS_DCDC3);
    pmu.disablePowerOutput(XPOWERS_DCDC4);
    pmu.disablePowerOutput(XPOWERS_DCDC5);
    pmu.disablePowerOutput(XPOWERS_DLDO1);
    pmu.disablePowerOutput(XPOWERS_DLDO2);
    pmu.disablePowerOutput(XPOWERS_VBACKUP);

    // 18650 battery charging: same settings as MeshCore. The temperature
    // sensor (TS) is not wired: without disabling it, the PMU would refuse
    // to charge.
    _pmu.setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);
    _pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
    _pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    _pmu.disableTSPinMeasure();
    _pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    _pmu.clearIrqStatus();
    // PMU PWR button: 4 s press to power the device off.
    _pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);

    delay(150);  // let the rails settle before the OLED init

    // Actual rail state (read back from the PMU), for diagnostics
    logTs();
    Serial.printf("PMU AXP2101 (ID 0x%02X):", _pmu.getChipID());
    logRail(pmu, "DCDC1", XPOWERS_DCDC1, " (ESP32)");
    logRail(pmu, "ALDO1", XPOWERS_ALDO1, " (OLED)");
    logRail(pmu, "ALDO2", XPOWERS_ALDO2, " (sensors)");
    logRail(pmu, "ALDO3", XPOWERS_ALDO3, " (LoRa)");
    logRail(pmu, "ALDO4", XPOWERS_ALDO4, " (GNSS)");
    logRail(pmu, "BLDO1", XPOWERS_BLDO1, " (SD)");
    logRail(pmu, "BLDO2", XPOWERS_BLDO2, "");
    logRail(pmu, "DCDC3", XPOWERS_DCDC3, "");
    logRail(pmu, "DCDC4", XPOWERS_DCDC4, "");
    logRail(pmu, "DCDC5", XPOWERS_DCDC5, "");
    Serial.println();
    logTs();
    Serial.printf("PMU: VBUS %s, battery %u mV\n",
                  _pmu.isVbusIn() ? "yes" : "no",
                  (unsigned)_pmu.getBattVoltage());

    // OLED bus, started only now: its pull-ups sit on ALDO1. A slave left
    // stuck mid-transaction (SDA held low - the sensors share the bus) is
    // freed by hand first.
    recoverI2cBus(kPinOledSda, kPinOledScl);
    Wire.begin(kPinOledSda, kPinOledScl);
  }

  const char *selfCheckError() const override { return _selfCheckError; }

  Display &display() override { return _display; }

  void beginDisplay() override {
    logTimingProbes();
    logI2cScan("Wire (OLED, sensors)", Wire, kWireDevices,
               sizeof(kWireDevices) / sizeof(kWireDevices[0]));
    logI2cScan("Wire1 (PMU)", Wire1, kWire1Devices,
               sizeof(kWire1Devices) / sizeof(kWire1Devices[0]));

    // 0x3D comes first: on boards where the OLED sits there, the
    // magnetometer answers at 0x3C and would have us initialize the
    // display at the wrong address. The OLED has just been powered up, so
    // give it a window to answer before falling back to 0x3C.
    uint8_t addr = kOledAddrPrimary;
    bool ackPrimary = false, ackAlternate = false;
    uint32_t deadline = millis() + 1000;
    while (millis() < deadline) {
      if (i2cAck(Wire, kOledAddrPrimary)) {
        ackPrimary = true;
        break;
      }
      ackAlternate = ackAlternate || i2cAck(Wire, kOledAddrAlternate);
      delay(20);
    }
    if (!ackPrimary && ackAlternate) {
      addr = kOledAddrAlternate;
    }
    logTs();
    Serial.printf("OLED: %s -> address 0x%02X\n",
                  ackPrimary ? "0x3D responds"
                             : ackAlternate ? "0x3C responds" : "no I2C response",
                  addr);

    // State before init, then check that this read did not disturb the
    // OLED (NOP probe); free the bus if needed.
    char status[48];
    describeOledStatus(i2cReadByte0(Wire, addr), status, sizeof(status));
    uint8_t err = i2cOledNop(addr);
    logTs();
    Serial.printf("OLED: status before init %s, NOP probe %s\n", status,
                  wireErrorName(err));
    if (err != 0) {
      restartOledBus();
    }

    _u8g2.setI2CAddress(addr << 1);  // U8g2 expects the 8-bit address
    if (!initOled(addr, 400000)) {
      // Bus stuck or display still off: free the bus and start over at
      // the standard speed, which every slave supports.
      logTs();
      Serial.println("OLED: retrying at 100 kHz after bus recovery");
      restartOledBus();
      initOled(addr, 100000);
    }

    // Cost of a full frame (and last bus state before the app)
    uint32_t t0 = millis();
    _display.clear();
    _display.send();
    uint32_t frameMs = millis() - t0;
    err = i2cOledNop(addr);
    logTs();
    Serial.printf("OLED: blank frame sent in %lu ms at %lu kHz, NOP probe %s\n",
                  (unsigned long)frameMs, (unsigned long)(_busHz / 1000),
                  wireErrorName(err));
  }

  // Restarts the OLED bus from scratch: Wire.begin() is a no-op once the
  // bus is running, so the peripheral is torn down first, the lines are
  // freed by hand, then the bus is configured again.
  void restartOledBus() {
    Wire.end();
    recoverI2cBus(kPinOledSda, kPinOledScl);
    Wire.begin(kPinOledSda, kPinOledScl);
  }

  // Initializes the OLED at the given bus speed. True if the OLED still
  // accepts commands afterwards and reports itself as on.
  bool initOled(uint8_t addr, uint32_t busHz) {
    uint32_t t0 = millis();
    _busHz = busHz;
    _u8g2.setBusClock(busHz);
    _u8g2.begin();

    // The SH1106 generates the panel high voltage itself (internal
    // charge pump, C1/C2 capacitors on the display flex). U8g2's SH1106
    // init sequence, inherited from the SSD1306, does not drive that
    // converter and relies on its post-reset state, which is not enough
    // for every SH1106-compatible controller: enable it explicitly,
    // display off as the SH1106 requires, and take over the settings of
    // the Adafruit SH110X library that MeshCore uses on this board:
    // pump at 9 V, maximum contrast, and pre-charge period 0x1F - on the
    // SH1106 the two nibbles of that command are swapped compared to the
    // SSD1306, where U8g2's 0xF1 means a minimum pre-charge.
    _u8g2.setPowerSave(1);
    _u8g2.sendF("cacac", 0xAD, 0x8B,  // DC-DC ON
                0xD9, 0x1F,           // pre-charge 15 DCLK, discharge 1
                0x33);                // VPP 9 V
    _u8g2.setContrast(0xFF);
    _u8g2.setPowerSave(0);
    uint32_t initMs = millis() - t0;

    uint8_t err = i2cOledNop(addr);
    int status = -1;
    char statusText[48];
    if (err == 0) {
      status = i2cReadByte0(Wire, addr);
      describeOledStatus(status, statusText, sizeof(statusText));
    } else {
      snprintf(statusText, sizeof(statusText), "not read");
    }
    logTs();
    Serial.printf("OLED: init at %lu kHz took %lu ms, NOP probe %s, status %s\n",
                  (unsigned long)(busHz / 1000), (unsigned long)initMs,
                  wireErrorName(err), statusText);
    return err == 0 && status >= 0 && (status & 0x40) == 0;
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
    t.dio2AsRfSwitch = true;  // DIO2 drives the antenna switch
    t.tcxoVoltage = 1.8f;     // TCXO on DIO3
    t.currentLimitmA = 140;
    return t;
  }

  int8_t txPowerMaxDbm() const override { return 22; }

  const ButtonSpec *buttons(size_t &count) const override {
    count = sizeof(kButtons) / sizeof(kButtons[0]);
    return kButtons;
  }

private:
  const char *_selfCheckError = nullptr;
  uint32_t _busHz = 0;
  XPowersAXP2101 _pmu;
  // No pin numbers here, on purpose: Wire is started in initPower() (the
  // PMU sequencing and the probes need it before the display exists), and
  // when U8g2 is given the I2C pins its Arduino GPIO init calls
  // pinMode(OUTPUT) on them, which on the ESP32 detaches them from the I2C
  // peripheral (plain GPIO outputs, driven low): the bus is dead from then
  // on, and the Wire.begin() U8g2 does next is a no-op on a running bus.
  // With U8X8_PIN_NONE, U8g2 leaves the pins alone and reuses the bus.
  U8G2_SH1106_128X64_NONAME_F_HW_I2C _u8g2{U8G2_R0, U8X8_PIN_NONE};
  U8g2Display _display{_u8g2};
};

}  // namespace

Board &board() {
  static TBeamSupremeBoard instance;
  return instance;
}
#endif  // BOARD_TBEAM_SUPREME
