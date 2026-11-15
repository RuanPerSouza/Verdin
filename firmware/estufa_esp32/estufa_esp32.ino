// =============================================================================
//  ESTUFA INTELIGENTE - ESP32
// -----------------------------------------------------------------------------
//  Hardware: ESP32 DevKit V1, DHT11, sensor de solo resistivo (LM393), LDR,
//            sensor de nível por eletrodos, bomba 5V (via MOSFET) e LCD
//            gráfico 128x64 ST7920.
//  App:      conecta pelo Wi-Fi (API REST + WebSocket). Ver docs/api.md
//
//  Organização dos arquivos (abas na Arduino IDE):
//    config.h        pinos, limites, Wi-Fi           <- comece por aqui
//    types.h         estruturas de dados
//    sensors.*       leitura dos sensores
//    irrigation.*    lógica da bomba + proteções
//    status.*        alertas e estado geral
//    display.*       telas do LCD
//    api_server.*    Wi-Fi, API e WebSocket
//    settings.*      configurações salvas na flash
//
//  Regra de ouro do loop(): NADA de delay(). Cada tarefa confere com millis()
//  se já está na hora de rodar e devolve o controle rapidamente.
// =============================================================================

#include <esp_task_wdt.h>

#include "config.h"
#include "types.h"
#include "settings.h"
#include "sensors.h"
#include "irrigation.h"
#include "status.h"
#include "display.h"
#include "api_server.h"

// ------------------------------- Estado global -------------------------------
static Settings         settings;
static SensorData       data;
static IrrigationStatus irr;
static uint32_t         alerts  = 0;
static OverallStatus    overall = OverallStatus::Ok;
static QueueHandle_t    commandQueue;

// Marcadores de tempo de cada tarefa periódica
static uint32_t lastDisplay   = 0;
static uint32_t lastPublish   = 0;
static uint32_t lastBroadcast = 0;
static uint32_t lastSerialLog = 0;

// =============================================================================
//  Watchdog: se o loop() travar por WATCHDOG_TIMEOUT_S, o ESP32 reinicia.
//  Ao reiniciar, o pino da bomba volta a ser entrada e o resistor de 10k entre
//  o gate do MOSFET e o GND mantém a bomba DESLIGADA.
// =============================================================================
static void setupWatchdog() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = {
    .timeout_ms     = WATCHDOG_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic  = true,
  };
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
#else
  esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);  // vigia a tarefa do loop()
}

// =============================================================================
//  Execução dos comandos vindos do app (fila preenchida pelo servidor web)
// =============================================================================
static void applyCalibration(CalibrationTarget t) {
  Settings next = settings;
  switch (t) {
    case CalibrationTarget::SoilDry:     next.soilDryMv     = data.soil.mv;  break;
    case CalibrationTarget::SoilWet:     next.soilWetMv     = data.soil.mv;  break;
    case CalibrationTarget::LightDark:   next.lightDarkMv   = data.light.mv; break;
    case CalibrationTarget::LightBright: next.lightBrightMv = data.light.mv; break;
  }
  String err;
  if (!settingsValidate(next, err)) {
    Serial.printf("[CAL] Calibracao rejeitada: %s\n", err.c_str());
    return;
  }
  settings = next;
  settingsSave(settings);
  sensorsRecompute(settings, data);
  Serial.println(F("[CAL] Calibracao salva"));
}

static void processCommands(uint32_t now) {
  Command cmd;
  while (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
    switch (cmd.type) {
      case CommandType::PumpOn: {
        String reason;
        if (!irrigationManualStart(now, cmd.value, data, settings, reason)) {
          Serial.printf("[CMD] Bomba nao ligou: %s\n", reason.c_str());
        }
        break;
      }
      case CommandType::PumpOff:
        irrigationManualStop(now);
        break;

      case CommandType::SetMode:
        settings.pumpMode = (PumpMode)cmd.value;
        irrigationSetMode(now, settings.pumpMode);
        settingsSave(settings);
        break;

      case CommandType::ApplySettings:
        cmd.settings.pumpMode = settings.pumpMode;  // modo tem comando próprio
        settings = cmd.settings;
        settingsSave(settings);
        sensorsRecompute(settings, data);
        Serial.println(F("[CMD] Configuracao atualizada pelo app"));
        break;

      case CommandType::Calibrate:
        applyCalibration((CalibrationTarget)cmd.value);
        break;

      case CommandType::ResetFault:
        irrigationResetFault();
        break;

      case CommandType::FactoryReset:
        settings = Settings();
        settingsSave(settings);
        irrigationSetMode(now, settings.pumpMode);
        sensorsRecompute(settings, data);
        Serial.println(F("[CMD] Configuracao de fabrica restaurada"));
        break;
    }
  }
}

// =============================================================================
//  Comandos pelo Monitor Serial (útil para testar a montagem sem o app)
//    p = imprime leituras   b = liga a bomba 3 s   s = para a bomba
//    a/m/o = modo auto/manual/off   f = reseta falha
// =============================================================================
static void printReadings() {
  Serial.println(F("------------------------------------------------------"));
  Serial.printf("Solo: %5.1f%%  (%4u mV)  %s\n", data.soil.percent, data.soil.mv,
                data.soil.valid ? "ok" : "INVALIDO");
  Serial.printf("Ar:   %5.1f C  %5.1f%%   %s\n", data.air.temperature,
                data.air.humidity, data.air.valid ? "ok" : "INVALIDO");
  Serial.printf("Luz:  %5.1f%%  (%4u mV)  ~%.0f lx  %s  %s\n", data.light.percent,
                data.light.mv, data.light.lux, lightLevelName(data.light.level),
                data.light.valid ? "ok" : "INVALIDO");
  Serial.printf("Agua: %3u%%  medidor %u/%u %s  eletrodos(mV):", data.reservoir.percent,
                data.reservoir.meter, RESERVOIR_METER_FULL,
                reservoirLevelName(data.reservoir.level));
  for (int i = 0; i < LEVEL_ELECTRODE_COUNT; i++) {
    Serial.printf(" %u%s", data.reservoir.mv[i], data.reservoir.electrodes[i] ? "*" : "");
  }
  Serial.printf("  %s\n", data.reservoir.valid ? "ok" : "INVALIDO");
  Serial.printf("Bomba: %s  modo=%s  estado=%s  geral=%s\n", irr.pumpOn ? "LIGADA" : "desligada",
                pumpModeName(irr.mode), irrigationStateName(irr.state), overallName(overall));
  NetInfo net = networkInfo();
  Serial.printf("Wi-Fi: %s  IP=%s  apps=%u\n",
                net.staConnected ? "roteador" : (net.apActive ? "rede propria" : "conectando"),
                net.staConnected ? net.staIp.c_str() : net.apIp.c_str(), net.wsClients);
}

static void processSerial(uint32_t now) {
  while (Serial.available()) {
    char c = Serial.read();
    String reason;
    switch (c) {
      case 'p': printReadings(); break;
      case 'b':
        if (!irrigationManualStart(now, 3, data, settings, reason))
          Serial.printf("Bomba nao ligou: %s\n", reason.c_str());
        break;
      case 's': irrigationManualStop(now); break;
      case 'a': settings.pumpMode = PumpMode::Auto;   irrigationSetMode(now, settings.pumpMode); settingsSave(settings); break;
      case 'm': settings.pumpMode = PumpMode::Manual; irrigationSetMode(now, settings.pumpMode); settingsSave(settings); break;
      case 'o': settings.pumpMode = PumpMode::Off;    irrigationSetMode(now, settings.pumpMode); settingsSave(settings); break;
      case 'f': irrigationResetFault(); break;
      default: break;
    }
  }
}

// =============================================================================
//  LEDs de sinalização
// =============================================================================
static void updateLeds(uint32_t now, const NetInfo &net) {
  // LED vermelho: pisca rápido quando há alerta (água baixa, falha, sensor)
  bool alertOn = overall == OverallStatus::Alert && (now / 250) % 2;
  digitalWrite(PIN_LED_ALERT, alertOn ? HIGH : LOW);

  // LED azul da placa: aceso = no roteador | lento = rede própria | rápido = conectando
  bool wifiLed = net.staConnected ? true
               : net.apActive     ? (now / 1000) % 2
                                  : (now / 100) % 2;
  digitalWrite(PIN_LED_BUILTIN, wifiLed ? HIGH : LOW);
}

// =============================================================================
//  setup() e loop()
// =============================================================================
void setup() {
  Serial.begin(115200);
  settings = settingsLoad();

  // Calibração antiga tratava pouca tensão como escuro. No módulo de luz
  // acontece o contrário, e a leitura ficava presa em ~3 %.
  if (settings.lightBrightMv > settings.lightDarkMv) {
    settings.lightDarkMv   = DEFAULT_LIGHT_DARK_MV;
    settings.lightBrightMv = DEFAULT_LIGHT_BRIGHT_MV;
    settingsSave(settings);
    Serial.println(F("[CAL] Sensor de luz ajustado: mais luz = menos tensao"));
  }

  // Com o resistor de 10k no GND, solo seco fica perto de 0 V e o ADC
  // entrega ~142 mV. As curvas antigas tratavam isso como erro ou como 100 %.
  if ((settings.soilDryMv == 140 && settings.soilWetMv == 560) ||
      (settings.soilDryMv == 2800 && settings.soilWetMv == 900)) {
    settings.soilDryMv = DEFAULT_SOIL_DRY_MV;
    settings.soilWetMv = DEFAULT_SOIL_WET_MV;
    settingsSave(settings);
    Serial.println(F("[CAL] Solo 10k: tensao baixa = seco, tensao alta = molhado"));
  }

  // Bomba desligada é a PRIMEIRA coisa a garantir
  irrigationBegin(settings.pumpMode);

  pinMode(PIN_LED_ALERT, OUTPUT);
  pinMode(PIN_LED_BUILTIN, OUTPUT);

  Serial.println(F("\n=== Estufa Inteligente ESP32 v" FIRMWARE_VERSION " ==="));

  displayBegin();
  displayMessage("Estufa Inteligente", "Iniciando sensores...");

  sensorsBegin(settings, data);

  commandQueue = xQueueCreate(8, sizeof(Command));
  displayMessage("Estufa Inteligente", "Conectando Wi-Fi...");
  networkBegin(commandQueue, settings);

  setupWatchdog();
  Serial.println(F("Pronto. Digite 'p' no Monitor Serial para ver as leituras."));
}

void loop() {
  uint32_t now = millis();
  esp_task_wdt_reset();

  processCommands(now);
  processSerial(now);

  // 1) Ler sensores (cada um no seu ritmo)
  sensorsUpdate(now, settings, irr.pumpOn, data);

  // 2) Decidir sobre a irrigação
  IrrigationStatus prev = irr;
  irrigationUpdate(now, data, settings);
  irr = irrigationGetStatus(now, settings);

  // 3) Avaliar alertas e estado geral
  alerts  = computeAlerts(data, irr, settings);
  overall = overallFromAlerts(alerts);

  // 4) Rede
  networkLoop(now);
  static NetInfo net;
  static uint32_t lastNetInfo = 0;
  if (now - lastNetInfo >= 500) {
    lastNetInfo = now;
    net = networkInfo();
  }

  // Bomba ou estado mudou? Avisa o app na hora, sem esperar o intervalo.
  bool changed = prev.pumpOn != irr.pumpOn || prev.state != irr.state || prev.mode != irr.mode;
  bool broadcastDue = changed || now - lastBroadcast >= INTERVAL_BROADCAST_MS;
  if (changed || now - lastPublish >= 1000) {
    lastPublish = now;
    if (broadcastDue) lastBroadcast = now;
    networkPublish(now, data, irr, settings, alerts, overall, broadcastDue);
  }

  // 5) Interface local
  if (now - lastDisplay >= INTERVAL_DISPLAY_MS) {
    lastDisplay = now;
    displayUpdate(now, data, irr, alerts, overall, net);
  }
  updateLeds(now, net);

  if (now - lastSerialLog >= INTERVAL_SERIAL_LOG_MS) {
    lastSerialLog = now;
    printReadings();
  }
}
