// =============================================================================
//  sensors.h  -  LEITURA DE TODOS OS SENSORES (sem travar o loop)
// =============================================================================
#pragma once

#include "types.h"

// Configura os pinos e faz uma primeira leitura completa (pode demorar ~1,5 s).
void sensorsBegin(const Settings &s, SensorData &data);

// Chamada a cada volta do loop(). Cada sensor tem o seu próprio intervalo;
// a função só lê o que estiver "vencido" e retorna imediatamente.
// pumpOn = true acelera a leitura do nível (proteção contra bomba a seco).
void sensorsUpdate(uint32_t now, const Settings &s, bool pumpOn, SensorData &data);

// Recalcula porcentagens/estados com a calibração atual (após mudar ajustes).
void sensorsRecompute(const Settings &s, SensorData &data);

const char *lightLevelName(LightLevel l);
const char *reservoirLevelName(ReservoirLevel l);
