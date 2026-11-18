/*
 * ESP32 Range Test - SENDER (the one you walk away with)
 *
 * Broadcasts a small ESP-NOW packet 10 times a second. It also listens for
 * the receiver's replies, and puts the strength of the last reply into its
 * next packet, so the iPad can show the signal in both directions.
 *
 * Hardware: ESP32-U module (external antenna) + battery. Nothing else.
 * Board:    "ESP32 Dev Module" in Arduino IDE, esp32 core 3.x by Espressif.
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ---- Settings (must match receiver.ino) ----
#define WIFI_CHANNEL   1      // same channel as the receiver's Wi-Fi network
#define SEND_INTERVAL  100    // ms between packets (10 per second)
#define LED_PIN        -1     // set to 2 if your board has an LED on GPIO2, -1 = none
#define PACKET_MAGIC   0x52414E47UL  // "RANG"

struct __attribute__((packed)) RangePacket {
  uint32_t magic;
  uint32_t seq;        // counts up by 1 every packet, used to measure packet loss
  int8_t   ackRssi;    // strength of the last reply this sender heard (dBm), 0 = none
  uint8_t  ackFresh;   // 1 if that reply arrived in the last second
  uint32_t uptimeMs;
};

struct __attribute__((packed)) AckPacket {
  uint32_t magic;
  uint32_t seq;
};

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

volatile int8_t   lastAckRssi = 0;
volatile uint32_t lastAckMs   = 0;
uint32_t seq = 0;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(AckPacket)) return;
  AckPacket ack;
  memcpy(&ack, data, sizeof(ack));
  if (ack.magic != PACKET_MAGIC) return;
  lastAckRssi = info->rx_ctrl->rssi;
  lastAckMs   = millis();
}

void setup() {
  Serial.begin(115200);
  if (LED_PIN >= 0) pinMode(LED_PIN, OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);   // maximum transmit power

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed, restarting");
    delay(1000);
    ESP.restart();
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = WIFI_CHANNEL;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.printf("Sender ready on channel %d, MAC %s\n", WIFI_CHANNEL, WiFi.macAddress().c_str());
}

void loop() {
  static uint32_t lastSend = 0;
  uint32_t now = millis();
  if (now - lastSend < SEND_INTERVAL) return;
  lastSend = now;

  RangePacket p;
  p.magic    = PACKET_MAGIC;
  p.seq      = seq++;
  p.ackRssi  = lastAckRssi;
  p.ackFresh = (lastAckMs != 0 && now - lastAckMs < 1000) ? 1 : 0;
  p.uptimeMs = now;
  esp_now_send(BROADCAST, (uint8_t *)&p, sizeof(p));

  // LED: solid while the receiver is answering, slow blink when out of range
  if (LED_PIN >= 0) {
    digitalWrite(LED_PIN, p.ackFresh ? HIGH : ((now / 500) % 2));
  }
  if (p.seq % 10 == 0) {
    Serial.printf("seq %lu  reply rssi %d dBm %s\n", (unsigned long)p.seq, p.ackRssi,
                  p.ackFresh ? "" : "(no reply)");
  }
}
