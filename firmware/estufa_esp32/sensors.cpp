#include "sensors.h"

#include <DHT.h>
#include <math.h>

// =============================================================================
//  Leitura analógica robusta
// -----------------------------------------------------------------------------
//  analogReadMilliVolts() já aplica a calibração de fábrica do ADC do ESP32
//  (que não é linear). Fazemos várias amostras, ordenamos, descartamos os
//  extremos e tiramos a média do meio ("média aparada"). A diferença entre as
//  amostras ("spread") denuncia fio solto: um pino flutuando lê valores
//  aleatórios.
// =============================================================================
struct AdcResult {
  uint16_t mv;
  uint16_t spread;
};

static AdcResult readAdcMv(uint8_t pin) {
  // A primeira conversão depois de trocar de pino ainda é do canal anterior.
  // Em eletrodo (100 kΩ) e no AO do solo, sem essa pausa o pino lê ~140 mV
  // mesmo com o sensor ligado.
  analogSetPinAttenuation(pin, ADC_11db);
  (void)analogReadMilliVolts(pin);
  delayMicroseconds(400);

  uint16_t s[ADC_SAMPLES];
  for (int i = 0; i < ADC_SAMPLES; i++) {
    s[i] = analogReadMilliVolts(pin);
    delayMicroseconds(250);
  }

  // Ordenação por inserção (rápida para 16 elementos)
  for (int i = 1; i < ADC_SAMPLES; i++) {
    uint16_t key = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > key) { s[j + 1] = s[j]; j--; }
    s[j + 1] = key;
  }

  const int q = ADC_SAMPLES / 4;
  uint32_t sum = 0;
  for (int i = q; i < ADC_SAMPLES - q; i++) sum += s[i];

  const int e = ADC_SAMPLES / 8;  // ignora 1 ou 2 valores extremos de cada lado
  return { (uint16_t)(sum / (ADC_SAMPLES - 2 * q)),
           (uint16_t)(s[ADC_SAMPLES - 1 - e] - s[e]) };
}

static float mapToPercent(float mv, float zeroMv, float fullMv) {
  if (fullMv == zeroMv) return 0;
  float p = (mv - zeroMv) * 100.0f / (fullMv - zeroMv);
  return constrain(p, 0.0f, 100.0f);
}

// =============================================================================
//  DHT11 - temperatura e umidade do ar
// =============================================================================
static DHT dht(PIN_DHT, DHT11);
static uint32_t lastDht = 0;
static uint8_t  dhtFailures = 0;

static void readDht(AirReading &air) {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // Faixa de medição do DHT11: 0-50 °C e 20-90 %UR (com folga)
  bool ok = !isnan(h) && !isnan(t) && t >= -5 && t <= 60 && h >= 5 && h <= 100;
  if (ok) {
    air.temperature = t;
    air.humidity    = h;
    air.valid       = true;
    dhtFailures     = 0;
  } else if (++dhtFailures >= DHT_MAX_FAILURES) {
    // Mantém o último valor, mas avisa que ele não é confiável
    air.valid = false;
    dhtFailures = DHT_MAX_FAILURES;
  }
}

// =============================================================================
//  Umidade do solo (resistivo + LM393)
// -----------------------------------------------------------------------------
//  O sensor só recebe energia durante a leitura. Corrente contínua passando
//  pelo garfo o tempo todo causa eletrólise e corrói as trilhas em semanas.
//  Leitura em 2 fases, sem delay(): liga -> espera estabilizar -> lê -> desliga
// =============================================================================
static uint32_t lastSoil = 0;
static uint32_t soilPoweredAt = 0;
static bool     soilPowered = false;

static void computeSoil(const Settings &s, SoilReading &soil) {
  // Divisor com 10k no GND: seco ≈ 0 V, molhado sobe em direção aos 3,3 V.
  soil.percent = mapToPercent(soil.mv, s.soilDryMv, s.soilWetMv);
}

// O AO do módulo de solo é lento. A leitura rápida devolve o patamar de
// pino aberto (~140 mV) e a calibração antiga virava isso em 100 %.
static AdcResult readSlowMv(uint8_t pin) {
  analogSetPinAttenuation(pin, ADC_11db);
  uint16_t s[8];
  for (int i = 0; i < 8; i++) {
    delay(5);
    s[i] = analogReadMilliVolts(pin);
  }
  for (int i = 1; i < 8; i++) {
    uint16_t key = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > key) { s[j + 1] = s[j]; j--; }
    s[j + 1] = key;
  }
  uint32_t sum = 0;
  for (int i = 2; i < 6; i++) sum += s[i];
  return { (uint16_t)(sum / 4), (uint16_t)(s[6] - s[1]) };
}

static void storeSoil(const Settings &s, SoilReading &soil, const AdcResult &r) {
  soil.mv = r.mv;
  // Leitura estável, mesmo em ~142 mV: com o 10k no GND o solo seco
  // fica nesse fundo e o ADC não desce mais. Oscilação grande é que é fio solto.
  soil.valid = r.mv >= SOIL_MIN_VALID_MV && r.spread <= ADC_MAX_SPREAD_MV;
  computeSoil(s, soil);
  if (!soil.valid) soil.percent = 0;
}

static void soilPhase(uint32_t now, const Settings &s, SoilReading &soil) {
  if (!soilPowered && now - lastSoil >= INTERVAL_SOIL_MS) {
    digitalWrite(PIN_SOIL_POWER, HIGH);
    soilPowered   = true;
    soilPoweredAt = now;
  } else if (soilPowered && now - soilPoweredAt >= SOIL_SETTLE_MS) {
    AdcResult r = readSlowMv(PIN_SOIL_ANALOG);
    digitalWrite(PIN_SOIL_POWER, LOW);
    soilPowered = false;
    lastSoil    = now;
    storeSoil(s, soil, r);
  }
}

// =============================================================================
//  Luminosidade (LDR)
// =============================================================================
static uint32_t lastLdr = 0;

static void computeLight(const Settings &s, LightReading &light) {
  // A fórmula funciona nos dois sentidos: se no módulo LM393 "escuro" tem
  // tensão maior que "claro", a calibração já inverte o resultado.
  light.percent = mapToPercent(light.mv, s.lightDarkMv, s.lightBrightMv);

#if LDR_IS_MODULE
  light.lux = -1;
#else
  // Divisor: 3,3 V -> LDR -> (pino) -> resistor fixo -> GND
  // Mais luz = LDR com menos resistência = tensão maior no pino.
  // ESTIMATIVA: LDR não é um luxímetro; serve para comparar ambientes.
  const float vcc = 3300.0f;
  if (light.mv < 10) {
    light.lux = 0;
  } else if (light.mv >= vcc - 10) {
    light.lux = 100000;
  } else {
    float rLdr = LDR_FIXED_RESISTOR * (vcc - light.mv) / light.mv;
    light.lux  = 10.0f * powf(LDR_R10_OHMS / rLdr, 1.0f / LDR_GAMMA);
    light.lux  = constrain(light.lux, 0.0f, 100000.0f);
  }
#endif

  if (light.percent < s.lightLowPct)       light.level = LightLevel::Low;
  else if (light.percent > s.lightHighPct) light.level = LightLevel::High;
  else                                     light.level = LightLevel::Adequate;
}

static void readLight(const Settings &s, LightReading &light) {
  // Descarta a amostra do canal anterior e espera o divisor carregar.
  analogSetPinAttenuation(PIN_LDR_ANALOG, ADC_11db);
  (void)analogReadMilliVolts(PIN_LDR_ANALOG);
  delay(LDR_SETTLE_MS);

  AdcResult r = readAdcMv(PIN_LDR_ANALOG);
  light.mv    = r.mv;
  light.valid = r.spread <= ADC_MAX_SPREAD_MV;
  computeLight(s, light);
}

// =============================================================================
//  Nível do reservatório (eletrodos)
// -----------------------------------------------------------------------------
//  O eletrodo COMUM (no fundo) recebe 3,3 V só durante a leitura. Cada eletrodo
//  de nível vai a um pino ADC com um resistor de 100k para o GND. Se a água
//  tocar o eletrodo, a corrente passa pela água e a tensão no pino SOBE.
// =============================================================================
static uint32_t lastLevel = 0;
static uint32_t levelExcitedAt = 0;
static bool     levelExcited = false;
static bool     levelFirstRead = true;
static bool     pendingWet[LEVEL_ELECTRODE_COUNT] = {};
static uint8_t  pendingCount = 0;

static void computeReservoir(ReservoirReading &r) {
  r.wetCount = 0;
  uint16_t levelMv = 0;
  for (int i = 0; i < LEVEL_ELECTRODE_COUNT; i++) {
    if (r.electrodes[i]) r.wetCount++;
    if (r.mv[i] > levelMv) levelMv = r.mv[i];
  }

  // A condução na água oscila. O medidor usa o maior valor recente,
  // senão um afundamento completo cai para ~50 % entre uma leitura e outra.
  static uint16_t recent[4] = {};
  static uint8_t recentAt = 0;
  recent[recentAt] = levelMv;
  recentAt = (recentAt + 1) % 4;
  uint16_t peak = 0;
  for (int i = 0; i < 4; i++) if (recent[i] > peak) peak = recent[i];

  // 140 mV (pino seco) = 0. Cerca de 700 mV, com o sensor afogado = 240.
  int32_t above = (int32_t)peak - (int32_t)RESERVOIR_EMPTY_MV;
  if (above < 0) above = 0;
  uint32_t span = (uint32_t)(RESERVOIR_FULL_MV - RESERVOIR_EMPTY_MV);
  uint32_t meter = span ? (uint32_t)above * RESERVOIR_METER_FULL / span : 0;
  if (meter > RESERVOIR_METER_FULL) meter = RESERVOIR_METER_FULL;
  r.meter   = (uint16_t)meter;
  r.percent = (uint8_t)(meter * 100UL / RESERVOIR_METER_FULL);

  if (r.percent < 10)      r.level = ReservoirLevel::Empty;
  else if (r.percent < 35) r.level = ReservoirLevel::Low;
  else if (r.percent < 85) r.level = ReservoirLevel::Medium;
  else                     r.level = ReservoirLevel::Full;
}

static void levelPhase(uint32_t now, const Settings &s, bool pumpOn,
                       ReservoirReading &res) {
  uint32_t interval = pumpOn ? INTERVAL_LEVEL_PUMPING_MS : INTERVAL_LEVEL_MS;

  if (!levelExcited && now - lastLevel >= interval) {
    digitalWrite(PIN_LEVEL_EXCITE, HIGH);
    levelExcited   = true;
    levelExcitedAt = now;
    return;
  }
  if (!levelExcited || now - levelExcitedAt < LEVEL_SETTLE_MS) return;

  // Canal ruidoso (água balançando) reaproveita a última amostra boa
  // em vez de invalidar o reservatório inteiro.
  static uint16_t heldMv[LEVEL_ELECTRODE_COUNT] = {};
  static bool     heldWet[LEVEL_ELECTRODE_COUNT] = {};
  static bool     heldOk[LEVEL_ELECTRODE_COUNT] = {};

  bool wet[LEVEL_ELECTRODE_COUNT];
  bool anyFresh = false;
  for (int i = 0; i < LEVEL_ELECTRODE_COUNT; i++) {
    AdcResult a = readAdcMv(PIN_LEVEL_ELECTRODES[i]);
    bool noisy = a.spread > ADC_MAX_SPREAD_MV;
    if (noisy && heldOk[i]) {
      res.mv[i] = heldMv[i];
      wet[i]    = heldWet[i];
    } else {
      res.mv[i] = a.mv;
      wet[i]    = a.mv >= s.electrodeThreshMv;
      if (!noisy) {
        heldMv[i]  = a.mv;
        heldWet[i] = wet[i];
        heldOk[i]  = true;
        anyFresh   = true;
      }
    }
  }
  digitalWrite(PIN_LEVEL_EXCITE, LOW);
  levelExcited = false;
  lastLevel    = now;

  if (!anyFresh) {
    bool anyHeld = false;
    for (int i = 0; i < LEVEL_ELECTRODE_COUNT; i++) anyHeld = anyHeld || heldOk[i];
    if (!anyHeld) {
      res.valid = false;
      return;
    }
  }

  // Debounce: com a bomba ligada a água balança. Só aceita a mudança depois
  // de LEVEL_DEBOUNCE_READS leituras iguais seguidas.
  bool same = memcmp(wet, pendingWet, sizeof(wet)) == 0;
  pendingCount = same ? pendingCount + 1 : 1;
  memcpy(pendingWet, wet, sizeof(wet));

  if (levelFirstRead || pendingCount >= LEVEL_DEBOUNCE_READS) {
    memcpy(res.electrodes, wet, sizeof(wet));
    levelFirstRead = false;
  }
  // A porcentagem segue a tensão agora, mesmo enquanto o debounce dos
  // eletrodos ainda não trocou. Assim o reservatório cheio não fica em 50 %.
  computeReservoir(res);
  res.valid = true;
}

// =============================================================================
//  API pública
// =============================================================================
void sensorsBegin(const Settings &s, SensorData &data) {
  analogReadResolution(12);

  pinMode(PIN_SOIL_POWER, OUTPUT);
  digitalWrite(PIN_SOIL_POWER, LOW);
  pinMode(PIN_LEVEL_EXCITE, OUTPUT);
  digitalWrite(PIN_LEVEL_EXCITE, LOW);
  // Pinos 34-39 são só entrada; não aceitam INPUT_PULLUP/PULLDOWN.
  pinMode(PIN_SOIL_ANALOG, INPUT);
  pinMode(PIN_LDR_ANALOG, INPUT);
  for (int i = 0; i < LEVEL_ELECTRODE_COUNT; i++) pinMode(PIN_LEVEL_ELECTRODES[i], INPUT);

  dht.begin();

  // Primeira leitura completa, para a irrigação já começar com dados reais.
  // Aqui pode bloquear: ainda estamos no setup().
  digitalWrite(PIN_SOIL_POWER, HIGH);
  delay(SOIL_SETTLE_MS);
  AdcResult soilAdc = readSlowMv(PIN_SOIL_ANALOG);
  digitalWrite(PIN_SOIL_POWER, LOW);
  storeSoil(s, data.soil, soilAdc);

  readLight(s, data.light);

  uint32_t t = millis();
  lastLevel = t - INTERVAL_LEVEL_MS;                  // força leitura imediata
  levelPhase(t, s, false, data.reservoir);            // energiza
  delay(LEVEL_SETTLE_MS + 1);
  levelPhase(t + LEVEL_SETTLE_MS + 1, s, false, data.reservoir);  // lê

  delay(1200);  // DHT11 precisa de ~1 s após ligar
  readDht(data.air);

  uint32_t now = millis();
  lastSoil = lastLdr = lastDht = lastLevel = now;
}

void sensorsUpdate(uint32_t now, const Settings &s, bool pumpOn, SensorData &data) {
  soilPhase(now, s, data.soil);
  levelPhase(now, s, pumpOn, data.reservoir);

  if (now - lastLdr >= INTERVAL_LDR_MS) {
    lastLdr = now;
    readLight(s, data.light);
  }

  // O DHT11 bloqueia ~25 ms. Evita ler no mesmo ciclo em que um sensor
  // energizado está esperando para não atrasar a leitura dele.
  if (now - lastDht >= INTERVAL_DHT_MS && !soilPowered && !levelExcited) {
    lastDht = now;
    readDht(data.air);
  }
}

void sensorsRecompute(const Settings &s, SensorData &data) {
  computeSoil(s, data.soil);
  computeLight(s, data.light);
}

const char *lightLevelName(LightLevel l) {
  switch (l) {
    case LightLevel::Low:      return "baixa";
    case LightLevel::Adequate: return "adequada";
    case LightLevel::High:     return "alta";
  }
  return "?";
}

const char *reservoirLevelName(ReservoirLevel l) {
  switch (l) {
    case ReservoirLevel::Empty:  return "vazio";
    case ReservoirLevel::Low:    return "baixo";
    case ReservoirLevel::Medium: return "medio";
    case ReservoirLevel::Full:   return "cheio";
  }
  return "?";
}
