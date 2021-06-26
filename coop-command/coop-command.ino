#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#include <Wire.h>
#include <WEMOS_Motor.h>

#include <MQTT.h>

#include "env.h"
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

const int pinBlue = _D7;
const int pinGreen = _D5;
const int pinRed = _D6;

unsigned long int lastReportedMillis = -1;
unsigned long int lastUpdatedMillis = -1;

// default LED to red
int lastRed = 255;
int lastGreen = 14;
int lastBlue = 14;

// default LED to off
bool ledsOn = false;

// default doors to closed
bool door0 = false;
bool door1 = false;

const String trueString = "t";
const String falseString = "f";
WiFiClient net;
MQTTClient client;
const String commandTopicPrefix = "cmd/coop_command/";
const String stateTopicPrefix = "stat/coop_command/";
const String topicSuffix_door0 = "door0";
const String topicSuffix_door1 = "door1";
const String topicSuffix_door_open = "open";
const String topicSuffix_led0 = "led0";
const String topicSuffix_led_rgb = "rgb";
const String topicSuffix_led_on = "on";
const String topicSuffix_temp = "temp";
const String topicSuffix_humid = "humid";
int deferUpdateLEDs = 0;
int deferUpdateDoors = 0;
bool door0state = false;
bool door1state = false;
bool led0state = false;
bool led0state2 = false;
bool subscribedToState = false;

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
  for (int pwm = 5; pwm <= 100;)
  {
    door0Motor.setmotor(door0Dir, pwm);
    door1Motor.setmotor(door1Dir, pwm);
    delay(250);
    pwm += 5;
  }

  if (client.connected() && !subscribedToState)
  {
    client.publish(stateTopicPrefix + topicSuffix_door0 + "/" + topicSuffix_door_open, door0 ? trueString : falseString, true, 2);
    client.publish(stateTopicPrefix + topicSuffix_door1 + "/" + topicSuffix_door_open, door1 ? trueString : falseString, true, 2);
  }
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

  if (client.connected() && !subscribedToState)
  {
    client.publish(stateTopicPrefix + topicSuffix_led0 + "/" + topicSuffix_led_on, ledsOn ? trueString : falseString, true, 2);
    char buff[12];
    sprintf(buff, "%d,%d,%d", lastRed, lastGreen, lastBlue);
    client.publish(stateTopicPrefix + topicSuffix_led0 + "/" + topicSuffix_led_rgb, buff, true, 2);
  }
}

// update our device state commands when we get a valid message
void handleMQTTMessage(String &topic, String &payload)
{

  _PRINTLN("[mqtt] " + topic + " <- " + payload);

  // example commands:
  // cmd/coop_command/door0/open -> true, false
  // cmd/coop_command/led0/rgb -> FFFFFF, FEED00
  // cmd/coop_command/led0/on -> true, false
  // stat/...
  String activeTopicPrefix = commandTopicPrefix;
  if (topic.indexOf(activeTopicPrefix) != 0)
  {
    activeTopicPrefix = stateTopicPrefix;
    if (topic.indexOf(activeTopicPrefix) != 0)
    {
      _PRINTLN("[mqtt] ... unknown topic 1");
      return;
    }
  }

  int offsetLast = activeTopicPrefix.length();
  int offsetCurrent = topic.indexOf("/", offsetLast);

  if (offsetCurrent == -1)
  {
    _PRINTLN("[mqtt] ... unknown topic 2");
    return;
  }

  String entityName = topic.substring(offsetLast, offsetCurrent);
  offsetLast = offsetCurrent + 1;
  String stateField = topic.substring(offsetLast);

  if (entityName == topicSuffix_led0)
  {
    // handle LED updates
    if (stateField == topicSuffix_led_rgb)
    {
      // R,G,B -> eg 255,12,0
      int commaIndex = payload.indexOf(',');
      int secondCommaIndex = payload.indexOf(',', commaIndex + 1);
      if (commaIndex == -1 || secondCommaIndex == -1)
      {
        _PRINTLN("[mqtt] ... malformed led rgb payload");
        return;
      }

      String R = payload.substring(0, commaIndex);
      String G = payload.substring(commaIndex + 1, secondCommaIndex);
      String B = payload.substring(secondCommaIndex + 1);

      lastRed = R.toInt();
      lastGreen = G.toInt();
      lastBlue = B.toInt();

      led0state2 = true;
      if (!subscribedToState)
      {
        deferUpdateLEDs++;
      }
    }
    else if (stateField == topicSuffix_led_on)
    {
      if (payload == trueString)
      {
        ledsOn = true;
        led0state = true;
        if (!subscribedToState)
        {
          deferUpdateLEDs++;
        }
      }
      else if (payload == falseString)
      {
        ledsOn = false;
        led0state = true;
        if (!subscribedToState)
        {
          deferUpdateLEDs++;
        }
      }
      else
      {
        _PRINTLN("[mqtt] ... invalid value for led*/on");
      }
    }
    else
    {
      _PRINTLN("[mqtt] ... unknown led topic");
    }
  }
  else if (entityName == topicSuffix_door0 || entityName == topicSuffix_door1)
  {
    // handle door updates
    if (stateField == topicSuffix_door_open)
    {
      if (payload != trueString && payload != falseString)
      {
        _PRINTLN("[mqtt] ... malformed door open payload");
        return;
      }

      bool doorOpen = payload == trueString;
      if (entityName == topicSuffix_door0)
      {
        door0 = doorOpen;
        door0state = true;
      }
      else
      {
        door1 = doorOpen;
        door1state = true;
      }

      if (!subscribedToState)
      {
        deferUpdateDoors++;
      }
    }
    else
    {
      _PRINTLN("[mqtt] ... unknown door topic");
    }
  }
  else
  {
    _PRINTLN("[mqtt] ... unknown topic 3");
  }
}

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

void wifiInit()
{
  // Connect to WiFi network
  _PRINT("[wifi] connecting to ");
  _PRINTLN(ENV_WIFI_SSID);
  _PRINT("[wifi] ");
  _FLUSH();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ENV_WIFI_SSID, ENV_WIFI_PASS);

  uint64 count = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    count++;

    if (count % 20 == 0)
    {
      // every 10s
      _PRINT(".");
    }
  }

  _PRINTLN(".");
  _PRINT("[wifi] connected ");
  _PRINTLN(WiFi.localIP());

  // start local dns server (coop-command.local)
  if (MDNS.begin(ENV_HOSTNAME))
  {
    Serial.printf("[http] MDNS responder started for %s.local\n", ENV_HOSTNAME);
  }
}

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
    if (client.connected() && !subscribedToState)
    {
      client.publish(stateTopicPrefix + topicSuffix_temp, String(dhtTH.temperature), true, 2);
      reported = true;
    }
  }

  if (!isnan(dhtTH.humidity) && dhtTH.humidity > 0)
  {
    _PRINTLN(String("[dht] reporting ") + humidityDisplay(dhtTH));
    if (client.connected() && !subscribedToState)
    {
      client.publish(stateTopicPrefix + topicSuffix_humid, String(dhtTH.humidity), true, 2);
      reported = true;
    }
  }

  if (reported)
  {
    lastReportedMillis = nowMs;
  }
}

void sensorInit()
{
  pinMode(DHT_SENSOR_PIN, INPUT);
  dht.begin();
  delay(500);
}

void mqttInit()
{
  if (client.connected())
  {
    return;
  }

  // Connect to WiFi network
  _PRINT("[mqtt] connecting to ");
  _PRINTLN(ENV_MQTT_HOST);
  _PRINT("[mqtt] ");
  _FLUSH();

  // max time to go without a packet from client to broker.
  // the underlying library should continue to call PINGREQ packets in client.loop().
  client.setKeepAlive(240);

  // persist session messages in case we disconnect and re-connect
  client.setCleanSession(false);

  // timeout for any command
  client.setTimeout(5000);

  client.begin(ENV_MQTT_HOST, net);

  // connect with hostname as client_id, providing the configured username and password
  uint64 count = 0;
  while (!client.connect(ENV_HOSTNAME, ENV_MQTT_USERNAME, ENV_MQTT_PASSWORD))
  {
    delay(500);
    count++;

    if (count % 20 == 0)
    {
      // every 10s
      _PRINT("...");
      _PRINTLN(client.lastError());
    }
  }

  _PRINTLN();
  _PRINTLN("[mqtt] connected ");

  client.onMessage(handleMQTTMessage);
  client.subscribe(commandTopicPrefix + "#", 2);
}

void previousMQTTStateInit()
{
  subscribedToState = true;
  client.subscribe(stateTopicPrefix + "#", 0);
}
void handleQueuedState()
{

  if (subscribedToState && door0state && door1state && led0state && led0state2)
  {
    // once we receive our initial state for all devices, update them and remove our
    // state subscription so we can again publish updates to state

    _PRINTLN("[mqtt] initial state received for all devices");
    subscribedToState = false;
    client.unsubscribe(stateTopicPrefix + "#");
    updateLEDs();
    updateDoors();
  }
  else if (!subscribedToState)
  {
    // we cannot publish mqtt updates in the same callback as we receive them in,
    // so we do it in a seperate function after said handler completes (client.loop -> handleMQTTMessage)
    if (deferUpdateLEDs > 0)
    {
      deferUpdateLEDs--;
      updateLEDs();
    }
    if (deferUpdateDoors > 0)
    {
      deferUpdateDoors--;
      updateDoors();
    }
  }
}

void setup()
{
  serialInit();
  sensorInit();
  ledInit();
  wifiInit();
  mqttInit();
  previousMQTTStateInit();
  otaInit(ENV_HOSTNAME, ENV_OTA_PASSWORD);

  registerOtaStartHook([]() {
    stopBackgroundTasks();
  });

  // keep our mqtt subscription and handler active
  registerBackgroundTask([]() {
    if (!client.loop())
    {
      _PRINT("[mqtt] loop error ");
      _PRINTLN(client.lastError());
    }
    delay(10);
    mqttInit();
    handleQueuedState();
  });

  // read temperature and humidity, report
  registerBackgroundTask([]() { updateDHTValues(); });

  // update local dns, just in case
  registerBackgroundTask([]() { MDNS.update(); });

  // check if we have ota updates
  registerBackgroundTask([]() { handleOTA(); });
}

void loop()
{
  backgroundTasks();
}