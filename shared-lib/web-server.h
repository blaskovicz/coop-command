#ifndef WEB_SERVER_SHARED_H
#define WEB_SERVER_SHARED_H

#include "shared-lib-background-tasks.h"
#include "shared-lib-serial.h"
#include "shared-lib-date-format.h"
#include <ESP8266WebServer.h>

#ifndef SERVER_LOG_BUFFER_SIZE
#define SERVER_LOG_BUFFER_SIZE 50
#endif

#ifndef ENV_PORT
#define ENV_PORT 80
#endif

ESP8266WebServer server(ENV_PORT);

// Web logging circular buffer with timestamps
struct WebLogEntry {
    unsigned long timestamp;
    String message;
};

int webLogStart = -1;
int webLogNext = 0;
const int webLogCap = SERVER_LOG_BUFFER_SIZE;
WebLogEntry *webLogBuffer = NULL;
String webLogCurrentLine = "";  // Accumulates partial log lines

// Add a complete log line to the web log buffer with timestamp
void webLogAddLine(String logLine)
{
    if (logLine.length() == 0)
    {
        return;  // Skip empty lines
    }

    if (webLogBuffer == NULL)
    {
        webLogBuffer = new WebLogEntry[webLogCap];
    }

    if (webLogStart == webLogNext || webLogStart == -1)
    {
        webLogStart++;
    }

    if (webLogStart >= webLogCap)
    {
        webLogStart = 0;
    }

    webLogBuffer[webLogNext].timestamp = millis();
    webLogBuffer[webLogNext].message = logLine;
    webLogNext++;
    if (webLogNext >= webLogCap)
    {
        // buffer has wrapped around
        webLogNext = 0;
    }
}

// Append text to current log line (for print without newline)
void webLogAppend(String text)
{
    webLogCurrentLine += text;
}

// Append text and complete the line (for println)
void webLogAppendLine(String text)
{
    webLogCurrentLine += text;
    webLogAddLine(webLogCurrentLine);
    webLogCurrentLine = "";
}

// Get all logs as a single string with relative timestamps (most recent last)
String webLogGetAll()
{
    if (webLogBuffer == NULL || webLogStart == -1)
    {
        return "";
    }

    String result = "";
    int count = 0;
    unsigned long nowMillis = millis();
    
    // Calculate how many logs we have
    if (webLogNext > webLogStart)
    {
        count = webLogNext - webLogStart;
    }
    else if (webLogNext < webLogStart)
    {
        count = webLogCap - webLogStart + webLogNext;
    }
    else
    {
        // webLogNext == webLogStart after initial state means buffer is full
        count = webLogCap;
    }

    // Read from oldest to newest
    for (int i = 0; i < count; i++)
    {
        int index = (webLogStart + i) % webLogCap;
        String relativeTime = formatRelativeTime(webLogBuffer[index].timestamp, nowMillis);
        result += "[" + relativeTime + " ago]";
        result += webLogBuffer[index].message;
        result += "\n";
    }

    return result;
}

// Get the number of logs currently stored
int webLogGetCount()
{
    if (webLogBuffer == NULL || webLogStart == -1)
    {
        return 0;
    }

    if (webLogNext > webLogStart)
    {
        return webLogNext - webLogStart;
    }
    else if (webLogNext < webLogStart)
    {
        return webLogCap - webLogStart + webLogNext;
    }
    
    return 0;
}

// Server control functions (defined after webLog functions so _PRINTLN macros work)
void serverStop()
{
    _PRINTLN("[http] stopping server");
    server.stop();
}

void serverStart()
{
    _PRINTLN("[http] starting server");
    server.begin();
}

void serverInit()
{
    _PRINTLN("[http] starting server on port " + String(ENV_PORT));
    serverStart();
    registerBackgroundTask([]()
                           {
                               server.handleClient();
                           });
}

#endif