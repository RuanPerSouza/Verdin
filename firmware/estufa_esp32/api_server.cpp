#include "api_server.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <limits>

#include "irrigation.h"
#include "sensors.h"
#include "settings.h"
#include "status.h"

// =============================================================================
//  ATENÇÃO - CONCORRÊNCIA
// -----------------------------------------------------------------------------
//  O ESPAsyncWebServer responde às requisições em OUTRA tarefa do FreeRTOS,
//  em paralelo com o loop(). Para evitar dados corrompidos:
//   * o loop() publica uma "foto" pronta (texto JSON) protegida por mutex;
//   * as requisições que alteram algo apenas colocam um Command na fila,
//     que o loop() executa na próxima volta.
// =============================================================================

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static QueueHandle_t     cmdQueue = nullptr;
static SemaphoreHandle_t snapMutex = nullptr;

// "Foto" publicada pelo loop() (sempre acessar com o mutex)
static String   snapStatusJson = "{}";
static String   snapConfigJson = "{}";
static Settings snapSettings;
static bool     snapPumpAllowed = false;
static PumpMode snapMode = PumpMode::Auto;

static bool     apStarted   = false;
static bool     mdnsStarted = false;
static bool     wasConnected = false;
static uint32_t staStartedAt = 0;
static uint32_t lastWsCleanup = 0;

// =============================================================================
//  Utilitários
// =============================================================================
class SnapLock {
 public:
  SnapLock() { ok_ = xSemaphoreTake(snapMutex, pdMS_TO_TICKS(100)) == pdTRUE; }
  ~SnapLock() { if (ok_) xSemaphoreGive(snapMutex); }
  bool ok() const { return ok_; }
 private:
  bool ok_;
};

static void sendJson(AsyncWebServerRequest *req, int code, const String &body) {
  req->send(code, "application/json", body);
}

static void sendError(AsyncWebServerRequest *req, int code, const String &msg) {
  JsonDocument doc;
  doc["ok"]    = false;
  doc["error"] = msg;
  String out;
  serializeJson(doc, out);
  sendJson(req, code, out);
}

static void sendOk(AsyncWebServerRequest *req, const char *msg = nullptr) {
  JsonDocument doc;
  doc["ok"] = true;
  if (msg) doc["message"] = msg;
  String out;
  serializeJson(doc, out);
  sendJson(req, 200, out);
}

static bool enqueue(const Command &cmd) {
  return xQueueSend(cmdQueue, &cmd, 0) == pdTRUE;
}

// Lê um campo numérico opcional do JSON, conferindo tipo e limites do tipo T.
template <typename T>
static bool readField(JsonObject obj, const char *key, T &dst, String &err) {
  JsonVariant v = obj[key];
  if (v.isNull()) return true;  // campo ausente = mantém o valor atual
  if (!v.is<long>()) {
    err = String(key) + " deve ser um numero inteiro";
    return false;
  }
  long x = v.as<long>();
  if (x < (long)std::numeric_limits<T>::min() || x > (long)std::numeric_limits<T>::max()) {
    err = String(key) + " fora do intervalo permitido";
    return false;
  }
  dst = (T)x;
  return true;
}

// =============================================================================
//  Serialização (Settings <-> JSON)
// =============================================================================
static void settingsToJson(const Settings &s, JsonObject o) {
  o["soilMinPct"]        = s.soilMinPct;
  o["soilTargetPct"]     = s.soilTargetPct;
  o["pulseS"]            = s.pulseS;
  o["soakS"]             = s.soakS;
  o["maxPulses"]         = s.maxPulses;
  o["cooldownMin"]       = s.cooldownMin;
  o["maxManualS"]        = s.maxManualS;
  o["reservoirMinElec"]  = s.reservoirMinElec;
  o["lightLowPct"]       = s.lightLowPct;
  o["lightHighPct"]      = s.lightHighPct;
  o["tempMinC"]          = s.tempMinC;
  o["tempMaxC"]          = s.tempMaxC;
  o["airHumMinPct"]      = s.airHumMinPct;
  o["airHumMaxPct"]      = s.airHumMaxPct;
  o["soilDryMv"]         = s.soilDryMv;
  o["soilWetMv"]         = s.soilWetMv;
  o["lightDarkMv"]       = s.lightDarkMv;
  o["lightBrightMv"]     = s.lightBrightMv;
  o["electrodeThreshMv"] = s.electrodeThreshMv;

  JsonObject lim = o["limits"].to<JsonObject>();
  lim["userMaxOnS"]     = PUMP_USER_MAX_ON_S;
  lim["absoluteMaxOnS"] = PUMP_ABSOLUTE_MAX_ON_S;
  lim["electrodeCount"] = LEVEL_ELECTRODE_COUNT;
}

static bool settingsFromJson(JsonObject o, Settings &s, String &err) {
  return readField(o, "soilMinPct", s.soilMinPct, err) &&
         readField(o, "soilTargetPct", s.soilTargetPct, err) &&
         readField(o, "pulseS", s.pulseS, err) &&
         readField(o, "soakS", s.soakS, err) &&
         readField(o, "maxPulses", s.maxPulses, err) &&
         readField(o, "cooldownMin", s.cooldownMin, err) &&
         readField(o, "maxManualS", s.maxManualS, err) &&
         readField(o, "reservoirMinElec", s.reservoirMinElec, err) &&
         readField(o, "lightLowPct", s.lightLowPct, err) &&
         readField(o, "lightHighPct", s.lightHighPct, err) &&
         readField(o, "tempMinC", s.tempMinC, err) &&
         readField(o, "tempMaxC", s.tempMaxC, err) &&
         readField(o, "airHumMinPct", s.airHumMinPct, err) &&
         readField(o, "airHumMaxPct", s.airHumMaxPct, err) &&
         readField(o, "soilDryMv", s.soilDryMv, err) &&
         readField(o, "soilWetMv", s.soilWetMv, err) &&
         readField(o, "lightDarkMv", s.lightDarkMv, err) &&
         readField(o, "lightBrightMv", s.lightBrightMv, err) &&
         readField(o, "electrodeThreshMv", s.electrodeThreshMv, err);
}

static String buildConfigJson(const Settings &s) {
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>());
  String out;
  serializeJson(doc, out);
  return out;
}

static String buildStatusJson(uint32_t now, const SensorData &d,
                              const IrrigationStatus &irr, const Settings &s,
                              uint32_t alerts, OverallStatus overall) {
  JsonDocument doc;
  NetInfo net = networkInfo();

  JsonObject dev = doc["device"].to<JsonObject>();
  dev["name"]      = DEVICE_HOSTNAME;
  dev["firmware"]  = FIRMWARE_VERSION;
  dev["uptimeS"]   = now / 1000UL;
  dev["wifiMode"]  = net.staConnected ? "sta" : "ap";
  dev["ip"]        = net.staConnected ? net.staIp : net.apIp;
  dev["rssi"]      = net.rssi;
  dev["freeHeap"]  = ESP.getFreeHeap();
  dev["relayPin"]  = PIN_PUMP;

  JsonObject soil = doc["soil"].to<JsonObject>();
  soil["percent"] = roundf(d.soil.percent * 10) / 10;
  soil["mv"]      = d.soil.mv;
  soil["valid"]   = d.soil.valid;

  JsonObject air = doc["air"].to<JsonObject>();
  air["temperature"] = roundf(d.air.temperature * 10) / 10;
  air["humidity"]    = roundf(d.air.humidity * 10) / 10;
  air["valid"]       = d.air.valid;

  JsonObject light = doc["light"].to<JsonObject>();
  light["percent"] = roundf(d.light.percent * 10) / 10;
  if (d.light.lux >= 0) light["lux"] = roundf(d.light.lux);
  else                  light["lux"] = nullptr;
  light["luxEstimated"] = true;
  light["mv"]      = d.light.mv;
  light["level"]   = lightLevelName(d.light.level);
  light["valid"]   = d.light.valid;

  JsonObject res = doc["reservoir"].to<JsonObject>();
  res["percent"]     = d.reservoir.percent;
  res["meter"]       = d.reservoir.meter;
  res["meterFull"]   = RESERVOIR_METER_FULL;
  res["level"]       = reservoirLevelName(d.reservoir.level);
  res["valid"]       = d.reservoir.valid;
  res["pumpAllowed"] = reservoirAllowsPump(d, s);
  JsonArray el  = res["electrodes"].to<JsonArray>();
  JsonArray emv = res["electrodesMv"].to<JsonArray>();
  for (int i = 0; i < LEVEL_ELECTRODE_COUNT; i++) {
    el.add(d.reservoir.electrodes[i]);
    emv.add(d.reservoir.mv[i]);
  }

  JsonObject pump = doc["pump"].to<JsonObject>();
  pump["on"]                 = irr.pumpOn;
  pump["mode"]               = pumpModeName(irr.mode);
  pump["state"]              = irrigationStateName(irr.state);
  pump["pulsesInCycle"]      = irr.pulsesInCycle;
  pump["maxPulses"]          = s.maxPulses;
  pump["cooldownRemainingS"] = irr.cooldownRemainingS;
  if (irr.lastIrrigationAgoS >= 0) pump["lastIrrigationAgoS"] = irr.lastIrrigationAgoS;
  else                             pump["lastIrrigationAgoS"] = nullptr;
  pump["cyclesSinceBoot"]    = irr.cyclesSinceBoot;

  doc["overall"] = overallName(overall);
  JsonArray al = doc["alerts"].to<JsonArray>();
  for (int i = 0; i < ALERT_COUNT; i++) {
    if (alerts & alertBit(i)) al.add(alertCode(i));
  }
  doc["ts"] = now;

  String out;
  serializeJson(doc, out);
  return out;
}

// =============================================================================
//  Rotas HTTP
// =============================================================================
static void handlePing(AsyncWebServerRequest *req) {
  JsonDocument doc;
  doc["ok"]       = true;
  doc["name"]     = DEVICE_HOSTNAME;
  doc["firmware"] = FIRMWARE_VERSION;
  String out;
  serializeJson(doc, out);
  sendJson(req, 200, out);
}

static void handleStatus(AsyncWebServerRequest *req) {
  String body;
  {
    SnapLock lock;
    if (!lock.ok()) return sendError(req, 503, "ocupado, tente novamente");
    body = snapStatusJson;
  }
  sendJson(req, 200, body);
}

static void handleGetConfig(AsyncWebServerRequest *req) {
  String body;
  {
    SnapLock lock;
    if (!lock.ok()) return sendError(req, 503, "ocupado, tente novamente");
    body = snapConfigJson;
  }
  sendJson(req, 200, body);
}

static void handlePostConfig(AsyncWebServerRequest *req, JsonVariant &json) {
  if (!json.is<JsonObject>()) return sendError(req, 400, "corpo deve ser um objeto JSON");

  Command cmd{};
  cmd.type = CommandType::ApplySettings;
  {
    SnapLock lock;
    if (!lock.ok()) return sendError(req, 503, "ocupado, tente novamente");
    cmd.settings = snapSettings;  // parte da configuração atual
  }

  String err;
  if (!settingsFromJson(json.as<JsonObject>(), cmd.settings, err) ||
      !settingsValidate(cmd.settings, err)) {
    return sendError(req, 400, err);
  }
  if (!enqueue(cmd)) return sendError(req, 503, "fila de comandos cheia");

  String body = buildConfigJson(cmd.settings);
  {
    SnapLock lock;
    if (lock.ok()) {
      snapSettings   = cmd.settings;
      snapConfigJson = body;
    }
  }
  sendJson(req, 200, body);
}

static void handlePump(AsyncWebServerRequest *req, JsonVariant &json) {
  const char *action = json["action"] | "";
  Command cmd{};

  if (strcmp(action, "off") == 0) {
    cmd.type = CommandType::PumpOff;
  } else if (strcmp(action, "on") == 0) {
    bool allowed;
    PumpMode mode;
    uint16_t maxS;
    {
      SnapLock lock;
      if (!lock.ok()) return sendError(req, 503, "ocupado, tente novamente");
      allowed = snapPumpAllowed;
      mode    = snapMode;
      maxS    = snapSettings.maxManualS;
    }
    if (!allowed) return sendError(req, 409, "reservatorio com nivel insuficiente");

    long seconds = json["seconds"] | 5L;
    if (seconds < 1 || seconds > maxS) {
      return sendError(req, 400, "seconds deve ficar entre 1 e " + String(maxS));
    }
    // O app manda "ligar" mesmo com o modo salvo em desligado. Passa para
    // manual antes, senão o comando entra na fila e o loop descarta.
    if (mode == PumpMode::Off) {
      Command setMode{};
      setMode.type  = CommandType::SetMode;
      setMode.value = (uint32_t)PumpMode::Manual;
      if (!enqueue(setMode)) return sendError(req, 503, "fila de comandos cheia");
    }
    cmd.type  = CommandType::PumpOn;
    cmd.value = (uint32_t)seconds;
  } else {
    return sendError(req, 400, "action deve ser \"on\" ou \"off\"");
  }

  if (!enqueue(cmd)) return sendError(req, 503, "fila de comandos cheia");

  JsonDocument doc;
  doc["ok"]     = true;
  doc["action"] = action;
  doc["pin"]    = PIN_PUMP;
  if (cmd.type == CommandType::PumpOn) doc["seconds"] = cmd.value;
  String out;
  serializeJson(doc, out);
  sendJson(req, 200, out);
}

static void handleMode(AsyncWebServerRequest *req, JsonVariant &json) {
  PumpMode mode;
  if (!pumpModeFromName(json["mode"] | "", mode)) {
    return sendError(req, 400, "mode deve ser \"auto\", \"manual\" ou \"off\"");
  }
  Command cmd{};
  cmd.type  = CommandType::SetMode;
  cmd.value = (uint32_t)mode;
  if (!enqueue(cmd)) return sendError(req, 503, "fila de comandos cheia");
  sendOk(req);
}

static void handleCalibrate(AsyncWebServerRequest *req, JsonVariant &json) {
  const char *target = json["target"] | "";
  CalibrationTarget t;
  if      (strcmp(target, "soil_dry") == 0)     t = CalibrationTarget::SoilDry;
  else if (strcmp(target, "soil_wet") == 0)     t = CalibrationTarget::SoilWet;
  else if (strcmp(target, "light_dark") == 0)   t = CalibrationTarget::LightDark;
  else if (strcmp(target, "light_bright") == 0) t = CalibrationTarget::LightBright;
  else return sendError(req, 400, "target deve ser soil_dry, soil_wet, light_dark ou light_bright");

  Command cmd{};
  cmd.type  = CommandType::Calibrate;
  cmd.value = (uint32_t)t;
  if (!enqueue(cmd)) return sendError(req, 503, "fila de comandos cheia");
  sendOk(req, "calibracao sera aplicada com a leitura atual");
}

static void handleSimple(AsyncWebServerRequest *req, CommandType type) {
  Command cmd{};
  cmd.type = type;
  if (!enqueue(cmd)) return sendError(req, 503, "fila de comandos cheia");
  sendOk(req);
}

static void addJsonRoute(const char *uri,
                         void (*fn)(AsyncWebServerRequest *, JsonVariant &)) {
  auto *h = new AsyncCallbackJsonWebHandler(uri, fn);
  h->setMethod(HTTP_POST);
  server.addHandler(h);
}

static void onWsEvent(AsyncWebSocket *srv, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] App #%lu conectado (%s)\n", (unsigned long)client->id(),
                  client->remoteIP().toString().c_str());
    String body;
    {
      SnapLock lock;
      if (lock.ok()) body = snapStatusJson;
    }
    if (body.length()) client->text(body);  // já envia o estado atual
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] App #%lu desconectado\n", (unsigned long)client->id());
  }
}

static void setupRoutes() {
  // CORS: permite testar o app no navegador (Expo Web)
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain; charset=utf-8",
              "Estufa ESP32 " FIRMWARE_VERSION "\n"
              "API: /api/status  /api/config  /api/ping\n"
              "WebSocket: /ws\n");
  });
  server.on("/api/ping", HTTP_GET, handlePing);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);

  addJsonRoute("/api/config", handlePostConfig);
  addJsonRoute("/api/pump", handlePump);
  addJsonRoute("/api/mode", handleMode);
  addJsonRoute("/api/calibrate", handleCalibrate);

  server.on("/api/fault/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
    handleSimple(req, CommandType::ResetFault);
  });
  server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest *req) {
    handleSimple(req, CommandType::FactoryReset);
  });

  server.onNotFound([](AsyncWebServerRequest *req) {
    if (req->method() == HTTP_OPTIONS) {
      req->send(204);  // resposta ao "preflight" do navegador
    } else {
      sendError(req, 404, "rota nao encontrada");
    }
  });

  server.begin();
  Serial.println(F("[WEB] Servidor HTTP iniciado na porta 80"));
}

// =============================================================================
//  Wi-Fi
// =============================================================================
static void startAccessPoint() {
  WiFi.mode(WIFI_AP_STA);  // mantém tentando o roteador em paralelo
  if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    apStarted = true;
    Serial.printf("[WIFI] Rede propria criada: \"%s\" senha \"%s\" -> http://%s\n",
                  AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println(F("[WIFI] Falha ao criar o Access Point"));
  }
}

void networkBegin(QueueHandle_t commandQueue, const Settings &s) {
  cmdQueue  = commandQueue;
  snapMutex = xSemaphoreCreateMutex();
  snapSettings   = s;
  snapConfigJson = buildConfigJson(s);
  snapMode       = s.pumpMode;

  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // o modem em repouso derruba o IP e o app não entra
  esp_wifi_set_ps(WIFI_PS_NONE);
  // IP fixo: o app volta sempre em 172.20.42.179, na rede 172.20.40.0/21.
  WiFi.config(IPAddress(172, 20, 42, 179), IPAddress(172, 20, 40, 1),
              IPAddress(255, 255, 248, 0), IPAddress(172, 20, 40, 1));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  staStartedAt = millis();
  Serial.printf("[WIFI] Conectando em \"%s\"...\n", WIFI_SSID);

  setupRoutes();
}

// Anuncia o IP de tempos em tempos. Sem isso o roteador esquece a placa
// e o app perde a conexão mesmo com o ESP achando que ainda está online.
// Tem que rodar na tarefa do TCP/IP; chamar daqui direto reinicia a placa.
static void announceIp(void *) {
  if (netif_default != nullptr) etharp_gratuitous(netif_default);
}

static void linkWatch(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED) return;
  static uint32_t lastAnnounce = 0;
  if (now - lastAnnounce < 8000) return;
  lastAnnounce = now;
  tcpip_callback(announceIp, nullptr);
}

void networkLoop(uint32_t now) {
  bool connected = WiFi.status() == WL_CONNECTED;

  if (connected && !wasConnected) {
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    Serial.printf("[WIFI] Conectado! IP: %s  (sinal %d dBm)\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    if (!mdnsStarted && MDNS.begin(DEVICE_HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.printf("[WIFI] Tambem acessivel em http://%s.local\n", DEVICE_HOSTNAME);
    }
  } else if (!connected && wasConnected) {
    Serial.println(F("[WIFI] Conexao perdida, tentando reconectar..."));
    staStartedAt = now;
  }
  wasConnected = connected;
  if (connected) linkWatch(now);

  static bool everConnected = false;
  if (connected) everConnected = true;

  // A rede própria derruba o IP do roteador e o app cai. Só cria se nunca
  // chegou a conectar. Reconexão usa reconnect(), sem recomeçar o Wi-Fi.
  if (!connected && !everConnected && !apStarted && now - staStartedAt >= WIFI_STA_TIMEOUT_MS) {
    startAccessPoint();
  }
  if (!connected && everConnected) {
    static uint32_t lastWifiRetry = 0;
    if (now - lastWifiRetry >= 15000) {
      lastWifiRetry = now;
      Serial.println(F("[WIFI] Sem IP, reconectando sem reiniciar o radio..."));
      WiFi.reconnect();
    }
  }

  if (now - lastWsCleanup >= 1000) {
    lastWsCleanup = now;
    ws.cleanupClients(4);  // no máximo 4 apps ao mesmo tempo
  }
}

void networkPublish(uint32_t now, const SensorData &d, const IrrigationStatus &irr,
                    const Settings &s, uint32_t alerts, OverallStatus overall,
                    bool broadcast) {
  // Monta o texto FORA do mutex (é a parte demorada)
  String status = buildStatusJson(now, d, irr, s, alerts, overall);
  {
    SnapLock lock;
    if (lock.ok()) {
      snapStatusJson  = status;
      snapPumpAllowed = reservoirAllowsPump(d, s);
      snapMode        = irr.mode;
      if (memcmp(&snapSettings, &s, sizeof(Settings)) != 0) {
        snapSettings   = s;
        snapConfigJson = buildConfigJson(s);
      }
    }
  }
  if (broadcast && ws.count() > 0) ws.textAll(status);
}

NetInfo networkInfo() {
  NetInfo n;
  n.staConnected = WiFi.status() == WL_CONNECTED;
  n.apActive     = apStarted;
  n.staIp        = n.staConnected ? WiFi.localIP().toString() : String();
  n.apIp         = apStarted ? WiFi.softAPIP().toString() : String();
  n.rssi         = n.staConnected ? WiFi.RSSI() : 0;
  n.wsClients    = ws.count();
  return n;
}
