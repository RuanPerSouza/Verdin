// =============================================================================
//  api_server.h  -  WI-FI, API REST (HTTP) E WEBSOCKET PARA O APP
// -----------------------------------------------------------------------------
//  Contrato completo da API em docs/api.md
// =============================================================================
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "types.h"

// commandQueue: fila onde o servidor web coloca os comandos do app.
void networkBegin(QueueHandle_t commandQueue, const Settings &s);

// Mantém o Wi-Fi (fallback para Access Point, mDNS, limpeza de clientes).
void networkLoop(uint32_t now);

// Atualiza a "foto" dos dados que o servidor entrega ao app e, se broadcast
// for true, envia o status para todos os apps conectados via WebSocket.
void networkPublish(uint32_t now, const SensorData &d, const IrrigationStatus &irr,
                    const Settings &s, uint32_t alerts, OverallStatus overall,
                    bool broadcast);

NetInfo networkInfo();
