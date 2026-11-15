#include "status.h"

struct AlertInfo {
  uint32_t    bit;
  const char *code;   // estável: o app usa para traduzir/ícones
  const char *text;   // cabe na largura do LCD (21 caracteres)
  bool        severe; // true = "alerta", false = "atencao"
};

// Ordem = prioridade de exibição no LCD
static const AlertInfo ALERTS[ALERT_COUNT] = {
  {ALERT_PUMP_FAULT,      "BOMBA_FALHA",        "Falha na irrigacao",  true},
  {ALERT_RESERVOIR_EMPTY, "RESERVATORIO_VAZIO", "Reservatorio vazio",  true},
  {ALERT_RESERVOIR_LOW,   "RESERVATORIO_BAIXO", "Agua baixa",          true},
  {ALERT_SOIL_SENSOR,     "SENSOR_SOLO",        "Erro sensor solo",    true},
  {ALERT_LEVEL_SENSOR,    "SENSOR_NIVEL",       "Erro sensor nivel",   true},
  {ALERT_AIR_SENSOR,      "SENSOR_AR",          "Erro sensor DHT11",   false},
  {ALERT_LIGHT_SENSOR,    "SENSOR_LUZ",         "Erro sensor luz",     false},
  {ALERT_SOIL_DRY,        "SOLO_SECO",          "Solo seco",           false},
  {ALERT_TEMP_HIGH,       "TEMPERATURA_ALTA",   "Temperatura alta",    false},
  {ALERT_TEMP_LOW,        "TEMPERATURA_BAIXA",  "Temperatura baixa",   false},
  {ALERT_AIR_HUM_HIGH,    "UMIDADE_AR_ALTA",    "Umidade do ar alta",  false},
  {ALERT_AIR_HUM_LOW,     "UMIDADE_AR_BAIXA",   "Umidade do ar baixa", false},
  {ALERT_LIGHT_LOW,       "LUZ_BAIXA",          "Pouca luz",           false},
  {ALERT_LIGHT_HIGH,      "LUZ_ALTA",           "Luz muito forte",     false},
};

uint32_t computeAlerts(const SensorData &d, const IrrigationStatus &irr,
                       const Settings &s) {
  uint32_t a = 0;

  if (irr.state == IrrigationState::Fault) a |= ALERT_PUMP_FAULT;

  if (!d.reservoir.valid) {
    a |= ALERT_LEVEL_SENSOR;
  } else if (d.reservoir.level == ReservoirLevel::Empty) {
    a |= ALERT_RESERVOIR_EMPTY;
  } else if (d.reservoir.level == ReservoirLevel::Low) {
    a |= ALERT_RESERVOIR_LOW;
  }

  if (!d.soil.valid) {
    a |= ALERT_SOIL_SENSOR;
  } else if (d.soil.percent < s.soilMinPct) {
    a |= ALERT_SOIL_DRY;
  }

  if (!d.air.valid) {
    a |= ALERT_AIR_SENSOR;
  } else {
    if (d.air.temperature > s.tempMaxC)   a |= ALERT_TEMP_HIGH;
    if (d.air.temperature < s.tempMinC)   a |= ALERT_TEMP_LOW;
    if (d.air.humidity > s.airHumMaxPct)  a |= ALERT_AIR_HUM_HIGH;
    if (d.air.humidity < s.airHumMinPct)  a |= ALERT_AIR_HUM_LOW;
  }

  if (!d.light.valid) {
    a |= ALERT_LIGHT_SENSOR;
  } else if (d.light.level == LightLevel::Low) {
    a |= ALERT_LIGHT_LOW;
  } else if (d.light.level == LightLevel::High) {
    a |= ALERT_LIGHT_HIGH;
  }

  return a;
}

OverallStatus overallFromAlerts(uint32_t alerts) {
  OverallStatus o = OverallStatus::Ok;
  for (const AlertInfo &info : ALERTS) {
    if (!(alerts & info.bit)) continue;
    if (info.severe) return OverallStatus::Alert;
    o = OverallStatus::Warning;
  }
  return o;
}

const char *overallName(OverallStatus o) {
  switch (o) {
    case OverallStatus::Ok:      return "ok";
    case OverallStatus::Warning: return "atencao";
    case OverallStatus::Alert:   return "alerta";
  }
  return "?";
}

uint32_t alertBit(int i)          { return ALERTS[i].bit; }
const char *alertCode(int i)      { return ALERTS[i].code; }
const char *alertShortText(int i) { return ALERTS[i].text; }
