// =============================================================================
//  irrigation.h  -  MÁQUINA DE ESTADOS DA IRRIGAÇÃO + PROTEÇÕES DA BOMBA
// =============================================================================
#pragma once

#include "types.h"

void irrigationBegin(PumpMode mode);

// Chamada a cada volta do loop(): decide se liga/desliga a bomba.
void irrigationUpdate(uint32_t now, const SensorData &d, const Settings &s);

// Comandos (vindos do app). Retornam false + motivo se não puderem ser feitos.
bool irrigationManualStart(uint32_t now, uint32_t seconds, const SensorData &d,
                           const Settings &s, String &reason);
void irrigationManualStop(uint32_t now);
void irrigationSetMode(uint32_t now, PumpMode mode);
void irrigationResetFault();

IrrigationStatus irrigationGetStatus(uint32_t now, const Settings &s);

// Reservatório com água suficiente para a bomba funcionar?
bool reservoirAllowsPump(const SensorData &d, const Settings &s);

const char *irrigationStateName(IrrigationState st);
const char *pumpModeName(PumpMode m);
bool pumpModeFromName(const char *name, PumpMode &out);
