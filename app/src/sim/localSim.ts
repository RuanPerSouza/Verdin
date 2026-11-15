// Simulação local do ESP32. Roda inteira no aparelho, sem rede e sem o
// servidor tools/mock-esp32. O contrato é o mesmo da API (docs/api.md).

import type {
  ActionResult,
  CalibrationTarget,
  EstufaConfig,
  EstufaSettings,
  EstufaStatus,
  IrrigationState,
  PumpMode,
} from '../api/types';

const ELECTRODE_PERCENT = [20, 50, 90];
const USER_MAX_ON_S = 45;
const ABSOLUTE_MAX_ON_S = 60;

const DEFAULTS: EstufaSettings = {
  soilMinPct: 35,
  soilTargetPct: 60,
  pulseS: 5,
  soakS: 30,
  maxPulses: 6,
  cooldownMin: 30,
  maxManualS: 20,
  reservoirMinElec: 1,
  lightLowPct: 30,
  lightHighPct: 85,
  tempMinC: 15,
  tempMaxC: 32,
  airHumMinPct: 40,
  airHumMaxPct: 85,
  soilDryMv: 3000,
  soilWetMv: 1200,
  lightDarkMv: 50,
  lightBrightMv: 3000,
  electrodeThreshMv: 500,
};

type SimState = {
  soil: number;
  water: number;
  temp: number;
  hum: number;
  lightPhase: number;
  pumpOn: boolean;
  mode: PumpMode;
  state: IrrigationState;
  stateSince: number;
  pumpOnSince: number;
  manualMs: number;
  pulses: number;
  cycleStartSoil: number;
  lastCycleEnd: number;
  lastPumpOff: number;
  cycles: number;
  faultLatched: boolean;
};

function clamp(v: number, a: number, b: number) {
  return Math.min(b, Math.max(a, v));
}

function round1(v: number) {
  return Math.round(v * 10) / 10;
}

function freshSim(now: number): SimState {
  return {
    soil: 32,
    water: 70,
    temp: 25,
    hum: 60,
    lightPhase: 0,
    pumpOn: false,
    mode: 'auto',
    state: 'monitorando',
    stateSince: now,
    pumpOnSince: 0,
    manualMs: 0,
    pulses: 0,
    cycleStartSoil: 0,
    lastCycleEnd: 0,
    lastPumpOff: 0,
    cycles: 0,
    faultLatched: false,
  };
}

function validate(s: EstufaSettings): string | null {
  if (s.soilMinPct >= s.soilTargetPct) return 'O mínimo do solo precisa ser menor que a meta.';
  if (s.soilTargetPct > 95 || s.soilMinPct < 5) return 'Os limites de solo ficam entre 5% e 95%.';
  if (s.pulseS < 1 || s.pulseS > USER_MAX_ON_S) return `Cada pulso fica entre 1 e ${USER_MAX_ON_S} s.`;
  if (s.soakS < 5 || s.soakS > 600) return 'A espera para infiltrar fica entre 5 e 600 s.';
  if (s.maxPulses < 1 || s.maxPulses > 20) return 'Os pulsos por ciclo ficam entre 1 e 20.';
  if (s.cooldownMin < 1 || s.cooldownMin > 1440) return 'O intervalo fica entre 1 e 1440 min.';
  if (s.maxManualS < 1 || s.maxManualS > USER_MAX_ON_S) return `O limite manual fica entre 1 e ${USER_MAX_ON_S} s.`;
  if (s.reservoirMinElec < 1 || s.reservoirMinElec > ELECTRODE_PERCENT.length) {
    return 'A quantidade de eletrodos não é válida.';
  }
  if (s.lightLowPct >= s.lightHighPct || s.lightHighPct > 100) return 'A luz baixa precisa ser menor que a luz alta (máx. 100).';
  if (s.tempMinC >= s.tempMaxC) return 'A temperatura mínima precisa ser menor que a máxima.';
  if (s.airHumMinPct >= s.airHumMaxPct || s.airHumMaxPct > 100) {
    return 'A umidade mínima do ar precisa ser menor que a máxima (máx. 100).';
  }
  if (s.soilDryMv <= s.soilWetMv + 200) return 'O solo seco precisa ser pelo menos 200 mV maior que o solo molhado.';
  if (s.lightBrightMv === s.lightDarkMv) return 'Os valores de luz claro e escuro não podem ser iguais.';
  if (s.electrodeThreshMv < 50 || s.electrodeThreshMv > 3000) return 'O limite do eletrodo fica entre 50 e 3000 mV.';
  return null;
}

export function createLocalSim() {
  let settings: EstufaSettings = { ...DEFAULTS };
  let startedAt = Date.now();
  let sim = freshSim(startedAt);

  const electrodes = () => ELECTRODE_PERCENT.map((p) => sim.water >= p);
  const wetCount = () => electrodes().filter(Boolean).length;
  const pumpAllowed = () => wetCount() >= settings.reservoirMinElec;
  const inCooldown = (now: number) =>
    sim.lastCycleEnd !== 0 && now - sim.lastCycleEnd < settings.cooldownMin * 60000;

  const setState = (st: IrrigationState, now: number) => {
    if (st !== sim.state) {
      sim.state = st;
      sim.stateSince = now;
    }
  };

  const pump = (on: boolean, now: number) => {
    if (on === sim.pumpOn) return;
    sim.pumpOn = on;
    if (on) sim.pumpOnSince = now;
    else sim.lastPumpOff = now;
  };

  const endCycle = (now: number) => {
    pump(false, now);
    sim.lastCycleEnd = now;
    sim.pulses = 0;
  };

  const physics = (dt: number) => {
    const s = dt / 1000;
    sim.soil = clamp(sim.soil - 0.04 * s + (Math.random() - 0.5) * 0.05, 0, 100);
    if (sim.pumpOn) {
      sim.soil = clamp(sim.soil + 1.2 * s, 0, 100);
      sim.water = clamp(sim.water - 0.6 * s, 0, 100);
    }
    sim.temp = clamp(sim.temp + (Math.random() - 0.5) * 0.04, 10, 40);
    sim.hum = clamp(sim.hum + (Math.random() - 0.5) * 0.08, 20, 95);
    sim.lightPhase += s / 90;
  };

  const irrigation = (now: number) => {
    if (sim.pumpOn) {
      if (now - sim.pumpOnSince >= ABSOLUTE_MAX_ON_S * 1000) {
        endCycle(now);
        sim.faultLatched = true;
        return setState('falha', now);
      }
      if (!pumpAllowed()) {
        endCycle(now);
        return setState('sem_agua', now);
      }
      if (sim.mode === 'off') {
        endCycle(now);
        return setState('monitorando', now);
      }
    }
    const elapsed = now - sim.stateSince;

    if (sim.state === 'manual') {
      if (elapsed >= sim.manualMs) {
        endCycle(now);
        setState('monitorando', now);
      }
      return;
    }
    if (sim.state === 'irrigando') {
      if (elapsed >= settings.pulseS * 1000) {
        pump(false, now);
        sim.pulses++;
        setState('infiltrando', now);
      }
      return;
    }
    if (sim.state === 'infiltrando') {
      if (elapsed < settings.soakS * 1000) return;
      if (sim.soil >= settings.soilTargetPct) {
        endCycle(now);
        return setState('intervalo', now);
      }
      if (sim.pulses >= settings.maxPulses) {
        const rose = sim.soil - sim.cycleStartSoil >= 3;
        endCycle(now);
        if (!rose) sim.faultLatched = true;
        return setState(rose ? 'intervalo' : 'falha', now);
      }
      if (!pumpAllowed()) {
        endCycle(now);
        return setState('sem_agua', now);
      }
      pump(true, now);
      return setState('irrigando', now);
    }

    if (sim.mode !== 'auto') return setState('monitorando', now);
    if (sim.faultLatched) return setState('falha', now);
    if (inCooldown(now)) return setState('intervalo', now);
    if (sim.soil < settings.soilMinPct) {
      if (!pumpAllowed()) return setState('sem_agua', now);
      sim.pulses = 0;
      sim.cycleStartSoil = sim.soil;
      sim.cycles++;
      pump(true, now);
      return setState('irrigando', now);
    }
    setState('monitorando', now);
  };

  const status = (): EstufaStatus => {
    const now = Date.now();
    const lp = clamp(50 + 45 * Math.sin(sim.lightPhase * 2 * Math.PI), 0, 100);
    const lightLevel = lp < settings.lightLowPct ? 'baixa' : lp > settings.lightHighPct ? 'alta' : 'adequada';
    const wc = wetCount();
    const level = wc === 0 ? 'vazio' : wc >= ELECTRODE_PERCENT.length ? 'cheio' : wc === 1 ? 'baixo' : 'medio';
    const percent = wc === 0 ? 0 : ELECTRODE_PERCENT[wc - 1];
    const alerts: EstufaStatus['alerts'] = [];
    if (sim.state === 'falha') alerts.push('BOMBA_FALHA');
    if (level === 'vazio') alerts.push('RESERVATORIO_VAZIO');
    if (level === 'baixo') alerts.push('RESERVATORIO_BAIXO');
    if (sim.soil < settings.soilMinPct) alerts.push('SOLO_SECO');
    if (sim.temp > settings.tempMaxC) alerts.push('TEMPERATURA_ALTA');
    if (sim.temp < settings.tempMinC) alerts.push('TEMPERATURA_BAIXA');
    if (sim.hum > settings.airHumMaxPct) alerts.push('UMIDADE_AR_ALTA');
    if (sim.hum < settings.airHumMinPct) alerts.push('UMIDADE_AR_BAIXA');
    if (lightLevel === 'baixa') alerts.push('LUZ_BAIXA');
    if (lightLevel === 'alta') alerts.push('LUZ_ALTA');
    const severe = ['BOMBA_FALHA', 'RESERVATORIO_VAZIO', 'RESERVATORIO_BAIXO', 'SENSOR_SOLO', 'SENSOR_NIVEL'];
    const overall = alerts.some((a) => severe.includes(a)) ? 'alerta' : alerts.length ? 'atencao' : 'ok';
    const lightMv = Math.round(settings.lightDarkMv + (lp / 100) * (settings.lightBrightMv - settings.lightDarkMv));
    const cooldown = inCooldown(now)
      ? Math.floor((settings.cooldownMin * 60000 - (now - sim.lastCycleEnd)) / 1000)
      : 0;

    return {
      device: {
        name: 'simulação local',
        firmware: '1.0.0-local',
        uptimeS: Math.floor((now - startedAt) / 1000),
        wifiMode: 'sta',
        ip: 'neste aparelho',
        rssi: 0,
        freeHeap: 200000,
      },
      soil: {
        percent: round1(sim.soil),
        mv: Math.round(settings.soilDryMv - (sim.soil / 100) * (settings.soilDryMv - settings.soilWetMv)),
        valid: true,
      },
      air: { temperature: round1(sim.temp), humidity: round1(sim.hum), valid: true },
      light: {
        percent: round1(lp),
        lux: Math.round(lp * lp * 0.5),
        luxEstimated: true,
        mv: lightMv,
        level: lightLevel,
        valid: true,
      },
      reservoir: {
        percent,
        meter: Math.round((percent / 100) * 240),
        meterFull: 240,
        level,
        valid: true,
        pumpAllowed: pumpAllowed(),
        electrodes: electrodes(),
        electrodesMv: electrodes().map((w) => (w ? 1800 : 20)),
      },
      pump: {
        on: sim.pumpOn,
        mode: sim.mode,
        state: sim.state,
        pulsesInCycle: sim.pulses,
        maxPulses: settings.maxPulses,
        cooldownRemainingS: cooldown,
        lastIrrigationAgoS: sim.pumpOn ? 0 : sim.lastPumpOff ? Math.floor((now - sim.lastPumpOff) / 1000) : null,
        cyclesSinceBoot: sim.cycles,
      },
      overall,
      alerts,
      ts: now - startedAt,
    };
  };

  const config = (): EstufaConfig => ({
    ...settings,
    limits: { userMaxOnS: USER_MAX_ON_S, absoluteMaxOnS: ABSOLUTE_MAX_ON_S, electrodeCount: ELECTRODE_PERCENT.length },
  });

  return {
    reset() {
      startedAt = Date.now();
      settings = { ...DEFAULTS };
      sim = freshSim(startedAt);
    },
    tick(dt: number) {
      const now = Date.now();
      physics(dt);
      irrigation(now);
    },
    status,
    config,
    pumpOn(seconds: number): ActionResult {
      const now = Date.now();
      if (sim.mode === 'off') return { ok: false, error: 'Bomba em modo desligado.' };
      if (!pumpAllowed()) return { ok: false, error: 'Reservatório com nível insuficiente.' };
      if (!Number.isInteger(seconds) || seconds < 1 || seconds > settings.maxManualS) {
        return { ok: false, error: `O tempo fica entre 1 e ${settings.maxManualS} s.` };
      }
      sim.manualMs = seconds * 1000;
      sim.pulses = 0;
      pump(true, now);
      sim.state = 'manual';
      sim.stateSince = now;
      return { ok: true };
    },
    pumpOff(): ActionResult {
      const now = Date.now();
      if (sim.pumpOn) {
        endCycle(now);
        setState('monitorando', now);
      }
      return { ok: true };
    },
    setMode(mode: PumpMode): ActionResult {
      const now = Date.now();
      if (mode !== 'auto' && mode !== 'manual' && mode !== 'off') {
        return { ok: false, error: 'Modo inválido.' };
      }
      if (mode !== sim.mode) {
        sim.mode = mode;
        if (sim.pumpOn) endCycle(now);
        setState('monitorando', now);
      }
      return { ok: true };
    },
    resetFault(): ActionResult {
      sim.faultLatched = false;
      if (sim.state === 'falha') setState('monitorando', Date.now());
      return { ok: true };
    },
    calibrate(target: CalibrationTarget): ActionResult {
      const st = status();
      const map: Record<CalibrationTarget, [keyof EstufaSettings, number]> = {
        soil_dry: ['soilDryMv', st.soil.mv],
        soil_wet: ['soilWetMv', st.soil.mv],
        light_dark: ['lightDarkMv', st.light.mv],
        light_bright: ['lightBrightMv', st.light.mv],
      };
      const entry = map[target];
      if (!entry) return { ok: false, error: 'Alvo de calibração inválido.' };
      const next = { ...settings, [entry[0]]: entry[1] };
      const err = validate(next);
      if (err) return { ok: false, error: err };
      settings = next;
      return { ok: true };
    },
    saveConfig(changes: Partial<EstufaSettings>): ActionResult {
      const next = { ...settings };
      for (const key of Object.keys(DEFAULTS) as (keyof EstufaSettings)[]) {
        if (changes[key] === undefined) continue;
        if (!Number.isInteger(changes[key])) return { ok: false, error: `${key} precisa ser um número inteiro.` };
        next[key] = changes[key];
      }
      const err = validate(next);
      if (err) return { ok: false, error: err };
      settings = next;
      return { ok: true };
    },
    factoryReset(): ActionResult {
      settings = { ...DEFAULTS };
      sim.mode = 'auto';
      return { ok: true };
    },
  };
}
