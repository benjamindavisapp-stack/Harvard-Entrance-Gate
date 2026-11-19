# ESP32 Range Test

Two ESP32-U modules (the ones with an external antenna connector), each on a battery, test how far apart they can still talk. The receiver makes its own Wi-Fi network so you can watch the signal live on an iPad.

The sender also has a solar panel charging its battery, with an INA219 sensor measuring how much power the panel produces. Those readings travel to the iPad with the range-test packets.

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
- **Log this spot**: saves the current reading (including solar watts) with a note such as "150 m". The log is kept on the iPad.
- **Solar panel**: live watts, volts, current, energy collected (Wh) since the sender was switched on, and peak power
- **Solar log**: a one-minute average of the panel's output, kept for 24 hours, shown as a chart. **Download CSV** saves it to the iPad (Files app) for a spreadsheet.

The solar log is stored on the receiver, so it keeps recording even when the iPad page is closed, as long as the receiver is powered and can hear the sender. Minutes when the sender is out of range are skipped. Turning the receiver off clears the log, so download the CSV first.

## Solar / INA219 wiring (sender only)

The INA219 goes in the panel's **positive** wire, between the panel and the charge controller / charger board, so it measures only what the panel produces.

```
Solar panel (+) ──> INA219 VIN+      INA219 VIN- ──> charge controller SOLAR IN (+)
Solar panel (-) ──────────────────────────────────> charge controller SOLAR IN (-)

INA219 VCC ──> ESP32 3.3V
INA219 GND ──> ESP32 GND   (must share ground with the panel's (-) / charge controller)
INA219 SDA ──> ESP32 GPIO21
INA219 SCL ──> ESP32 GPIO22
```

- The INA219 handles up to **26 V** and, with its usual 0.1 Ω shunt, about **3.2 A**. That covers typical 5 V, 6 V, 12 V and "18 V" (12 V nominal) panels up to roughly 50 W. Check your panel's open-circuit voltage (Voc) is under 26 V.
- If the readings show 0 mA in full sun, VIN+ and VIN- are probably swapped.
- If the iPad says "Sender has no INA219 sensor", check the four wires above. The address is 0x40 unless you bridged A0/A1 on the board (change `INA219_ADDRESS` in `sender.ino`).

Rough guide: -30 to -60 dBm excellent, -60 to -70 good, -70 to -80 fair, -80 to -88 weak, below about -90 the link drops.

## Setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Add the ESP32 boards: **Tools > Board > Boards Manager**, search **esp32 by Espressif Systems**, install version **3.x**.
3. Add the sensor library: **Tools > Manage Libraries**, search **Adafruit INA219**, install it (say yes to installing its dependencies).
4. Pick **Tools > Board > ESP32 Dev Module**.
5. Upload `sender/sender.ino` to one module and label it SENDER.
6. Upload `receiver/receiver.ino` to the other and label it RECEIVER.
7. Always upload both sketches from the same version of this code. If they don't match, the receiver ignores the sender's packets.
8. Screw the antennas on **before** powering up. Running the radio without an antenna can damage it.

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
