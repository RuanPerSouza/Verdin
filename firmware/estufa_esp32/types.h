// =============================================================================
//  types.h  -  ESTRUTURAS DE DADOS COMPARTILHADAS ENTRE OS MÓDULOS
// =============================================================================
#pragma once

#include "config.h"

// ----------------------------- Leituras dos sensores -------------------------
struct SoilReading {
  float    percent = 0;     // 0 = seco, 100 = encharcado
  uint16_t mv      = 0;     // tensão bruta lida (usada na calibração)
  bool     valid   = false;
};

struct AirReading {
  float temperature = 0;    // °C
  float humidity    = 0;    // %UR
  bool  valid       = false;
};

enum class LightLevel : uint8_t { Low, Adequate, High };

struct LightReading {
  float      percent = 0;
  float      lux     = -1;  // -1 = não disponível (estimativa só com LDR puro)
  uint16_t   mv      = 0;
  LightLevel level   = LightLevel::Low;
  bool       valid   = false;
};

enum class ReservoirLevel : uint8_t { Empty, Low, Medium, Full };

struct ReservoirReading {
  uint8_t        percent    = 0;    // medida / 240, de 0 a 100
  uint16_t       meter      = 0;    // medidor calibrado de 0 (vazio) a 240 (cheio)
  uint8_t        wetCount   = 0;    // quantos eletrodos estão na água
  bool           electrodes[LEVEL_ELECTRODE_COUNT] = {};
  uint16_t       mv[LEVEL_ELECTRODE_COUNT] = {};
  ReservoirLevel level      = ReservoirLevel::Empty;
  bool           valid      = false;
};

struct SensorData {
  SoilReading      soil;
  AirReading       air;
  LightReading     light;
  ReservoirReading reservoir;
};

// ----------------------------- Irrigação -------------------------------------
enum class PumpMode : uint8_t {
  Auto,     // irrigação automática pela umidade do solo
  Manual,   // só liga por comando do app
  Off       // bomba bloqueada
};

enum class IrrigationState : uint8_t {
  Idle,          // monitorando, solo OK
  Watering,      // pulso de irrigação em andamento
  Soaking,       // aguardando a água infiltrar
  Cooldown,      // intervalo de segurança entre ciclos
  ManualRun,     // acionamento manual pelo app
  BlockedNoWater,// reservatório abaixo do mínimo
  SensorError,   // leitura de solo inválida -> não irriga
  Fault          // bomba ligou e o solo não respondeu (precisa reset no app)
};

struct IrrigationStatus {
  PumpMode        mode               = PumpMode::Auto;
  IrrigationState state              = IrrigationState::Idle;
  bool            pumpOn             = false;
  uint8_t         pulsesInCycle      = 0;
  uint32_t        cooldownRemainingS = 0;
  int32_t         lastIrrigationAgoS = -1;  // -1 = ainda não irrigou
  uint32_t        cyclesSinceBoot    = 0;
};

// ----------------------------- Configurações (salvas na flash) ---------------
// Ao mudar esta struct, incremente SETTINGS_VERSION para recarregar padrões.
#define SETTINGS_VERSION 1

struct Settings {
  uint16_t version          = SETTINGS_VERSION;
  // Irrigação
  uint8_t  soilMinPct       = DEFAULT_SOIL_MIN_PCT;
  uint8_t  soilTargetPct    = DEFAULT_SOIL_TARGET_PCT;
  uint16_t pulseS           = DEFAULT_PULSE_S;
  uint16_t soakS            = DEFAULT_SOAK_S;
  uint8_t  maxPulses        = DEFAULT_MAX_PULSES;
  uint16_t cooldownMin      = DEFAULT_COOLDOWN_MIN;
  uint16_t maxManualS       = DEFAULT_MAX_MANUAL_S;
  uint8_t  reservoirMinElec = DEFAULT_RESERVOIR_MIN_ELEC;
  // Faixas ideais (para o estado geral)
  uint8_t  lightLowPct      = DEFAULT_LIGHT_LOW_PCT;
  uint8_t  lightHighPct     = DEFAULT_LIGHT_HIGH_PCT;
  int8_t   tempMinC         = DEFAULT_TEMP_MIN_C;
  int8_t   tempMaxC         = DEFAULT_TEMP_MAX_C;
  uint8_t  airHumMinPct     = DEFAULT_AIR_HUM_MIN_PCT;
  uint8_t  airHumMaxPct     = DEFAULT_AIR_HUM_MAX_PCT;
  // Calibração (mV)
  uint16_t soilDryMv        = DEFAULT_SOIL_DRY_MV;
  uint16_t soilWetMv        = DEFAULT_SOIL_WET_MV;
  uint16_t lightDarkMv      = DEFAULT_LIGHT_DARK_MV;
  uint16_t lightBrightMv    = DEFAULT_LIGHT_BRIGHT_MV;
  uint16_t electrodeThreshMv= DEFAULT_ELECTRODE_THRESH_MV;
  // Modo da bomba também é lembrado após reiniciar
  PumpMode pumpMode         = PumpMode::Auto;
};

// ----------------------------- Estado geral / alertas ------------------------
enum class OverallStatus : uint8_t { Ok, Warning, Alert };

// Cada alerta é um bit, assim cabem todos em um único número.
enum AlertFlag : uint32_t {
  ALERT_RESERVOIR_EMPTY = 1UL << 0,
  ALERT_RESERVOIR_LOW   = 1UL << 1,
  ALERT_SOIL_SENSOR     = 1UL << 2,
  ALERT_AIR_SENSOR      = 1UL << 3,
  ALERT_LIGHT_SENSOR    = 1UL << 4,
  ALERT_LEVEL_SENSOR    = 1UL << 5,
  ALERT_PUMP_FAULT      = 1UL << 6,
  ALERT_TEMP_HIGH       = 1UL << 7,
  ALERT_TEMP_LOW        = 1UL << 8,
  ALERT_AIR_HUM_HIGH    = 1UL << 9,
  ALERT_AIR_HUM_LOW     = 1UL << 10,
  ALERT_SOIL_DRY        = 1UL << 11,
  ALERT_LIGHT_LOW       = 1UL << 12,
  ALERT_LIGHT_HIGH      = 1UL << 13,
};

// ----------------------------- Rede ------------------------------------------
struct NetInfo {
  bool     staConnected = false;  // conectado ao roteador
  bool     apActive     = false;  // rede própria "Estufa-ESP32" ligada
  String   staIp;
  String   apIp;
  int32_t  rssi         = 0;      // força do sinal (dBm, mais perto de 0 = melhor)
  uint8_t  wsClients    = 0;      // apps conectados no WebSocket
};

// ----------------------------- Comandos vindos do app ------------------------
// O servidor web roda em outra tarefa do FreeRTOS. Para não mexer no estado
// ao mesmo tempo que o loop(), ele apenas ENFILEIRA comandos.
enum class CommandType : uint8_t {
  PumpOn,        // value = segundos
  PumpOff,
  SetMode,       // value = PumpMode
  ApplySettings, // settings = nova configuração já validada
  Calibrate,     // value = CalibrationTarget
  ResetFault,
  FactoryReset
};

enum class CalibrationTarget : uint8_t { SoilDry, SoilWet, LightDark, LightBright };

struct Command {
  CommandType type;
  uint32_t    value;
  Settings    settings;
};
