#ifdef BOARD_TBEAM_SUPREME
// =====================================================================
// LilyGo T-Beam Supreme (« T-Beam S3 Supreme »), déclinaison SX1262
// 868 MHz — ESP32-S3 + SX1262, OLED SH1106 128x64 (I2C), un seul bouton
// utilisateur (GPIO0, au centre). Les déclinaisons LR1121 / 2,4 GHz ne
// sont pas gérées.
//
// Particularité : tous les rails périphériques sont commutés par un PMU
// AXP2101 (I2C, bus Wire1 sur GPIO42/41) — sans lui, ni la radio ni
// l'écran ne sont alimentés. Brochage et affectation des rails repris du
// firmware MeshCore (helpers/esp32/TBeamBoard.*) et des exemples LilyGo
// (LilyGo-LoRa-Series, utilities.h / LoRaBoards.cpp), vérifiés sur les
// schémas LilyGo (T-Beam-S3-Core, T-Beam Supreme V3.0 et V3.1).
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

// Bus I2C principal (Wire) : OLED, magnétomètre et BME280 (seul l'OLED
// nous sert). Ses pull-ups sont sur ALDO1, comme l'OLED.
constexpr uint8_t kPinOledSda = 17;
constexpr uint8_t kPinOledScl = 18;
// Bus I2C secondaire (Wire1) : PMU AXP2101 (+ RTC PCF8563, inutilisée)
constexpr uint8_t kPinPmuSda = 42;
constexpr uint8_t kPinPmuScl = 41;
constexpr uint8_t kPmuI2cAddress = 0x34;
constexpr uint8_t kPinButtonUser = 0;  // relié à la masse quand pressé

// Adresses I2C possibles de l'OLED. Elle change avec la révision de la
// carte, selon le magnétomètre qui partage le bus (doc LilyGo
// t_beam_supreme_hw.md) : QMC6310N en 0x3C -> OLED en 0x3D ; QMC6310U
// (0x1C, V3.0) ou QMC6309 (0x7C, V3.1) -> OLED en 0x3C.
constexpr uint8_t kOledAddrPrimary = 0x3D;
constexpr uint8_t kOledAddrAlternate = 0x3C;

const ButtonSpec kButtons[] = {
    {Key::Ok, kPinButtonUser, /*activeLow=*/true, /*internalPullup=*/true},
};

// Libère un bus I2C dont un esclave maintient SDA bas (transaction
// interrompue par un reset) : on cadence SCL jusqu'à ce qu'il lâche la
// ligne, puis on émet un STOP. À faire avant que Wire ne prenne les broches.
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
  pinMode(sda, OUTPUT_OPEN_DRAIN);  // STOP : SDA bas -> haut, SCL haut
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

// Lit un octet après avoir écrit 0x00 : sur un OLED SH1106/SSD1306 c'est
// l'octet d'état (bit 6 = écran éteint, bit 7 = occupé), sur un QMC6310
// son identifiant (0x80). Renvoie -1 sans réponse. Diagnostic seulement.
int i2cReadByte0(TwoWire &bus, uint8_t addr) {
  bus.beginTransmission(addr);
  bus.write((uint8_t)0x00);
  if (bus.endTransmission(false) != 0) return -1;
  if (bus.requestFrom(addr, (uint8_t)1) != 1) return -1;
  return bus.read();
}

void describeOledStatus(int status, char *buf, size_t len) {
  if (status < 0) {
    snprintf(buf, len, "sans réponse");
  } else {
    snprintf(buf, len, "0x%02X (écran %s%s)", status,
             (status & 0x40) ? "éteint" : "allumé",
             (status & 0x80) ? ", occupé" : "");
  }
}

// Inventaire d'un bus I2C sur le port série (diagnostic)
void logI2cScan(const char *label, TwoWire &bus) {
  Serial.printf("I2C %s :", label);
  bool any = false;
  for (uint8_t addr = 0x08; addr < 0x80; addr++) {
    if (!i2cAck(bus, addr)) continue;
    Serial.printf(" 0x%02X", addr);
    any = true;
  }
  Serial.println(any ? "" : " aucun périphérique");
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
      Wire.begin(kPinOledSda, kPinOledScl);  // pour tenter d'afficher l'erreur
      return;  // sans PMU, rien n'est alimenté : l'appli s'arrêtera là
    }
    // Les rails se pilotent via l'interface générique (canaux XPOWERS_*),
    // seule voie publique de XPowersLib pour ces opérations.
    XPowersLibInterface &pmu = _pmu;

    // Rails utiles : ALDO3 alimente le SX1262 ; ALDO1 l'OLED (avec les
    // pull-ups de son bus et le BME280), ALDO2 le magnétomètre — MeshCore
    // comme LilyGo allument les deux pour l'écran, on fait de même. Les
    // deux LDO sont coupés puis rallumés au démarrage à froid (recette
    // LilyGo, reprise par MeshCore) pour repartir d'un OLED réinitialisé.
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

    // Rails inutiles ici, coupés pour l'autonomie : GNSS (ALDO4), carte
    // SD (BLDO1), connecteurs d'extension et M.2 (BLDO2, DCDC3..5 — rien
    // d'autre n'y est raccordé d'après les schémas), sauvegarde RTC
    // (VBACKUP). DCDC1 alimente l'ESP32 : ne jamais y toucher.
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

    // Charge de la batterie 18650 : mêmes réglages que MeshCore. Le
    // capteur de température (TS) n'est pas câblé : sans le désactiver,
    // le PMU refuserait de charger.
    _pmu.setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);
    _pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
    _pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    _pmu.disableTSPinMeasure();
    _pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    _pmu.clearIrqStatus();
    // Bouton PWR du PMU : appui de 4 s pour éteindre l'appareil.
    _pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);

    delay(150);  // stabilisation des rails avant l'init de l'OLED

    // État réel des rails (relu dans le PMU), pour le diagnostic
    Serial.printf("PMU AXP2101 (ID 0x%02X) :", _pmu.getChipID());
    logRail(pmu, "DCDC1", XPOWERS_DCDC1, " (ESP32)");
    logRail(pmu, "ALDO1", XPOWERS_ALDO1, " (OLED)");
    logRail(pmu, "ALDO2", XPOWERS_ALDO2, " (capteurs)");
    logRail(pmu, "ALDO3", XPOWERS_ALDO3, " (LoRa)");
    logRail(pmu, "ALDO4", XPOWERS_ALDO4, " (GNSS)");
    logRail(pmu, "BLDO1", XPOWERS_BLDO1, " (SD)");
    logRail(pmu, "BLDO2", XPOWERS_BLDO2, "");
    logRail(pmu, "DCDC3", XPOWERS_DCDC3, "");
    logRail(pmu, "DCDC4", XPOWERS_DCDC4, "");
    logRail(pmu, "DCDC5", XPOWERS_DCDC5, "");
    Serial.printf("\nPMU : VBUS %s, batterie %u mV\n",
                  _pmu.isVbusIn() ? "oui" : "non",
                  (unsigned)_pmu.getBattVoltage());

    // Bus de l'OLED, démarré seulement maintenant : ses pull-ups sont sur
    // ALDO1. Un esclave resté bloqué mi-transaction (SDA maintenu bas —
    // les capteurs partagent le bus) est d'abord libéré à la main.
    recoverI2cBus(kPinOledSda, kPinOledScl);
    Wire.begin(kPinOledSda, kPinOledScl);
  }

  const char *selfCheckError() const override { return _selfCheckError; }

  Display &display() override { return _display; }

  void beginDisplay() override {
    // Inventaire des deux bus (diagnostic : révision de la carte, adresses
    // de l'OLED et du magnétomètre, présence des capteurs)
    logI2cScan("Wire (OLED, capteurs)", Wire);
    logI2cScan("Wire1 (PMU)", Wire1);

    // On privilégie 0x3D : sur les cartes où l'OLED y est, le
    // magnétomètre répond en 0x3C et ferait initialiser l'écran à la
    // mauvaise adresse. L'OLED vient d'être mis sous tension : on lui
    // laisse une fenêtre pour répondre avant de se rabattre sur 0x3C.
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
    char status[48];
    describeOledStatus(i2cReadByte0(Wire, addr), status, sizeof(status));
    Serial.printf("OLED : %s -> adresse 0x%02X, état avant init %s\n",
                  ackPrimary ? "0x3D répond"
                             : ackAlternate ? "0x3C répond" : "aucune réponse I2C",
                  addr, status);

    _u8g2.setI2CAddress(addr << 1);  // U8g2 attend l'adresse 8 bits
    _display.begin();

    // Le SH1106 fabrique lui-même la haute tension du panneau (pompe de
    // charge interne, condensateurs C1/C2 sur la nappe de l'écran). La
    // séquence d'init SH1106 de U8g2, héritée du SSD1306, ne pilote pas
    // ce convertisseur et s'en remet à son état de sortie de reset, ce
    // qui ne suffit pas à tous les contrôleurs compatibles SH1106 : on
    // l'active explicitement, écran éteint comme le demande le SH1106,
    // avec les réglages de la bibliothèque Adafruit SH110X qu'utilise
    // MeshCore sur cette carte (pompe à 9 V, contraste maximal).
    _u8g2.setPowerSave(1);
    _u8g2.sendF("cac", 0xAD, 0x8B, 0x33);  // DC-DC ON, VPP 9 V
    _u8g2.setContrast(0xFF);
    _u8g2.setPowerSave(0);

    describeOledStatus(i2cReadByte0(Wire, addr), status, sizeof(status));
    Serial.printf("OLED : état après init %s\n", status);
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
    t.dio2AsRfSwitch = true;  // DIO2 pilote le switch d'antenne
    t.tcxoVoltage = 1.8f;     // TCXO sur DIO3
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
  XPowersAXP2101 _pmu;
  U8G2_SH1106_128X64_NONAME_F_HW_I2C _u8g2{U8G2_R0, U8X8_PIN_NONE,
                                           kPinOledScl, kPinOledSda};
  U8g2Display _display{_u8g2};
};

}  // namespace

Board &board() {
  static TBeamSupremeBoard instance;
  return instance;
}
#endif  // BOARD_TBEAM_SUPREME
