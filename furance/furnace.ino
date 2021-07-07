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

const String stateTopicPrefix = "stat/furnace/";
const String topicSuffix_fuelLevel = "fuel-test";

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
unsigned int convertVlRange(unsigned int rangeInMM)
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

  // start local dns server (coop-command.local)
  if (MDNS.begin(ENV_HOSTNAME))
  {
    Serial.printf("[http] MDNS responder started for %s.local\n", ENV_HOSTNAME);
  }
}

bool readVL()
{
  int times = 20;
  int i;
  uint8_t vlValue;
  unsigned int vlSum = 0;
  int successes = 0;
  int de = 100;

  _PRINTLN(String("[vl-sensor][")+String(millis())+String("] reading ") + String(times) + String("x"));
  for (i = 0; i < times; i++)
  {
    delay(de);
    vlValue = readVLSingle();
    if (vlValue == 0)
    {
      continue;
    }
    successes++;
    vlSum += vlValue;
  }

  // 50% must succeed
  int expectedMin = times / 2;
  if (successes < expectedMin)
  {
    _PRINTLN(String("[vl-sensor][")+String(millis())+String("] read threshold failed; expected at least ") + String(expectedMin) + String(", got ") + String(successes));
    return false;
  }

  unsigned int previousVlRange = lastVlRange;
  lastVlRange = vlSum / successes;
  lastUpdatedMillis = millis();

  _PRINTLN(String("[vl-sensor][")+String(millis())+String("] read ") + String(lastVlRange) + String(" (was ") + String(previousVlRange) + String(") from ") + String(successes) + String(" of ") + String(times) + String(" checks"));

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

  // an error occurred, ignore this range
  if ((status >= VL6180X_ERROR_SYSERR_1) && (status <= VL6180X_ERROR_SYSERR_5))
  {
    Serial.println("[vl-sensor] system error");
  }
  else if (status == VL6180X_ERROR_ECEFAIL)
  {
    Serial.println("[vl-sensor] ece failure");
  }
  else if (status == VL6180X_ERROR_NOCONVERGE)
  {
    Serial.println("[vl-sensor] no convergence");
  }
  else if (status == VL6180X_ERROR_RANGEIGNORE)
  {
    Serial.println("[vl-sensor] ignoring range");
  }
  else if (status == VL6180X_ERROR_SNR)
  {
    Serial.println("[vl-sensor] signal/noise error");
  }
  else if (status == VL6180X_ERROR_RAWUFLOW)
  {
    Serial.println("[vl-sensor] raw reading underflow");
  }
  else if (status == VL6180X_ERROR_RAWOFLOW)
  {
    Serial.println("[vl-sensor] raw reading overflow");
  }
  else if (status == VL6180X_ERROR_RANGEUFLOW)
  {
    Serial.println("[vl-sensor] range reading underflow");
  }
  else if (status == VL6180X_ERROR_RANGEOFLOW)
  {
    Serial.println("[vl-sensor] range reading overflow");
  }
  else
  {
    Serial.println("[vl-sensor] unknown error");
  }

  return 0;
}

const unsigned long int ONE_MINUTE_MS = 60000;
void updateFuelLevel()
{
  // wait at least 30 min between reads
  unsigned long int nowMs = millis();
  if (
    (lastUpdatedMillis > 0 && (nowMs - lastUpdatedMillis) < (30*ONE_MINUTE_MS)) ||
    !readVL()
  )
  {
    return;
  }

  if (lastVlRange > 0)
  {
    // convert to gallons left
    unsigned int gallonsRemaining = convertVlRange(lastVlRange);
    _PRINTLN(String("[vl-sensor] reporting ") + String(gallonsRemaining));
    if (client.connected())
    {
      client.publish(stateTopicPrefix + topicSuffix_fuelLevel, String(gallonsRemaining), true, 2);
    }
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
        _PRINT("...");
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
                               _PRINT("[mqtt] loop error ");
                               _PRINTLN(client.lastError());
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
}
