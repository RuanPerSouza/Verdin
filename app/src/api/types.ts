// Tipos que espelham o JSON enviado pelo ESP32 (ver docs/api.md).
// Se mudar o firmware, atualize aqui também.

export type PumpMode = 'auto' | 'manual' | 'off';

export type IrrigationState =
  | 'monitorando'
  | 'irrigando'
  | 'infiltrando'
  | 'intervalo'
  | 'manual'
  | 'sem_agua'
  | 'erro_sensor'
  | 'falha';

export type LightLevel = 'baixa' | 'adequada' | 'alta';
export type ReservoirLevel = 'vazio' | 'baixo' | 'medio' | 'cheio';
export type Overall = 'ok' | 'atencao' | 'alerta';

export type AlertCode =
  | 'BOMBA_FALHA'
  | 'RESERVATORIO_VAZIO'
  | 'RESERVATORIO_BAIXO'
  | 'SENSOR_SOLO'
  | 'SENSOR_NIVEL'
  | 'SENSOR_AR'
  | 'SENSOR_LUZ'
  | 'SOLO_SECO'
  | 'TEMPERATURA_ALTA'
  | 'TEMPERATURA_BAIXA'
  | 'UMIDADE_AR_ALTA'
  | 'UMIDADE_AR_BAIXA'
  | 'LUZ_BAIXA'
  | 'LUZ_ALTA';

export interface EstufaStatus {
  device: {
    name: string;
    firmware: string;
    uptimeS: number;
    wifiMode: 'sta' | 'ap';
    ip: string;
    rssi: number;
    freeHeap: number;
  };
  soil: { percent: number; mv: number; valid: boolean };
  air: { temperature: number; humidity: number; valid: boolean };
  light: {
    percent: number;
    lux: number | null;
    luxEstimated: boolean;
    mv: number;
    level: LightLevel;
    valid: boolean;
  };
  reservoir: {
    percent: number;
    meter: number;
    meterFull: number;
    level: ReservoirLevel;
    valid: boolean;
    pumpAllowed: boolean;
    electrodes: boolean[];
    electrodesMv: number[];
  };
  pump: {
    on: boolean;
    mode: PumpMode;
    state: IrrigationState;
    pulsesInCycle: number;
    maxPulses: number;
    cooldownRemainingS: number;
    lastIrrigationAgoS: number | null;
    cyclesSinceBoot: number;
  };
  overall: Overall;
  alerts: AlertCode[];
  ts: number;
}

export interface EstufaSettings {
  soilMinPct: number;
  soilTargetPct: number;
  pulseS: number;
  soakS: number;
  maxPulses: number;
  cooldownMin: number;
  maxManualS: number;
  reservoirMinElec: number;
  lightLowPct: number;
  lightHighPct: number;
  tempMinC: number;
  tempMaxC: number;
  airHumMinPct: number;
  airHumMaxPct: number;
  soilDryMv: number;
  soilWetMv: number;
  lightDarkMv: number;
  lightBrightMv: number;
  electrodeThreshMv: number;
}

export interface EstufaConfig extends EstufaSettings {
  limits: { userMaxOnS: number; absoluteMaxOnS: number; electrodeCount: number };
}

export type CalibrationTarget = 'soil_dry' | 'soil_wet' | 'light_dark' | 'light_bright';

export type ActionResult = { ok: true } | { ok: false; error: string };
