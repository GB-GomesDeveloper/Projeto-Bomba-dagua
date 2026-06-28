#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <UrlEncode.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <ArduinoOTA.h>

// ================= PINOS =================
#define LedEsp 2
#define RELEBOMBA 27
#define SENSORWATER 26
// #define BUZZ 13
// ================= WIFI =================
char* ssid = "";
char* pass = "";
// ================= TELNET =================
WiFiServer telnetServer(23);
WiFiClient client;
// ================ http =============
WebServer httpServer(80);
// ==== OTA ===
uint32_t last_ota_time = 0;

// Endereço deste esp
// uint8_t serialMacReceptor[] = { 0x00, 0x70, 0x07, 0x26, 0xA8, 0x34 };

// Endereço esp2
// uint8_t serialMacReceptor[] = { 0x00, 0x70, 0x07, 0x26, 0x33, 0x00 };

// ================= ESP-NOW =================
// Estrutura de dados
typedef struct message {
  float valor;
} message;
message dadosRecebidos;
// Guarda o tempo da última mensagem
unsigned long ultimoRecebido = 0;
// Tempo limite (ms)
int timeout = 1000;  // 1 segundos

//================= FLUXO =================
volatile int pulsos = 0;
float fluxo = 0;
float fluxoFiltrado = 0;
// controle de envio (anti-spam)

// ================= CONTROLE =================
// bool mensagemEnviada = false;
// bool mensagemWhats = false;
unsigned long lastFluxo = 0;
unsigned long lastPrint = 0;
//Sensor
// unsigned long tempoSemFluxo = 0;
// const unsigned long delayFalha = 3000;
// Whats
bool modoAuto = false;
bool semFluxo = true;
// ============= Ip Fixo ==================
IPAddress local_IP(192,168,0,3);

// ================= INTERRUPÇÃO =================
void IRAM_ATTR countPulso() {
  pulsos++;
}
// ================= RECEBIMENTO ESP-NOW =================
void OnDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  memcpy(&dadosRecebidos, data, sizeof(dadosRecebidos));
  // Atualiza tempo
  ultimoRecebido = millis();
  // Liga LED
  digitalWrite(LedEsp, HIGH);
}
// ================= ENVIO WHATS =================
// void sendMessage(String message) {
//   if (WiFi.status() != WL_CONNECTED) return;

//   String url = "https://api.callmebot.com/whatsapp.php?phone=" + number + "&apikey=" + api + "&text=" + urlEncode(message);

//   HTTPClient http;
//   http.begin(url);

//   int httpResponseCode = http.GET();

//   if (httpResponseCode == 200) {
//     Serial.println("Mensagem enviada - Whats");
//   } else {
//     Serial.print("Erro HTTP: ");
//     Serial.println(httpResponseCode);
//   }

//   http.end();
// }
// ================ Request http =============
void responseFluxo() {
  httpServer.send(200, "text/plain", String(fluxoFiltrado));
}
void responseLigar() {
  modoAuto = false;
  digitalWrite(RELEBOMBA, HIGH);
  client.println("Manual - Ligada");
   httpServer.send(200, "text/plain", "sucesso");
}
void responseDesligar() {
  modoAuto = false;
  digitalWrite(RELEBOMBA, LOW);
  client.println("Manual - Desligada");
     httpServer.send(200, "text/plain", "sucesso");
}
void responseAtivarAuto() {
  modoAuto = true;
  client.println("Modo automatico ligado");
     httpServer.send(200, "text/plain", "sucesso");
}
void responseDesativarAuto() {
  modoAuto = false;
  client.println("Modo automatico desligado");
     httpServer.send(200, "text/plain", "sucesso");
}


// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.println("Carregando!");
  // LedComunicação
  pinMode(LedEsp, OUTPUT);
  // Selecionando os pinos da placa e o tipo.
  pinMode(RELEBOMBA, OUTPUT);
  pinMode(SENSORWATER, INPUT_PULLUP);
  // pinMode(BUZZ, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(SENSORWATER), countPulso, RISING);

  // ===== WIFI PRIMEIRO =====
  WiFi.config(local_IP);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  WiFi.setSleep(false);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Falha ao conectar, reiniciando.");
    delay(2000);
    ESP.restart();
  }
  Serial.println("\nConectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // ===== FIXAR CANAL =====
  // FORÇA O CANAL
  // esp_wifi_set_channel(WiFi.channel(), WIFI_SECOND_CHAN_NONE);

  // ===== mDNS =====
  // mDNS (faz OTA aparecer certo)
  MDNS.begin("esp32");

  // ===== OTA =====
  ArduinoOTA
    .onStart([]() {
      Serial.println("Iniciando OTA...");
    })
    .onEnd([]() {
      Serial.println("\nOTA Finalizado");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      if (millis() - last_ota_time > 500) {
        Serial.printf("Progresso: %u%%\n", (progress / (total / 100)));
        last_ota_time = millis();
      }
    })
    .onError([](ota_error_t error) {
      Serial.printf("Erro[%u]\n", error);
    });

  ArduinoOTA.begin();

  // ===== TELNET =====
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("Pronto!");

  // ===== ESP-NOW =====
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao iniciar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("Sistema pronto!");

  // Exe por Http
  httpServer.on("/fluxo", responseFluxo);
  httpServer.on("/on", responseLigar);
  httpServer.on("/off", responseDesligar);
  httpServer.on("/autoOn", responseAtivarAuto);
  httpServer.on("/autoOff", responseDesativarAuto);
  httpServer.begin();
}

// ================= LOOP =================
void loop() {
  httpServer.handleClient();
  ArduinoOTA.handle();
  yield();

  // ===== TELNET =====
  if (telnetServer.hasClient()) {
    WiFiClient newClient = telnetServer.available();

    if (!client || !client.connected()) {
      client = newClient;
      client.setNoDelay(true);
      Serial.println("Telnet conectado");
    } else {
      newClient.stop();
    }
  }

  // ===== FLUXO =====
  if (millis() - lastFluxo > 1000) {
    fluxo = pulsos / 7.5;
    pulsos = 0;
    lastFluxo = millis();

    // filtro com inicialização correta
    if (fluxoFiltrado == 0) {
      fluxoFiltrado = fluxo;
    } else {
      fluxoFiltrado = (fluxoFiltrado * 0.8) + (fluxo * 0.2);
    }
  }

  // ===== COMANDOS =====
  if (client && client.connected() && client.available()) {
    String comando = client.readStringUntil('\n');
    comando.trim();

    if (comando.equalsIgnoreCase("ON")) {
      modoAuto = false;
      digitalWrite(RELEBOMBA, HIGH);
      client.println("Manual - Ligada");

    } else if (comando.equalsIgnoreCase("OFF")) {
      modoAuto = false;
      digitalWrite(RELEBOMBA, LOW);
      client.println("Manual - Desligada");

    } else if (comando.equalsIgnoreCase("AUTO")) {
      modoAuto = true;
      client.println("Modo automatico");
    }
  }

  // ===== LÓGICA AUTOMÁTICA =====
  if (modoAuto) {
    bool caixaCheia = dadosRecebidos.valor >= 0.5;

    // ===== HISTERSE CORRETA =====
    if (fluxoFiltrado >= 1.10) {
      semFluxo = false;
    } else if (fluxoFiltrado <= 0.20) {
      semFluxo = true;
    }

    // ===== CONTROLE =====
    if (caixaCheia || semFluxo) {
      digitalWrite(RELEBOMBA, LOW);
    } else {
      digitalWrite(RELEBOMBA, HIGH);
    }
  }else{
      digitalWrite(RELEBOMBA, HIGH);
  }

  // ===== PRINT CONTROLADO =====
  if (millis() - lastPrint > 1000) {
    if (client && client.connected()) {
      client.print("Fluxo bruto: ");
      client.print(fluxo);

      client.print(" | Filtrado: ");
      client.print(fluxoFiltrado);

      client.print(" | Caixa: ");
      client.print(dadosRecebidos.valor);

      client.print(" | Modo: ");
      client.println(modoAuto ? "AUTO" : "MANUAL");
    }
    lastPrint = millis();
  }







  // ArduinoOTA.handle();
  // yield();

  // // ===== TELNET =====
  // if (server.hasClient()) {
  //   WiFiClient newClient = server.available();

  //   if (!client || !client.connected()) {
  //     client = newClient;
  //     client.setNoDelay(true);
  //     Serial.println("Telnet conectado");
  //   } else {
  //     newClient.stop();
  //   }
  // }

  // // ===== TIMEOUT ESP-NOW =====
  // if (millis() - ultimoRecebido > timeout) {
  //   // Alarme Buzzer
  //   // digitalWrite(BUZZ, HIGH);
  //   // delay(200);
  //   // digitalWrite(BUZZ, LOW);
  //   // Aviso Led
  //   digitalWrite(LedEsp, HIGH);
  //   delay(200);
  //   digitalWrite(LedEsp, LOW);

  //   // digitalWrite(RELEBOMBA, LOW);
  // }

  // // ==== FLUXO ====
  // if (millis() - lastFluxo > 1000) {
  //   fluxo = pulsos / 7.5 * 2;
  //   pulsos = 0;
  //   lastFluxo = millis();

  //   fluxoFiltrado = (fluxoFiltrado * 0.7) + (fluxo * 0.3);

  //   if (client && client.connected()) {
  //     client.print("Fluxo: ");
  //     client.println(fluxoFiltrado);

  //     client.print("Valor recebido: ");
  //     client.println(dadosRecebidos.valor);
  //   }
  // }

  // // Serial.print("Fluxo: ");
  // // Serial.print(fluxo);
  // // Serial.println(" L/min");

  // // Serial.print("Valor recebido: ");
  // // Serial.println(dadosRecebidos.valor);

  // // pulsos = 0;
  // // delay(500);
  // // fluxo = pulsos / 7.5;

  // // =====COMANDO=====
  // if (client.available()) {
  //   String comando = client.readStringUntil('\n');
  //   comando.trim();

  //   if (comando.equalsIgnoreCase("ON")) {
  //     modoAuto = false;
  //     digitalWrite(RELEBOMBA, HIGH);
  //     client.println("Manual - Ligada");

  //   } else if (comando.equalsIgnoreCase("OFF")) {
  //     modoAuto = false;
  //     digitalWrite(RELEBOMBA, LOW);
  //     client.println("Manual - Desligada");

  //   } else if (comando.equalsIgnoreCase("AUTO")) {
  //     modoAuto = true;
  //   }
  // }
  // // ===== LÓGICA ====
  // if (modoAuto) {
  //   bool caixaCheia = dadosRecebidos.valor == 0.50;
  //   bool semFluxo = true;

  //   // histerese
  //   if (fluxoFiltrado >= 1.20) {
  //     semFluxo = false;
  //   } else if (fluxo <= 0.13) {
  //     fluxoFiltrado = true;
  //   }

  //   if (caixaCheia || semFluxo) {
  //     client.println("bomba desligada");
  //     digitalWrite(RELEBOMBA, LOW);
  //   } else {
  //     digitalWrite(RELEBOMBA, HIGH);
  //     client.println("Enchendo caixa");
  //   }
  // }
}
