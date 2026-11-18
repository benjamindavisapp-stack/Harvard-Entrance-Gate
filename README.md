# ESP32 Range Test

Two ESP32-U modules (the ones with an external antenna connector), each on a battery, test how far apart they can still talk. The receiver makes its own Wi-Fi network so you can watch the signal live on an iPad.

```
 [Sender ESP32-U]  ))) ESP-NOW, 10 packets/s )))  [Receiver ESP32-U]  ~~~ Wi-Fi ~~~  [iPad, Safari]
  walk away with it       <<< replies <<<           stays with you                  http://192.168.4.1
```

## What you see on the iPad

- **Signal strength** (RSSI, in dBm) of the sender, as a big number, bars and a quality label
- **Sender hears us**: the strength of the receiver's replies as heard at the sender, so you can see both directions
- **Packet loss** over the last 5 seconds
- **Best / worst** signal and packet counts
- A **2-minute chart**
- **Log this spot**: saves the current reading with a note such as "150 m". The log is kept on the iPad.

Rough guide: -30 to -60 dBm excellent, -60 to -70 good, -70 to -80 fair, -80 to -88 weak, below about -90 the link drops.

## Setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Add the ESP32 boards: **Tools > Board > Boards Manager**, search **esp32 by Espressif Systems**, install version **3.x**.
3. Pick **Tools > Board > ESP32 Dev Module**.
4. Upload `sender/sender.ino` to one module and label it SENDER.
5. Upload `receiver/receiver.ino` to the other (keep `page.h` in the same folder) and label it RECEIVER.
6. Screw the antennas on **before** powering up. Running the radio without an antenna can damage it.

## Running a test

1. Power both modules from their batteries.
2. On the iPad, open Settings > Wi-Fi and join **ESP32-RangeTest** (password `rangetest`). iPadOS will say "No Internet Connection"; that's expected, stay connected.
3. Open Safari and go to **http://192.168.4.1**. Tip: Share > Add to Home Screen for a full-screen view.
4. Keep the receiver next to you and the iPad. Have someone carry the sender away (or leave the receiver and walk away yourself, as long as the iPad stays within Wi-Fi range of the receiver).
5. Tap **Log this spot** at each distance.

## Settings you can change

At the top of each sketch:

| Setting | Where | Notes |
|---|---|---|
| `WIFI_CHANNEL` | both | Must be the same on both. Try 1, 6 or 11 if there is interference. |
| `SEND_INTERVAL` | both | Packet rate. Must be the same on both. |
| `AP_SSID`, `AP_PASSWORD` | receiver | iPad network name and password (8+ characters). |
| `LED_PIN` | sender | Set to 2 if your board has an LED on GPIO2: solid = receiver answering, blinking = out of range. |

The Serial Monitor (115200 baud) also prints the readings if you have a laptop plugged in.

## Tips for the longest range

- Lift both antennas up: 1–2 m off the ground makes a big difference. Keep them vertical and pointing the same way.
- Keep a clear line of sight. People, cars, trees and walls all cut the signal.
- Use 2.4 GHz antennas (the ones sold for ESP32 / Wi-Fi). Check the antenna connector actually fits the module (usually U.FL / IPEX).
- Use a good battery. A weak battery or thin USB cable can cause brownouts and resets when the radio transmits at full power.
