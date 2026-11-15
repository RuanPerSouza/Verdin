# Parte 3: Montagem, testes, calibração e problemas (etapas 9 a 11)

## 9. Procedimento de montagem e testes

Monte **um componente por vez** e teste antes de passar ao próximo. Se algo der errado, você sabe exatamente onde procurar.

Antes de começar: grave o firmware no ESP32 sozinho, abra o Monitor Serial (115200) e digite `p` a qualquer momento para ver as leituras.

| Etapa | O que ligar | Como testar | Resultado esperado |
|---|---|---|---|
| 1 | Só o ESP32 no USB | Monitor Serial | Aparece `Conectado! IP: ...`. Abra `http://IP/api/status` no navegador do PC ou do celular e veja o JSON. Sensores ainda aparecem como inválidos, e isso é normal |
| 2 | LCD + conversor de nível | Reinicie | "Estufa Inteligente / Iniciando sensores..." e depois as telas. Se acender sem texto, ajuste o trimpot de contraste |
| 3 | DHT11 | `p` | Temperatura e umidade coerentes. Assopre o sensor: a umidade sobe |
| 4 | LDR | `p` | O valor em mV cai ao cobrir o LDR e sobe com a lanterna do celular |
| 5 | Sensor de solo | `p` | No ar: valor alto (cerca de 2.500 a 3.100 mV). Num copo d'água: valor bem menor |
| 6 | Eletrodos | `p` | Seco: 0 a 50 mV. Com a ponta na água: centenas ou milhares de mV, marcado com `*` |
| 7 | Bomba + MOSFET (**na fonte 5 V**, não no USB) | Deixe o modo manual (`m`), encha o reservatório e digite `b` | A bomba liga por 3 s e desliga. Sem água no reservatório, o Serial responde "reservatorio com nivel insuficiente" |
| 8 | Sistema completo | Modo automático (`a`) | Veja os testes de proteção abaixo |
| 9 | App | Conecte pelo IP | O painel atualiza sozinho a cada 2 s |

### Testes das proteções (faça todos antes de deixar a estufa sozinha)

1. **Reservatório vazio.** Tire o E1 da água com a bomba ligada. A bomba deve desligar em até cerca de 1 s e aparecer "SEM AGUA" no LCD.
2. **Solo seco.** Tire o sensor de solo da terra (leitura alta). No automático, o ciclo começa: pulso, espera, pulso...
3. **Falha de irrigação.** Com o sensor no ar e a mangueira fora do vaso, após `maxPulses` pulsos o sistema entra em "falha" e para. Reconheça no app ou com `f`.
4. **Sensor desconectado.** Solte o fio AO do solo. A leitura vira "ERRO" e a irrigação automática pausa.
5. **Reinício.** Reinicie o ESP32 com a bomba ligada (botão EN). Ela deve desligar imediatamente e não ligar durante o boot.

## 10. Calibração dos sensores

Dá para calibrar **pelo app** (aba Ajustes → Calibração assistida) ou anotando os valores em mV do Monitor Serial e digitando no app.

### Umidade do solo
1. Sensor limpo e seco no ar → **Marcar SECO**.
2. Sensor enterrado em terra **encharcada** (ou na água até a linha do garfo) → **Marcar MOLHADO**.
3. Melhor ainda: use a terra do próprio vaso, seca e depois bem regada. Cada tipo de terra dá valores diferentes.

### Luminosidade
1. Cubra o LDR totalmente → **Marcar ESCURO**.
2. Lanterna do celular colada ao LDR → **Marcar CLARO**.
3. Ajuste "Luz baixa abaixo de" e "Luz alta acima de" observando a porcentagem nos horários de luz que considera bons para a planta.

### Eletrodos de nível
1. Digite `p` (ou veja na aba Ajustes) com os eletrodos **fora** da água e depois **dentro**.
2. Escolha um limiar no meio do caminho. Exemplo: seco em 20 mV e molhado em 1.600 mV → limiar de 500 a 800 mV.
3. Água muito pura (filtrada, de chuva) conduz pouco: o valor "molhado" fica baixo e o limiar precisa ser menor.

### DHT11
Não tem ajuste: a precisão é de ±2 °C e ±5 %. Compare com um termômetro confiável. Se a diferença for grande, o sensor pode estar perto de uma fonte de calor (o regulador do ESP32 esquenta), então afaste-o da placa.

### Limites de irrigação (dependem da planta)
Valores iniciais sugeridos, para ajustar observando a planta:

| Planta | Começar a irrigar | Parar |
|---|---|---|
| Suculentas e cactos | 15 % | 30 % |
| Ervas (manjericão, hortelã) | 35 % | 60 % |
| Hortaliças folhosas (alface) | 45 % | 70 % |

## 11. Possíveis problemas e soluções

| Sintoma | Causa provável | Solução |
|---|---|---|
| ESP32 reinicia quando a bomba liga ("Brownout detector was triggered") | Queda de tensão no pico do motor | Capacitor de 1000 µF na fonte, fonte de 2 A ou mais, fios curtos e grossos, diodo na bomba |
| Não grava ("Failed to connect to ESP32") | Placa não entrou em modo de gravação, ou algo ligado no GPIO 12 | Segure o botão BOOT ao iniciar a gravação; confira se nada está no GPIO 12 |
| Não aparece porta COM | Falta driver USB | Instale o driver CP210x ou CH340, conforme o chip da placa |
| LCD aceso sem texto | Contraste | Gire o trimpot do módulo (ou o potenciômetro no V0) |
| LCD com lixo ou sem resposta | PSB não está no GND, nível lógico de 3,3 V, fio trocado | PSB → GND; use o conversor de nível; confira E=18, R/W=23, RS=13 |
| DHT11 sempre "ERRO" | Sem pull-up (versão de 4 pinos), fio longo, sensor no 5 V com sinal fraco | Resistor de 10 kΩ entre DATA e 3V3; fio de até 1 m |
| Solo sempre "ERRO" | Módulo sem alimentação (VCC tem que ir ao GPIO 25) ou AO solto | Confira as ligações; com `p`, o valor deve mudar entre ar e água |
| Solo marca 100 % ou 0 % fixo | Calibração errada | Refaça a calibração seco/molhado |
| Nível "ERRO SENSOR" | Falta resistor de 100 kΩ (o pino flutua), eletrodos trocados, sujeira ligando eletrodos | Confira os resistores e a ordem E1→34, E2→35, E3→32; limpe os eletrodos |
| Nível sempre vazio com água | Limiar alto demais, água muito pura, comum desligado | Veja os mV com `p` e ajuste `electrodeThreshMv`; confira GPIO 26 → 1 kΩ → comum |
| Bomba não liga | MOSFET não é logic-level (IRF520), diodo invertido, sem GND comum | Use IRLZ44N; faixa do diodo no +5 V; GND da fonte ligado ao GND do ESP32 |
| Bomba não desliga | MOSFET em curto, gate sem resistor de 10 kΩ, relé ativo em nível baixo com `PUMP_ACTIVE_HIGH = true` | Troque o MOSFET; coloque o resistor; ajuste `config.h` |
| Entra em "falha" sem motivo | Pulso curto ou espera curta para a terra absorver; sensor longe da mangueira | Aumente `pulseS` ou `soakS`; coloque a saída da mangueira mais perto do sensor |
| Não conecta no Wi-Fi | Rede de 5 GHz, senha errada, `secrets.h` ausente | O ESP32 só funciona em **2,4 GHz**; confira `secrets.h`; use a rede própria "Estufa-ESP32" |
| App: "Sem conexão com o ESP32" | Celular em outra rede ou nos dados móveis, IP mudou | Mesma rede Wi-Fi; veja o IP no LCD; reserve um IP fixo para o ESP32 no roteador (DHCP reservation) |
| App não abre `estufa.local` | O Android não resolve mDNS em todos os aparelhos | Use o IP |
| Sensor de solo corroído | Eletrólise | Normal em sensores resistivos, mesmo com a alimentação pulsada. Substitua ou migre para um sensor capacitivo |
