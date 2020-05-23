#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <AdafruitIO_WiFi.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/Picopixel.h>

#include <Adafruit_Si7021.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#include "env.h"
#include "site-html.h"

// set up the sensor
// needs A0 since the esp8266 has only one analog input
#define TEMP_PIN 0
#define DHT_SENSOR_PIN 14
DHT_Unified dht(DHT_SENSOR_PIN, DHT22);

Adafruit_Si7021 si7021 = Adafruit_Si7021();

// create instance of oled display
Adafruit_SSD1306 display(128, 64);

// Create an instance of the server
ESP8266WebServer server(ENV_PORT);

// Create an instance of Adafruit HTTP Api Client
AdafruitIO_WiFi io(ENV_AIO_USERNAME, ENV_AIO_KEY, ENV_WIFI_SSID, ENV_WIFI_PASS);

// set up the 'temperature' and 'humidity' feeds
AdafruitIO_Feed *temperature = io.feed("incubator.temperature");
AdafruitIO_Feed *humidity = io.feed("incubator.humidity");

struct tempAndHumidity
{
  float temperature;
  float humidity;
} averageTH, dhtTH, thermistorTH, si7021TH;
const int thArrayLength = 4;
tempAndHumidity *thArray[thArrayLength] = {&averageTH, &dhtTH, &thermistorTH, &si7021TH};
String thNames[thArrayLength] = {String("Average"), String("DHT22"), String("Thermistor"), String("SI7021")};

unsigned long int lastReportedMillis = -1;
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
  display.clearDisplay();                    // Clear the buffer
  display.setTextColor(WHITE);               // Set color of the text
  display.setRotation(0);                    // Set orientation. Goes from 0, 1, 2 or 3

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
  dht.begin();
  si7021.begin();
}

void handleRoot()
{
  server.send(200, "text/html", siteHtml);
}

void handleGetDHT()
{
  String lastTemperatureString = isnan(averageTH.temperature) || averageTH.temperature < 0 ? String("null") : String(averageTH.temperature);
  String lastHumidityString = isnan(averageTH.humidity) || averageTH.humidity < 0 ? String("null") : String(averageTH.humidity);
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

String temperatureDisplay(tempAndHumidity th)
{
  return String("T: ") + (!isnan(th.temperature) && th.temperature > 0 ? String(th.temperature) + String("F") : String("--"));
}

String humidityDisplay(tempAndHumidity th)
{
  return String("H: ") + (!isnan(th.humidity) && th.humidity > 0 ? String(th.humidity) + String("%") : String("--"));
}

float readDHTTemp()
{
  sensors_event_t event;
  dht.temperature().getEvent(&event);

  float newCelsius = event.temperature;
  float newFahrenheit = (newCelsius * 1.8) + 32;

  return newFahrenheit;
}

float readDHTHumidity()
{
  sensors_event_t event;
  dht.humidity().getEvent(&event);
  float newHumidity = event.relative_humidity;

  return newHumidity;
}

float readSi7021Temp()
{
  return si7021.readTemperature() * 1.8 + 32;
}

float readSi7021Humidity()
{
  return si7021.readHumidity();
}

float readThermistorTemp()
{
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

  return newFahrenheit / samples;
}

void updateDHTValues()
{
  // wait at least 1 second between reads
  unsigned long int nowMs = millis();
  if (lastUpdatedMillis > 0 && (nowMs - lastUpdatedMillis) < 1000)
  {
    return;
  }

  lastUpdatedMillis = nowMs;

  dhtTH.humidity = readDHTHumidity();
  si7021TH.humidity = readSi7021Humidity();

  dhtTH.temperature = readDHTTemp();
  si7021TH.temperature = readSi7021Temp();
  thermistorTH.temperature = readThermistorTemp();

  // average
  float averageCountTemp = 0.0;
  float averageCountHumidity = 0.0;
  float averageSumTemp = 0.0;
  float averageSumHumidity = 0.0;

  for (int i = 1; i < thArrayLength; i++)
  {
    String sensorName = thNames[i];
    tempAndHumidity *sensorTH = thArray[i];

    if (!isnan(sensorTH->temperature) && sensorTH->temperature > 0)
    {
      averageCountTemp += 1;
      averageSumTemp += sensorTH->temperature;
    }
    else
    {
      Serial.println(String("Ignoring temperature ") + String(sensorTH->temperature) + String(" for ") + String(sensorName));
    }

    if (!isnan(sensorTH->humidity) && sensorTH->humidity > 0)
    {
      averageCountHumidity += 1;
      averageSumHumidity += sensorTH->humidity;
    }
    else
    {
      Serial.println(String("Ignoring humidity ") + String(sensorTH->humidity) + String(" for ") + String(sensorName));
    }
  }

  if (averageCountTemp > 0)
  {
    averageTH.temperature = averageSumTemp / averageCountTemp;
  }
  if (averageCountHumidity > 0)
  {
    averageTH.humidity = averageSumHumidity / averageCountHumidity;
  }

  // wait at least 30 seconds to report
  if (lastReportedMillis > 0 && (nowMs - lastReportedMillis) < 30000)
  {
    return;
  }

  lastReportedMillis = nowMs;

  if (!isnan(averageTH.temperature) && averageTH.temperature > 0)
  {
    Serial.println(String("Reporting ") + temperatureDisplay(averageTH));
    temperature->save(averageTH.temperature);
  }

  if (!isnan(averageTH.humidity))
  {
    Serial.println(String("Reporting ") + humidityDisplay(averageTH));
    humidity->save(averageTH.humidity);
  }
}

void backgroundTasks()
{
  // io.run(); is required for all sketches.
  // it should always be present at the top of your loop
  // function. it keeps the client connected to
  // io.adafruit.com, and processes any incoming data.
  io.run();

  // handle incoming http clients
  server.handleClient();

  // update local dns, just in case
  MDNS.update();
}

// sleep 20 ms at a time, performing background tasks in between
// this is needed due to the single-threaded nature of arduino and the necessity
// of some foreground tasks to function as intended
// (like updating displays and waiting for N seconds in between)
const unsigned long delayBucketMs = 20;
void delayWithBackgroundTasks(unsigned long ms)
{
  if (ms < delayBucketMs)
  {
    ms = delayBucketMs;
  }

  while (ms > 0)
  {
    ms -= delayBucketMs;
    backgroundTasks();
    delay(delayBucketMs);
  }
}

void renderDisplay()
{
  for (int i = 0; i < thArrayLength; i++)
  {
    delayWithBackgroundTasks(2000);

    // updated models and maybe report
    updateDHTValues();

    tempAndHumidity *sensorTH = thArray[i];
    String sensorName = thNames[i];

    display.clearDisplay();          // Clear the display so we can refresh
    display.setFont(&Picopixel);     // Set a custom font
    display.setTextSize(0);          // Set text size. We are using a custom font so you should always use the text size of 0
    display.setCursor(0, 10);        // (x,y)
    display.println(WiFi.localIP()); // Text or value to print
    display.setFont(&FreeMono9pt7b);
    display.setCursor(0, 30);
    display.println(sensorName);
    display.setCursor(0, 45);
    display.println(temperatureDisplay(*sensorTH));
    display.setCursor(0, 60);
    display.println(humidityDisplay(*sensorTH));
    display.display(); // Print everything we set previously

    delayWithBackgroundTasks(2000);
  }
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
  backgroundTasks();

  // update oled display and fetch values
  renderDisplay();
}
