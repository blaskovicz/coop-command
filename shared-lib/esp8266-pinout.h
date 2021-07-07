#ifndef ESP8266_PINOUT_H
#define ESP8266_PINOUT_H

// https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/

#define LED_BUILTIN 16

static const uint8_t _LED_BUILTIN = 16;

static const uint8_t _D0 = 16;
static const uint8_t _D1 = 5; // SCL (I2C)
static const uint8_t _D2 = 4; // SDA (I2C)
static const uint8_t _D3 = 0;
static const uint8_t _D4 = 2;
static const uint8_t _D5 = 14;
static const uint8_t _D6 = 12;
static const uint8_t _D7 = 13;
static const uint8_t _D8 = 15;
static const uint8_t _D9 = 3;
static const uint8_t _D10 = 1;

#endif