import AsyncStorage from '@react-native-async-storage/async-storage';
import { useCallback, useEffect, useRef, useState } from 'react';

import { estufaApi, normalizeHost, wsUrl } from '../api/client';
import type {
  ActionResult,
  CalibrationTarget,
  EstufaConfig,
  EstufaSettings,
  EstufaStatus,
  PumpMode,
} from '../api/types';
import { createLocalSim } from '../sim/localSim';

const HOST_KEY = 'estufa.host';
const POLL_MS = 4000; // HTTP segue mesmo com o WebSocket aberto
const STALE_MS = 20000; // uma falha curta não marca o app como desconectado
const MAX_RECONNECT_MS = 10000;

export type LinkState = 'connecting' | 'online' | 'offline';

export function useEstufa() {
  const [host, setHostState] = useState<string | null>(null);
  const [hostLoaded, setHostLoaded] = useState(false);
  const [status, setStatus] = useState<EstufaStatus | null>(null);
  const [config, setConfig] = useState<EstufaConfig | null>(null);
  const [link, setLink] = useState<LinkState>('connecting');
  const [lastUpdate, setLastUpdate] = useState<number | null>(null);
  const [reconnectKey, setReconnectKey] = useState(0);
  const [demo, setDemo] = useState(false);

  const lastUpdateRef = useRef(0);
  const simRef = useRef(createLocalSim());

  useEffect(() => {
    AsyncStorage.getItem(HOST_KEY)
      .then((saved) => setHostState(saved))
      .finally(() => setHostLoaded(true));
  }, []);

  const receive = useCallback((s: EstufaStatus) => {
    const now = Date.now();
    lastUpdateRef.current = now;
    setStatus(s);
    setLastUpdate(now);
    setLink('online');
  }, []);

  // Conexão em tempo real: WebSocket com reconexão automática + polling HTTP
  // como reserva enquanto o WebSocket estiver fora.
  useEffect(() => {
    if (!demo) return;
    const sim = simRef.current;
    sim.reset();
    const push = () => {
      receive(sim.status());
      const next = sim.config();
      setConfig((prev) => (prev && JSON.stringify(prev) === JSON.stringify(next) ? prev : next));
    };
    push();
    const timer = setInterval(() => {
      sim.tick(500);
      push();
    }, 500);
    return () => clearInterval(timer);
  }, [demo, receive]);

  useEffect(() => {
    if (!host || demo) return;

    let cancelled = false;
    let ws: WebSocket | null = null;
    let retry = 0;
    let reconnectTimer: ReturnType<typeof setTimeout> | undefined;

    lastUpdateRef.current = 0;

    const openWs = () => {
      if (cancelled) return;
      ws = new WebSocket(wsUrl(host));
      ws.onopen = () => {
        retry = 0;
      };
      ws.onmessage = (event) => {
        try {
          receive(JSON.parse(String(event.data)) as EstufaStatus);
        } catch {
          // mensagem malformada: ignora e espera a próxima
        }
      };
      ws.onclose = () => {
        if (cancelled) return;
        const delay = Math.min(1000 * 2 ** retry, MAX_RECONNECT_MS);
        retry++;
        reconnectTimer = setTimeout(openWs, delay);
      };
      ws.onerror = () => ws?.close();
    };

    const poll = async () => {
      try {
        receive(await estufaApi.status(host));
      } catch {
        // a checagem de "dados velhos" abaixo marca como offline
      }
    };

    const loadConfig = () => {
      estufaApi
        .config(host)
        .then((c) => {
          if (cancelled) return;
          // mantém o mesmo objeto se nada mudou (não apaga edições na tela de Ajustes)
          setConfig((prev) => (JSON.stringify(prev) === JSON.stringify(c) ? prev : c));
        })
        .catch(() => undefined);
    };

    openWs();
    poll();
    loadConfig();

    const pollTimer = setInterval(poll, POLL_MS);
    const startedAt = Date.now();
    const staleTimer = setInterval(() => {
      const since = lastUpdateRef.current || startedAt;
      if (Date.now() - since > STALE_MS) setLink('offline');
    }, 1000);
    // Recarrega a configuração ao voltar a ficar online
    const configTimer = setInterval(() => {
      if (lastUpdateRef.current && Date.now() - lastUpdateRef.current < STALE_MS) loadConfig();
    }, 30000);

    return () => {
      cancelled = true;
      clearTimeout(reconnectTimer);
      clearInterval(pollTimer);
      clearInterval(staleTimer);
      clearInterval(configTimer);
      if (ws) {
        ws.onclose = null;
        ws.close();
      }
    };
  }, [host, demo, reconnectKey, receive]);

  // ------------------------------------------------------------ conexão
  const connect = useCallback(async (input: string): Promise<ActionResult> => {
    const h = normalizeHost(input);
    if (!h) return { ok: false, error: 'Digite o IP do ESP32' };
    let lastError = 'Sem conexão com o ESP32. Confira o IP e a rede Wi-Fi.';
    for (let attempt = 0; attempt < 3; attempt++) {
      try {
        await estufaApi.ping(h);
        lastError = '';
        break;
      } catch (e) {
        lastError = e instanceof Error ? e.message : String(e);
      }
    }
    if (lastError) return { ok: false, error: lastError };
    await AsyncStorage.setItem(HOST_KEY, h);
    setStatus(null);
    setConfig(null);
    setLink('connecting');
    setHostState(h);
    return { ok: true };
  }, []);

  const startDemo = useCallback(() => {
    setStatus(null);
    setConfig(null);
    setLink('online');
    setDemo(true);
  }, []);

  const disconnect = useCallback(async () => {
    if (demo) {
      setDemo(false);
      setStatus(null);
      setConfig(null);
      setLink('connecting');
      return;
    }
    await AsyncStorage.removeItem(HOST_KEY);
    setHostState(null);
    setStatus(null);
    setConfig(null);
    setLink('connecting');
  }, [demo]);

  const reconnect = useCallback(() => {
    if (demo) return;
    setLink('connecting');
    setReconnectKey((k) => k + 1);
  }, [demo]);

  // ------------------------------------------------------------ comandos
  // Após cada comando, busca o status na hora para a tela reagir rápido.
  const withRefresh = useCallback(
    async (fn: (h: string) => Promise<ActionResult>): Promise<ActionResult> => {
      if (!host) return { ok: false, error: 'Não conectado' };
      const result = await fn(host);
      estufaApi.status(host).then(receive).catch(() => undefined);
      return result;
    },
    [host, receive],
  );

  const applyDemo = useCallback(
    (result: ActionResult) => {
      if (result.ok) {
        receive(simRef.current.status());
        setConfig(simRef.current.config());
      }
      return result;
    },
    [receive],
  );

  const pumpOn = useCallback(
    (seconds: number) =>
      demo ? Promise.resolve(applyDemo(simRef.current.pumpOn(seconds))) : withRefresh((h) => estufaApi.pumpOn(h, seconds)),
    [demo, applyDemo, withRefresh],
  );
  const pumpOff = useCallback(
    () => (demo ? Promise.resolve(applyDemo(simRef.current.pumpOff())) : withRefresh(estufaApi.pumpOff)),
    [demo, applyDemo, withRefresh],
  );
  const setMode = useCallback(
    (mode: PumpMode) =>
      demo ? Promise.resolve(applyDemo(simRef.current.setMode(mode))) : withRefresh((h) => estufaApi.setMode(h, mode)),
    [demo, applyDemo, withRefresh],
  );
  const resetFault = useCallback(
    () => (demo ? Promise.resolve(applyDemo(simRef.current.resetFault())) : withRefresh(estufaApi.resetFault)),
    [demo, applyDemo, withRefresh],
  );

  const calibrate = useCallback(
    async (target: CalibrationTarget) => {
      if (demo) return applyDemo(simRef.current.calibrate(target));
      const r = await withRefresh((h) => estufaApi.calibrate(h, target));
      // o ESP32 aplica a calibração na próxima volta do loop
      if (r.ok && host) setTimeout(() => estufaApi.config(host).then(setConfig).catch(() => undefined), 600);
      return r;
    },
    [demo, applyDemo, withRefresh, host],
  );

  const saveConfig = useCallback(
    async (changes: Partial<EstufaSettings>): Promise<ActionResult> => {
      if (demo) return applyDemo(simRef.current.saveConfig(changes));
      if (!host) return { ok: false, error: 'Não conectado' };
      try {
        setConfig(await estufaApi.saveConfig(host, changes));
        return { ok: true };
      } catch (e) {
        return { ok: false, error: e instanceof Error ? e.message : String(e) };
      }
    },
    [demo, applyDemo, host],
  );

  const factoryReset = useCallback(async () => {
    if (demo) return applyDemo(simRef.current.factoryReset());
    const r = await withRefresh(estufaApi.factoryReset);
    if (r.ok && host) setTimeout(() => estufaApi.config(host).then(setConfig).catch(() => undefined), 600);
    return r;
  }, [demo, applyDemo, withRefresh, host]);

  return {
    host,
    demo,
    hostLoaded,
    status,
    config,
    link,
    lastUpdate,
    connect,
    startDemo,
    disconnect,
    reconnect,
    pumpOn,
    pumpOff,
    setMode,
    resetFault,
    calibrate,
    saveConfig,
    factoryReset,
  };
}

export type Estufa = ReturnType<typeof useEstufa>;
