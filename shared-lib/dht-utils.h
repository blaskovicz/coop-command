#ifndef DHT_UTILS_H
#define DHT_UTILS_H

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

struct tempAndHumidity
{
    float temperature;
    float humidity;
    String lastUpdated;
};

float readDHTTemp(DHT_Unified *dht)
{
    sensors_event_t event;
    dht->temperature().getEvent(&event);

    float newCelsius = event.temperature;
    float newFahrenheit = (newCelsius * 1.8) + 32;

    return newFahrenheit;
}

float readDHTHumidity(DHT_Unified *dht)
{
    sensors_event_t event;
    dht->humidity().getEvent(&event);

    float newHumidity = event.relative_humidity;

    return newHumidity;
}

String temperatureDisplay(tempAndHumidity th)
{
    return String("T: ") + (!isnan(th.temperature) && th.temperature > 0 ? String(th.temperature) + String("F") : String("--"));
}

String humidityDisplay(tempAndHumidity th)
{
    return String("H: ") + (!isnan(th.humidity) && th.humidity > 0 ? String(th.humidity) + String("%") : String("--"));
}

#endif