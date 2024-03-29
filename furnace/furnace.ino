#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include "Adafruit_VL6180X.h"
#include <MQTT.h>

#include "env.h"
#include "shared-lib-ota.h"
#include "shared-lib-background-tasks.h"
#include "shared-lib-serial.h"

unsigned long int lastUpdatedMillis = 0;
unsigned int lastVlRange = 0;

WiFiClient net;
MQTTClient client;
Adafruit_VL6180X vlSensor = Adafruit_VL6180X();

/**
 *    [ VL ]
 * | ------- | top of guage                  -----          
 * |         |                                 |              
 * |         |                                 |   1.2 CM     
 * | ------- | F (full line)                 -----
 * |         |                                 |
 * |         |                                 |
 * |         |                                 |   3.3 CM
 * | ------- | 1/2 (half full line)          -----
 * |         |                                 |
 * |         |                                 |
 * |         |                                 |   3 CM
 * | ------- | E (empty line)                -----
 */
unsigned int convertVlRangeToGallonsRemaining(unsigned int rangeInMM)
{
  // assume we have a 330 gallon tank.
  if (rangeInMM <= 12)
  {
    return 0;
  }

  unsigned int gallonsRemaining = static_cast<unsigned int>(330.0 - (330.0 / (75.0 - 12.0) * (rangeInMM - 12.0)));
  if (gallonsRemaining > 330)
  {
    return 330;
  }
  return gallonsRemaining;
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

  // start local dns server (furnace.local)
  if (MDNS.begin(ENV_HOSTNAME))
  {
    _PRINTF("[http] MDNS responder started for %s.local\n", ENV_HOSTNAME);
  }
}

bool readVL()
{
  int times = 20;
  int minSuccessTimes = times / 2;
  
  int i;
  
  uint8_t vlValue;
  unsigned int vlSum = 0;
  
  int successes = 0;

  int delayMs = 100;

  _PRINTLN(String("[vl-sensor][") + String(millis()) + String("] reading ") + String(times) + String("x"));
  for (i = 0; i < times; i++)
  {
    delay(delayMs);

    vlValue = readVLSingle();
    if (vlValue == 0)
    {
      // exponentially increasing on each failure
      // eg: 100 -> 200 -> 400 -> 800..
      delayMs += delayMs;
      continue;
    }

    successes++;
    vlSum += vlValue;

    // reset delay upon failure
    delayMs = 100;
  }

  // threshold not met, return false
  if (successes < minSuccessTimes)
  {
    _PRINTLN(String("[vl-sensor][") + String(millis()) + String("] read threshold failed; expected>=") + String(minSuccessTimes) + String(" got=") + String(successes));
    return false;
  }

  unsigned int previousVlRange = lastVlRange;
  lastVlRange = vlSum / successes;
  lastUpdatedMillis = millis();

  _PRINTLN(String("[vl-sensor][") + String(millis()) + String("] read ") + String(lastVlRange) + String(" (was ") + String(previousVlRange) + String(") from ") + String(successes) + String(" of ") + String(times) + String(" checks"));

  return true;
}

uint8_t readVLSingle()
{
  // float lux = vl.readLux(VL6180X_ALS_GAIN_5);
  // Serial.print("Lux: "); Serial.println(lux);

  uint8_t range = vlSensor.readRange();
  uint8_t status = vlSensor.readRangeStatus();

  if (status == VL6180X_ERROR_NONE)
  {
    return range;
  }

  _PRINT(String("[vl-sensor] "));

  // an error occurred, ignore this range
  if ((status >= VL6180X_ERROR_SYSERR_1) && (status <= VL6180X_ERROR_SYSERR_5))
  {
    _PRINT(String("system error"));
  }
  else if (status == VL6180X_ERROR_ECEFAIL)
  {
    _PRINT(String("ece failure"));
  }
  else if (status == VL6180X_ERROR_NOCONVERGE)
  {
    _PRINT(String("no convergence"));
  }
  else if (status == VL6180X_ERROR_RANGEIGNORE)
  {
    _PRINT(String("ignoring range"));
  }
  else if (status == VL6180X_ERROR_SNR)
  {
    _PRINT(String("signal/noise error"));
  }
  else if (status == VL6180X_ERROR_RAWUFLOW)
  {
    _PRINT(String("raw reading underflow"));
  }
  else if (status == VL6180X_ERROR_RAWOFLOW)
  {
    _PRINT(String("raw reading overflow"));
  }
  else if (status == VL6180X_ERROR_RANGEUFLOW)
  {
    _PRINT(String("range reading underflow"));
  }
  else if (status == VL6180X_ERROR_RANGEOFLOW)
  {
    _PRINT(String("range reading overflow"));
  }
  else
  {
    _PRINT(String("unknown error ") + String(status));
  }

  _PRINTLN();

  return 0;
}

const unsigned long int ONE_MINUTE_MS = 60000;
void updateFuelLevel()
{
  // wait at least 30 min between reads
  unsigned long int nowMs = millis();
  if (
      (lastUpdatedMillis > 0 && (nowMs - lastUpdatedMillis) < (30 * ONE_MINUTE_MS)) ||
      !readVL())
  {
    return;
  }

  if (lastVlRange <= 0 || !client.connected())
  {
    return;
  }

  // build and publish influx format message to mqtt for fuel level
  // see https://docs.influxdata.com/influxdb/v1.8/write_protocols/line_protocol_tutorial/#syntax
  String message = String("fuel level=") + String(convertVlRangeToGallonsRemaining(lastVlRange));

  _PRINTLN(String("[vl-sensor] reporting ") + String(ENV_MQTT_TOPIC) + String(" ") + message);
  bool published = client.publish(ENV_MQTT_TOPIC, message, true, 2);

  if (!published)
  {
    _PRINTLN("[mqtt] publish.error");
  }
}

void sensorInit()
{
  _PRINT("[vl-sensor] startup ");
  uint64 count = 0;
  while (!vlSensor.begin())
  {
    delay(500);
    count++;

    if (count % 20 == 0)
    {
      // every 10s
      _PRINT(".");
    }
  }
  _PRINTLN();
  _PRINTLN("[vl-sensor] started");
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
      _PRINT(".");
      _PRINTLN(client.lastError());
    }
  }

  _PRINTLN();
  _PRINTLN("[mqtt] connected ");
}

void setup()
{
  serialInit();
  sensorInit();
  wifiInit();
  mqttInit();
  otaInit(ENV_HOSTNAME, ENV_OTA_PASSWORD);

  registerOtaStartHook([]()
                       { stopBackgroundTasks(); });

  // keep our mqtt subscription and handler active
  registerBackgroundTask([]()
                         {
                           if (!client.loop())
                           {
                             _PRINT("[mqtt] loop last-error=");
                             _PRINT(client.lastError());
                             _PRINT(" return-code=");
                             _PRINTLN(client.returnCode());
                           }
                           delay(10);
                           mqttInit();
                         });

  // read fuel level, report
  registerBackgroundTask([]()
                         { updateFuelLevel(); });

  // update local dns, just in case
  registerBackgroundTask([]()
                         { MDNS.update(); });

  // check if we have ota updates
  registerBackgroundTask([]()
                         { handleOTA(); });
}

void loop()
{
  backgroundTasks();
  _FLUSH();
}
