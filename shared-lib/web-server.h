#ifndef WEB_SERVER_SHARED_H
#define WEB_SERVER_SHARED_H

#include "env.h"
#include "shared-lib-background-tasks.h"
#include "shared-lib-serial.h"
#include <ESP8266WebServer.h>

#ifndef SERVER_LOG_BUFFER_SIZE
#define SERVER_LOG_BUFFER_SIZE 50
#endif

#ifndef ENV_PORT
#define ENV_PORT 80
#endif

ESP8266WebServer server(ENV_PORT);

int logStart = -1;
int logNext = 0;
const int logCap = SERVER_LOG_BUFFER_SIZE;
String *logBuffer = NULL;

void serverInit()
{
    _PRINTLN("[http] starting server on port " + String(ENV_PORT));
    server.begin();
    registerBackgroundTask([]()
                           {
                               server.handleClient();
                           });
}

void log(String logLine)
{
    if (logBuffer == NULL)
    {
        logBuffer = new String[logCap];
    }

    if (logStart == logNext || logStart == -1)
    {
        logStart++;
    }

    if (logStart >= logCap)
    {
        logStart = -1;
    }

    logBuffer[logNext] = logLine;
    logNext++;
    if (logNext >= logCap)
    {
        // buffer has wrapped around
        logNext = 0;
    }
}

#endif