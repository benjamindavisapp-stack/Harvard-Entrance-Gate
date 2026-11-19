/*
 * ESP32 Range Test - SENDER (the one you walk away with)
 *
 * Broadcasts a small ESP-NOW packet 10 times a second. It also listens for
 * the receiver's replies, and puts the strength of the last reply into its
 * next packet, so the iPad can show the signal in both directions.
 *
 * It also reads an INA219 sensor wired in the solar panel's line and sends
 * the panel's voltage, current, power and the energy collected so far.
 *
 * Hardware: ESP32-U module (external antenna) + battery + solar panel + INA219.
 *           INA219 wiring: VCC->3.3V, GND->GND, SDA->GPIO21, SCL->GPIO22,
 *           VIN+ -> solar panel (+), VIN- -> charge controller solar input (+).
 * Board:    "ESP32 Dev Module" in Arduino IDE, esp32 core 3.x by Espressif.
 * Library:  "Adafruit INA219" (Library Manager, install with its dependencies).
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

// ---- Settings (must match receiver.ino) ----
#define WIFI_CHANNEL   1      // same channel as the receiver's Wi-Fi network
#define SEND_INTERVAL  100    // ms between packets (10 per second)
#define LED_PIN        -1     // set to 2 if your board has an LED on GPIO2, -1 = none
#define PACKET_MAGIC   0x52414E47UL  // "RANG"
#define INA219_ADDRESS 0x40   // default; 0x41/0x44/0x45 if you bridged A0/A1
#define I2C_SDA        21
#define I2C_SCL        22

struct __attribute__((packed)) RangePacket {
  uint32_t magic;
  uint32_t seq;        // counts up by 1 every packet, used to measure packet loss
  int8_t   ackRssi;    // strength of the last reply this sender heard (dBm), 0 = none
  uint8_t  ackFresh;   // 1 if that reply arrived in the last second
  uint32_t uptimeMs;
  // Solar panel, from the INA219
  uint8_t  solarOk;    // 1 if the INA219 was found
  float    solarV;     // panel voltage (V)
  float    solarMa;    // current from the panel (mA)
  float    solarMw;    // power from the panel (mW)
  float    solarMwh;   // energy collected since the sender was switched on (mWh)
  float    solarPeakMw;// highest power seen since switch-on (mW)
};

struct __attribute__((packed)) AckPacket {
  uint32_t magic;
  uint32_t seq;
};

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

volatile int8_t   lastAckRssi = 0;
volatile uint32_t lastAckMs   = 0;
uint32_t seq = 0;

Adafruit_INA219 ina219(INA219_ADDRESS);
bool  inaOk     = false;
float energyMwh = 0;
float peakMw    = 0;
uint32_t lastSolarMs = 0;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(AckPacket)) return;
  AckPacket ack;
  memcpy(&ack, data, sizeof(ack));
  if (ack.magic != PACKET_MAGIC) return;
  lastAckRssi = info->rx_ctrl->rssi;
  lastAckMs   = millis();
}

void readSolar(uint32_t now, RangePacket &p) {
  p.solarOk = inaOk;
  p.solarV = p.solarMa = p.solarMw = 0;
  if (inaOk) {
    float shuntMv = ina219.getShuntVoltage_mV();
    p.solarV  = ina219.getBusVoltage_V() + shuntMv / 1000.0f;   // voltage at the panel
    p.solarMa = ina219.getCurrent_mA();
    if (p.solarMa < 0) p.solarMa = 0;    // tiny negative readings at night
    p.solarMw = p.solarV * p.solarMa;
    if (lastSolarMs) energyMwh += p.solarMw * (now - lastSolarMs) / 3600000.0f;
    if (p.solarMw > peakMw) peakMw = p.solarMw;
    lastSolarMs = now;
  }
  p.solarMwh    = energyMwh;
  p.solarPeakMw = peakMw;
}

void setup() {
  Serial.begin(115200);
  if (LED_PIN >= 0) pinMode(LED_PIN, OUTPUT);

  Wire.begin(I2C_SDA, I2C_SCL);
  inaOk = ina219.begin();
  if (inaOk) {
    ina219.setCalibration_32V_2A();   // up to 26 V and about 3.2 A
    Serial.println("INA219 found");
  } else {
    Serial.println("INA219 NOT found - check wiring. Range test still runs.");
  }

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
  readSolar(now, p);
  esp_now_send(BROADCAST, (uint8_t *)&p, sizeof(p));

  // LED: solid while the receiver is answering, slow blink when out of range
  if (LED_PIN >= 0) {
    digitalWrite(LED_PIN, p.ackFresh ? HIGH : ((now / 500) % 2));
  }
  if (p.seq % 10 == 0) {
    Serial.printf("seq %lu  reply rssi %d dBm %s  solar %.2f V %.0f mA %.2f W %.1f mWh\n",
                  (unsigned long)p.seq, p.ackRssi, p.ackFresh ? "" : "(no reply)",
                  p.solarV, p.solarMa, p.solarMw / 1000.0f, p.solarMwh);
  }
}
