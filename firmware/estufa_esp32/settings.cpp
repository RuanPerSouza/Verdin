#include "settings.h"

#include <Preferences.h>

static const char *NVS_NAMESPACE = "estufa";
static const char *NVS_KEY       = "settings";

Settings settingsLoad() {
  Settings s;  // já nasce com os valores padrão
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    Serial.println(F("[CFG] NVS indisponivel, usando padroes"));
    return s;
  }

  Settings stored;
  size_t len = prefs.getBytes(NVS_KEY, &stored, sizeof(stored));
  prefs.end();

  String err;
  if (len == sizeof(Settings) && stored.version == SETTINGS_VERSION &&
      settingsValidate(stored, err)) {
    Serial.println(F("[CFG] Configuracao carregada da flash"));
    return stored;
  }

  Serial.println(F("[CFG] Sem configuracao valida salva, usando padroes"));
  return s;
}

bool settingsSave(const Settings &s) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) return false;
  size_t written = prefs.putBytes(NVS_KEY, &s, sizeof(s));
  prefs.end();
  return written == sizeof(s);
}

bool settingsValidate(const Settings &s, String &error) {
  if (s.soilMinPct >= s.soilTargetPct) {
    error = "soilMinPct deve ser menor que soilTargetPct";
    return false;
  }
  if (s.soilTargetPct > 95 || s.soilMinPct < 5) {
    error = "limites de solo devem ficar entre 5% e 95%";
    return false;
  }
  if (s.pulseS < 1 || s.pulseS > PUMP_USER_MAX_ON_S) {
    error = "pulseS deve ficar entre 1 e " + String(PUMP_USER_MAX_ON_S);
    return false;
  }
  if (s.soakS < 5 || s.soakS > 600) {
    error = "soakS deve ficar entre 5 e 600";
    return false;
  }
  if (s.maxPulses < 1 || s.maxPulses > 20) {
    error = "maxPulses deve ficar entre 1 e 20";
    return false;
  }
  if (s.cooldownMin < 1 || s.cooldownMin > 1440) {
    error = "cooldownMin deve ficar entre 1 e 1440";
    return false;
  }
  if (s.maxManualS < 1 || s.maxManualS > PUMP_USER_MAX_ON_S) {
    error = "maxManualS deve ficar entre 1 e " + String(PUMP_USER_MAX_ON_S);
    return false;
  }
  if (s.reservoirMinElec < 1 || s.reservoirMinElec > LEVEL_ELECTRODE_COUNT) {
    error = "reservoirMinElec deve ficar entre 1 e " + String(LEVEL_ELECTRODE_COUNT);
    return false;
  }
  if (s.lightLowPct >= s.lightHighPct || s.lightHighPct > 100) {
    error = "lightLowPct deve ser menor que lightHighPct (max 100)";
    return false;
  }
  if (s.tempMinC >= s.tempMaxC) {
    error = "tempMinC deve ser menor que tempMaxC";
    return false;
  }
  if (s.airHumMinPct >= s.airHumMaxPct || s.airHumMaxPct > 100) {
    error = "airHumMinPct deve ser menor que airHumMaxPct (max 100)";
    return false;
  }
  {
    uint16_t lo = s.soilDryMv < s.soilWetMv ? s.soilDryMv : s.soilWetMv;
    uint16_t hi = s.soilDryMv < s.soilWetMv ? s.soilWetMv : s.soilDryMv;
    if (hi < lo + 200) {
      error = "seco e molhado do solo precisam ter ao menos 200 mV de diferenca";
      return false;
    }
  }
  if (s.lightBrightMv == s.lightDarkMv) {
    error = "lightBrightMv e lightDarkMv nao podem ser iguais";
    return false;
  }
  if (s.electrodeThreshMv < 50 || s.electrodeThreshMv > 3000) {
    error = "electrodeThreshMv deve ficar entre 50 e 3000";
    return false;
  }
  if (s.pumpMode > PumpMode::Off) {
    error = "pumpMode invalido";
    return false;
  }
  return true;
}
