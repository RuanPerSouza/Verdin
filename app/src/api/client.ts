import type {
  ActionResult,
  CalibrationTarget,
  EstufaConfig,
  EstufaSettings,
  EstufaStatus,
  PumpMode,
} from './types';

const REQUEST_TIMEOUT_MS = 8000;

// Aceita "192.168.0.50", "http://192.168.0.50/", "estufa.local" ou "10.0.0.5:8080".
// A porta 8080 é do simulador; o ESP32 real responde na porta 80.
export function normalizeHost(input: string): string {
  return input
    .trim()
    .replace(/^(https?|wss?):\/\//i, '')
    .replace(/\/+$/, '')
    .replace(/:(80|8080)$/i, '');
}

export function httpUrl(host: string, path: string): string {
  return `http://${host}${path}`;
}

export function wsUrl(host: string): string {
  return `ws://${host}/ws`;
}

async function request<T>(host: string, path: string, init?: RequestInit): Promise<T> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  try {
    const res = await fetch(httpUrl(host, path), {
      ...init,
      headers: { 'Content-Type': 'application/json', ...init?.headers },
      signal: controller.signal,
    });
    const body = await res.json().catch(() => null);
    if (!res.ok) {
      const message = body && typeof body.error === 'string' ? body.error : `HTTP ${res.status}`;
      throw new Error(message);
    }
    return body as T;
  } catch (e) {
    if (e instanceof Error && e.name === 'AbortError') {
      throw new Error('O ESP32 não respondeu (tempo esgotado)');
    }
    if (e instanceof TypeError) {
      throw new Error('Sem conexão com o ESP32. Confira o IP e a rede Wi-Fi.');
    }
    throw e;
  } finally {
    clearTimeout(timer);
  }
}

function post<T>(host: string, path: string, body?: object): Promise<T> {
  return request<T>(host, path, { method: 'POST', body: JSON.stringify(body ?? {}) });
}

async function action(promise: Promise<unknown>): Promise<ActionResult> {
  try {
    await promise;
    return { ok: true };
  } catch (e) {
    return { ok: false, error: e instanceof Error ? e.message : String(e) };
  }
}

export const estufaApi = {
  ping: (host: string) =>
    request<{ ok: boolean; name: string; firmware: string }>(host, '/api/ping'),
  status: (host: string) => request<EstufaStatus>(host, '/api/status'),
  config: (host: string) => request<EstufaConfig>(host, '/api/config'),

  saveConfig: (host: string, changes: Partial<EstufaSettings>) =>
    post<EstufaConfig>(host, '/api/config', changes),

  pumpOn: (host: string, seconds: number) =>
    action(post(host, '/api/pump', { action: 'on', seconds })),
  pumpOff: (host: string) => action(post(host, '/api/pump', { action: 'off' })),
  setMode: (host: string, mode: PumpMode) => action(post(host, '/api/mode', { mode })),
  calibrate: (host: string, target: CalibrationTarget) =>
    action(post(host, '/api/calibrate', { target })),
  resetFault: (host: string) => action(post(host, '/api/fault/reset')),
  factoryReset: (host: string) => action(post(host, '/api/factory-reset')),
};
