#ifndef BLINK_H
#define BLINK_H

#include "shared-lib-esp8266-pinout.h"
#include "shared-lib-background-tasks.h"

void blink(int times = 4, unsigned long int delayMs = 250)
{
    for (int i = 1; i <= times; i++)
    {
        digitalWrite(LED_BUILTIN, HIGH);
        delayWithBackgroundTasks(delayMs);
        digitalWrite(_LED_BUILTIN, LOW);
        delayWithBackgroundTasks(delayMs);
    }
}

#endif