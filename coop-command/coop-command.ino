#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <AdafruitIO_WiFi.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#include <Wire.h>
#include <WEMOS_Motor.h>

// #include <Adafruit_GFX.h>
// #include <Adafruit_SSD1306.h>
// #include <Fonts/FreeMonoBold12pt7b.h>
// #include <Fonts/FreeMono9pt7b.h>

#include <LittleFS.h>

#define DEBUG_MODE

#include "env.h"
// #include "site-html.h"
#include "shared-lib-esp8266-pinout.h"
#include "shared-lib-ota.h"
#include "shared-lib-background-tasks.h"
#include "shared-lib-dht-utils.h"
#include "shared-lib-serial.h"

#define DHT_SENSOR_PIN _D4 // TODO: maybe switch to _D8 since D4 is pulled high and can cause issues with failed reads
DHT_Unified dht(DHT_SENSOR_PIN, DHT22);
tempAndHumidity dhtTH;

// Motor shield default I2C Address: 0x30
// PWM frequency: 1000Hz(1kHz)
Motor door0Motor(0x30, _MOTOR_A, 1000); //Motor A
Motor door1Motor(0x30, _MOTOR_B, 1000); //Motor B

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
bool door0 = false;
bool door1 = false;

const int waves = 3;
const String rgbParam[waves] = {"r", "g", "b"};
const String onParam = "on";
const String door0Param = "door0";
const String door1Param = "door1";
const String trueString = "true";
const String falseString = "false";
const String nullString = "null";

int debounceFeed = 0;

// connect to the led control feeds
AdafruitIO_Feed *ledFeed = io.feed("coop-command.leds");
AdafruitIO_Feed *ledOnOffFeed = io.feed("coop-command.leds-on-off");

// connect to the door control feeds
AdafruitIO_Feed *door0Feed = io.feed("coop-command.door0");
AdafruitIO_Feed *door1Feed = io.feed("coop-command.door1");

// set up the 'temperature' and 'humidity' feeds
AdafruitIO_Feed *temperature = io.feed("coop-command.temperature");
AdafruitIO_Feed *humidity = io.feed("coop-command.humidity");

// max history for graph
// 25% of free heap, divided by size of float (4 bytes), divided by number of feeds
// for 2 feeds, this is about 300 points per feed
// static const int MAX_POINTS_PER_FEED = float(ESP.getFreeHeap()) * 0.25 / 4 / 2;
// int *temperatureFeed = new int[MAX_POINTS_PER_FEED];
// int *humidityFeed = new int[MAX_POINTS_PER_FEED];

// void addToFeed(int *feed, float value){
// }

void handleGetLEDs();
void handleGetDoors();

String rgbDisplay()
{
  return String(String(ledsOn ? "(on)" : "(off)") + " rgb(" + String(lastRed) + ", " + String(lastGreen) + ", " + String(lastBlue) + ")");
}

void updateDoors()
{
  // _CCW = opening = true
  // _CW = closing = false

  int door0Dir = door0 ? _CCW : _CW;
  int door1Dir = door1 ? _CCW : _CW;

  _PRINT("[doors] ");
  _PRINT("door0 ");
  _PRINTLN(door0 ? "open" : "closed");

  _PRINT("[doors] ");
  _PRINT("door1 ");
  _PRINTLN(door1 ? "open" : "closed");

  // slow start
  for (int pwm = 1; pwm <= 100; pwm++)
  {
    door0Motor.setmotor(door0Dir, pwm);
    door1Motor.setmotor(door1Dir, pwm);
    delay(100);
    // TODO, standby ?
  }

  // TODO eventually add our limit switch checking logic here
  // once they exist since we can right now just assume that
  // the door is running or stopped after a period of time

  //     Serial.println("Motor STOP");
  //     M1.setmotor(_STOP);
  //     //   M2.setmotor( _STOP);

  //     delay(delayMs);

  //     for (pwm = 0; pwm <= 100; pwm++)
  //     {
  //         M1.setmotor(_CCW, pwm);
  //         //delay(1);
  //         // M2.setmotor(_CCW, pwm);
  //         Serial.print("Counterclockwise PWM: ");
  //         Serial.println(pwm);
  //         delay(100);
  //     }

  //     Serial.println("Motor A&B STANDBY");
  //     M1.setmotor(_STANDBY);
  //     //   M2.setmotor( _STANDBY);
  //     delay(delayMs);
}

void updateLEDs()
{

  _PRINT("[leds] ");
  _PRINTLN(rgbDisplay());

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
  _PRINTLN("[adafruit] " + String(data->feedName()) + " <- " + dataString);

  if (debounceFeed > 0)
  {
    _PRINTLN("[adafruit] ... ignored (debounced)");
    debounceFeed--;
    return;
  }

  if (dataFeed == "coop-command.leds" && dataString.indexOf('#') == 0)
  {
    lastRed = data->toRed();
    lastGreen = data->toGreen();
    lastBlue = data->toBlue();

    updateLEDs();
    return;
  }
  else if (dataFeed == "coop-command.leds-on-off" && (dataString == "ON" || dataString == "OFF" || dataString == "1" || dataString == "0"))
  {

    ledsOn = dataString == "ON" || dataString == "1";

    updateLEDs();
    return;
  }
  else if (dataFeed.indexOf("coop-command.door") == 0 && (dataString == "ON" || dataString == "OFF" || dataString == "1" || dataString == "0"))
  {
    if (dataFeed.indexOf("door0") != -1)
    {
      door0 = dataString == "ON" || dataString == "1";
      updateDoors();
      return;
    }
    else if (dataFeed.indexOf("door1") != -1)
    {
      door1 = dataString == "ON" || dataString == "1";
      updateDoors();
      return;
    }
  }

  _PRINTLN("[adafruit] ... ignored (malformed)");
}

void connectAdafruitIO()
{
  // connect to io.adafruit.com
  _PRINT("[adafruit] connecting");
  io.connect();

  // set up a message handlers for the feeds
  ledFeed->onMessage(handleAdafruitMessage);
  ledOnOffFeed->onMessage(handleAdafruitMessage);
  door0Feed->onMessage(handleAdafruitMessage);
  door1Feed->onMessage(handleAdafruitMessage);

  // wait for a connection
  while (io.status() < AIO_CONNECTED)
  {
    _PRINT(".");
    delay(500);
  }

  // we are connected
  _PRINTLN();
  _PRINT("[adafruit] ");
  _PRINTLN(io.statusText());
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

// char *subString(const char *input, int offset, int len, char *dest)
// {
//   int input_len = strlen(input);

//   if (offset > input_len)
//   {
//     return NULL;
//   }

//   strncpy(dest, input + offset, len);
//   return dest;
// }

void handleRoot()
{
  File f = LittleFS.open("/site.html", "r+");
  server.streamFile(f, "text/html");
  // int buffSize = 128;
  // char destBuff[buffSize + 1];
  // server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  // int i = 0;
  // while (subString(siteHtml, i * buffSize, buffSize, destBuff))
  // {
  //   destBuff[buffSize] = '\0';

  //   if (i != 0)
  //   {
  //     server.sendContent(destBuff);
  //   }
  //   else
  //   {
  //     server.send(200, "text/html", destBuff);
  //   }
  //   break;
  //   i++;
  // }
}

void handleGetBoardInfo()
{
  server.send(200, "application/json", "{\"mem_free_bytes\": " + String(ESP.getFreeHeap()) + "}");
}

void badRequest(String param)
{
  server.send(400, "text/plain", "Bad request: invalid value for '" + param + "' param");
}

bool *parseTrueFalse(String tf)
{
  if (tf == "true")
  {
    return new bool(true);
  }
  else if (tf == "false")
  {
    return new bool(false);
  }
  return NULL;
}

void handlePutLEDs()
{
  int newWave[waves];
  for (int i = 0; i < waves; i++)
  {
    String wave = rgbParam[i];
    if (!server.hasArg(wave))
    {
      badRequest(wave);
      return;
    }

    int waveValue = server.arg(wave).toInt();
    if (isnan(waveValue) || waveValue < 0 || waveValue > 255)
    {
      badRequest(wave);
      return;
    }

    newWave[i] = waveValue;
  }

  bool newOn;
  bool *parsedNewOn = parseTrueFalse(server.arg(onParam));
  if (parsedNewOn != NULL)
  {
    newOn = *parsedNewOn;
  }
  else
  {
    badRequest(onParam);
    return;
  }

  lastRed = newWave[0];
  lastGreen = newWave[1];
  lastBlue = newWave[2];

  ledsOn = newOn;

  char buff[8];
  sprintf(buff, "#%x%x%x", lastRed, lastGreen, lastBlue);

  debounceFeed += 2;

  ledFeed->save(buff);
  // for some reason, no matter what I put here, I cannot send a raw string like "ON" or "OFF" and have it assemble in the API as anything other than 1
  ledOnOffFeed->save(ledsOn);

  _PRINTLN("[adafruit] led feeds updated");

  handleGetLEDs();
  updateLEDs();
}

void handlePutDoors()
{
  bool *newDoor0 = parseTrueFalse(server.arg(door0Param));
  bool *newDoor1 = parseTrueFalse(server.arg(door1Param));

  if (newDoor0 == NULL)
  {
    badRequest(door0Param);
    return;
  }
  if (newDoor1 == NULL)
  {
    badRequest(door1Param);
    return;
  }

  door0 = *newDoor0;
  door1 = *newDoor1;

  debounceFeed += 2;

  door0Feed->save(door0);
  door1Feed->save(door1);

  _PRINTLN("[adafruit] door feeds updated");

  handleGetDoors();
  updateDoors();
}

void handleGetDoors()
{
  String door0OnString = door0 ? trueString : falseString;
  String door1OnString = door1 ? trueString : falseString;
  server.send(200, "application/json", "{\"door0\": " + door0OnString + ", \"door1\": " + door1OnString + "}");
}

void handleGetLEDs()
{
  String ledsOnString = ledsOn ? trueString : falseString;
  server.send(200, "application/json", "{\"r\": " + String(lastRed) + ", \"g\": " + String(lastGreen) + ", \"b\": " + String(lastBlue) + ", \"on\": " + String(ledsOnString) + "}");
}

void handleGetDHT()
{
  String lastTemperatureString = isnan(dhtTH.temperature) || dhtTH.temperature < 0 ? nullString : String(dhtTH.temperature);
  String lastHumidityString = isnan(dhtTH.humidity) || dhtTH.humidity < 0 ? nullString : String(dhtTH.humidity);
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
  _PRINTLN();
  _PRINTLN();
  _PRINT("[wifi] connecting to ");
  _PRINTLN(ENV_WIFI_SSID);
  _PRINT("[wifi] ");
  _FLUSH();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ENV_WIFI_SSID, ENV_WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    _PRINT(F("."));
  }

  _PRINTLN();
  _PRINT("[wifi] connected ");
  _PRINTLN(WiFi.localIP());
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
  server.on("/api/_info", HTTP_GET, handleGetBoardInfo);
  server.on("/api/doors", HTTP_PUT, handlePutDoors);
  server.on("/api/doors", HTTP_GET, handleGetDoors);
  server.on("/api/leds", HTTP_PUT, handlePutLEDs);
  server.on("/api/leds", HTTP_GET, handleGetLEDs);
  server.on("/api/dht", HTTP_GET, handleGetDHT);
  server.onNotFound(handleNotFound);

  // start server
  server.begin();
  _PRINTLN("[http] server started");
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
  // wait at least .5 second between reads
  unsigned long int nowMs = millis();
  if (lastUpdatedMillis > 0 && (nowMs - lastUpdatedMillis) < 500)
  {
    return;
  }

  lastUpdatedMillis = nowMs;

  dhtTH.humidity = readDHTHumidity(&dht);
  dhtTH.temperature = readDHTTemp(&dht);
  //dhtTH.lastUpdated = lastUpdatedMillis;

  // wait at least 60 seconds to report
  if (lastReportedMillis > 0 && (nowMs - lastReportedMillis) < 60000)
  {
    return;
  }

  bool reported = false;

  if (!isnan(dhtTH.temperature) && dhtTH.temperature > 0)
  {
    _PRINTLN(String("[dht] reporting ") + temperatureDisplay(dhtTH));
    temperature->save(dhtTH.temperature);
    reported = true;
  }

  if (!isnan(dhtTH.humidity) && dhtTH.humidity > 0)
  {
    _PRINTLN(String("[dht] reporting ") + humidityDisplay(dhtTH));
    humidity->save(dhtTH.humidity);
    reported = true;
  }

  if (reported)
  {
    lastReportedMillis = nowMs;
  }

  // addToFeed(temperatureFeed, dhtTH.temperature);
  // addToFeed(humidityFeed, dhtTH.humidity);
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
//     _PRINTLN("[button] pressed 4 times in 500ms, disabling");
//     return;
//   }

//   lastPressedMillis = nowMs;
//   buttonPressedTimes = 0;
//   _PRINTLN("[button] pressed");

//   // we will receive the adafruit message, but do this for instant feedback
//   ledsOn = !ledsOn;
//   updateLEDs();

//   String publishValue = String(ledsOn ? "ON" : "OFF");
//   bool published = ledOnOffFeed->save(publishValue);

//   if (published)
//   {
//     _PRINT("[button] published");
//   }
//   else
//   {
//     _PRINT("[button] failed to publish");
//   }
//   _PRINTLN(" " + publishValue + " to " + String(ledOnOffFeed->name));
// }

void sensorInit()
{
  pinMode(DHT_SENSOR_PIN, INPUT);
  dht.begin();
  delay(500);
}

void doorInit()
{
  // TODO
}

void loadInitialState()
{
  // get initial states
  ledFeed->get();
  ledOnOffFeed->get();
  door0Feed->get();
  door1Feed->get();
}

void setup()
{
  // buttonInit();
  serialInit();
  LittleFS.begin();
  // displayInit();
  sensorInit();
  ledInit();
  doorInit();
  connectAdafruitIO();
  loadInitialState();
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
}

void loop()
{
  // dhtTH.humidity = readDHTHumidity(&dht);
  // dhtTH.temperature = readDHTTemp(&dht);

  // _PRINTLN(String("[dht] reporting ") + temperatureDisplay(dhtTH));

  // _PRINTLN(String("[dht] reporting ") + humidityDisplay(dhtTH));
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
  _PRINTLN();
  _PRINT("Configuring access point...");
  / You can remove the password parameter if you want the AP to be open. /
  WiFi.softAP(ssid, password);

  IPAddress myIP = WiFi.softAPIP();
  _PRINT("AP IP address: ");
  _PRINTLN(myIP);
  server.on("/", handleRoot);
  server.begin();
  _PRINTLN("HTTP server started");
}

void loop() {
  server.handleClient();
}
*/

// Start sketch: tb6612 on breakout
// #include "SparkFun_TB6612.h"

// // tb6612 stuff
// #define In1 _D0
// #define In2 _D1
// #define Standby _D3
// // https://github.com/sparkfun/SparkFun_TB6612FNG_Arduino_Library/blob/master/src/SparkFun_TB6612.cpp
// void setup()
// {
//     pinMode(In1, OUTPUT);
//     pinMode(In2, OUTPUT);
//     pinMode(Standby, OUTPUT);
//     digitalWrite(Standby, HIGH);
// }
// void loop()
// {
//     // fwd
//     digitalWrite(In1, HIGH);
//     digitalWrite(In2, LOW);
//     delayWithBackgroundTasks(5000);
//     // rev
//     digitalWrite(In1, LOW);
//     digitalWrite(In2, HIGH);
//     delayWithBackgroundTasks(5000);
//     // stop
//     digitalWrite(In1, HIGH);
//     digitalWrite(In2, HIGH);
//     delayWithBackgroundTasks(5000);
// }

// START SKETCH: motor with d1 mini shield

// const int delayMs = 1000;

// void loop()
// {

// }
