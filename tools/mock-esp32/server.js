// Simulador do ESP32 da estufa.
// Implementa o mesmo contrato da API do firmware (docs/api.md) com sensores
// simulados: o solo seca com o tempo, a bomba molha o solo e gasta água do
// reservatório, a luz segue um ciclo e o clima varia aos poucos.
//
// Uso:  npm install  &&  npm start        (porta padrão 8080)
//       PORT=80 npm start                 (outra porta)
// No app, use o IP do seu computador, ex.: 192.168.0.10:8080

import http from 'node:http';
import os from 'node:os';
import { WebSocketServer } from 'ws';

const PORT = Number(process.env.PORT ?? 8080);
const TICK_MS = 500;
const ELECTRODE_PERCENT = [20, 50, 90];
const USER_MAX_ON_S = 45;
const ABSOLUTE_MAX_ON_S = 60;

const DEFAULTS = {
  soilMinPct: 35, soilTargetPct: 60, pulseS: 5, soakS: 30, maxPulses: 6,
  cooldownMin: 30, maxManualS: 20, reservoirMinElec: 1,
  lightLowPct: 30, lightHighPct: 85, tempMinC: 15, tempMaxC: 32,
  airHumMinPct: 40, airHumMaxPct: 85,
  soilDryMv: 3000, soilWetMv: 1200, lightDarkMv: 50, lightBrightMv: 3000,
  electrodeThreshMv: 500,
};

let settings = { ...DEFAULTS };
const startedAt = Date.now();

// ---------------------------------------------------------------- simulação
const sim = {
  soil: 42, water: 70, temp: 25, hum: 60, lightPhase: 0,
  pumpOn: false, mode: 'auto', state: 'monitorando',
  stateSince: Date.now(), pumpOnSince: 0, manualMs: 0,
  pulses: 0, cycleStartSoil: 0, lastCycleEnd: 0, lastPumpOff: 0,
  cycles: 0, faultLatched: false,
};

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const round1 = (v) => Math.round(v * 10) / 10;

function electrodes() {
  return ELECTRODE_PERCENT.map((p) => sim.water >= p);
}
function wetCount() {
  return electrodes().filter(Boolean).length;
}
function pumpAllowed() {
  return wetCount() >= settings.reservoirMinElec;
}
function inCooldown(now) {
  return sim.lastCycleEnd !== 0 && now - sim.lastCycleEnd < settings.cooldownMin * 60000;
}
function setState(st, now) {
  if (st !== sim.state) {
    console.log(`[IRRIG] ${sim.state} -> ${st}`);
    sim.state = st;
    sim.stateSince = now;
  }
}
function pump(on, now) {
  if (on === sim.pumpOn) return;
  sim.pumpOn = on;
  if (on) sim.pumpOnSince = now;
  else sim.lastPumpOff = now;
  console.log(`[BOMBA] ${on ? 'LIGADA' : 'DESLIGADA'}`);
}
function endCycle(now) {
  pump(false, now);
  sim.lastCycleEnd = now;
  sim.pulses = 0;
}

function physics(dt) {
  const s = dt / 1000;
  sim.soil = clamp(sim.soil - 0.02 * s + (Math.random() - 0.5) * 0.1, 0, 100);
  if (sim.pumpOn) {
    sim.soil = clamp(sim.soil + 1.2 * s, 0, 100);
    sim.water = clamp(sim.water - 0.6 * s, 0, 100);
  }
  sim.temp = clamp(sim.temp + (Math.random() - 0.5) * 0.1, 10, 40);
  sim.hum = clamp(sim.hum + (Math.random() - 0.5) * 0.3, 20, 95);
  sim.lightPhase += s / 120; // ciclo de "dia" de ~12 min
}

function irrigation(now) {
  if (sim.pumpOn) {
    if (now - sim.pumpOnSince >= ABSOLUTE_MAX_ON_S * 1000) {
      endCycle(now); sim.faultLatched = true; return setState('falha', now);
    }
    if (!pumpAllowed()) { endCycle(now); return setState('sem_agua', now); }
    if (sim.mode === 'off') { endCycle(now); return setState('monitorando', now); }
  }
  const elapsed = now - sim.stateSince;

  if (sim.state === 'manual') {
    if (elapsed >= sim.manualMs) { endCycle(now); setState('monitorando', now); }
    return;
  }
  if (sim.state === 'irrigando') {
    if (elapsed >= settings.pulseS * 1000) {
      pump(false, now); sim.pulses++; setState('infiltrando', now);
    }
    return;
  }
  if (sim.state === 'infiltrando') {
    if (elapsed < settings.soakS * 1000) return;
    if (sim.soil >= settings.soilTargetPct) { endCycle(now); return setState('intervalo', now); }
    if (sim.pulses >= settings.maxPulses) {
      const rose = sim.soil - sim.cycleStartSoil >= 3;
      endCycle(now);
      if (!rose) sim.faultLatched = true;
      return setState(rose ? 'intervalo' : 'falha', now);
    }
    if (!pumpAllowed()) { endCycle(now); return setState('sem_agua', now); }
    pump(true, now);
    return setState('irrigando', now);
  }

  if (sim.mode !== 'auto') return setState('monitorando', now);
  if (sim.faultLatched) return setState('falha', now);
  if (inCooldown(now)) return setState('intervalo', now);
  if (sim.soil < settings.soilMinPct) {
    if (!pumpAllowed()) return setState('sem_agua', now);
    sim.pulses = 0; sim.cycleStartSoil = sim.soil; sim.cycles++;
    pump(true, now);
    return setState('irrigando', now);
  }
  setState('monitorando', now);
}

// --------------------------------------------------------------- status JSON
function lightPercent() {
  return clamp(50 + 45 * Math.sin(sim.lightPhase * 2 * Math.PI), 0, 100);
}

function buildStatus() {
  const now = Date.now();
  const lp = lightPercent();
  const lightLevel = lp < settings.lightLowPct ? 'baixa' : lp > settings.lightHighPct ? 'alta' : 'adequada';
  const wc = wetCount();
  const level = wc === 0 ? 'vazio' : wc >= ELECTRODE_PERCENT.length ? 'cheio' : wc === 1 ? 'baixo' : 'medio';
  const percent = wc === 0 ? 0 : ELECTRODE_PERCENT[wc - 1];

  const alerts = [];
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
    ? Math.floor((settings.cooldownMin * 60000 - (now - sim.lastCycleEnd)) / 1000) : 0;

  return {
    device: {
      name: 'estufa-simulador', firmware: '1.0.0-mock',
      uptimeS: Math.floor((now - startedAt) / 1000),
      wifiMode: 'sta', ip: localIp(), rssi: -55, freeHeap: 200000,
    },
    soil: {
      percent: round1(sim.soil),
      mv: Math.round(settings.soilDryMv - (sim.soil / 100) * (settings.soilDryMv - settings.soilWetMv)),
      valid: true,
    },
    air: { temperature: round1(sim.temp), humidity: round1(sim.hum), valid: true },
    light: {
      percent: round1(lp), lux: Math.round(lp * lp * 0.5), luxEstimated: true,
      mv: lightMv, level: lightLevel, valid: true,
    },
    reservoir: {
      percent, meter: Math.round((percent / 100) * 240), meterFull: 240,
      level, valid: true, pumpAllowed: pumpAllowed(),
      electrodes: electrodes(), electrodesMv: electrodes().map((w) => (w ? 1800 : 20)),
    },
    pump: {
      on: sim.pumpOn, mode: sim.mode, state: sim.state,
      pulsesInCycle: sim.pulses, maxPulses: settings.maxPulses,
      cooldownRemainingS: cooldown,
      lastIrrigationAgoS: sim.pumpOn ? 0 : sim.lastPumpOff ? Math.floor((now - sim.lastPumpOff) / 1000) : null,
      cyclesSinceBoot: sim.cycles,
    },
    overall,
    alerts,
    ts: now - startedAt,
  };
}

function buildConfig() {
  return {
    ...settings,
    limits: { userMaxOnS: USER_MAX_ON_S, absoluteMaxOnS: ABSOLUTE_MAX_ON_S, electrodeCount: ELECTRODE_PERCENT.length },
  };
}

function validate(s) {
  if (s.soilMinPct >= s.soilTargetPct) return 'soilMinPct deve ser menor que soilTargetPct';
  if (s.soilTargetPct > 95 || s.soilMinPct < 5) return 'limites de solo devem ficar entre 5% e 95%';
  if (s.pulseS < 1 || s.pulseS > USER_MAX_ON_S) return `pulseS deve ficar entre 1 e ${USER_MAX_ON_S}`;
  if (s.soakS < 5 || s.soakS > 600) return 'soakS deve ficar entre 5 e 600';
  if (s.maxPulses < 1 || s.maxPulses > 20) return 'maxPulses deve ficar entre 1 e 20';
  if (s.cooldownMin < 1 || s.cooldownMin > 1440) return 'cooldownMin deve ficar entre 1 e 1440';
  if (s.maxManualS < 1 || s.maxManualS > USER_MAX_ON_S) return `maxManualS deve ficar entre 1 e ${USER_MAX_ON_S}`;
  if (s.reservoirMinElec < 1 || s.reservoirMinElec > ELECTRODE_PERCENT.length) return 'reservoirMinElec invalido';
  if (s.lightLowPct >= s.lightHighPct || s.lightHighPct > 100) return 'lightLowPct deve ser menor que lightHighPct (max 100)';
  if (s.tempMinC >= s.tempMaxC) return 'tempMinC deve ser menor que tempMaxC';
  if (s.airHumMinPct >= s.airHumMaxPct || s.airHumMaxPct > 100) return 'airHumMinPct deve ser menor que airHumMaxPct (max 100)';
  if (s.soilDryMv <= s.soilWetMv + 200) return 'soilDryMv deve ser pelo menos 200 mV maior que soilWetMv';
  if (s.lightBrightMv === s.lightDarkMv) return 'lightBrightMv e lightDarkMv nao podem ser iguais';
  if (s.electrodeThreshMv < 50 || s.electrodeThreshMv > 3000) return 'electrodeThreshMv deve ficar entre 50 e 3000';
  return null;
}

// ------------------------------------------------------------------- HTTP
const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

function send(res, code, body) {
  res.writeHead(code, { 'Content-Type': 'application/json', ...CORS });
  res.end(JSON.stringify(body));
}
const ok = (res, message) => send(res, 200, message ? { ok: true, message } : { ok: true });
const fail = (res, code, error) => send(res, code, { ok: false, error });

function readBody(req) {
  return new Promise((resolve) => {
    let raw = '';
    req.on('data', (c) => (raw += c));
    req.on('end', () => {
      try { resolve(raw ? JSON.parse(raw) : {}); } catch { resolve(undefined); }
    });
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x').pathname;
  if (req.method === 'OPTIONS') { res.writeHead(204, CORS); return res.end(); }

  if (req.method === 'GET') {
    if (url === '/api/ping') return send(res, 200, { ok: true, name: 'estufa-simulador', firmware: '1.0.0-mock' });
    if (url === '/api/status') return send(res, 200, buildStatus());
    if (url === '/api/config') return send(res, 200, buildConfig());
    if (url === '/') {
      res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8', ...CORS });
      return res.end('Estufa ESP32 (SIMULADOR)\nAPI: /api/status  /api/config  /api/ping\nWebSocket: /ws\n');
    }
    return fail(res, 404, 'rota nao encontrada');
  }

  if (req.method !== 'POST') return fail(res, 404, 'rota nao encontrada');
  const body = await readBody(req);
  if (body === undefined || typeof body !== 'object' || Array.isArray(body)) {
    return fail(res, 400, 'corpo deve ser um objeto JSON');
  }
  const now = Date.now();

  switch (url) {
    case '/api/config': {
      const next = { ...settings };
      for (const key of Object.keys(DEFAULTS)) {
        if (body[key] === undefined) continue;
        if (!Number.isInteger(body[key])) return fail(res, 400, `${key} deve ser um numero inteiro`);
        next[key] = body[key];
      }
      const err = validate(next);
      if (err) return fail(res, 400, err);
      settings = next;
      return send(res, 200, buildConfig());
    }
    case '/api/pump': {
      if (body.action === 'off') {
        if (sim.pumpOn) { endCycle(now); setState('monitorando', now); }
        broadcast();
        return ok(res);
      }
      if (body.action !== 'on') return fail(res, 400, 'action deve ser "on" ou "off"');
      if (sim.mode === 'off') return fail(res, 409, 'bomba em modo desligado');
      if (!pumpAllowed()) return fail(res, 409, 'reservatorio com nivel insuficiente');
      const seconds = body.seconds ?? 5;
      if (!Number.isInteger(seconds) || seconds < 1 || seconds > settings.maxManualS) {
        return fail(res, 400, `seconds deve ficar entre 1 e ${settings.maxManualS}`);
      }
      sim.manualMs = seconds * 1000;
      sim.pulses = 0;
      pump(true, now);
      sim.state = 'manual';
      sim.stateSince = now;
      broadcast();
      return ok(res);
    }
    case '/api/mode': {
      if (!['auto', 'manual', 'off'].includes(body.mode)) {
        return fail(res, 400, 'mode deve ser "auto", "manual" ou "off"');
      }
      if (body.mode !== sim.mode) {
        sim.mode = body.mode;
        if (sim.pumpOn) endCycle(now);
        setState('monitorando', now);
      }
      broadcast();
      return ok(res);
    }
    case '/api/calibrate': {
      const st = buildStatus();
      const map = {
        soil_dry: ['soilDryMv', st.soil.mv], soil_wet: ['soilWetMv', st.soil.mv],
        light_dark: ['lightDarkMv', st.light.mv], light_bright: ['lightBrightMv', st.light.mv],
      };
      const entry = map[body.target];
      if (!entry) return fail(res, 400, 'target deve ser soil_dry, soil_wet, light_dark ou light_bright');
      const next = { ...settings, [entry[0]]: entry[1] };
      if (!validate(next)) settings = next;
      else console.log('[CAL] rejeitada:', validate(next));
      return ok(res, 'calibracao sera aplicada com a leitura atual');
    }
    case '/api/fault/reset':
      sim.faultLatched = false;
      if (sim.state === 'falha') setState('monitorando', now);
      return ok(res);
    case '/api/factory-reset':
      settings = { ...DEFAULTS };
      sim.mode = 'auto';
      return ok(res);
    // Extras SÓ do simulador, para testar situações sem esperar
    case '/sim/refill':
      sim.water = 100;
      return ok(res, 'reservatorio cheio');
    case '/sim/drain':
      sim.water = 10;
      return ok(res, 'reservatorio quase vazio');
    case '/sim/dry':
      sim.soil = 20;
      sim.lastCycleEnd = 0;
      return ok(res, 'solo seco, sem intervalo de seguranca');
    default:
      return fail(res, 404, 'rota nao encontrada');
  }
});

// -------------------------------------------------------------- WebSocket
const wss = new WebSocketServer({ server, path: '/ws' });
wss.on('connection', (socket, req) => {
  console.log(`[WS] app conectado (${req.socket.remoteAddress})`);
  socket.send(JSON.stringify(buildStatus()));
  socket.on('close', () => console.log('[WS] app desconectado'));
});

function broadcast() {
  const msg = JSON.stringify(buildStatus());
  for (const c of wss.clients) if (c.readyState === 1) c.send(msg);
}

let last = Date.now();
let lastBroadcast = 0;
let prevKey = '';
setInterval(() => {
  const now = Date.now();
  physics(now - last);
  last = now;
  irrigation(now);
  const key = `${sim.pumpOn}|${sim.state}|${sim.mode}`;
  if (key !== prevKey || now - lastBroadcast >= 2000) {
    prevKey = key;
    lastBroadcast = now;
    broadcast();
  }
}, TICK_MS);

function localIp() {
  for (const list of Object.values(os.networkInterfaces())) {
    for (const i of list ?? []) {
      if (i.family === 'IPv4' && !i.internal) return i.address;
    }
  }
  return '127.0.0.1';
}

server.listen(PORT, () => {
  console.log(`Simulador da estufa rodando.`);
  console.log(`  No app use:  ${localIp()}:${PORT}`);
  console.log(`  Navegador:   http://localhost:${PORT}/api/status`);
  console.log(`  Atalhos:     POST /sim/dry  /sim/drain  /sim/refill`);
});
