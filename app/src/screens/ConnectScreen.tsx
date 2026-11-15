import { useState } from 'react';
import { KeyboardAvoidingView, Platform, ScrollView, StyleSheet, Text, TextInput } from 'react-native';

import { Button, Card, Logo } from '../components/ui';
import type { Estufa } from '../hooks/useEstufa';
import { colors } from '../labels';

export function ConnectScreen({ estufa }: { estufa: Estufa }) {
  const [input, setInput] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  const tryConnect = async (value = input) => {
    setLoading(true);
    setError(null);
    const r = await estufa.connect(value);
    setLoading(false);
    if (!r.ok) setError(r.error);
  };

  return (
    <KeyboardAvoidingView
      style={styles.screen}
      behavior={Platform.OS === 'ios' ? 'padding' : undefined}
    >
      <ScrollView style={styles.screen} contentContainerStyle={styles.container} keyboardShouldPersistTaps="handled">
      <Logo width={210} />

      <Button title="Ver simulação" onPress={() => estufa.startDemo()} style={{ marginTop: 8 }} />

      <Card style={{ marginTop: 16 }}>
        <Text style={styles.label}>Endereço do ESP32</Text>
        <TextInput
          value={input}
          onChangeText={setInput}
          placeholder="ex.: 192.168.0.50"
          placeholderTextColor={colors.muted}
          autoCapitalize="none"
          autoCorrect={false}
          keyboardType={Platform.OS === 'ios' ? 'url' : 'default'}
          onSubmitEditing={() => tryConnect()}
          style={styles.input}
        />
        {error && <Text style={styles.error}>{error}</Text>}
        <Button title="Conectar" onPress={() => tryConnect()} loading={loading} style={{ marginTop: 12 }} />
      </Card>

      <Button
        title="172.20.42.179"
        variant="secondary"
        onPress={() => tryConnect('172.20.42.179')}
        style={{ marginTop: 12 }}
      />
      <Button
        title="192.168.4.1"
        variant="secondary"
        onPress={() => tryConnect('192.168.4.1')}
        style={{ marginTop: 10 }}
      />
      <Button
        title="estufa.local"
        variant="secondary"
        onPress={() => tryConnect('estufa.local')}
        style={{ marginTop: 10 }}
      />
      </ScrollView>
    </KeyboardAvoidingView>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1 },
  container: { flexGrow: 1, padding: 20, justifyContent: 'center' },
  label: { color: colors.muted, marginBottom: 6 },
  input: {
    backgroundColor: colors.input,
    color: colors.text,
    borderRadius: 10,
    paddingHorizontal: 12,
    paddingVertical: 12,
    fontSize: 17,
    borderWidth: 1,
    borderColor: colors.cardBorder,
  },
  error: { color: colors.red, marginTop: 8 },
});
