// =============================================================================
//  display.h  -  LCD GRÁFICO 128x64 (ST7920) EM MODO SERIAL
// =============================================================================
#pragma once

#include "types.h"

void displayBegin();

// Mensagem simples durante a inicialização
void displayMessage(const char *line1, const char *line2 = nullptr);

// Redesenha a tela. Alterna entre as páginas a cada DISPLAY_PAGE_MS.
void displayUpdate(uint32_t now, const SensorData &d, const IrrigationStatus &irr,
                   uint32_t alerts, OverallStatus overall, const NetInfo &net);
