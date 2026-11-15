import type { AlertCode, IrrigationState, LightLevel, Overall, PumpMode, ReservoirLevel } from './api/types';

// Cores tiradas da logo Verdin: vaso e nome em verde-escuro, folhas em verde
// médio e a gota em azul, sobre fundo branco.
export const colors = {
  bg: '#f4f8f3',
  card: '#ffffff',
  cardBorder: '#d3e4d4',
  text: '#185830',
  muted: '#5c7a62',
  green: '#1e6b38',
  leaf: '#4a942e',
  yellow: '#c48a12',
  red: '#c63d3d',
  blue: '#1f8cc4',
  input: '#eaf3ea',
  onGreen: '#ffffff',
};

export const overallInfo: Record<Overall, { label: string; color: string }> = {
  ok: { label: 'Tudo certo', color: colors.green },
  atencao: { label: 'Atenção', color: colors.yellow },
  alerta: { label: 'Alerta', color: colors.red },
};

export const alertInfo: Record<AlertCode, { text: string; severe: boolean }> = {
  BOMBA_FALHA: {
    text: 'A bomba ligou, mas o solo não ficou mais úmido. Verifique mangueira, bomba e sensor. Toque em "Reconhecer falha" depois de resolver.',
    severe: true,
  },
  RESERVATORIO_VAZIO: { text: 'Reservatório vazio: a bomba está bloqueada.', severe: true },
  RESERVATORIO_BAIXO: { text: 'Reservatório com pouca água. Reabasteça.', severe: true },
  SENSOR_SOLO: { text: 'Erro no sensor de umidade do solo: irrigação automática pausada.', severe: true },
  SENSOR_NIVEL: { text: 'Leitura inconsistente dos eletrodos de nível.', severe: true },
  SENSOR_AR: { text: 'Sem leitura do DHT11 (temperatura/umidade do ar).', severe: false },
  SENSOR_LUZ: { text: 'Leitura instável do sensor de luz (LDR).', severe: false },
  SOLO_SECO: { text: 'Solo abaixo do limite mínimo.', severe: false },
  TEMPERATURA_ALTA: { text: 'Temperatura acima da faixa ideal.', severe: false },
  TEMPERATURA_BAIXA: { text: 'Temperatura abaixo da faixa ideal.', severe: false },
  UMIDADE_AR_ALTA: { text: 'Umidade do ar acima da faixa ideal.', severe: false },
  UMIDADE_AR_BAIXA: { text: 'Umidade do ar abaixo da faixa ideal.', severe: false },
  LUZ_BAIXA: { text: 'Pouca luz para a planta.', severe: false },
  LUZ_ALTA: { text: 'Luz muito intensa.', severe: false },
};

export const stateLabel: Record<IrrigationState, string> = {
  monitorando: 'Monitorando',
  irrigando: 'Irrigando',
  infiltrando: 'Aguardando a água infiltrar',
  intervalo: 'Intervalo de segurança',
  manual: 'Irrigação manual',
  sem_agua: 'Bloqueada: sem água',
  erro_sensor: 'Pausada: erro no sensor',
  falha: 'Falha na irrigação',
};

export const modeLabel: Record<PumpMode, string> = {
  auto: 'Automático',
  manual: 'Manual',
  off: 'Desligada',
};

export const lightLabel: Record<LightLevel, string> = {
  baixa: 'Baixa',
  adequada: 'Adequada',
  alta: 'Alta',
};

export const reservoirLabel: Record<ReservoirLevel, string> = {
  vazio: 'Vazio',
  baixo: 'Baixo',
  medio: 'Médio',
  cheio: 'Cheio',
};

export function formatDuration(totalSeconds: number): string {
  const s = Math.max(0, Math.floor(totalSeconds));
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m} min`;
  const h = Math.floor(m / 60);
  return `${h}h ${m % 60}min`;
}
