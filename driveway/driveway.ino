#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <AdafruitIO_WiFi.h>

// start SKETCH PIR
#include "env.h"
#include "site-html.h"
#include "shared-lib-esp8266-pinout.h"
#include "shared-lib-serial.h"
#include "shared-lib-background-tasks.h"
#include "shared-lib-ota.h"

// PIR stuff
static const uint8_t PIR_PIN = _D5;
static const uint8_t DOORBELL_PIN = _D6;

static const int MOTION_READ_DELAY_MS = 300;
static const int MOTION_AFTER_PERIOD_MS = MOTION_READ_DELAY_MS * 4;
static const int WAIT_NEXT_NOTIFY_PERIOD_MS = 20000;
static const int NOISE_AFTER_PEROID_MS = 5000;

unsigned long int lastReportedMs = 0;
unsigned long int lastNotifiedMs = 0;
int motionCounter = 0;

// Create an instance of the server
ESP8266WebServer server(ENV_PORT);

// Create an instance of Adafruit HTTP Api Client
AdafruitIO_WiFi io(ENV_AIO_USERNAME, ENV_AIO_KEY, ENV_WIFI_SSID, ENV_WIFI_PASS);

// set up the 'motion' feed
AdafruitIO_Feed *motion = io.feed("driveway.motion");
bool silenceDoorbell = false;

void notifyMotion()
{
    Serial.println(String("[notify-motion] reporting"));

    motion->save(1);

    if (silenceDoorbell)
    {
        return;
    }

    digitalWrite(DOORBELL_PIN, 1);
    delayWithBackgroundTasks(400);
    digitalWrite(DOORBELL_PIN, 0);
}

void checkForMotion()
{
    delayWithBackgroundTasks(MOTION_READ_DELAY_MS);
    int motionDetected = digitalRead(PIR_PIN);
    unsigned long int nowMs = millis();

    if (motionDetected == 1)
    {
        motionCounter++;
    }
    else
    {
        motionCounter = 0;
    }

    int motionOngoingMs = motionCounter * MOTION_READ_DELAY_MS;
    if (motionOngoingMs < MOTION_AFTER_PERIOD_MS)
    {
        // no motion
        if (motionCounter == 0 && (lastReportedMs == 0 || (nowMs - lastReportedMs) >= WAIT_NEXT_NOTIFY_PERIOD_MS))
        {
            // report a 0
            lastReportedMs = nowMs;
            motion->save(0);
        }
        // else motion, but waiting to confirm
        return;
    }
    else if (motionOngoingMs > NOISE_AFTER_PEROID_MS)
    {
        // sensor is open, meaning probably disconnected; treat as noise
        if (lastReportedMs == 0 || (nowMs - lastReportedMs) >= WAIT_NEXT_NOTIFY_PERIOD_MS)
        {
            // report a 0
            lastReportedMs = nowMs;
            motion->save(-1);
        }
        return;
    }

    // motion confirmed
    if (lastNotifiedMs > 0 && (nowMs - lastNotifiedMs) < WAIT_NEXT_NOTIFY_PERIOD_MS)
    {
        // already notified
        return;
    }

    // need to notify
    lastNotifiedMs = nowMs;
    lastReportedMs = nowMs;

    notifyMotion();
}

void connectWifiClient()
{
    // Connect to WiFi network
    Serial.print("[wifi] connecting to ");
    Serial.println(ENV_WIFI_SSID);
    Serial.flush();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ENV_WIFI_SSID, ENV_WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(F("."));
    }
    Serial.println();

    Serial.print("[wifi] connected ");
    Serial.println(WiFi.localIP());
}

void handleRoot()
{
    server.send(200, "text/html", siteHtml);
}

void handleGetMotion()
{
    String lastNotifiedMsString = lastNotifiedMs == 0 ? String("null") : String(millis() - lastNotifiedMs, 10);
    server.send(200, "application/json", "{\"last_notified_ms_ago\": " + lastNotifiedMsString + "}");
}

void handleNotFound()
{
    String message = "Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArgs: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++)
    {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
}

void connectAndServeHTTP()
{
    connectWifiClient();

    // start local dns server (incubator.local)
    if (MDNS.begin(ENV_HOSTNAME))
    {
        Serial.printf("[wifi] MDNS responder started for %s.local\n", ENV_HOSTNAME);
    }

    // mount server routes
    server.on("/", handleRoot);
    server.on("/api/motion", handleGetMotion);
    server.onNotFound(handleNotFound);

    // start server
    server.begin();
    Serial.println("[http] server started");
}

void connectAdafruitIO()
{
    // connect to io.adafruit.com
    Serial.print("[adafruit] connecting to Adafruit IO");
    io.connect();

    // wait for a connection
    while (io.status() < AIO_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }

    // we are connected
    Serial.println();
    Serial.print("[adafruit] ");
    Serial.println(io.statusText());
}

void sensorInit()
{
    pinMode(PIR_PIN, INPUT_PULLUP);
    pinMode(DOORBELL_PIN, OUTPUT);
}

void setup()
{
    serialInit();
    sensorInit();
    connectAdafruitIO();
    otaInit(ENV_HOSTNAME, ENV_OTA_PASSWORD);
    connectAndServeHTTP();

    // keep our client connected to
    // io.adafruit.com, and processes any incoming data.
    registerBackgroundTask([]() { io.run(); });

    // handle incoming http clients
    registerBackgroundTask([]() { server.handleClient(); });

    // update local dns, just in case
    registerBackgroundTask([]() { MDNS.update(); });

    // check if we have ota updates
    registerBackgroundTask([]() { handleOTA(); });

    // update our ntp client
    // registerBackgroundTask([]() { timeClient.update(); });
}

void loop()
{
    // read pir, report
    checkForMotion();
    backgroundTasks();
}

// Start sketch: tb6612 on breakout
// #include "SparkFun_TB6612.h"

// // tb6612 stuff
// #define In1 _D0
// #define In2 _D1
// #define Standby _D3
// // https://github.com/sparkfun/SparkFun_TB6612FNG_Arduino_Library/blob/master/src/SparkFun_TB6612.cpp
// void setup()
// {
//     pinMode(In1, OUTPUT);
//     pinMode(In2, OUTPUT);
//     pinMode(Standby, OUTPUT);
//     digitalWrite(Standby, HIGH);
// }
// void loop()
// {
//     // fwd
//     digitalWrite(In1, HIGH);
//     digitalWrite(In2, LOW);
//     delayWithBackgroundTasks(5000);
//     // rev
//     digitalWrite(In1, LOW);
//     digitalWrite(In2, HIGH);
//     delayWithBackgroundTasks(5000);
//     // stop
//     digitalWrite(In1, HIGH);
//     digitalWrite(In2, HIGH);
//     delayWithBackgroundTasks(5000);
// }

// START SKETCH: motor with d1 mini shield

// #include <Wire.h>

// #include "WEMOS_Motor.h"

// //Motor shield default I2C Address: 0x30
// //PWM frequency: 1000Hz(1kHz)
// Motor M1(0x30, _MOTOR_A, 1000); //Motor A
// // Motor M2(0x30, _MOTOR_B, 1000); //Motor B

// const int delayMs = 1000;

// void setup()
// {

//     Serial.begin(9600);
//     Serial.println("Starting demo");
// }

// void loop()
// {

//     int pwm;

//     for (pwm = 0; pwm <= 100; pwm++)
//     {
//         M1.setmotor(_CW, pwm);
//         // M2.setmotor(_CW, pwm);
//         Serial.print("Clockwise PWM: ");
//         Serial.println(pwm);
//         delay(100);
//     }

//     Serial.println("Motor STOP");
//     M1.setmotor(_STOP);
//     //   M2.setmotor( _STOP);

//     delay(delayMs);

//     for (pwm = 0; pwm <= 100; pwm++)
//     {
//         M1.setmotor(_CCW, pwm);
//         //delay(1);
//         // M2.setmotor(_CCW, pwm);
//         Serial.print("Counterclockwise PWM: ");
//         Serial.println(pwm);
//         delay(100);
//     }

//     Serial.println("Motor A&B STANDBY");
//     M1.setmotor(_STANDBY);
//     //   M2.setmotor( _STANDBY);
//     delay(delayMs);
// }
