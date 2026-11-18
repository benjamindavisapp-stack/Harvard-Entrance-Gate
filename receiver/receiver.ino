/*
 * ESP32 Range Test - RECEIVER (the one that stays with you and the iPad)
 *
 * - Listens for the sender's ESP-NOW packets and measures their strength (RSSI)
 *   and how many packets are lost.
 * - Replies to every packet so the sender's side of the link can be measured too.
 * - Creates its own Wi-Fi network. Join it from the iPad and open
 *   http://192.168.4.1 in Safari to see live signal strength.
 *
 * Hardware: ESP32-U module (external antenna) + battery. Nothing else.
 * Board:    "ESP32 Dev Module" in Arduino IDE, esp32 core 3.x by Espressif.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "page.h"

// ---- Settings (channel and magic must match sender.ino) ----
#define WIFI_CHANNEL   1
#define AP_SSID        "ESP32-RangeTest"
#define AP_PASSWORD    "rangetest"     // at least 8 characters
#define PACKET_MAGIC   0x52414E47UL
#define SEND_INTERVAL  100             // must match the sender, used for loss %
#define LOSS_WINDOW_MS 5000            // packet loss is measured over the last 5 s

struct __attribute__((packed)) RangePacket {
  uint32_t magic;
  uint32_t seq;
  int8_t   ackRssi;
  uint8_t  ackFresh;
  uint32_t uptimeMs;
};

struct __attribute__((packed)) AckPacket {
  uint32_t magic;
  uint32_t seq;
};

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

WebServer server(80);
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// Updated by the ESP-NOW callback (inside the lock)
volatile int8_t   lastRssi     = 0;
volatile int8_t   lastAckRssi  = 0;
volatile uint8_t  lastAckFresh = 0;
volatile uint32_t lastSeq      = 0;
volatile uint32_t lastRxMs     = 0;
volatile uint32_t totalRx      = 0;
volatile uint32_t totalLost    = 0;
volatile bool     havePacket   = false;
volatile bool     ackPending   = false;
volatile uint32_t ackSeq       = 0;
volatile int32_t  rssiSum      = 0;   // for a smoothed average
volatile uint16_t rssiCount    = 0;

int8_t minRssi = 0, maxRssi = -127;

// Each received packet's arrival time, used to compute recent packet loss
#define RX_LOG_SIZE 128
volatile uint32_t rxLog[RX_LOG_SIZE];
volatile uint16_t rxLogHead = 0;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(RangePacket)) return;
  RangePacket p;
  memcpy(&p, data, sizeof(p));
  if (p.magic != PACKET_MAGIC) return;

  int8_t rssi = info->rx_ctrl->rssi;
  uint32_t now = millis();

  portENTER_CRITICAL(&lock);
  if (havePacket && p.seq > lastSeq) {
    totalLost += p.seq - lastSeq - 1;
  } else if (havePacket && p.seq < lastSeq) {
    // Sender was restarted; start counting again
    totalLost = 0; totalRx = 0;
  }
  lastSeq      = p.seq;
  lastRssi     = rssi;
  lastAckRssi  = p.ackRssi;
  lastAckFresh = p.ackFresh;
  lastRxMs     = now;
  totalRx++;
  havePacket   = true;
  rssiSum     += rssi;
  rssiCount++;
  rxLog[rxLogHead] = now;
  rxLogHead = (rxLogHead + 1) % RX_LOG_SIZE;
  ackSeq     = p.seq;
  ackPending = true;
  portEXIT_CRITICAL(&lock);
}

void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleData() {
  uint32_t now = millis();

  portENTER_CRITICAL(&lock);
  bool     have     = havePacket;
  int8_t   rssi     = lastRssi;
  int8_t   ackRssi  = lastAckRssi;
  uint8_t  ackFresh = lastAckFresh;
  uint32_t seq      = lastSeq;
  uint32_t rxMs     = lastRxMs;
  uint32_t rx       = totalRx;
  uint32_t lost     = totalLost;
  int32_t  sum      = rssiSum;
  uint16_t cnt      = rssiCount;
  rssiSum = 0; rssiCount = 0;
  uint16_t recent = 0;
  for (int i = 0; i < RX_LOG_SIZE; i++) {
    if (rxLog[i] != 0 && now - rxLog[i] < LOSS_WINDOW_MS) recent++;
  }
  portEXIT_CRITICAL(&lock);

  uint32_t age = have ? now - rxMs : 0;
  bool connected = have && age < 2000;

  // Average RSSI of all packets since the iPad last asked (smoother than one packet)
  int avg = cnt ? (int)(sum / cnt) : rssi;
  if (connected) {
    if (avg < minRssi || minRssi == 0) minRssi = avg;
    if (avg > maxRssi) maxRssi = avg;
  }

  // Loss over the last few seconds: packets we got vs. packets the sender sent
  uint32_t expected = LOSS_WINDOW_MS / SEND_INTERVAL;
  if (now < LOSS_WINDOW_MS) expected = now / SEND_INTERVAL;
  float loss = expected ? 100.0f * (1.0f - (float)recent / expected) : 0;
  if (loss < 0) loss = 0;
  if (!have) loss = 100;

  char json[320];
  snprintf(json, sizeof(json),
    "{\"have\":%s,\"connected\":%s,\"rssi\":%d,\"avg\":%d,\"min\":%d,\"max\":%d,"
    "\"ack\":%d,\"ackOk\":%s,\"loss\":%.1f,\"rx\":%lu,\"lost\":%lu,\"seq\":%lu,\"age\":%lu}",
    have ? "true" : "false", connected ? "true" : "false",
    rssi, avg, minRssi, maxRssi, ackRssi, (ackFresh && connected) ? "true" : "false",
    loss, (unsigned long)rx, (unsigned long)lost, (unsigned long)seq, (unsigned long)age);

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleReset() {
  portENTER_CRITICAL(&lock);
  totalRx = 0; totalLost = 0;
  portEXIT_CRITICAL(&lock);
  minRssi = 0; maxRssi = -127;
  server.send(200, "text/plain", "ok");
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD, WIFI_CHANNEL);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed, restarting");
    delay(1000);
    ESP.restart();
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = WIFI_CHANNEL;
  peer.ifidx   = WIFI_IF_AP;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/reset", handleReset);
  server.begin();

  Serial.printf("Receiver ready. Join Wi-Fi \"%s\" (password \"%s\") and open http://%s\n",
                AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
}

void loop() {
  server.handleClient();

  // Reply to the sender (done here, not in the callback, as ESP-NOW recommends)
  if (ackPending) {
    AckPacket ack;
    portENTER_CRITICAL(&lock);
    ack.seq = ackSeq;
    ackPending = false;
    portEXIT_CRITICAL(&lock);
    ack.magic = PACKET_MAGIC;
    esp_now_send(BROADCAST, (uint8_t *)&ack, sizeof(ack));
  }

  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.printf("rssi %d dBm  rx %lu  lost %lu\n", lastRssi,
                  (unsigned long)totalRx, (unsigned long)totalLost);
  }
}
