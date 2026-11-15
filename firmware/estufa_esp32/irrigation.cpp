#include "irrigation.h"

// =============================================================================
//  Por que irrigar em PULSOS?
// -----------------------------------------------------------------------------
//  A água demora para chegar até o sensor. Se a bomba ficasse ligada "até o
//  sensor marcar 60%", a terra encharcaria antes da leitura subir. Então:
//    liga X s  ->  espera Y s (infiltrar)  ->  mede  ->  repete se precisar
//  Com limite de pulsos por ciclo e um intervalo de segurança entre ciclos.
// =============================================================================

static PumpMode        mode  = PumpMode::Auto;
static IrrigationState state = IrrigationState::Idle;

static bool     pumpOn          = false;
static uint32_t pumpOnSince     = 0;
static uint32_t stateSince      = 0;     // quando entrou no estado atual
static uint32_t manualDurationMs= 0;

static uint8_t  pulses          = 0;
static float    cycleStartSoil  = 0;
static bool     hasIrrigated    = false;
static uint32_t lastCycleEnd    = 0;     // base do intervalo de segurança
static uint32_t lastPumpOff     = 0;
static uint32_t cycles          = 0;
static bool     faultLatched    = false; // falha só sai com reset pelo app

// -----------------------------------------------------------------------------
//  Driver da bomba: ÚNICO lugar que mexe no pino.
// -----------------------------------------------------------------------------
static void pumpPin(bool on) {
  pinMode(PIN_PUMP, OUTPUT);
  // HIGH desliga o relé. LOW liga. O status do app acompanha esse nível.
  digitalWrite(PIN_PUMP, on ? LOW : HIGH);
}

static void pumpWrite(bool on, uint32_t now) {
  pumpPin(on);
  if (on == pumpOn) return;
  pumpOn = on;
  if (on) {
    pumpOnSince = now;
  } else {
    lastPumpOff  = now;
    hasIrrigated = true;
  }
  Serial.printf("[BOMBA] %s\n", on ? "LIGADA" : "DESLIGADA");
}

static void setState(IrrigationState st, uint32_t now) {
  if (st == state) return;
  Serial.printf("[IRRIG] %s -> %s\n", irrigationStateName(state), irrigationStateName(st));
  state      = st;
  stateSince = now;
}

static void endCycle(uint32_t now) {
  pumpWrite(false, now);
  lastCycleEnd = now;
  pulses = 0;
}

bool reservoirAllowsPump(const SensorData &d, const Settings &s) {
  return d.reservoir.valid && d.reservoir.wetCount >= s.reservoirMinElec;
}

static bool inCooldown(uint32_t now, const Settings &s) {
  return lastCycleEnd != 0 && now - lastCycleEnd < (uint32_t)s.cooldownMin * 60000UL;
}

void irrigationBegin(PumpMode m) {
  mode  = m;
  state = IrrigationState::Idle;
  stateSince = millis();
  pumpWrite(false, stateSince);
}

// =============================================================================
//  Coração da lógica
// =============================================================================
void irrigationUpdate(uint32_t now, const SensorData &d, const Settings &s) {
  // Mantém o pino no mesmo nível que o app está mostrando.
  pumpPin(pumpOn);

  // --------------------- PROTEÇÕES (valem em qualquer estado) ----------------
  // Ligada de propósito (duração 0) fica assim até o app mandar parar.
  bool heldOn = state == IrrigationState::ManualRun && manualDurationMs == 0;
  if (pumpOn && !heldOn) {
    if (now - pumpOnSince >= (uint32_t)PUMP_ABSOLUTE_MAX_ON_S * 1000UL) {
      Serial.println(F("[PROTECAO] Tempo maximo da bomba excedido!"));
      endCycle(now);
      faultLatched = true;
      setState(IrrigationState::Fault, now);
      return;
    }
    if (!reservoirAllowsPump(d, s)) {
      Serial.println(F("[PROTECAO] Reservatorio baixo, bomba desligada"));
      endCycle(now);
      setState(IrrigationState::BlockedNoWater, now);
      return;
    }
    if (mode == PumpMode::Off) {
      endCycle(now);
      setState(IrrigationState::Idle, now);
      return;
    }
  }

  uint32_t elapsed = now - stateSince;

  switch (state) {
    case IrrigationState::ManualRun:
      // Duração 0 fica ligada até o comando de parar.
      if (manualDurationMs > 0 && elapsed >= manualDurationMs) {
        endCycle(now);  // irrigação manual também conta para o intervalo
        setState(IrrigationState::Idle, now);
      }
      return;

    case IrrigationState::Watering:
      if (!d.soil.valid) {
        endCycle(now);
        setState(IrrigationState::SensorError, now);
      } else if (elapsed >= (uint32_t)s.pulseS * 1000UL) {
        pumpWrite(false, now);
        pulses++;
        setState(IrrigationState::Soaking, now);
      }
      return;

    case IrrigationState::Soaking:
      if (!d.soil.valid) {
        endCycle(now);
        setState(IrrigationState::SensorError, now);
        return;
      }
      if (elapsed < (uint32_t)s.soakS * 1000UL) return;

      if (d.soil.percent >= s.soilTargetPct) {
        Serial.printf("[IRRIG] Meta atingida (%.0f%%)\n", d.soil.percent);
        endCycle(now);
        setState(IrrigationState::Cooldown, now);
      } else if (pulses >= s.maxPulses) {
        // Bombeou o máximo e o solo quase não mudou: mangueira solta,
        // bomba queimada, sensor fora da terra... melhor parar e avisar.
        if (d.soil.percent - cycleStartSoil < PUMP_FAULT_MIN_RISE_PCT) {
          Serial.println(F("[PROTECAO] Solo nao respondeu a irrigacao -> FALHA"));
          faultLatched = true;
          endCycle(now);
          setState(IrrigationState::Fault, now);
        } else {
          endCycle(now);
          setState(IrrigationState::Cooldown, now);
        }
      } else if (!reservoirAllowsPump(d, s)) {
        endCycle(now);
        setState(IrrigationState::BlockedNoWater, now);
      } else {
        pumpWrite(true, now);
        setState(IrrigationState::Watering, now);
      }
      return;

    default:
      break;
  }

  // ----- Estados "parados": Idle, Cooldown, BlockedNoWater, SensorError, Fault
  // Não liga sozinha logo no boot: espera os sensores e o Wi-Fi assentarem.
  if (now < 20000UL) {
    setState(IrrigationState::Idle, now);
    return;
  }
  if (mode != PumpMode::Auto) {
    setState(IrrigationState::Idle, now);
    return;
  }
  if (faultLatched) {
    setState(IrrigationState::Fault, now);
    return;
  }
  if (!d.soil.valid) {
    setState(IrrigationState::SensorError, now);
    return;
  }
  if (inCooldown(now, s)) {
    setState(IrrigationState::Cooldown, now);
    return;
  }
  if (d.soil.percent < s.soilMinPct) {
    if (!reservoirAllowsPump(d, s)) {
      setState(IrrigationState::BlockedNoWater, now);
      return;
    }
    Serial.printf("[IRRIG] Solo seco (%.0f%%), iniciando ciclo\n", d.soil.percent);
    pulses         = 0;
    cycleStartSoil = d.soil.percent;
    cycles++;
    pumpWrite(true, now);
    setState(IrrigationState::Watering, now);
    return;
  }
  setState(IrrigationState::Idle, now);
}

// =============================================================================
//  Comandos
// =============================================================================
bool irrigationManualStart(uint32_t now, uint32_t seconds, const SensorData &d,
                           const Settings &s, String &reason) {
  if (mode == PumpMode::Off) {
    reason = "bomba em modo desligado";
    return false;
  }
  if (!reservoirAllowsPump(d, s)) {
    reason = "reservatorio com nivel insuficiente";
    return false;
  }
  seconds = constrain(seconds, 1UL, (uint32_t)s.maxManualS);
  manualDurationMs = seconds * 1000UL;
  pulses = 0;
  pumpWrite(true, now);
  state      = IrrigationState::ManualRun;  // força, mesmo se já estava nele
  stateSince = now;
  Serial.printf("[IRRIG] Acionamento manual por %lus\n", (unsigned long)seconds);
  return true;
}

void irrigationManualStop(uint32_t now) {
  // Reescreve o pino mesmo se o software já marcou a bomba como parada,
  // para soltar um relé que ficou preso. O intervalo de segurança só
  // começa quando ela estava realmente ligada.
  pumpPin(false);
  if (!pumpOn) return;
  pumpOn       = false;
  lastPumpOff  = now;
  hasIrrigated = true;
  Serial.println(F("[BOMBA] DESLIGADA"));
  lastCycleEnd = now;
  pulses       = 0;
  setState(IrrigationState::Idle, now);
}

void irrigationSetMode(uint32_t now, PumpMode m) {
  if (m == mode) return;
  Serial.printf("[IRRIG] Modo: %s\n", pumpModeName(m));
  mode = m;
  if (pumpOn) endCycle(now);
  setState(IrrigationState::Idle, now);
}

void irrigationResetFault() {
  faultLatched = false;
  if (state == IrrigationState::Fault) {
    state      = IrrigationState::Idle;
    stateSince = millis();
  }
  Serial.println(F("[IRRIG] Falha reconhecida pelo usuario"));
}

IrrigationStatus irrigationGetStatus(uint32_t now, const Settings &s) {
  IrrigationStatus st;
  st.mode            = mode;
  st.state           = state;
  st.pumpOn          = pumpOn;
  st.pulsesInCycle   = pulses;
  st.cyclesSinceBoot = cycles;
  st.cooldownRemainingS = inCooldown(now, s)
      ? ((uint32_t)s.cooldownMin * 60000UL - (now - lastCycleEnd)) / 1000UL
      : 0;
  st.lastIrrigationAgoS = hasIrrigated ? (int32_t)((now - lastPumpOff) / 1000UL) : -1;
  if (pumpOn) st.lastIrrigationAgoS = 0;
  return st;
}

const char *irrigationStateName(IrrigationState st) {
  switch (st) {
    case IrrigationState::Idle:           return "monitorando";
    case IrrigationState::Watering:       return "irrigando";
    case IrrigationState::Soaking:        return "infiltrando";
    case IrrigationState::Cooldown:       return "intervalo";
    case IrrigationState::ManualRun:      return "manual";
    case IrrigationState::BlockedNoWater: return "sem_agua";
    case IrrigationState::SensorError:    return "erro_sensor";
    case IrrigationState::Fault:          return "falha";
  }
  return "?";
}

const char *pumpModeName(PumpMode m) {
  switch (m) {
    case PumpMode::Auto:   return "auto";
    case PumpMode::Manual: return "manual";
    case PumpMode::Off:    return "off";
  }
  return "?";
}

bool pumpModeFromName(const char *name, PumpMode &out) {
  if (!name) return false;
  if (strcmp(name, "auto") == 0)   { out = PumpMode::Auto;   return true; }
  if (strcmp(name, "manual") == 0) { out = PumpMode::Manual; return true; }
  if (strcmp(name, "off") == 0)    { out = PumpMode::Off;    return true; }
  return false;
}
