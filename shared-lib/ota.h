#ifndef OTA_H
#define OTA_H
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "shared-lib-serial.h"

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
        _PRINTLN("[ota] start updating " + type);
        otaStartHook();
    });
    ArduinoOTA.onEnd([]() {
        _PRINTLN("[ota] end");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        _PRINTF("[ota] progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        _PRINTF("[ota] error[%u]: ", error);

        if (error == OTA_AUTH_ERROR)
        {
            _PRINTLN("auth failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            _PRINTLN("begin failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            _PRINTLN("connect failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            _PRINTLN("receive failed");
        }
        else if (error == OTA_END_ERROR)
        {
            _PRINTLN("end failed");
        }
        else
        {
            _PRINTLN("<unknown>");
        }
    });

    ArduinoOTA.begin();

    _PRINTLN("[ota] initalized");
}

void handleOTA()
{
    ArduinoOTA.handle();
}

#endif