#include "display.h"

#include <SPI.h>
#include <U8g2lib.h>

#include "irrigation.h"
#include "sensors.h"
#include "status.h"

// ST7920 em modo serial usando o SPI de hardware (VSPI).
// "F" = buffer completo (1 KB de RAM): desenha tudo e envia de uma vez.
// Reset do LCD ligado ao 5V pelo próprio módulo -> U8X8_PIN_NONE.
static U8G2_ST7920_128X64_F_HW_SPI lcd(U8G2_R0, PIN_LCD_CS, U8X8_PIN_NONE);

static const uint8_t LINE_H = 10;  // altura de cada linha com a fonte 6x10

static void drawHeader(OverallStatus overall, uint32_t alerts) {
  const char *title = "ESTUFA";
  const char *badge = overall == OverallStatus::Ok      ? "OK"
                    : overall == OverallStatus::Warning ? "ATENCAO"
                                                        : "ALERTA";

  // Alerta visual: com pouca água o cabeçalho alterna a cada atualização
  static bool blinkPhase = false;
  blinkPhase = !blinkPhase;
  bool waterAlert = alerts & (ALERT_RESERVOIR_LOW | ALERT_RESERVOIR_EMPTY);
  if (waterAlert && blinkPhase) {
    title = (alerts & ALERT_RESERVOIR_EMPTY) ? "!! SEM AGUA !!" : "!! AGUA BAIXA !!";
    badge = "";
  }

  lcd.setDrawColor(1);
  lcd.drawBox(0, 0, 128, LINE_H + 1);
  lcd.setDrawColor(0);  // texto "vazado" na faixa preenchida
  lcd.drawStr(2, 9, title);
  lcd.drawStr(126 - lcd.getStrWidth(badge), 9, badge);
  lcd.setDrawColor(1);
}

static void pageSensors(const SensorData &d, const IrrigationStatus &irr) {
  char buf[32];
  uint8_t y = 21;

  if (d.soil.valid) snprintf(buf, sizeof(buf), "Solo:  %3.0f%%", d.soil.percent);
  else              snprintf(buf, sizeof(buf), "Solo:  ERRO");
  lcd.drawStr(0, y, buf);
  y += LINE_H;

  if (d.air.valid) {
    snprintf(buf, sizeof(buf), "Ar: %4.1f\xB0" "C  %3.0f%%", d.air.temperature, d.air.humidity);
  } else {
    snprintf(buf, sizeof(buf), "Ar:   ERRO DHT11");
  }
  lcd.drawStr(0, y, buf);
  y += LINE_H;

  if (d.light.valid) {
    snprintf(buf, sizeof(buf), "Luz:  %3.0f%% %s", d.light.percent,
             d.light.level == LightLevel::Low ? "BAIXA"
             : d.light.level == LightLevel::High ? "ALTA" : "ADEQ.");
  } else {
    snprintf(buf, sizeof(buf), "Luz:  ERRO");
  }
  lcd.drawStr(0, y, buf);
  y += LINE_H;

  if (d.reservoir.valid) {
    const char *lvl = d.reservoir.level == ReservoirLevel::Empty  ? "VAZIO"
                    : d.reservoir.level == ReservoirLevel::Low    ? "BAIXO"
                    : d.reservoir.level == ReservoirLevel::Medium ? "MEDIO"
                                                                  : "CHEIO";
    snprintf(buf, sizeof(buf), "Agua: %3u%% %s", d.reservoir.percent, lvl);
  } else {
    snprintf(buf, sizeof(buf), "Agua: ERRO SENSOR");
  }
  lcd.drawStr(0, y, buf);
  y += LINE_H;

  snprintf(buf, sizeof(buf), "Bomba: %s", irr.pumpOn ? "LIGADA" : "DESLIG.");
  lcd.drawStr(0, y, buf);
  const char *mode = pumpModeName(irr.mode);
  lcd.drawStr(128 - lcd.getStrWidth(mode), y, mode);
}

static void pageSystem(const IrrigationStatus &irr, uint32_t alerts, const NetInfo &net) {
  char buf[32];
  uint8_t y = 21;

  snprintf(buf, sizeof(buf), "Irrig: %s", irrigationStateName(irr.state));
  lcd.drawStr(0, y, buf);
  y += LINE_H;

  if (irr.cooldownRemainingS > 0) {
    snprintf(buf, sizeof(buf), "Proxima em %lu min",
             (unsigned long)((irr.cooldownRemainingS + 59) / 60));
  } else if (irr.lastIrrigationAgoS >= 0) {
    snprintf(buf, sizeof(buf), "Ultima: ha %ld min", (long)(irr.lastIrrigationAgoS / 60));
  } else {
    snprintf(buf, sizeof(buf), "Ultima: nenhuma");
  }
  lcd.drawStr(0, y, buf);
  y += LINE_H;

  // O IP é a informação que o app precisa para conectar
  if (net.staConnected)  snprintf(buf, sizeof(buf), "IP %s", net.staIp.c_str());
  else if (net.apActive) snprintf(buf, sizeof(buf), "AP %s", net.apIp.c_str());
  else                   snprintf(buf, sizeof(buf), "Wi-Fi conectando...");
  lcd.drawStr(0, y, buf);
  y += LINE_H;

  // Até 2 alertas, na ordem de prioridade
  int shown = 0;
  for (int i = 0; i < ALERT_COUNT && shown < 2; i++) {
    if (!(alerts & alertBit(i))) continue;
    lcd.drawStr(0, y, alertShortText(i));
    y += LINE_H;
    shown++;
  }
  if (shown == 0) lcd.drawStr(0, y, "Sem alertas");
}

void displayBegin() {
  // Define os pinos do SPI antes da U8g2 (ela chama SPI.begin() sem pinos,
  // que é ignorado quando o barramento já foi iniciado).
  SPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_SID, -1);
  lcd.setBusClock(LCD_SPI_CLOCK_HZ);
  lcd.begin();
  lcd.setFont(u8g2_font_6x10_tf);
  lcd.setFontPosBaseline();
}

void displayMessage(const char *line1, const char *line2) {
  lcd.clearBuffer();
  lcd.drawStr(0, 24, line1);
  if (line2) lcd.drawStr(0, 40, line2);
  lcd.sendBuffer();
}

void displayUpdate(uint32_t now, const SensorData &d, const IrrigationStatus &irr,
                   uint32_t alerts, OverallStatus overall, const NetInfo &net) {
  uint8_t page = (now / DISPLAY_PAGE_MS) % 2;

  lcd.clearBuffer();
  drawHeader(overall, alerts);
  if (page == 0) pageSensors(d, irr);
  else           pageSystem(irr, alerts, net);
  lcd.sendBuffer();
}
