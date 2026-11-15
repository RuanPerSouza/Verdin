# API do ESP32 (contrato entre firmware e app)

- Base: `http://<IP-do-ESP32>` (porta 80). No modo rede própria: `http://192.168.4.1`. Com mDNS: `http://estufa.local`.
- Formato: JSON (UTF-8). CORS liberado (`*`), então funciona também no navegador.
- Erros: código HTTP 4xx/5xx com corpo `{ "ok": false, "error": "mensagem em português" }`.
- O simulador (`tools/mock-esp32`) implementa exatamente este contrato.

Os tipos TypeScript equivalentes estão em `app/src/api/types.ts`.

## Leitura

### `GET /api/ping`
Teste rápido de conexão.
```json
{ "ok": true, "name": "estufa", "firmware": "1.0.0" }
```

### `GET /api/status`
Estado completo. É o mesmo objeto enviado pelo WebSocket.
```json
{
  "device":   { "name": "estufa", "firmware": "1.0.0", "uptimeS": 3600, "wifiMode": "sta",
                "ip": "192.168.0.50", "rssi": -58, "freeHeap": 210000 },
  "soil":     { "percent": 47.5, "mv": 2150, "valid": true },
  "air":      { "temperature": 24.0, "humidity": 61.0, "valid": true },
  "light":    { "percent": 72.3, "lux": 850, "luxEstimated": true, "mv": 2200,
                "level": "adequada", "valid": true },
  "reservoir":{ "percent": 50, "level": "medio", "valid": true, "pumpAllowed": true,
                "electrodes": [true, true, false], "electrodesMv": [1650, 1580, 12] },
  "pump":     { "on": false, "mode": "auto", "state": "monitorando", "pulsesInCycle": 0,
                "maxPulses": 6, "cooldownRemainingS": 0, "lastIrrigationAgoS": 1200,
                "cyclesSinceBoot": 3 },
  "overall":  "ok",
  "alerts":   [],
  "ts": 3600123
}
```

| Campo | Valores |
|---|---|
| `device.wifiMode` | `sta` (no roteador) ou `ap` (rede própria) |
| `light.lux` | número estimado, ou `null` se usar módulo LDR |
| `light.level` | `baixa`, `adequada`, `alta` |
| `reservoir.level` | `vazio`, `baixo`, `medio`, `cheio` |
| `reservoir.electrodes` | do eletrodo mais baixo para o mais alto; `true` = na água |
| `pump.mode` | `auto`, `manual`, `off` |
| `pump.state` | `monitorando`, `irrigando`, `infiltrando`, `intervalo`, `manual`, `sem_agua`, `erro_sensor`, `falha` |
| `pump.lastIrrigationAgoS` | segundos desde a última irrigação, ou `null` se ainda não irrigou desde que ligou |
| `overall` | `ok`, `atencao`, `alerta` |
| `alerts[]` | `BOMBA_FALHA`, `RESERVATORIO_VAZIO`, `RESERVATORIO_BAIXO`, `SENSOR_SOLO`, `SENSOR_NIVEL`, `SENSOR_AR`, `SENSOR_LUZ`, `SOLO_SECO`, `TEMPERATURA_ALTA`, `TEMPERATURA_BAIXA`, `UMIDADE_AR_ALTA`, `UMIDADE_AR_BAIXA`, `LUZ_BAIXA`, `LUZ_ALTA` |

Quando `valid` é `false`, o valor numérico é o último conhecido (ou zero) e não deve ser usado em decisões.

### `GET /api/config`
```json
{
  "soilMinPct": 35, "soilTargetPct": 60, "pulseS": 5, "soakS": 30, "maxPulses": 6,
  "cooldownMin": 30, "maxManualS": 20, "reservoirMinElec": 1,
  "lightLowPct": 30, "lightHighPct": 85, "tempMinC": 15, "tempMaxC": 32,
  "airHumMinPct": 40, "airHumMaxPct": 85,
  "soilDryMv": 3000, "soilWetMv": 1200, "lightDarkMv": 50, "lightBrightMv": 3000,
  "electrodeThreshMv": 500,
  "limits": { "userMaxOnS": 45, "absoluteMaxOnS": 60, "electrodeCount": 3 }
}
```

## Comandos (todos `POST` com corpo JSON)

| Rota | Corpo | Efeito |
|---|---|---|
| `/api/config` | qualquer subconjunto dos campos de `/api/config` (inteiros) | Valida, salva na flash e responde a configuração completa. Erro 400 explica o campo inválido |
| `/api/pump` | `{ "action": "on", "seconds": 5 }` | Irrigação manual (1 a `maxManualS` s). 409 se o modo for `off` ou faltar água |
| `/api/pump` | `{ "action": "off" }` | Para a bomba |
| `/api/mode` | `{ "mode": "auto" \| "manual" \| "off" }` | Muda o modo (salvo na flash) |
| `/api/calibrate` | `{ "target": "soil_dry" \| "soil_wet" \| "light_dark" \| "light_bright" }` | Salva a leitura atual como referência |
| `/api/fault/reset` | `{}` | Reconhece a falha de irrigação |
| `/api/factory-reset` | `{}` | Volta todas as configurações para o padrão de fábrica |

Resposta de sucesso: `{ "ok": true }`, ou a configuração completa no caso de `/api/config`.

Os comandos entram numa fila e são executados pelo `loop()` em poucos milissegundos. Para ver o efeito, leia o próximo status: o WebSocket envia um status novo **na hora** sempre que a bomba ou o modo mudam.

## Tempo real: WebSocket `ws://<IP>/ws`

- Ao conectar, o ESP32 envia o status atual.
- Depois envia o status a cada 2 s e imediatamente quando a bomba, o estado da irrigação ou o modo mudam.
- As mensagens têm o mesmo formato de `GET /api/status`.
- O WebSocket só envia dados: comandos são feitos pela API REST.
- Máximo de 4 clientes simultâneos.

## Extras do simulador (não existem no ESP32)

| Rota | Efeito |
|---|---|
| `POST /sim/dry` | Solo a 20 % e zera o intervalo de segurança (dispara a irrigação) |
| `POST /sim/drain` | Reservatório quase vazio |
| `POST /sim/refill` | Reservatório cheio |
