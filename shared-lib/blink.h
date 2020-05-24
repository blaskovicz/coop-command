#ifndef BLINK_H
#define BLINK_H

void blink(int times = 4, unsigned long int delayMs = 250)
{
    for (int i = 1; i < times; i++)
    {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(delayMs);
        digitalWrite(LED_BUILTIN, LOW);
        delay(delayMs);
    }
}

#endif