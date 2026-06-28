#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

// ================= PINOS =================
#define LedEsp 2
#define Boia1 18
#define Boia2 19

// Endereço deste esp
// uint8_t serialMacReceptor[] = { 0x00, 0x70, 0x07, 0x26, 0x33, 0x00 };

// ================= ENDERECO MAC =================
// Endereço esp2
uint8_t serialMacReceptor[] = { 0x00, 0x70, 0x07, 0x26, 0xA8, 0x34 };

// ================= WIFI =================
char* ssid = "";
char* pass = "";

// ================= TELNET =================
WiFiServer server(24);
WiFiClient client;

// ==== OTA ===
uint32_t last_ota_time = 0;

// ================= ESP-NOW =================
typedef struct message {
  float valor;
} message;
message dados;
esp_now_peer_info_t peerInfo;

//  ================= Envio ESP-NOW =================
// Callback de envio
bool estadoAnteriorErro = false;
void OnDataSent(const esp_now_send_info_t* info, esp_now_send_status_t status) {

  bool erroAtual = (status != ESP_NOW_SEND_SUCCESS);
  // Se mudou de estado → imprime
  if (erroAtual != estadoAnteriorErro) {
    if (erroAtual) {
      Serial.println("Status: Error");
      client.println("Status: Error");
    } else {
      Serial.println("Status: Conexao restabelecida");
      client.println("Status: Conexao restabelecida");
    }
    estadoAnteriorErro = erroAtual;
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.println("Carregando!");
  // LedComunicação
  pinMode(LedEsp, OUTPUT);
  pinMode(Boia1, INPUT_PULLUP);
  pinMode(Boia2, INPUT_PULLUP);

  // ===== WIFI PRIMEIRO =====
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  WiFi.setSleep(false);
  esp_wifi_set_max_tx_power(84);  // máximo

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    client.println("Falha ao conectar, reiniciando.");
    delay(2000);
    ESP.restart();
  }
  Serial.println("\nConectado WIFI.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

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
  server.begin();
  server.setNoDelay(true);
  Serial.println("Pronto!");

  // ===== WIFI ESP_NOW =====
  // esp_wifi_set_channel(7, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao iniciar ESP-NOW");
    return;
  }
  Serial.println("Conectado ESP_NOW.");

  esp_now_register_send_cb(OnDataSent);

  // Perr
  memcpy(peerInfo.peer_addr, serialMacReceptor, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Perr verification
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Erro ao adicionar peer");
    return;
  }
}

// ================= LOOP =================
void loop() {
  ArduinoOTA.handle();
  yield();

  // ===== TELNET =====
  if (server.hasClient()) {
    WiFiClient newClient = server.available();

    if (!client || !client.connected()) {
      client = newClient;
      client.setNoDelay(true);
      Serial.println("Telnet conectado");
    } else {
      newClient.stop();
    }
  }

  float resultado;
  int toogleBoia1 = digitalRead(Boia1);
  int toogleBoia2 = digitalRead(Boia2);


  if (toogleBoia1 == LOW && toogleBoia2 == LOW) {
    resultado = 1;
  } else if (toogleBoia1 == LOW || toogleBoia2 == LOW) {
    resultado = 0.5;
  } else {
    resultado = 0;
  }
  delay(500);


  if (client && client.connected()) {
    client.println(resultado);
  }

  dados.valor = resultado;

  esp_now_send(serialMacReceptor, (uint8_t*)&dados, sizeof(dados));
}