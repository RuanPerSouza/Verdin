// =============================================================================
//  config.h  -  CONFIGURAÇÕES CENTRAIS DA ESTUFA
// -----------------------------------------------------------------------------
//  Tudo que depende da MONTAGEM (pinos, tipo de módulo, rede Wi-Fi) fica aqui.
//  Os limites de irrigação abaixo são apenas os valores de FÁBRICA: depois do
//  primeiro boot eles ficam salvos na memória flash (NVS) e podem ser
//  alterados pelo app, sem precisar recompilar.
// =============================================================================
#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Identificação
// -----------------------------------------------------------------------------
#define FIRMWARE_VERSION   "1.0.0"
#define DEVICE_HOSTNAME    "estufa"          // acessível em http://estufa.local

// -----------------------------------------------------------------------------
//  Wi-Fi
//  Crie o arquivo secrets.h (copie de secrets.example.h) com o nome e a senha
//  da sua rede. Se ele não existir, os valores abaixo são usados.
// -----------------------------------------------------------------------------
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

#ifndef WIFI_SSID
  #define WIFI_SSID        "NOME_DA_REDE"
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD    "SENHA_DA_REDE"
#endif

// Se não conectar no roteador em WIFI_STA_TIMEOUT_MS, o ESP32 cria a própria
// rede (modo Access Point). O app conecta nela e acessa http://192.168.4.1
#define WIFI_STA_TIMEOUT_MS   20000UL
#ifndef AP_SSID
  #define AP_SSID          "Estufa-ESP32"
#endif
#ifndef AP_PASSWORD
  #define AP_PASSWORD      "estufa123"       // mínimo de 8 caracteres
#endif

// -----------------------------------------------------------------------------
//  MAPA DE PINOS (ESP32 DevKit V1 / WROOM-32, 30 ou 38 pinos)
//
//  Regras seguidas na escolha:
//   * Leituras analógicas SOMENTE em pinos do ADC1 (32 a 39). O ADC2 não
//     funciona enquanto o Wi-Fi está ligado.
//   * Pinos 34, 35, 36 e 39 são apenas ENTRADA e não têm pull-up/pull-down
//     interno (por isso os resistores externos nos eletrodos).
//   * Evitados os pinos de boot (0, 2*, 12, 15) para cargas e sensores.
//     (* o GPIO2 só aciona o LED da própria placa)
//   * GPIO 21 e 22 (I2C) ficam LIVRES para expansões futuras.
// -----------------------------------------------------------------------------
// DHT11 (temperatura e umidade do ar)
#define PIN_DHT              4

// Sensor de umidade do solo resistivo (módulo LM393, saída AO)
#define PIN_SOIL_ANALOG      36   // VP  - ADC1_CH0 (entrada analógica)
#define PIN_SOIL_POWER       25   // alimenta o sensor só durante a leitura

// LDR (luminosidade)
#define PIN_LDR_ANALOG       39   // VN  - ADC1_CH3

// Sensor de nível por eletrodos
// O comum saiu do GPIO 26: esse pino é o IN do relé na montagem atual.
#define PIN_LEVEL_EXCITE     27
#define LEVEL_ELECTRODE_COUNT 3
// Do eletrodo MAIS BAIXO para o MAIS ALTO
static const uint8_t PIN_LEVEL_ELECTRODES[LEVEL_ELECTRODE_COUNT] = {34, 35, 32};
// Medidor contínuo: 0 = vazio, 240 = cheio.
// Fora da água o eletrodo fica em ~140 mV. Afogado de verdade, a tensão
// estável fica em torno de 700 mV. Acima disso a escala antiga marcava ~50 %.
#define RESERVOIR_METER_FULL     240
#define RESERVOIR_EMPTY_MV       140
#define RESERVOIR_FULL_MV        720

// Relé: VCC no 5 V, GND no GND, IN no GPIO 26. Powerbank alimenta o conjunto.
// Nível alto desliga a bobina. O comando "ligar" do app põe o pino em LOW.
#define PIN_PUMP             26
#define PUMP_ACTIVE_HIGH     false

// Display LCD gráfico 128x64 (controlador ST7920) em modo SERIAL (PSB = GND)
//   pino E   do LCD  -> SCLK
//   pino R/W do LCD  -> SID (dados)
//   pino RS  do LCD  -> CS
#define PIN_LCD_SCLK         18
#define PIN_LCD_SID          23
#define PIN_LCD_CS           13
#define LCD_SPI_CLOCK_HZ     500000UL   // ST7920 é lento; 500 kHz é seguro

// LEDs de alerta
#define PIN_LED_ALERT        14   // LED vermelho externo (alerta visual)
#define PIN_LED_BUILTIN      2    // LED azul da placa (status do Wi-Fi)

// Pino livre do ADC1 reservado para expansão: GPIO 33

// -----------------------------------------------------------------------------
//  Tipo de ligação do LDR
//   false = LDR + resistor de 10k em divisor de tensão (mais luz = mais tensão)
//   true  = módulo LDR (mais luz = menos tensão na saída AO; não estima lux)
// -----------------------------------------------------------------------------
#define LDR_IS_MODULE        true
#define LDR_FIXED_RESISTOR   10000.0f   // resistor do divisor (ohms)
// Parâmetros típicos do LDR GL5528 (para a ESTIMATIVA de lux)
#define LDR_R10_OHMS         15000.0f   // resistência do LDR com 10 lux
#define LDR_GAMMA            0.7f

// -----------------------------------------------------------------------------
//  Intervalos de leitura (ms). DHT11 não pode ser lido mais de 1x por segundo.
// -----------------------------------------------------------------------------
#define INTERVAL_DHT_MS            2500UL
#define INTERVAL_SOIL_MS           1000UL
#define INTERVAL_LDR_MS            1000UL
#define INTERVAL_LEVEL_MS          2000UL
#define INTERVAL_LEVEL_PUMPING_MS   500UL   // mais rápido com a bomba ligada
#define INTERVAL_DISPLAY_MS        1000UL
#define DISPLAY_PAGE_MS            5000UL   // troca de tela do LCD
#define INTERVAL_BROADCAST_MS      2000UL   // envio de dados ao app (WebSocket)
#define INTERVAL_SERIAL_LOG_MS     5000UL

// Tempo para o sensor estabilizar depois de energizado
// O ADC do ESP32 erra em fontes de alta impedância (eletrodo com 100 kΩ,
// saída do módulo de solo) se a leitura for imediata. Estes tempos deixam
// a tensão estabilizar antes da amostra.
#define SOIL_SETTLE_MS             300UL
#define LEVEL_SETTLE_MS            40UL
// O divisor do LDR tem impedância alta. Sem essa espera o ADC1 devolve
// o patamar de pino aberto (~140 mV) e a luz fica presa em ~3 %.
#define LDR_SETTLE_MS              12UL

// Amostras por leitura analógica (média com descarte dos extremos)
#define ADC_SAMPLES                16

// -----------------------------------------------------------------------------
//  Detecção de leituras inválidas
// -----------------------------------------------------------------------------
#define SOIL_MIN_VALID_MV          80    // abaixo disso: sensor sem alimentação/curto
#define ADC_MAX_SPREAD_MV          450   // variação excessiva entre amostras = pino solto
#define DHT_MAX_FAILURES           3     // falhas seguidas até marcar erro
#define LEVEL_DEBOUNCE_READS       2     // leituras iguais para aceitar mudança

// -----------------------------------------------------------------------------
//  Proteções da bomba (fixas no firmware, NÃO alteráveis pelo app)
// -----------------------------------------------------------------------------
#define PUMP_ABSOLUTE_MAX_ON_S     60    // nunca fica ligada mais que isso
#define PUMP_USER_MAX_ON_S         45    // maior pulso/manual que o app aceita
#define PUMP_FAULT_MIN_RISE_PCT    3.0f  // subida mínima esperada após um ciclo
#define WATCHDOG_TIMEOUT_S         10    // reinicia o ESP32 se o loop travar

// -----------------------------------------------------------------------------
//  VALORES DE FÁBRICA (alteráveis pelo app e salvos na flash)
// -----------------------------------------------------------------------------
#define DEFAULT_SOIL_MIN_PCT        35   // abaixo disso começa a irrigar
#define DEFAULT_SOIL_TARGET_PCT     60   // para de irrigar ao atingir
#define DEFAULT_PULSE_S             5    // tempo de bomba ligada por pulso
#define DEFAULT_SOAK_S              30   // espera para a água infiltrar
#define DEFAULT_MAX_PULSES          6    // pulsos máximos por ciclo
#define DEFAULT_COOLDOWN_MIN        30   // intervalo de segurança entre ciclos
#define DEFAULT_MAX_MANUAL_S        20   // limite de um acionamento manual
#define DEFAULT_RESERVOIR_MIN_ELEC  1    // eletrodos molhados p/ liberar a bomba
#define DEFAULT_LIGHT_LOW_PCT       30
#define DEFAULT_LIGHT_HIGH_PCT      85
#define DEFAULT_TEMP_MIN_C          15
#define DEFAULT_TEMP_MAX_C          32
#define DEFAULT_AIR_HUM_MIN_PCT     40
#define DEFAULT_AIR_HUM_MAX_PCT     85

// Calibração de fábrica (mV). Ajuste pelo app ou pelo Monitor Serial.
#define DEFAULT_SOIL_DRY_MV         150 // 10k para o GND: solo seco fica no fundo da escala
#define DEFAULT_SOIL_WET_MV        2400 // solo molhado puxa a tensão para cima
#define DEFAULT_LIGHT_DARK_MV       2400 // módulo coberto: tensão sobe
#define DEFAULT_LIGHT_BRIGHT_MV     100  // módulo sob luz: tensão cai
#define DEFAULT_ELECTRODE_THRESH_MV 500  // acima disso o eletrodo está na água
