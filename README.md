# ESP-01 Web Relay

Control an **ESP-01 / ESP-01S 5V Relay Module** from a mobile-friendly web panel. Uses the Arduino IDE (not PlatformIO).

If the module cannot join WiFi, it starts an access point with a config portal so you can scan networks, choose one, enter the password, and still toggle the relay.

## Hardware

Typical ESP-01S 5V relay boards wire the relay to **GPIO0**, **active LOW** (LOW = ON). That is the default in the sketch:

```cpp
#define RELAY_PIN 0
#define RELAY_ACTIVE_LOW true
```

Change those defines if your board differs.

### Flashing

Use a USB-TTL adapter (CH340 / CP2102 / FTDI) wired to the ESP-01:

| Adapter | ESP-01 |
|---------|--------|
| 3.3V    | VCC, CH_PD (EN) |
| GND     | GND |
| TX      | RX |
| RX      | TX |

To enter flash mode: hold **GPIO0** LOW (to GND), then reset (or power on), then release GPIO0. Many USB programmers for ESP-01 have a flash button that does this.

> Use **3.3V logic** on TX/RX. Do not power the ESP from 5V on VCC.

## Arduino IDE setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software).
2. Add ESP8266 board support: **File → Preferences → Additional Board Manager URLs**:

   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```

3. **Tools → Board → Boards Manager** → install **esp8266** by ESP8266 Community.
4. Open `EspWebRelay/EspWebRelay.ino`.
5. Suggested **Tools** settings for ESP-01:

   | Setting       | Value                      |
   |---------------|----------------------------|
   | Board         | Generic ESP8266 Module     |
   | Flash Size    | 1MB (FS:64KB) or match chip |
   | Upload Speed  | 115200                     |
   | Reset Method  | ck                         |
   | CPU Frequency | 80 MHz                     |

6. Select the serial port and click **Upload**.

No extra libraries are required (`ESP8266WiFi`, `ESP8266WebServer`, `EEPROM` ship with the board package).

## First use (AP config portal)

1. Power the relay module. On first boot (no saved WiFi), it creates an AP.
2. On your phone, join WiFi:

   - **SSID:** `ESP-Relay-XXXX` (XXXX = last 4 hex digits of chip ID)
   - **Password:** `relay1234`
3. Open a browser to **http://192.168.4.1**
4. Use **Turn ON / OFF** to control the relay.
5. Under **WiFi settings**, tap a scanned network (or type the SSID), enter the password → **Save & Connect**.
6. The device reboots and joins your network as hostname **`ESP-Relay-XXXX`** (same as the AP SSID). Check Serial Monitor (115200 baud) for the LAN IP, then open that IP (or the hostname, if your router/DNS supports it) in a browser.

If STA join fails within ~15 seconds, it returns to AP mode.

## Web endpoints

| Method | Path          | Action                                      |
|--------|---------------|---------------------------------------------|
| GET    | `/`           | Control panel (WiFi form shown in AP mode)  |
| GET    | `/wifi`       | Panel with WiFi settings (also in STA)      |
| GET    | `/scan`       | JSON list of nearby WiFi networks           |
| GET    | `/on`         | Relay ON                                    |
| GET    | `/off`        | Relay OFF                                   |
| GET    | `/toggle`     | Toggle relay                                |
| GET    | `/status`     | JSON `{"relay":true/false,"mode":"STA"|"AP"}` |
| POST   | `/wifi`       | Save SSID/password → reboot                 |
| POST   | `/reset-wifi` | Clear credentials → reboot into AP          |

## Forget WiFi

On the WiFi settings page, tap **Forget WiFi**, or POST to `/reset-wifi`. The device clears EEPROM credentials and restarts in AP mode.
