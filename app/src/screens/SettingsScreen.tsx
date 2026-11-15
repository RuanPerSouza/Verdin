import { useMemo, useState } from 'react';
import { Alert, StyleSheet, Text, TextInput, View } from 'react-native';

import type { CalibrationTarget, EstufaConfig, EstufaSettings, EstufaStatus } from '../api/types';
import { Button, Card, SectionTitle } from '../components/ui';
import type { Estufa } from '../hooks/useEstufa';
import { colors, formatDuration } from '../labels';

type Field = { key: keyof EstufaSettings; label: string; unit: string; hint?: string };

const GROUPS: { title: string; fields: Field[] }[] = [
  {
    title: 'Irrigação automática',
    fields: [
      { key: 'soilMinPct', label: 'Começar a irrigar abaixo de', unit: '%' },
      { key: 'soilTargetPct', label: 'Parar ao atingir', unit: '%' },
      { key: 'pulseS', label: 'Bomba ligada por pulso', unit: 's' },
      { key: 'soakS', label: 'Espera para infiltrar', unit: 's', hint: 'Tempo entre pulsos para a água chegar ao sensor.' },
      { key: 'maxPulses', label: 'Pulsos máximos por ciclo', unit: '' },
      { key: 'cooldownMin', label: 'Intervalo de segurança entre ciclos', unit: 'min' },
      { key: 'maxManualS', label: 'Limite da irrigação manual', unit: 's' },
      {
        key: 'reservoirMinElec',
        label: 'Eletrodos molhados para liberar a bomba',
        unit: '',
        hint: '1 = basta o eletrodo mais baixo estar na água.',
      },
    ],
  },
  {
    title: 'Faixas ideais (alertas)',
    fields: [
      { key: 'tempMinC', label: 'Temperatura mínima', unit: '°C' },
      { key: 'tempMaxC', label: 'Temperatura máxima', unit: '°C' },
      { key: 'airHumMinPct', label: 'Umidade do ar mínima', unit: '%' },
      { key: 'airHumMaxPct', label: 'Umidade do ar máxima', unit: '%' },
      { key: 'lightLowPct', label: 'Luz baixa abaixo de', unit: '%' },
      { key: 'lightHighPct', label: 'Luz alta acima de', unit: '%' },
    ],
  },
  {
    title: 'Calibração (valores brutos)',
    fields: [
      { key: 'soilDryMv', label: 'Solo seco', unit: 'mV' },
      { key: 'soilWetMv', label: 'Solo encharcado', unit: 'mV' },
      { key: 'lightDarkMv', label: 'LDR no escuro', unit: 'mV' },
      { key: 'lightBrightMv', label: 'LDR em luz forte', unit: 'mV' },
      {
        key: 'electrodeThreshMv',
        label: 'Eletrodo considerado molhado acima de',
        unit: 'mV',
        hint: 'Use as leituras dos eletrodos abaixo para escolher um valor entre "seco" e "na água".',
      },
    ],
  },
];

const ALL_FIELDS = GROUPS.flatMap((g) => g.fields);

function toForm(cfg: EstufaConfig): Record<string, string> {
  return Object.fromEntries(ALL_FIELDS.map((f) => [f.key, String(cfg[f.key])]));
}

function Calibration({ estufa, status }: { estufa: Estufa; status: EstufaStatus }) {
  const [busy, setBusy] = useState<CalibrationTarget | null>(null);

  const calibrate = (target: CalibrationTarget, instruction: string) => {
    Alert.alert('Calibrar', `${instruction}\n\nA leitura atual será salva como referência.`, [
      { text: 'Cancelar', style: 'cancel' },
      {
        text: 'Salvar leitura',
        onPress: async () => {
          setBusy(target);
          const r = await estufa.calibrate(target);
          setBusy(null);
          if (!r.ok) Alert.alert('Erro', r.error);
        },
      },
    ]);
  };

  return (
    <Card>
      <Text style={styles.calTitle}>Solo agora: {status.soil.mv} mV</Text>
      <View style={styles.row}>
        <Button
          title="Marcar SECO"
          variant="secondary"
          loading={busy === 'soil_dry'}
          onPress={() => calibrate('soil_dry', 'Deixe o sensor de solo no ar (ou em terra bem seca).')}
          style={{ flex: 1 }}
        />
        <Button
          title="Marcar MOLHADO"
          variant="secondary"
          loading={busy === 'soil_wet'}
          onPress={() => calibrate('soil_wet', 'Coloque o sensor em terra encharcada (ou num copo d’água até a linha).')}
          style={{ flex: 1 }}
        />
      </View>

      <Text style={[styles.calTitle, { marginTop: 14 }]}>Luz agora: {status.light.mv} mV</Text>
      <View style={styles.row}>
        <Button
          title="Marcar ESCURO"
          variant="secondary"
          loading={busy === 'light_dark'}
          onPress={() => calibrate('light_dark', 'Cubra totalmente o LDR com o dedo ou um pano escuro.')}
          style={{ flex: 1 }}
        />
        <Button
          title="Marcar CLARO"
          variant="secondary"
          loading={busy === 'light_bright'}
          onPress={() => calibrate('light_bright', 'Ilumine o LDR com a lanterna do celular bem de perto.')}
          style={{ flex: 1 }}
        />
      </View>

      <Text style={[styles.calTitle, { marginTop: 14 }]}>Eletrodos (de baixo para cima)</Text>
      {status.reservoir.electrodesMv.map((mv, i) => (
        <Text key={i} style={styles.muted}>
          Eletrodo {i + 1}: {mv} mV {status.reservoir.electrodes[i] ? '(na água)' : '(seco)'}
        </Text>
      ))}
    </Card>
  );
}

export function SettingsScreen({ estufa, status }: { estufa: Estufa; status: EstufaStatus | null }) {
  const cfg = estufa.config;
  const [form, setForm] = useState<Record<string, string>>(() => (cfg ? toForm(cfg) : {}));
  const [formSource, setFormSource] = useState(cfg);
  const [saving, setSaving] = useState(false);
  const [message, setMessage] = useState<{ ok: boolean; text: string } | null>(null);

  // Nova configuração chegou do ESP32: recarrega o formulário
  if (cfg !== formSource) {
    setFormSource(cfg);
    if (cfg) setForm(toForm(cfg));
  }

  const changes = useMemo(() => {
    if (!cfg) return {};
    const out: Partial<EstufaSettings> = {};
    for (const f of ALL_FIELDS) {
      const raw = form[f.key];
      if (raw === undefined || raw.trim() === '') continue;
      const n = Number(raw.replace(',', '.'));
      if (Number.isFinite(n) && n !== cfg[f.key]) out[f.key] = Math.round(n);
    }
    return out;
  }, [form, cfg]);

  const dirty = Object.keys(changes).length > 0;

  const save = async () => {
    setSaving(true);
    setMessage(null);
    const r = await estufa.saveConfig(changes);
    setSaving(false);
    setMessage(
      r.ok
        ? { ok: true, text: estufa.demo ? 'Configuração salva na simulação.' : 'Configuração salva no ESP32.' }
        : { ok: false, text: r.error },
    );
  };

  const confirmFactoryReset = () => {
    Alert.alert('Restaurar padrões', 'Todos os limites e calibrações voltarão aos valores de fábrica.', [
      { text: 'Cancelar', style: 'cancel' },
      {
        text: 'Restaurar',
        style: 'destructive',
        onPress: async () => {
          const r = await estufa.factoryReset();
          if (!r.ok) Alert.alert('Erro', r.error);
        },
      },
    ]);
  };

  if (!cfg) {
    return <Text style={[styles.muted, { marginTop: 20 }]}>Carregando configuração do ESP32…</Text>;
  }

  return (
    <View>
      {GROUPS.map((g) => (
        <View key={g.title}>
          <SectionTitle>{g.title}</SectionTitle>
          <Card>
            {g.fields.map((f, i) => (
              <View key={f.key} style={[styles.field, i > 0 && styles.fieldBorder]}>
                <View style={{ flex: 1 }}>
                  <Text style={styles.fieldLabel}>{f.label}</Text>
                  {f.hint && <Text style={styles.hint}>{f.hint}</Text>}
                </View>
                <TextInput
                  value={form[f.key] ?? ''}
                  onChangeText={(t) => setForm((prev) => ({ ...prev, [f.key]: t }))}
                  keyboardType="numbers-and-punctuation"
                  style={[styles.input, changes[f.key] !== undefined && styles.inputChanged]}
                />
                <Text style={styles.unit}>{f.unit}</Text>
              </View>
            ))}
          </Card>
        </View>
      ))}

      {message && (
        <Text style={[styles.message, { color: message.ok ? colors.green : colors.red }]}>{message.text}</Text>
      )}
      <View style={[styles.row, { marginTop: 12 }]}>
        <Button
          title="Desfazer"
          variant="secondary"
          disabled={!dirty}
          onPress={() => setForm(toForm(cfg))}
          style={{ flex: 1 }}
        />
        <Button
          title={estufa.demo ? 'Salvar na simulação' : 'Salvar no ESP32'}
          disabled={!dirty}
          loading={saving}
          onPress={save}
          style={{ flex: 2 }}
        />
      </View>

      {status && (
        <>
          <SectionTitle>Calibração assistida</SectionTitle>
          <Calibration estufa={estufa} status={status} />

          <SectionTitle>Dispositivo</SectionTitle>
          <Card>
            <Text style={styles.muted}>Nome: {status.device.name}</Text>
            <Text style={styles.muted}>Firmware: {status.device.firmware}</Text>
            {estufa.demo ? (
              <Text style={styles.muted}>Rede: simulação neste aparelho, sem Wi-Fi.</Text>
            ) : (
              <>
                <Text style={styles.muted}>
                  Rede: {status.device.wifiMode === 'sta' ? 'roteador' : 'rede própria do ESP32'} · IP {status.device.ip}
                </Text>
                {status.device.wifiMode === 'sta' && (
                  <Text style={styles.muted}>Sinal Wi-Fi: {status.device.rssi} dBm</Text>
                )}
              </>
            )}
            <Text style={styles.muted}>Ligado há: {formatDuration(status.device.uptimeS)}</Text>
            <Text style={styles.muted}>Memória livre: {Math.round(status.device.freeHeap / 1024)} KB</Text>
            <Text style={styles.muted}>
              Proteção fixa: bomba nunca passa de {cfg.limits.absoluteMaxOnS}s seguidos
            </Text>
          </Card>
        </>
      )}

      <View style={[styles.row, { marginTop: 16 }]}>
        <Button
          title={estufa.demo ? 'Sair da simulação' : 'Trocar ESP32'}
          variant="secondary"
          onPress={estufa.disconnect}
          style={{ flex: 1 }}
        />
        <Button title="Restaurar padrões" variant="danger" onPress={confirmFactoryReset} style={{ flex: 1 }} />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  row: { flexDirection: 'row', gap: 10 },
  field: { flexDirection: 'row', alignItems: 'center', paddingVertical: 10, gap: 8 },
  fieldBorder: { borderTopWidth: 1, borderTopColor: colors.cardBorder },
  fieldLabel: { color: colors.text, fontSize: 15 },
  hint: { color: colors.muted, fontSize: 12, marginTop: 2 },
  input: {
    backgroundColor: colors.input,
    color: colors.text,
    borderRadius: 8,
    borderWidth: 1,
    borderColor: colors.cardBorder,
    width: 72,
    paddingVertical: 8,
    paddingHorizontal: 10,
    textAlign: 'right',
    fontSize: 16,
  },
  inputChanged: { borderColor: colors.yellow },
  unit: { color: colors.muted, width: 30 },
  message: { marginTop: 12, fontWeight: '600' },
  calTitle: { color: colors.text, fontWeight: '700', marginBottom: 8 },
  muted: { color: colors.muted, marginTop: 4, lineHeight: 20 },
});
