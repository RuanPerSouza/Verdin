import type { ReactNode } from 'react';
import { ActivityIndicator, Image, Pressable, StyleSheet, Text, View, type ViewStyle } from 'react-native';

import { colors } from '../labels';

export function Logo({ width = 200 }: { width?: number }) {
  return (
    <Image
      source={require('../../assets/verdin-logo.png')}
      accessibilityLabel="Verdin"
      style={{ width, height: width * (372 / 346), alignSelf: 'center' }}
      resizeMode="contain"
    />
  );
}

export function Mark({ size = 36 }: { size?: number }) {
  return (
    <Image
      source={require('../../assets/verdin-mark.png')}
      accessibilityLabel="Verdin"
      style={{ width: size * (191 / 260), height: size }}
      resizeMode="contain"
    />
  );
}

export function Card({ children, style }: { children: ReactNode; style?: ViewStyle }) {
  return <View style={[styles.card, style]}>{children}</View>;
}

export function SectionTitle({ children }: { children: ReactNode }) {
  return <Text style={styles.sectionTitle}>{children}</Text>;
}

type ButtonVariant = 'primary' | 'secondary' | 'danger';

export function Button({
  title,
  onPress,
  variant = 'primary',
  disabled,
  loading,
  style,
}: {
  title: string;
  onPress: () => void;
  variant?: ButtonVariant;
  disabled?: boolean;
  loading?: boolean;
  style?: ViewStyle;
}) {
  const bg = variant === 'primary' ? colors.green : variant === 'danger' ? colors.red : colors.card;
  const fg = variant === 'secondary' ? colors.green : colors.onGreen;
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled || loading}
      style={({ pressed }) => [
        styles.button,
        { backgroundColor: bg, opacity: disabled ? 0.4 : pressed ? 0.82 : 1 },
        variant === 'secondary' && styles.buttonSecondary,
        style,
      ]}
    >
      {loading ? <ActivityIndicator color={fg} /> : <Text style={[styles.buttonText, { color: fg }]}>{title}</Text>}
    </Pressable>
  );
}

// Barra horizontal 0-100% com marcadores opcionais (ex.: limite mínimo e meta)
export function ProgressBar({
  percent,
  color,
  markers = [],
}: {
  percent: number;
  color: string;
  markers?: { at: number; color: string }[];
}) {
  const p = Math.max(0, Math.min(100, percent));
  return (
    <View style={styles.barTrack}>
      <View style={[styles.barFill, { width: `${p}%`, backgroundColor: color }]} />
      {markers.map((m) => (
        <View key={m.at} style={[styles.barMarker, { left: `${m.at}%`, backgroundColor: m.color }]} />
      ))}
    </View>
  );
}

export function Segmented<T extends string>({
  options,
  value,
  onChange,
  disabled,
}: {
  options: { value: T; label: string }[];
  value: T;
  onChange: (v: T) => void;
  disabled?: boolean;
}) {
  return (
    <View style={styles.segmented}>
      {options.map((o) => {
        const active = o.value === value;
        return (
          <Pressable
            key={o.value}
            disabled={disabled}
            onPress={() => onChange(o.value)}
            style={[styles.segment, active && styles.segmentActive]}
          >
            <Text style={[styles.segmentText, active && styles.segmentTextActive]}>{o.label}</Text>
          </Pressable>
        );
      })}
    </View>
  );
}

export function Dot({ color }: { color: string }) {
  return <View style={[styles.dot, { backgroundColor: color }]} />;
}

const styles = StyleSheet.create({
  card: {
    backgroundColor: colors.card,
    borderColor: colors.cardBorder,
    borderWidth: 1,
    borderRadius: 16,
    padding: 14,
    shadowColor: '#185830',
    shadowOpacity: 0.06,
    shadowRadius: 10,
    shadowOffset: { width: 0, height: 3 },
    elevation: 2,
  },
  sectionTitle: {
    color: colors.muted,
    fontSize: 13,
    fontWeight: '600',
    textTransform: 'uppercase',
    letterSpacing: 0.8,
    marginTop: 18,
    marginBottom: 8,
  },
  button: {
    borderRadius: 12,
    paddingVertical: 12,
    paddingHorizontal: 16,
    alignItems: 'center',
    justifyContent: 'center',
    minHeight: 46,
  },
  buttonSecondary: { borderWidth: 1.5, borderColor: colors.green },
  buttonText: { fontWeight: '700', fontSize: 15 },
  barTrack: {
    height: 10,
    borderRadius: 5,
    backgroundColor: colors.input,
    overflow: 'hidden',
    marginTop: 8,
  },
  barFill: { height: '100%', borderRadius: 5 },
  barMarker: { position: 'absolute', top: 0, bottom: 0, width: 2 },
  segmented: {
    flexDirection: 'row',
    backgroundColor: colors.input,
    borderRadius: 10,
    padding: 3,
  },
  segment: { flex: 1, paddingVertical: 9, alignItems: 'center', borderRadius: 8 },
  segmentActive: { backgroundColor: colors.green },
  segmentText: { color: colors.muted, fontWeight: '600' },
  segmentTextActive: { color: colors.onGreen },
  dot: { width: 10, height: 10, borderRadius: 5 },
});
