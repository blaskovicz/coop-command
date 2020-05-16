#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <AdafruitIO_WiFi.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/Picopixel.h>

// #include <Adafruit_Sensor.h>
// #include <DHT.h>
// #include <DHT_U.h>
// #include <dht_nonblocking.h>

#include "env.h"

static const uint8_t _D0 = 16;
static const uint8_t _D1 = 5;
static const uint8_t _D2 = 4;
static const uint8_t _D3 = 0;
static const uint8_t _D4 = 2;
static const uint8_t _D5 = 14;
static const uint8_t _D6 = 12;
static const uint8_t _D7 = 13;
static const uint8_t _D8 = 15;
static const uint8_t _D9 = 3;
static const uint8_t _D10 = 1;

// set up the sensor
// needs A0 since the esp8266 has only one analog input
#define TEMP_PIN 0
// #define DHT_SENSOR_PIN 14
// DHT_Unified dht(DHT_SENSOR_PIN, DHT11);
// DHT_nonblocking dht(DHT_SENSOR_PIN, DHT_TYPE_11);

// create instance of oled display
Adafruit_SSD1306 display(128, 64);

// Create an instance of the server
ESP8266WebServer server(ENV_PORT);

// Create an instance of Adafruit HTTP Api Client
AdafruitIO_WiFi io(ENV_AIO_USERNAME, ENV_AIO_KEY, ENV_WIFI_SSID, ENV_WIFI_PASS);

// set up the 'temperature' and 'humidity' feeds
AdafruitIO_Feed *temperature = io.feed("incubator.temperature");
AdafruitIO_Feed *humidity = io.feed("incubator.humidity");

float lastHumidity = -1;
float lastTemperature = -1;
unsigned long int lastUpdatedMillis = -1;

void connectAdafruitIO()
{
  // connect to io.adafruit.com
  Serial.print("Connecting to Adafruit IO");
  io.connect();

  // wait for a connection
  while (io.status() < AIO_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }

  // we are connected
  Serial.println();
  Serial.println(io.statusText());
}

void displayInit()
{
  delay(100); // This delay is needed to let the display to initialize

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Initialize display with the I2C address of 0x3C

  display.clearDisplay(); // Clear the buffer

  display.setTextColor(WHITE); // Set color of the text

  display.setRotation(0); // Set orientation. Goes from 0, 1, 2 or 3

  //display.setTextWrap(false);  // By default, long lines of text are set to automatically “wrap” back to the leftmost column.
  // To override this behavior (so text will run off the right side of the display - useful for
  // scrolling marquee effects), use setTextWrap(false). The normal wrapping behavior is restored
  // with setTextWrap(true).

  display.dim(0); //Set brightness (0 is maximun and 1 is a little dim)
}

void serialInit()
{
  // set up serial monitor and wait for it to open
  Serial.begin(9600);

  do
  {
    delay(100);
  } while (!Serial);

  Serial.println();
}

void sensorInit()
{
  // analogReference
  // dht.begin();
}

const char *indexPage = R""""(
<html>
  <head>
    <title>Coop Command</title>
    <script src="https://cdn.jsdelivr.net/npm/vue@2.5.17/dist/vue.js"></script>
    <style>
      .led {
        display: inline-block;
        width: 50px;
        height: 50px;
        margin-left: 10px;
        border: 1px solid #000;
        border-radius: 2px;
        vertical-align: middle;
      }
      .error {
        color: #ea1717;
      }
    </style>
  </head>
  <body>
    <div id='app'>
      <h1>Incubator Command</h1>
      <h2 v-if="error" class="error">An error occurred fetching data: <pre>{{error}}</pre></h2>
      <div v-if="!error">
        <h2>Temperature: {{temperatureDisplay}}</h2>
        <h2>Humidity: {{humidityDisplay}}</h2>
      </div>
    </div>

    <script>
      new Vue({
        el: '#app',
        async created() {
          await this.fetchState();
        },
        methods: {
          async fetchState() {
            const resp = await fetch('/api/dht');
            if (!resp.ok) {
              this.error = resp.statusText;
            }
            const data = await resp.json();
            this.temperature = data.temperature;
            this.humidity = data.humidity;
          },
        },
        computed: {
            temperatureDisplay() {
                return this.temperature !== null ? `${this.temperature} F` : '--';
            },
            humidityDisplay() {
                return this.humidity !== null ? `${this.humidity} %` : '--';
            }
        },
        data: {
          error: null,
          temperature: null,
          humidity: null,
        },
      });
    </script>
  </body>
</html>
)"""";

void handleRoot()
{
  server.send(200, "text/html", indexPage);
}

void handleGetDHT()
{
  String lastTemperatureString = lastTemperature < 0 ? String("null") : String(lastTemperature);
  String lastHumidityString = lastHumidity < 0 ? String("null") : String(lastHumidity);
  server.send(200, "application/json", "{\"temperature\": " + lastTemperatureString + ", \"humidity\": " + lastHumidityString + "}");
}

void handleNotFound()
{
  String message = "Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArgs: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void connectWifiClient()
{
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print(F("Connecting to "));
  Serial.println(ENV_WIFI_SSID);
  Serial.flush();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ENV_WIFI_SSID, ENV_WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println();
  Serial.println(F("WiFi connected"));
}

void connectAndServeHTTP()
{
  connectWifiClient();

  // start local dns server (coop-command.local)
  if (MDNS.begin("incubator-command"))
  {
    Serial.println("MDNS responder started");
  }

  // mount server routes
  server.on("/", handleRoot);
  server.on("/api/dht", handleGetDHT);
  server.onNotFound(handleNotFound);

  // start server
  server.begin();
  Serial.println(F("Server started"));

  // Print the IP address
  Serial.println(WiFi.localIP());
}

String temperatureDisplay()
{
  return String("T: ") + (lastTemperature >= 0 ? String(lastTemperature) + String(" F") : String("--"));
}

String humidityDisplay()
{
  return String("H: ") + (lastHumidity >= 0 ? String(lastHumidity) + String(" %") : String("--"));
}

void updateDHTValues()
{
  // wait at least 5 seconds between updates
  unsigned long int nowMs = millis();
  if (lastUpdatedMillis > 0 && (nowMs - lastUpdatedMillis) < 5000)
  {
    return;
  }

  lastUpdatedMillis = nowMs;

  // float newCelsius;
  // float newHumidity;

  // if (!dht.measure(&newCelsius, &newHumidity))
  // {
  //     return;
  // }

  // sensors_event_t event;

  // dht.temperature().getEvent(&event);

  // float newCelsius = event.temperature;
  // float newFahrenheit = (newCelsius * 1.8) + 32;

  float newFahrenheit;
  int samples = 10;
  for (int i = 0; i < samples; i++)
  {
    int tempReading = analogRead(TEMP_PIN);
    double newKelvin = log(10000.0 * ((1024.0 / tempReading - 1)));
    newKelvin = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * newKelvin * newKelvin)) * newKelvin);
    float newCelsius = newKelvin - 273.15;
    float newFahrenheitL = (newCelsius * 9.0) / 5.0 + 32.0;

    newFahrenheit = newFahrenheit + newFahrenheitL;
    delay(10);
  }

  newFahrenheit = newFahrenheit / samples;

  if (lastTemperature != newFahrenheit)
  {
    lastTemperature = newFahrenheit;
    Serial.println(temperatureDisplay());
    temperature->save(newFahrenheit);
  }

  // dht.humidity().getEvent(&event);

  // newHumidity = event.relative_humidity;

  // if (lastHumidity != newHumidity)
  // {
  //     lastHumidity = newHumidity;
  //     Serial.println(humidityDisplay());
  //     humidity->save(newHumidity);
  // }
}

void renderDisplay()
{
  display.clearDisplay();          // Clear the display so we can refresh
  display.setFont(&Picopixel);     // Set a custom font
  display.setTextSize(0);          // Set text size. We are using a custom font so you should always use the text size of 0
  display.setCursor(0, 10);        // (x,y)
  display.println(WiFi.localIP()); // Text or value to print
  display.setCursor(0, 30);
  display.setFont(&FreeMono9pt7b); // Set a custom font
  display.println(temperatureDisplay());
  display.setCursor(0, 50);
  display.println(humidityDisplay());
  display.display(); // Print everything we set previously
}

void setup()
{
  serialInit();
  displayInit();
  sensorInit();
  connectAdafruitIO();
  connectAndServeHTTP();
  // TODO make a buzz on speaker if temp or humidty is out of range for too long
}

void loop()
{
  // io.run(); is required for all sketches.
  // it should always be present at the top of your loop
  // function. it keeps the client connected to
  // io.adafruit.com, and processes any incoming data.
  io.run();

  // read DHT
  updateDHTValues();

  // handle incoming http clients
  server.handleClient();

  // update local dns, just in case
  MDNS.update();

  // update oled display
  renderDisplay();
}
