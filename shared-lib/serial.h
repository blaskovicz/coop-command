#ifndef SERIAL_SHARED_H
#define SERIAL_SHARED_H

// Forward declarations for web logging functions (defined in web-server.h)
#ifdef ENABLE_WEB_LOGGING
void webLogAppend(String text);
void webLogAppendLine(String text);
#endif

// Helper to convert various types to String for web logging
template<typename T>
String _toString(T value) {
    return String(value);
}

// Overload for empty calls
inline String _toString() {
    return String("");
}

// Overload for IPAddress
inline String _toString(IPAddress value) {
    return value.toString();
}

#ifdef DEBUG_MODE

    #ifdef ENABLE_WEB_LOGGING
        // Dual logging: both serial and web
        #define _PRINT(...) do { Serial.print(__VA_ARGS__); webLogAppend(_toString(__VA_ARGS__)); } while(0)
        #define _PRINTLN(...) do { Serial.println(__VA_ARGS__); webLogAppendLine(_toString(__VA_ARGS__)); } while(0)
        #define _PRINTF(...) do { char buf[256]; snprintf(buf, sizeof(buf), __VA_ARGS__); Serial.print(buf); webLogAppend(String(buf)); } while(0)
        #define _FLUSH() Serial.flush()
    #else
        // Serial only
        #define _PRINT(...) Serial.print(__VA_ARGS__)
        #define _PRINTLN(...) Serial.println(__VA_ARGS__)
        #define _PRINTF(...) Serial.printf(__VA_ARGS__)
        #define _FLUSH() Serial.flush()
    #endif

#else

    #ifdef ENABLE_WEB_LOGGING
        // Web logging only (no serial)
        #define _PRINT(...) webLogAppend(_toString(__VA_ARGS__))
        #define _PRINTLN(...) webLogAppendLine(_toString(__VA_ARGS__))
        #define _PRINTF(...) do { char buf[256]; snprintf(buf, sizeof(buf), __VA_ARGS__); webLogAppend(String(buf)); } while(0)
        #define _FLUSH()
    #else
        // No logging at all
        #define _PRINT(...)
        #define _PRINTLN(...)
        #define _PRINTF(...)
        #define _FLUSH(...)
    #endif

#endif

void serialInit()
{
    // set up serial monitor and wait for it to open
    Serial.begin(115200);

    do
    {
        delay(100);
    } while (!Serial);

    _PRINTLN();
    _PRINTLN("[serial] initialized");

    // log GIT_VERSION if present
    if (GIT_VERSION != NULL)
    {
        _PRINTLN("[serial] git version: " + String(GIT_VERSION));
    }
}

#endif