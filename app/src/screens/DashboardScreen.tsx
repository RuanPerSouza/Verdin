import { useState } from 'react';
import { Alert, StyleSheet, Text, View } from 'react-native';

import type { ActionResult, EstufaStatus, PumpMode } from '../api/types';
import { Button, Card, ProgressBar, SectionTitle, Segmented } from '../components/ui';
import type { Estufa } from '../hooks/useEstufa';
import {
  alertInfo,
  colors,
  formatDuration,
  lightLabel,
  modeLabel,
  reservoirLabel,
  stateLabel,
} from '../labels';

function showError(r: ActionResult) {
  if (!r.ok) Alert.alert('Não foi possível', r.error);
}

function Metric({
  title,
  value,
  unit,
  detail,
  invalid,
  half,
  children,
}: {
  title: string;
  value: string;
  unit?: string;
  detail?: string;
  invalid?: boolean;
  half?: boolean;
  children?: React.ReactNode;
}) {
  return (
    <Card style={half ? styles.half : undefined}>
      <Text style={styles.metricTitle}>{title}</Text>
      {invalid ? (
        <Text style={[styles.metricValue, { color: colors.red, fontSize: 20 }]}>Erro no sensor</Text>
      ) : (
        <Text style={styles.metricValue}>
          {value}
          {unit && <Text style={styles.metricUnit}> {unit}</Text>}
        </Text>
      )}
      {detail && !invalid && <Text style={styles.metricDetail}>{detail}</Text>}
      {!invalid && children}
    </Card>
  );
}

function Alerts({ status }: { status: EstufaStatus }) {
  if (status.alerts.length === 0) return null;
  return (
    <>
      <SectionTitle>Alertas</SectionTitle>
      {status.alerts.map((code) => {
        const info = alertInfo[code] ?? { text: code, severe: false };
        return (
          <View
            key={code}
            style={[styles.alert, { borderLeftColor: info.severe ? colors.red : colors.yellow }]}
          >
            <Text style={styles.alertText}>{info.text}</Text>
          </View>
        );
      })}
    </>
  );
}

function Reservoir({ status }: { status: EstufaStatus }) {
  const r = status.reservoir;
  const color = r.level === 'vazio' || r.level === 'baixo' ? colors.red : colors.blue;
  return (
    <Metric
      title="Reservatório"
      value={`${r.percent}%`}
      detail={`medida ${r.meter} de ${r.meterFull} · ${reservoirLabel[r.level]} · ${
        r.pumpAllowed ? 'bomba liberada' : 'bomba bloqueada'
      }`}
      invalid={!r.valid}
      half
    >
      <View style={styles.electrodes}>
        {/* eletrodos de cima para baixo */}
        {[...r.electrodes].reverse().map((wet, i) => (
          <View key={i} style={[styles.electrode, { backgroundColor: wet ? color : colors.input }]} />
        ))}
      </View>
    </Metric>
  );
}

function PumpPanel({ estufa, status }: { estufa: Estufa; status: EstufaStatus }) {
  const [busy, setBusy] = useState<string | null>(null);
  const p = status.pump;
  const maxManual = estufa.config?.maxManualS ?? 20;
  const canRun = p.mode !== 'off' && status.reservoir.pumpAllowed;

  const run = async (key: string, fn: () => Promise<ActionResult>) => {
    setBusy(key);
    showError(await fn());
    setBusy(null);
  };

  let info = stateLabel[p.state] ?? p.state;
  if (p.state === 'irrigando' || p.state === 'infiltrando') {
    info += ` (pulso ${p.pulsesInCycle + (p.state === 'irrigando' ? 1 : 0)}/${p.maxPulses})`;
  }

  return (
    <Card>
      <View style={styles.pumpHeader}>
        <View>
          <Text style={styles.metricTitle}>Bomba</Text>
          <Text style={[styles.pumpState, { color: p.on ? colors.blue : colors.text }]}>
            {p.on ? 'Ligada' : 'Desligada'}
          </Text>
        </View>
        <View style={[styles.pumpIndicator, { backgroundColor: p.on ? colors.blue : colors.input }]} />
      </View>
      <Text style={styles.metricDetail}>{info}</Text>
      {p.cooldownRemainingS > 0 && (
        <Text style={styles.metricDetail}>
          Próxima irrigação automática liberada em {formatDuration(p.cooldownRemainingS)}
        </Text>
      )}
      <Text style={styles.metricDetail}>
        Última irrigação:{' '}
        {p.lastIrrigationAgoS === null ? 'nenhuma desde que ligou' : `há ${formatDuration(p.lastIrrigationAgoS)}`}
      </Text>

      <Text style={[styles.metricTitle, { marginTop: 14, marginBottom: 6 }]}>Modo</Text>
      <Segmented<PumpMode>
        value={p.mode}
        disabled={busy !== null}
        onChange={(m) => run('mode', () => estufa.setMode(m))}
        options={(['auto', 'manual', 'off'] as PumpMode[]).map((m) => ({ value: m, label: modeLabel[m] }))}
      />

      <Text style={[styles.metricTitle, { marginTop: 14, marginBottom: 6 }]}>Irrigação manual</Text>
      <View style={styles.row}>
        {[5, 10].filter((s) => s <= maxManual).map((s) => (
          <Button
            key={s}
            title={`Regar ${s}s`}
            disabled={!canRun || p.on}
            loading={busy === `on${s}`}
            onPress={() => run(`on${s}`, () => estufa.pumpOn(s))}
            style={{ flex: 1 }}
          />
        ))}
        <Button
          title="Parar"
          variant="danger"
          loading={busy === 'off'}
          onPress={() => run('off', estufa.pumpOff)}
          style={{ flex: 1 }}
        />
      </View>
      {!canRun && (
        <Text style={[styles.metricDetail, { color: colors.yellow }]}>
          {p.mode === 'off' ? 'Bomba em modo desligado.' : 'Reservatório abaixo do mínimo: bomba bloqueada.'}
        </Text>
      )}

      {p.state === 'falha' && (
        <Button
          title="Reconhecer falha e voltar ao automático"
          variant="secondary"
          loading={busy === 'fault'}
          onPress={() => run('fault', estufa.resetFault)}
          style={{ marginTop: 12 }}
        />
      )}
    </Card>
  );
}

export function DashboardScreen({ estufa, status }: { estufa: Estufa; status: EstufaStatus }) {
  const cfg = estufa.config;
  const soilColor =
    cfg && status.soil.percent < cfg.soilMinPct
      ? colors.yellow
      : colors.leaf;

  return (
    <View>
      <Alerts status={status} />

      <SectionTitle>Sensores</SectionTitle>
      <Metric
        title="Umidade do solo"
        value={status.soil.percent.toFixed(0)}
        unit="%"
        detail={cfg ? `Irriga abaixo de ${cfg.soilMinPct}% · para em ${cfg.soilTargetPct}%` : undefined}
        invalid={!status.soil.valid}
      >
        <ProgressBar
          percent={status.soil.percent}
          color={soilColor}
          markers={
            cfg
              ? [
                  { at: cfg.soilMinPct, color: colors.yellow },
                  { at: cfg.soilTargetPct, color: colors.text },
                ]
              : []
          }
        />
      </Metric>

      <View style={styles.grid}>
        <Metric
          title="Temperatura"
          value={status.air.temperature.toFixed(1)}
          unit="°C"
          invalid={!status.air.valid}
          half
        />
        <Metric
          title="Umidade do ar"
          value={status.air.humidity.toFixed(0)}
          unit="%"
          invalid={!status.air.valid}
          half
        />
      </View>

      <View style={styles.grid}>
        <Metric
          title="Luminosidade"
          value={status.light.percent.toFixed(0)}
          unit="%"
          detail={`${lightLabel[status.light.level]}${
            status.light.lux !== null ? ` · ~${status.light.lux} lx (estimado)` : ''
          }`}
          invalid={!status.light.valid}
          half
        >
          <ProgressBar percent={status.light.percent} color={colors.yellow} />
        </Metric>
        <Reservoir status={status} />
      </View>

      <SectionTitle>Irrigação</SectionTitle>
      <PumpPanel estufa={estufa} status={status} />
    </View>
  );
}

const styles = StyleSheet.create({
  grid: { flexDirection: 'row', gap: 10, marginTop: 10 },
  half: { flex: 1 },
  metricTitle: { color: colors.muted, fontSize: 13, fontWeight: '600' },
  metricValue: { color: colors.text, fontSize: 32, fontWeight: '800', marginTop: 4 },
  metricUnit: { fontSize: 16, fontWeight: '600', color: colors.muted },
  metricDetail: { color: colors.muted, fontSize: 13, marginTop: 4 },
  alert: {
    backgroundColor: colors.card,
    borderLeftWidth: 4,
    borderRadius: 8,
    padding: 12,
    marginBottom: 8,
  },
  alertText: { color: colors.text, lineHeight: 20 },
  electrodes: { marginTop: 8, gap: 4 },
  electrode: { height: 8, borderRadius: 4 },
  pumpHeader: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  pumpState: { fontSize: 24, fontWeight: '800', marginTop: 2 },
  pumpIndicator: { width: 22, height: 22, borderRadius: 11 },
  row: { flexDirection: 'row', gap: 10 },
});
