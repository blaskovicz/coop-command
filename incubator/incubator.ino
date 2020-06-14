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

#include <NTPClient.h>
#include <WiFiUdp.h>

#include "env.h"
#include "site-html.h"
#include "shared-lib-ota.h"
#include "shared-lib-blink.h"
#include "shared-lib-background-tasks.h"
#include "shared-lib-dht-utils.h"
#include "shared-lib-serial.h"

WiFiUDP ntpUDP;

// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset; use New_York epoch seconds offset
NTPClient timeClient(ntpUDP, -14400);

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

tempAndHumidity averageTH, dhtTH, si7021TH;
const int thArrayLength = 3;
tempAndHumidity *thArray[thArrayLength] = {&averageTH, &dhtTH, &si7021TH};
String thNames[thArrayLength] = {String("Average"), String("DHT22"), String("SI7021")};

unsigned long int lastReportedMillis = -1;
unsigned long int lastUpdatedMillis = -1;
String lastUpdated;

void connectAdafruitIO()
{
  // connect to io.adafruit.com
  Serial.print("[adafruit] connecting to Adafruit IO");
  io.connect();

  // wait for a connection
  while (io.status() < AIO_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }

  // we are connected
  Serial.println();
  Serial.print("[adafruit] ");
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
  server.send(200, "application/json", "{\"temperature\": " + lastTemperatureString + ", \"humidity\": " + lastHumidityString + ", \"as_of\": \"" + lastUpdated + "\"}");
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
  Serial.print("[wifi] connecting to ");
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

  Serial.print("[wifi] connected");
  Serial.println(WiFi.localIP());
}

void connectAndServeHTTP()
{
  connectWifiClient();

  // start local dns server (incubator.local)
  if (MDNS.begin(ENV_HOSTNAME))
  {
    Serial.printf("[wifi] MDNS responder started for %s.local\n", ENV_HOSTNAME);
  }

  // mount server routes
  server.on("/", handleRoot);
  server.on("/api/dht", handleGetDHT);
  server.onNotFound(handleNotFound);

  // start server
  server.begin();
  Serial.println("[http] server started");
}

float readSi7021Temp()
{
  return si7021.readTemperature() * 1.8 + 32;
}

float readSi7021Humidity()
{
  return si7021.readHumidity();
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

  dhtTH.humidity = readDHTHumidity(&dht);
  si7021TH.humidity = readSi7021Humidity();

  dhtTH.temperature = readDHTTemp(&dht);
  si7021TH.temperature = readSi7021Temp();

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
      Serial.println(String("[update-dht-values] ignoring temperature ") + String(sensorTH->temperature) + String(" for ") + String(sensorName));
    }

    if (!isnan(sensorTH->humidity) && sensorTH->humidity > 0)
    {
      averageCountHumidity += 1;
      averageSumHumidity += sensorTH->humidity;
    }
    else
    {
      Serial.println(String("[update-dht-values] ignoring humidity ") + String(sensorTH->humidity) + String(" for ") + String(sensorName));
    }
  }

  if (averageCountTemp > 0.0)
  {
    averageTH.temperature = averageSumTemp / averageCountTemp;
  }
  else
  {
    averageTH.temperature = NAN;
  }

  if (averageCountHumidity > 0.0)
  {
    averageTH.humidity = averageSumHumidity / averageCountHumidity;
  }
  else
  {
    averageTH.humidity = NAN;
  }

  lastUpdated = timeClient.getFormattedTime();

  // wait at least 30 seconds to report
  if (lastReportedMillis > 0 && (nowMs - lastReportedMillis) < 30000)
  {
    return;
  }

  lastReportedMillis = nowMs;

  if (!isnan(averageTH.temperature) && averageTH.temperature > 0)
  {
    Serial.println(String("[update-dht-values] reporting ") + temperatureDisplay(averageTH));
    temperature->save(averageTH.temperature);
  }

  if (!isnan(averageTH.humidity))
  {
    Serial.println(String("[update-dht-values] reporting ") + humidityDisplay(averageTH));
    humidity->save(averageTH.humidity);
  }
}

void renderDisplay()
{
  for (int i = 0; i < thArrayLength; i++)
  {
    delayWithBackgroundTasks(2000);

    tempAndHumidity *sensorTH = thArray[i];
    String sensorName = thNames[i];

    Serial.println("[render] " + sensorName);
    display.clearDisplay();        // Clear the display so we can refresh
    display.setFont(&Picopixel);   // Set a custom font
    display.setTextSize(0);        // Set text size. We are using a custom font so you should always use the text size of 0
    display.setCursor(0, 10);      // (x,y)
    display.print(WiFi.localIP()); // Text or value to print
    display.print(" ");
    display.println(lastUpdated);
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
  timeClient.begin();
  displayInit();
  sensorInit();
  connectAdafruitIO();
  otaInit(ENV_HOSTNAME, ENV_OTA_PASSWORD);
  connectAndServeHTTP();

  // keep our client connected to
  // io.adafruit.com, and processes any incoming data.
  registerBackgroundTask([]() { io.run(); });

  // read temperature and humidity, report
  registerBackgroundTask([]() { updateDHTValues(); });

  // handle incoming http clients
  registerBackgroundTask([]() { server.handleClient(); });

  // update local dns, just in case
  registerBackgroundTask([]() { MDNS.update(); });

  // check if we have ota updates
  registerBackgroundTask([]() { handleOTA(); });

  registerBackgroundTask([]() { timeClient.update(); });

  // TODO make a buzz on speaker if temp or humidty is out of range for too long
  blink();
}

void loop()
{
  backgroundTasks();

  // update oled display and fetch values
  renderDisplay();
}
