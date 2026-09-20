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
// (LilyGo-LoRa-Series, utilities.h / LoRaBoards.cpp).
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

// Bus I2C principal (Wire) : OLED (+ BME280 et magnétomètre, inutilisés)
constexpr uint8_t kPinOledSda = 17;
constexpr uint8_t kPinOledScl = 18;
// Bus I2C secondaire (Wire1) : PMU AXP2101 (+ RTC PCF8563, inutilisée)
constexpr uint8_t kPinPmuSda = 42;
constexpr uint8_t kPinPmuScl = 41;
constexpr uint8_t kPmuI2cAddress = 0x34;
constexpr uint8_t kPinButtonUser = 0;  // relié à la masse quand pressé

// Adresse I2C de l'OLED selon la variante radio (schéma LilyGo V3.1)
constexpr uint8_t kOledAddrUhf = 0x3D;  // SX1262 868/915 MHz
constexpr uint8_t kOledAddrVhf = 0x3C;

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

bool i2cAck(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
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

    // Rails utiles : ALDO3 alimente le SX1262 ; ALDO1 l'OLED (et le
    // BME280), ALDO2 le magnétomètre — MeshCore comme LilyGo allument les
    // deux pour l'écran, on fait de même. Les deux LDO de l'écran sont
    // coupés puis rallumés au démarrage à froid (recette LilyGo) pour
    // repartir d'un OLED réinitialisé.
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
    // SD (BLDO1), connecteurs d'extension et M.2 (BLDO2, DCDC3..5),
    // sauvegarde RTC (VBACKUP). DCDC1 alimente l'ESP32 : ne jamais y
    // toucher.
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

    Serial.printf("PMU AXP2101 : ALDO1 %u mV (OLED), ALDO3 %u mV (LoRa), "
                  "VBUS %s, batterie %u mV\n",
                  (unsigned)pmu.getPowerChannelVoltage(XPOWERS_ALDO1),
                  (unsigned)pmu.getPowerChannelVoltage(XPOWERS_ALDO3),
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
    // L'adresse de l'OLED dépend de la variante radio de la carte
    // (schéma LilyGo V3.1, strap R51/R52) : 0x3D sur la version UHF —
    // la nôtre, SX1262 —, 0x3C sur la version VHF. On privilégie 0x3D :
    // le magnétomètre partage le bus et certaines références répondent
    // en 0x3C, ce qui ferait initialiser l'écran à la mauvaise adresse.
    // L'OLED vient d'être mis sous tension : on lui laisse une fenêtre
    // pour répondre avant de se rabattre.
    uint8_t addr = kOledAddrUhf;
    bool ackUhf = false, ackVhf = false;
    uint32_t deadline = millis() + 1000;
    while (millis() < deadline) {
      if (i2cAck(kOledAddrUhf)) {
        ackUhf = true;
        break;
      }
      ackVhf = ackVhf || i2cAck(kOledAddrVhf);
      delay(20);
    }
    if (!ackUhf && ackVhf) {
      addr = kOledAddrVhf;
    }
    Serial.printf("OLED : %s -> adresse 0x%02X\n",
                  ackUhf ? "0x3D répond" : ackVhf ? "0x3C répond" : "aucune réponse I2C",
                  addr);
    _u8g2.setI2CAddress(addr << 1);  // U8g2 attend l'adresse 8 bits
    _display.begin();
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
