#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <AdafruitIO_WiFi.h>

#include <DHT.h>
#include <DHT_U.h>

// #include <Adafruit_GFX.h>
// #include <Adafruit_SSD1306.h>
// #include <Fonts/FreeMonoBold12pt7b.h>
// #include <Fonts/FreeMono9pt7b.h>

#include "env.h"
#include "site-html.h"
#include "shared-lib-esp8266-pinout.h"
#include "shared-lib-ota.h"
#include "shared-lib-blink.h"
#include "shared-lib-background-tasks.h"
#include "shared-lib-dht-utils.h"

DHT_Unified dht(_D4, DHT22);
tempAndHumidity dhtTH;

// const int pinButtonOnOff = _D3;

const int pinBlue = _D7;
const int pinGreen = _D5;
const int pinRed = _D6;

unsigned long int lastReportedMillis = -1;
unsigned long int lastUpdatedMillis = -1;
// unsigned long int lastPressedMillis = -1;
// unsigned long int buttonPressedTimes = 0;
// bool buttonDisabled = false;

// create instance of oled display
// Adafruit_SSD1306 display(128, 64);

// Create an instance of the server
ESP8266WebServer server(ENV_PORT);

// Create an instance of Adafruit HTTP Api Client
AdafruitIO_WiFi io(ENV_AIO_USERNAME, ENV_AIO_KEY, ENV_WIFI_SSID, ENV_WIFI_PASS);

int lastRed = 0;
int lastGreen = 0;
int lastBlue = 0;
bool ledsOn = false;

// Using that, connect to the led control feed
AdafruitIO_Feed *ledFeed = io.feed("coop-command.leds");
AdafruitIO_Feed *ledOnOffFeed = io.feed("coop-command.leds-on-off");

// set up the 'temperature' and 'humidity' feeds
AdafruitIO_Feed *temperature = io.feed("coop-command.temperature");
AdafruitIO_Feed *humidity = io.feed("coop-command.humidity");

String rgbDisplay()
{
  return String(String(ledsOn ? "(on)" : "(off)") + " rgb(" + String(lastRed) + ", " + String(lastGreen) + ", " + String(lastBlue) + ")");
}

void updateLEDs()
{

  Serial.print("[leds] ");
  Serial.println(rgbDisplay());

  if (!ledsOn)
  {
    analogWrite(pinRed, 0);
    analogWrite(pinGreen, 0);
    analogWrite(pinBlue, 0);
  }
  else
  {
    analogWrite(pinRed, lastRed);
    analogWrite(pinGreen, lastGreen);
    analogWrite(pinBlue, lastBlue);
  }
}

// update the led rgb value when we receive valid feed message
void handleAdafruitMessage(AdafruitIO_Data *data)
{
  String dataString = data->toString();
  String dataFeed = data->feedName();
  Serial.println("[adafruit] " + String(data->feedName()) + " <- " + dataString);

  if (dataFeed == "coop-command.leds" && dataString.indexOf('#') == 0)
  {
    lastRed = data->toRed();
    lastGreen = data->toGreen();
    lastBlue = data->toBlue();

    updateLEDs();
    return;
  }
  else if (dataFeed == "coop-command.leds-on-off" && (dataString == "ON" || dataString == "OFF"))
  {
    if (dataString == "ON")
    {
      ledsOn = true;
    }
    else
    {
      ledsOn = false;
    }

    updateLEDs();
    return;
  }

  Serial.println("[adafruit] malformed message, ignored");
}

void connectAdafruitIO()
{
  // connect to io.adafruit.com
  Serial.print("[adafruit] connecting");
  io.connect();

  // set up a message handlers for the feeds
  ledFeed->onMessage(handleAdafruitMessage);
  ledOnOffFeed->onMessage(handleAdafruitMessage);

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

  // get initial states
  ledFeed->get();
  ledOnOffFeed->get();
}

// void displayInit()
// {
//   delay(100); // This delay is needed to let the display to initialize

//   display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Initialize display with the I2C address of 0x3C

//   display.clearDisplay(); // Clear the buffer

//   display.setTextColor(WHITE); // Set color of the text

//   display.setRotation(0); // Set orientation. Goes from 0, 1, 2 or 3

//   //display.setTextWrap(false);  // By default, long lines of text are set to automatically “wrap” back to the leftmost column.
//   // To override this behavior (so text will run off the right side of the display - useful for
//   // scrolling marquee effects), use setTextWrap(false). The normal wrapping behavior is restored
//   // with setTextWrap(true).

//   display.dim(0); //Set brightness (0 is maximun and 1 is a little dim)
// }

void ledInit()
{
  // prepare LEDs
  int pins[3] = {pinRed, pinGreen, pinBlue};
  for (int i = 0; i < 3; i++)
  {
    pinMode(pins[i], OUTPUT);
    analogWrite(pins[i], 255);
  }
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

void handleRoot()
{
  server.send(200, "text/html", siteHtml);
}

void handleGetLEDs()
{
  // TODO: caching reads
  server.send(200, "application/json", "{\"r\": " + String(lastRed) + ", \"g\": " + String(lastGreen) + ", \"b\": " + String(lastBlue) + "}");
}

void handleGetDHT()
{
  String lastTemperatureString = isnan(dhtTH.temperature) || dhtTH.temperature < 0 ? String("null") : String(dhtTH.temperature);
  String lastHumidityString = isnan(dhtTH.humidity) || dhtTH.humidity < 0 ? String("null") : String(dhtTH.humidity);
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
  Serial.print("[wifi] connecting to ");
  Serial.println(ENV_WIFI_SSID);
  Serial.print("[wifi] ");
  Serial.flush();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ENV_WIFI_SSID, ENV_WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(F("."));
  }

  Serial.println();
  Serial.print("[wifi] connected ");
  Serial.println(WiFi.localIP());
}

void connectAndServeHTTP()
{
  connectWifiClient();

  // start local dns server (coop-command.local)
  if (MDNS.begin(ENV_HOSTNAME))
  {
    Serial.printf("[http] MDNS responder started for %s.local\n", ENV_HOSTNAME);
  }

  // mount server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/leds", HTTP_GET, handleGetLEDs);
  server.on("/api/dht", HTTP_GET, handleGetDHT);
  server.onNotFound(handleNotFound);

  // start server
  server.begin();
  Serial.println("[http] server started");
}

// void renderDisplay()
// {
//   display.clearDisplay();          // Clear the display so we can refresh
//   display.setFont(&FreeMono9pt7b); // Set a custom font
//   display.setTextSize(0);          // Set text size. We are using a custom font so you should always use the text size of 0
//   display.setCursor(0, 10);        // (x,y)
//   display.println(WiFi.localIP()); // Text or value to print
//   display.setCursor(0, 30);
//   display.println(rgbDisplay());
//   display.display(); // Print everything we set previously
// }

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
  dhtTH.temperature = readDHTTemp(&dht);

  // wait at least 60 seconds to report
  if (lastReportedMillis > 0 && (nowMs - lastReportedMillis) < 60000)
  {
    return;
  }

  lastReportedMillis = nowMs;

  if (!isnan(dhtTH.temperature) && dhtTH.temperature > 0)
  {
    Serial.println(String("[dht] reporting ") + temperatureDisplay(dhtTH));
    temperature->save(dhtTH.temperature);
  }

  if (!isnan(dhtTH.humidity) && dhtTH.humidity > 0)
  {
    Serial.println(String("[dht] reporting ") + humidityDisplay(dhtTH));
    humidity->save(dhtTH.humidity);
  }
}

// void buttonInit()
// {
//   pinMode(pinButtonOnOff, INPUT);
// }

// void readButton()
// {
//   // incorrect wiring
//   if (buttonDisabled)
//   {
//     return;
//   }

//   int buttonState = digitalRead(pinButtonOnOff);
//   if (buttonState != HIGH)
//   {
//     return;
//   }

//   // wait at least 0.5 seconds between reads
//   unsigned long int nowMs = millis();
//   if (lastPressedMillis > 0 && (nowMs - lastPressedMillis) < 500)
//   {
//     buttonPressedTimes++;
//     return;
//   }

//   if (buttonPressedTimes > 4)
//   {
//     buttonDisabled = true;
//     Serial.println("[button] pressed 4 times in 500ms, disabling");
//     return;
//   }

//   lastPressedMillis = nowMs;
//   buttonPressedTimes = 0;
//   Serial.println("[button] pressed");

//   // we will receive the adafruit message, but do this for instant feedback
//   ledsOn = !ledsOn;
//   updateLEDs();

//   String publishValue = String(ledsOn ? "ON" : "OFF");
//   bool published = ledOnOffFeed->save(publishValue);

//   if (published)
//   {
//     Serial.print("[button] published");
//   }
//   else
//   {
//     Serial.print("[button] failed to publish");
//   }
//   Serial.println(" " + publishValue + " to " + String(ledOnOffFeed->name));
// }

void sensorInit()
{
  dht.begin();
}

void setup()
{
  // buttonInit();
  serialInit();
  // displayInit();
  sensorInit();
  ledInit();
  connectAdafruitIO();
  otaInit(ENV_HOSTNAME, ENV_OTA_PASSWORD);
  connectAndServeHTTP();

  // keep our client connected to
  // io.adafruit.com, and processes any incoming data.
  registerBackgroundTask([]() { io.run(); });

  // read temperature and humidity, report
  registerBackgroundTask([]() { updateDHTValues(); });

  // read button
  // registerBackgroundTask([]() { readButton(); });

  // handle incoming http clients
  registerBackgroundTask([]() { server.handleClient(); });

  // update local dns, just in case
  registerBackgroundTask([]() { MDNS.update(); });

  // update oled display
  // registerBackgroundTask([]() { renderDisplay(); });

  // check if we have ota updates
  registerBackgroundTask([]() { handleOTA(); });

  blink();
}

void loop()
{
  backgroundTasks();
}

// TODO: bootstrap AP with network vvv
/*

/ Just a little test message.  Go to http://192.168.4.1 in a web browser
   connected to this access point to see it.
/
void handleRoot() {
  server.send(200, "text/html", "<h1>You are connected</h1>");
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();
  Serial.print("Configuring access point...");
  / You can remove the password parameter if you want the AP to be open. /
  WiFi.softAP(ssid, password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
*/