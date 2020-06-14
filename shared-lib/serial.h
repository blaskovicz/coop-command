#ifndef SERIAL_SHARED_H
#define SERIAL_SHARED_H
void serialInit()
{
    // set up serial monitor and wait for it to open
    Serial.begin(9600);

    do
    {
        delay(100);
    } while (!Serial);

    Serial.println("[serial] initialized");
}
#endif