// Program to exercise the MD_MAX72XX library
//
// Uses most of the functions in the library
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <SPI.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#include "shared-lib-esp8266-pinout.h"
#include "shared-lib-serial.h"
#include "shared-lib-dht-utils.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN _D0  // or SCK
#define CS_PIN _D1   // or SS
#define DATA_PIN _D2 // or MOSI

// Software SPI on arbitrary pins; initializes the matrix with 4 8x8 grids and one "zone" (zone 0);
// zones are logical units of work that are used by the parola library for animations
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// set up our ntp client to get time from the internet (over udp)
// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset; use New_York epoch seconds offset
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, -14400);

char timeBuffer[9]; // hh:mm:ss\0

unsigned long int lastUpdatedTimeMillis = 0;

unsigned long int lastUpdatedDHTTimeMillis = 0;

#define DHT_SENSOR_PIN _D3
DHT_Unified dht(DHT_SENSOR_PIN, DHT22);

tempAndHumidity dhtTH;

char tempBuff[9];   // 100.5 °F\0
char humidBuff[10]; // 100.0 %RH\0

const char *nanTemp = "---.- ";
const char *tempSuffix = "F";
const char *nanHumid = "---.-";
const char *humidSuffix = "%H";

#define PAUSE_TIME 2000
#define SPEED_TIME 20

void updateDHTValues()
{
    // wait at least 1 second between reads
    unsigned long int nowMs = millis();
    if (lastUpdatedDHTTimeMillis > 0 && (nowMs - lastUpdatedDHTTimeMillis) < 1000)
    {
        return;
    }

    lastUpdatedDHTTimeMillis = nowMs;

    // read dht humidity then build display string
    dhtTH.humidity = readDHTHumidity(&dht);
    if (isnan(dhtTH.humidity))
    {
        strcpy(humidBuff, nanHumid);
    }
    else
    {
        dtostrf(dhtTH.humidity, 3, 1, humidBuff);
    }
    strcat(humidBuff, humidSuffix);

    // read dht temperature then build display string
    dhtTH.temperature = readDHTTemp(&dht);
    if (isnan(dhtTH.temperature))
    {
        strcpy(tempBuff, nanTemp);
    }
    else
    {
        dtostrf(dhtTH.temperature, 3, 1, tempBuff);
    }
    strcat(tempBuff, tempSuffix);
}

void readTime()
{
    // wait at least 1 second between reads
    unsigned long int nowMs = millis();
    if (lastUpdatedTimeMillis > 0 && (nowMs - lastUpdatedTimeMillis) < 1000)
    {
        return;
    }

    // read from our ntp time client and build display string
    const String t = timeClient.getFormattedTime();
    strcpy(timeBuffer, t.c_str());
    lastUpdatedTimeMillis = nowMs;
}

void ledMatrixInit()
{
    P.begin();
    P.setIntensity(0);
}

void sensorInit()
{
    dht.begin();
}

void setup(void)
{
    serialInit();
    sensorInit();
    ledMatrixInit();
    readTime();
    updateDHTValues();
}

int display = 0;
void loop(void)
{
    // update display values
    timeClient.update();
    readTime();
    updateDHTValues();

    // animate the current zone, as necessary
    P.displayAnimate();

    // if current animation is not finished, wait until it has
    if (!P.getZoneStatus(0))
    {
        return;
    }

    // switch displayed text via a simple flag
    display++;

    switch (display)
    {
    case 1:
        P.displayText(timeBuffer, PA_CENTER, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_SCAN_VERT);
        break;
    case 2:
        P.displayText(tempBuff, PA_CENTER, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_SCAN_VERT);
        break;
    case 3:
        P.displayText(humidBuff, PA_CENTER, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_SCAN_VERT);
        display = 0;
        break;
    }

    //P.displayReset();
}