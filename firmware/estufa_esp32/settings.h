// =============================================================================
//  settings.h  -  CONFIGURAÇÕES PERSISTENTES (memória flash / NVS)
// =============================================================================
#pragma once

#include "types.h"

// Carrega da flash. Se não houver nada salvo (ou a versão mudou), usa padrões.
Settings settingsLoad();

// Salva na flash. Retorna false em caso de erro.
bool settingsSave(const Settings &s);

// Verifica se os valores fazem sentido. Em caso de erro, explica em "error".
bool settingsValidate(const Settings &s, String &error);
