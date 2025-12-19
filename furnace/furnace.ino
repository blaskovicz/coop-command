#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include <math.h>
#include "Adafruit_VL6180X.h"
#include <MQTT.h>

// this must be before other shared libs due to macro loading order
#include "env.h"

#include "shared-lib-ota.h"
#include "shared-lib-background-tasks.h"
#include "shared-lib-serial.h"
#include "shared-lib-date-format.h"
#include "shared-lib-web-server.h"

unsigned long int lastUpdatedMillis = 0;
unsigned int lastVlRange = 0;
bool isReadingFuelLevel = false;  // Guard to prevent re-entry during sensor reading

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
const float GALLONS_FULL = 330.0;
const float GALLONS_EMPTY = 0.0;
const float MM_FULL = 12.0;
const float MM_EMPTY = 75.0;

unsigned int convertVlRangeToGallonsRemaining(float rangeInMM)
{
  if (rangeInMM <= MM_FULL)
  {
    return GALLONS_FULL;
  }
  if (rangeInMM >= MM_EMPTY)
  {
    return GALLONS_EMPTY;
  }

  unsigned int gallonsRemaining = static_cast<unsigned int>(GALLONS_FULL - (GALLONS_FULL / (MM_EMPTY - MM_FULL) * (rangeInMM - MM_FULL)));
  if (gallonsRemaining > GALLONS_FULL)
  {
    return GALLONS_FULL;
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

/**
 * Sorts readings array, calculates median, rejects outliers, and returns trimmed mean.
 * @param readings Array of sensor readings (will be sorted in place)
 * @param validCount Number of valid readings in the array
 * @param outlierThreshold Threshold for rejecting outliers (e.g., 0.15 for 15%)
 * @param medianOut Reference to store the calculated median
 * @param inlierCountOut Reference to store the number of inliers after outlier rejection
 * @return Trimmed mean of inlier readings, or 0 if too many outliers
 */
unsigned int calculateFilteredReading(uint8_t readings[], int validCount, float outlierThreshold, uint8_t& medianOut, int& inlierCountOut)
{
  // Sort readings to find median (simple bubble sort for small arrays)
  for (int i = 0; i < validCount - 1; i++)
  {
    for (int j = i + 1; j < validCount; j++)
    {
      if (readings[i] > readings[j])
      {
        uint8_t temp = readings[i];
        readings[i] = readings[j];
        readings[j] = temp;
      }
    }
  }

  // Calculate median
  medianOut = readings[validCount / 2];
  
  // Reject outliers and calculate trimmed mean
  // Outliers are readings that deviate more than threshold from median
  unsigned int sum = 0;
  inlierCountOut = 0;
  float medianFloat = (float)medianOut;
  
  for (int i = 0; i < validCount; i++)
  {
    float deviation = fabs((float)readings[i] - medianFloat) / medianFloat;
    if (deviation <= outlierThreshold)
    {
      sum += readings[i];
      inlierCountOut++;
    }
  }

  if (inlierCountOut == 0)
  {
    return 0;
  }

  return sum / inlierCountOut;
}

bool readVL()
{
  const int times = 20;
  const int minSuccessTimes = times / 2;
  const float OUTLIER_THRESHOLD = 0.15; // Reject readings >15% from median
  const int DEFAULT_DELAY_MS = 150;
  const int MAX_DELAY_MS = 30000;
  uint8_t readings[times];
  int validCount = 0;
  int delayMs = DEFAULT_DELAY_MS;  // Increased delay to let sensor settle (helps with electrical noise)

  _PRINTLN(String("[vl-sensor]") + String(" reading ") + String(times) + String("x"));
  
  // Collect all valid readings
  for (int i = 0; i < times; i++)
  {
    // Abort sensor reading if OTA is in progress
    if (isOtaInProgress)
    {
      _PRINTLN("[vl-sensor] aborting read - OTA in progress");
      return false;
    }
    
    // cap delay at 1min
    if (delayMs > MAX_DELAY_MS)
    {
      delayMs = MAX_DELAY_MS;
    }

    delayWithBackgroundTasks(delayMs);  // Non-blocking delay to keep web server responsive
    uint8_t vlValue = readVLSingle();
    
    if (vlValue == 0)
    {
      delayMs += delayMs; // exponential backoff on failure
      continue;
    }
    
    readings[validCount++] = vlValue;
    delayMs = DEFAULT_DELAY_MS; // reset on success
  }

  if (validCount < minSuccessTimes)
  {
    _PRINTLN(String("[vl-sensor]") + String(" read threshold failed; expected>=") + String(minSuccessTimes) + String(" got=") + String(validCount));
    return false;
  }

  // Sort, calculate median, reject outliers, and get trimmed mean
  uint8_t median;
  int inlierCount;
  unsigned int filteredReading = calculateFilteredReading(readings, validCount, OUTLIER_THRESHOLD, median, inlierCount);

  if (inlierCount < minSuccessTimes)
  {
    _PRINTLN(String("[vl-sensor][") + String(millis()) + String("] too many outliers; kept ") + String(inlierCount) + String(" of ") + String(validCount));
    return false;
  }

  unsigned int previousVlRange = lastVlRange;
  lastVlRange = filteredReading;
  lastUpdatedMillis = millis();

  _PRINTLN(String("[vl-sensor]") + String(" read ") + String(lastVlRange) + String(" (median=") + String(median) + String(", was ") + String(previousVlRange) + String(") from ") + String(inlierCount) + String(" inliers of ") + String(validCount) + String(" valid"));

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

void handleRoot()
{
  String response = "";
  unsigned long nowMillis = millis();
  
  // IP Address
  response += "IP: ";
  response += WiFi.localIP().toString();
  response += "\n";
  
  // MQTT Connection Status
  response += "MQTT Connected: ";
  response += client.connected() ? "yes" : "no";
  response += "\n";
  
  // Last Read Timestamp
  response += "Last Read: ";
  if (lastUpdatedMillis > 0)
  {
    response += formatRelativeTime(lastUpdatedMillis, nowMillis) + " ago";
    response += " (" + String(lastUpdatedMillis) + " ms)";
  }
  else
  {
    response += "-";
  }
  response += "\n";
  
  // Gallons Remaining
  response += "Gallons Remaining: ";
  if (lastVlRange > 0)
  {
    response += String(convertVlRangeToGallonsRemaining(lastVlRange));
  }
  else
  {
    response += "-";
  }
  response += "\n";
  
  // Raw Sensor Reading
  response += "Raw Sensor Reading: ";
  if (lastVlRange > 0)
  {
    response += String(lastVlRange) + String("mm");
  }
  else
  {
    response += "-";
  }
  response += "\n";

  // is reading fuel level?
  response += "Reading Fuel Level: ";
  response += isReadingFuelLevel ? "yes" : "no";
  response += "\n";

  // Hostname
  response += "Hostname: ";
  response += ENV_HOSTNAME;
  response += "\n";

  // free heap
  response += "Free Heap: ";
  response += String(ESP.getFreeHeap()) + String("B");
  response += "\n";
  
  // Git Version
  response += "Git Version: ";
  response += GIT_VERSION;
  response += "\n";

  // system uptime
  response += "Uptime: ";
  response += formatMillisToRelativeTime(nowMillis);
  response += "\n";
  
  // Add separator before logs
  response += "\n";
  response += "=== Recent Logs ===\n";
  
  // Get web logs
  String logs = webLogGetAll();
  if (logs.length() > 0)
  {
    response += logs;
  }
  else
  {
    response += "-\n";
  }
  
  server.send(200, "text/plain", response);
}

void handleReadSingle()
{
  _PRINTLN("[api] raw sensor read requested");
  
  delay(150); // wait for sensor to settle
  uint8_t rawValue = readVLSingle();
  
  if (rawValue > 0)
  {
    String response = "";
    response += "Raw Sensor Reading: " + String(rawValue) + "mm\n";
    response += "Gallons Remaining: " + String(convertVlRangeToGallonsRemaining(rawValue)) + "\n";    
    server.send(200, "text/plain", response);
  }
  else
  {
    server.send(500, "text/plain", "Sensor read failed");
  }
}

const unsigned long int ONE_MINUTE_MS = 60000;
void updateFuelLevel()
{
  // Prevent re-entry while sensor reading is in progress
  if (isReadingFuelLevel)
  {
    return;
  }
  
  // Don't start new sensor read if OTA is in progress
  if (isOtaInProgress)
  {
    return;
  }
  
  // wait at least 30 min between reads
  unsigned long int nowMs = millis();
  if (lastUpdatedMillis > 0 && (nowMs - lastUpdatedMillis) < (30 * ONE_MINUTE_MS))
  {
    return;
  }
  
  // Set guard flag before reading
  isReadingFuelLevel = true;
  bool success = readVL();
  isReadingFuelLevel = false;
  
  if (!success)
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
  wifiInit();
  mqttInit();
  otaInit(ENV_HOSTNAME, ENV_OTA_PASSWORD);
  serverInit();
  sensorInit();

  // setup web server routes
  server.on("/", handleRoot);
  server.on("/read", handleReadSingle);

  registerOtaStartHook([]()
                       { 
                         _PRINTLN("[ota] stopping background tasks and sensor reads");
                         stopBackgroundTasks();
                         serverStop();
                         client.disconnect();
                         _PRINTLN("[ota] disconnected MQTT");
                       });

  registerOtaEndHook([]()
                     { 
                       _PRINTLN("[ota] resuming background tasks and sensor reads");
                       serverStart();
                       resumeBackgroundTasks();
                     });

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

  // check if we have ota updates (critical task - must run even when other tasks are stopped)
  const bool isCritical = true;
  registerBackgroundTask([]()
                         { handleOTA(); }, isCritical);
}

void loop()
{
  backgroundTasks();
  _FLUSH();
}
