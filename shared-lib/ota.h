#ifndef OTA_H
#define OTA_H
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

typedef void (*hookFunction)();
hookFunction startHook = NULL;

void registerOtaStartHook(hookFunction func)
{
    startHook = func;
}

void otaStartHook()
{
    if (startHook == NULL)
        return;
    startHook();
}

// based on the guide at
// https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html#arduino-ide

void otaInit(char *hostname = NULL, char *password = NULL)
{
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    if (hostname != NULL)
    {
        ArduinoOTA.setHostname(hostname);
    }

    // No authentication by default
    if (password != NULL)
    {
        ArduinoOTA.setPassword(password);
    }

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "sketch";
        }
        else
        { // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        Serial.println("[ota] start updating " + type);
        otaStartHook();
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("[ota] end");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[ota] progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[ota] error[%u]: ", error);

        if (error == OTA_AUTH_ERROR)
        {
            Serial.println("auth failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            Serial.println("begin failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            Serial.println("connect failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            Serial.println("receive failed");
        }
        else if (error == OTA_END_ERROR)
        {
            Serial.println("end failed");
        }
        else
        {
            Serial.println("<unknown>");
        }
    });

    ArduinoOTA.begin();

    Serial.println("[ota] initalized");
}

void handleOTA()
{
    ArduinoOTA.handle();
}

#endif