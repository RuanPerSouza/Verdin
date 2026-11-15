import { StatusBar } from 'expo-status-bar';
import { useState } from 'react';
import { ActivityIndicator, Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';

import { Dot, Mark, Segmented } from './src/components/ui';
import { useEstufa, type Estufa, type LinkState } from './src/hooks/useEstufa';
import { colors, overallInfo } from './src/labels';
import { ConnectScreen } from './src/screens/ConnectScreen';
import { DashboardScreen } from './src/screens/DashboardScreen';
import { SettingsScreen } from './src/screens/SettingsScreen';

type Tab = 'painel' | 'ajustes';

const linkInfo: Record<LinkState, { label: string; color: string }> = {
  connecting: { label: 'Conectando…', color: colors.yellow },
  online: { label: 'Online', color: colors.green },
  offline: { label: 'Sem conexão', color: colors.red },
};

function Header({ estufa }: { estufa: Estufa }) {
  const overall = estufa.status ? overallInfo[estufa.status.overall] : null;
  const link = linkInfo[estufa.link];
  return (
    <View style={styles.header}>
      <View style={{ flex: 1 }}>
        <View style={styles.brandRow}>
          <Mark size={34} />
          <Text style={styles.title}>Verdin</Text>
        </View>
        <Pressable onPress={estufa.reconnect} style={styles.linkRow}>
          <Dot color={link.color} />
          <Text style={styles.linkText}>
            {estufa.demo ? 'Simulação neste aparelho' : `${link.label} · ${estufa.host}`}
          </Text>
        </Pressable>
      </View>
      {overall && estufa.link === 'online' && (
        <View style={[styles.badge, { borderColor: overall.color }]}>
          <Text style={[styles.badgeText, { color: overall.color }]}>{overall.label}</Text>
        </View>
      )}
    </View>
  );
}

function Main({ estufa }: { estufa: Estufa }) {
  const [tab, setTab] = useState<Tab>('painel');

  return (
    <>
      <Header estufa={estufa} />
      <View style={styles.tabs}>
        <Segmented<Tab>
          value={tab}
          onChange={setTab}
          options={[
            { value: 'painel', label: 'Painel' },
            { value: 'ajustes', label: 'Ajustes' },
          ]}
        />
      </View>
      <ScrollView contentContainerStyle={styles.scroll} keyboardShouldPersistTaps="handled">
        {estufa.link === 'offline' && !estufa.demo && (
          <View style={styles.offline}>
            <Text style={styles.offlineText}>
              Sem resposta do ESP32. Verifique se ele está ligado e se o celular está na mesma rede.
              {estufa.status ? ' Os dados abaixo são da última leitura.' : ''}
            </Text>
          </View>
        )}
        {tab === 'painel' ? (
          estufa.status ? (
            <DashboardScreen estufa={estufa} status={estufa.status} />
          ) : (
            <View style={styles.loading}>
              <ActivityIndicator color={colors.green} size="large" />
              <Text style={styles.linkText}>Aguardando dados da estufa…</Text>
            </View>
          )
        ) : (
          <SettingsScreen estufa={estufa} status={estufa.status} />
        )}
      </ScrollView>
    </>
  );
}

export default function App() {
  const estufa = useEstufa();

  return (
    <SafeAreaProvider>
      <SafeAreaView style={styles.container}>
        <StatusBar style="dark" />
        {!estufa.hostLoaded ? null : estufa.host || estufa.demo ? <Main estufa={estufa} /> : <ConnectScreen estufa={estufa} />}
      </SafeAreaView>
    </SafeAreaProvider>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: { flexDirection: 'row', alignItems: 'center', paddingHorizontal: 16, paddingTop: 8 },
  brandRow: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  title: { color: colors.text, fontSize: 22, fontWeight: '800' },
  linkRow: { flexDirection: 'row', alignItems: 'center', gap: 6, marginTop: 4 },
  linkText: { color: colors.muted, fontSize: 13 },
  badge: { borderWidth: 1.5, borderRadius: 20, paddingHorizontal: 12, paddingVertical: 5 },
  badgeText: { fontWeight: '700' },
  tabs: { paddingHorizontal: 16, paddingTop: 12 },
  scroll: { padding: 16, paddingTop: 0, paddingBottom: 40 },
  offline: { backgroundColor: '#fdecec', borderRadius: 10, padding: 12, marginTop: 12 },
  offlineText: { color: colors.red, lineHeight: 20 },
  loading: { alignItems: 'center', gap: 12, marginTop: 60 },
});
