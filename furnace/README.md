# IoT Heating Oil Gauge Reader (ESP8266 + VL6180X)

This Arduino project is a DIY smart home device that retrofits onto a standard heating oil tank gauge. It uses a **Time-of-Flight (ToF) laser distance sensor** to read the position of the float indicator inside the gauge vial, calculates the remaining gallons, and reports the data via **MQTT** and a local **Web Interface**.
[pictures coming soon]

## 🎯 Features

* **Non-Invasive:** Mounts on top of your existing mechanical gauge (reading the float disk position). No tank drilling required.
* **Accurate:** Uses a **VL6180X** sensor (eg: Adafruit or HiLetgo) with a custom filtering algorithm that takes 20 readings, sorts by median, and rejects outliers (>15% deviation) to ensure stable data.
* **Smart Integration:** Publishes fuel level to **MQTT** (InfluxDB line protocol format) for integration with Home Assistant, Grafana, or OpenHAB.
* **Web Dashboard:** Built-in web server displays live gallons, raw sensor data, system uptime, and logs.
* **Reliability:**
    * **Auto-Recovery:** Automatically reconnects to Wi-Fi and MQTT if the connection is lost.
    * **Background Tasks:** Uses non-blocking delay loops to keep the web server responsive while the sensor settles.
* **OTA Updates:** Supports Over-the-Air firmware updates so you don't have to unmount the device to change code.

## 🛠 Hardware Required

* **Microcontroller:** ESP8266 (e.g., Wemos D1 Mini or NodeMCU like HiLetgo). [example on amazon](https://www.amazon.com/dp/B081CSJV2V?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_3&th=1)
* **Microcontroller Break Out Board:** (optional) ESP8266 breakout board for connecting the mirocontroller to incoming sensor wires. [example on amazon](https://www.amazon.com/dp/B08D3FF6WY?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_2)
* **Sensor:** VL6180X Time-of-Flight Distance Sensor (Adafruit, HiLetgo). [example on amazon](https://www.amazon.com/dp/B0834JTHT2?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_1)
* **Sensor Mount:** (optional) A 3D printed cap/adapter is required to hold the sensor centered above the float. Without this, electrical tape can be used. [TODO]
* **Microcontroller Break Out Board Mount:** (optional) A 3d printed container for the microcontroller break out board. [example on makerworld](https://makerworld.com/en/models/2132616-esp8266-breakout-board-case#profileId-2309598)
* **Wires:** wires to connect from the microcontroller to the sensor. small guage (18AWG or smaller) recommended. Guage and connectors of wires depends on distance to sensor from board.

### Wiring

| VL6180X Pin | ESP8266 Pin | Description |
| :--- | :--- | :--- |
| **VIN** | 3.3V / 5V | Power |
| **GND** | G | Ground |
| **SDA** | D2 (GPIO 4) | I2C Data (Standard Wire.h) |
| **SCL** | D1 (GPIO 5) | I2C Clock (Standard Wire.h) |

## ⚙️ Configuration

The project uses a header file for sensitive configuration.

1.  Rename `env.h.sample` to `env.h`.
2.  Update the following settings in `env.h`:

```cpp
#define ENV_WIFI_SSID "Your_WiFi_Name"
#define ENV_WIFI_PASS "Your_WiFi_Password"
#define ENV_MQTT_HOST "192.168.1.X"      // Your MQTT Broker IP
#define ENV_MQTT_TOPIC "stat/furnace"    // Topic to publish to
#define ENV_HOSTNAME "coop-command"      // mDNS name ([http://coop-command.local](http://coop-command.local))
```
If you wish to disable MQTT (and only have a web interface) leave ENV_MQTT_HOST blank. [TODO]

After this, using the [Arudino IDE](https://www.arduino.cc/en/software/), build and deploy this to your Arduino board. Note that you will need to install libraries thru the IDE like the Adafruit VL6180X, MQTT library, etc.
Getting up and running with Arduino and ESP8266 is outside the scope of this doc.

### Calibration

You must measure the physical limits of your gauge's float movement to calibrate the sensor. Edit the constants in `furnace.ino`:

```cpp
// Tank Capacity
const float GALLONS_FULL = 330.0; // Total tank capacity

// Sensor Calibration (in millimeters)
const float MM_FULL = 12.0;   // Distance from sensor to float when FULL
const float MM_EMPTY = 75.0;  // Distance from sensor to float when EMPTY
```

* **MM_FULL:** The distance (in mm) from the sensor lens to the red disk when the tank is full.
* **MM_EMPTY:** The distance (in mm) from the sensor lens to the red disk when the tank is empty.

## 🚀 Installation & Usage

1.  **Mounting:** Place the sensor directly on top of the clear plastic gauge vial. Ensure it is perfectly vertical and pointing down at the float indicator. If you have no mount, please use electrical tape.
2.  **Power Up:** Connect via USB.
3.  **Web Interface:** Navigate to `http://<device-ip>/` or `http://<hostname>.local` (check this on your router).
    * Check **"Raw Sensor Reading"**. It should roughly match the distance to the red disk inside the vial. (note that there is 5-15% margin of error depending on the type of sensor used).
4.  **MQTT:** The device publishes messages every 30 minutes (and immediately on boot) if the sensor reading is valid (and mqtt is enabled).
    ```text
    Topic: stat/furnace
    Payload: fuel level=275
    ```

## 🧩 How It Works

1.  **Sleep/Wake Cycle:** The device runs a non-blocking loop. Every 30 minutes, it initiates a fuel level update.
2.  **Filtering Logic:**
    * Takes **20 rapid readings**.
    * Sorts readings to find the **median**.
    * Rejects any reading that deviates by more than **15%** (outlier threshold) from the median.
    * Calculates the average of the remaining "valid" inliers.
3.  **Calculation:** The filtered distance (mm) is mapped linearly between `MM_FULL` and `MM_EMPTY` to calculate the gallons remaining.
