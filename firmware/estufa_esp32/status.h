// =============================================================================
//  status.h  -  ALERTAS E ESTADO GERAL DA ESTUFA
// =============================================================================
#pragma once

#include "types.h"

// Analisa sensores + irrigação e devolve os alertas ativos (bits AlertFlag).
uint32_t computeAlerts(const SensorData &d, const IrrigationStatus &irr,
                       const Settings &s);

// "alerta" = precisa de ação (água, falha, sensor quebrado)
// "atencao" = fora da faixa ideal, mas sem risco imediato
OverallStatus overallFromAlerts(uint32_t alerts);

const char *overallName(OverallStatus o);

// Percorre os alertas: devolve o código (para o app) e o texto curto (LCD).
// Uso: for (int i = 0; i < ALERT_COUNT; i++) if (alerts & alertBit(i)) ...
constexpr int ALERT_COUNT = 14;
uint32_t    alertBit(int index);
const char *alertCode(int index);
const char *alertShortText(int index);
