#ifndef SERIAL_SHARED_H
#define SERIAL_SHARED_H

#ifdef DEBUG_MODE

#define _PRINT(...) Serial.print(__VA_ARGS__)
#define _PRINTLN(...) Serial.println(__VA_ARGS__)
#define _FLUSH() Serial.flush()

#else

#define _PRINT(...)
#define _PRINTLN(...)
#define _FLUSH(...)

#endif

void serialInit()
{
    // set up serial monitor and wait for it to open
    Serial.begin(115200);

    do
    {
        delay(100);
    } while (!Serial);

    _PRINTLN("[serial] initialized");
}

#endif