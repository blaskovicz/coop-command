#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <AdafruitIO_WiFi.h>

static const uint8_t _D0 = 16;
static const uint8_t _D1 = 5;
static const uint8_t _D2 = 4;
static const uint8_t _D3 = 0;
static const uint8_t _D4 = 2;
static const uint8_t _D5 = 14;
static const uint8_t _D6 = 12;
static const uint8_t _D7 = 13;
static const uint8_t _D8 = 15;
static const uint8_t _D9 = 3;
static const uint8_t _D10 = 1;


/* env setup, to be filled in before usage in liue of EEPROM persistence */
#ifndef ENV_H
#define ENV_H

#define ENV_WIFI_SSID     ""
#define ENV_WIFI_PASS     ""
#define ENV_AIO_USERNAME  ""
#define ENV_AIO_KEY       ""
#define ENV_PORT 80

#endif

const int pinBlue = _D1;
const int pinGreen = _D2;
const int pinRed = _D6;

int lastRed = 255;
int lastGreen = 255;
int lastBlue = 255;

// Create an instance of the server
ESP8266WebServer server(ENV_PORT);

// Create an instance of Adafruit HTTP Api Client
AdafruitIO_WiFi io(ENV_AIO_USERNAME, ENV_AIO_KEY, ENV_WIFI_SSID, ENV_WIFI_PASS);

// Using that, connect to the digital feed
AdafruitIO_Feed *digitalFeed = io.feed("digital");

// update the led rgb value when we receive valid feed message
void handleAdafruitMessage(AdafruitIO_Data *data) {
  String dataString = data->toString();
  Serial.println("received <- " + dataString);
  

  if (dataString.indexOf("#") != 0) {
    Serial.println("... malformed message, ignored.");
    return;
  }

  lastRed = data->toRed();
  lastGreen = data->toGreen();
  lastBlue = data->toBlue();

  Serial.println("red:" + String(lastRed));
  Serial.println("green:" + String(lastGreen));
  Serial.println("blue:" + String(lastBlue));

  analogWrite(pinRed, lastRed);
  analogWrite(pinGreen, lastGreen);  
  analogWrite(pinBlue, lastBlue);
}

void connectAdafruitIO() {
  // connect to io.adafruit.com
  Serial.print("Connecting to Adafruit IO");
  io.connect();

  // set up a message handler for the feed
  digitalFeed->onMessage(handleAdafruitMessage);

  // wait for a connection
  while(io.status() < AIO_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  // we are connected
  Serial.println();
  Serial.println(io.statusText());
  digitalFeed->get();
}

void ledInit() {
  // prepare LEDs
  int pins [3] = {pinRed, pinGreen, pinBlue};
  for(int i = 0; i < 3; i++) {
    pinMode(pins[i], OUTPUT);
    analogWrite(pins[i], 255);
  }
}

void serialInit() {
  // set up serial monitor and wait for it to open
  Serial.begin(115200);

  do {
    delay(100);
  } while(!Serial);
  
  Serial.println();
}

const char *indexPage = R""""(
<html>
  <head>
    <title>Coop Command</title>
    <script src="https://cdn.jsdelivr.net/npm/vue@2.5.17/dist/vue.js"></script>
    <style>
      .led {
        display: inline-block;
        width: 50px;
        height: 50px;
        margin-left: 10px;
        border: 1px solid #000;
        border-radius: 2px;
        vertical-align: middle;
      }
      .error {
        color: #ea1717;
      }
    </style>
  </head>
  <body>
    <div id='app'>
      <h1>Coop Command</h1>
      <h2 v-if="error" class="error">An error occurred fetching data: <pre>{{error}}</pre></h2>
      <div v-if="!error">
        <h2>LED Status: {{ledStatus}} <div class="led" :style="ledStyle"></div></h2>
      </div>
    </div>

    <script>
      new Vue({
        el: '#app',
        async created() {
          await this.fetchLEDState();
        },
        methods: {
          async fetchLEDState() {
            const resp = await fetch('/api/leds');
            if (!resp.ok) {
              this.error = resp.statusText;
            }
            this.led = await resp.json();
          },
        },
        computed: {
          ledStyle() {
            return { 'background-color': `rgb(${this.led.r}, ${this.led.g}, ${this.led.b})` };
          },
          ledStatus() {
            const sum = this.led.r + this.led.g + this.led.b;
            return sum === 0 ? 'off' : 'on';
          },
        },
        data: {
          error: null,
          led: {
            r: 0,
            g: 0,
            b: 0,
          },
        },
      });
    </script>
  </body>
</html>
)"""";

void handleRoot() {
  server.send(200, "text/html", indexPage);
}

void handleGetLEDs() {
  // TODO: caching reads
  server.send(200, "application/json", "{\"r\": " + String(lastRed) + ", \"g\": " + String(lastGreen) + ", \"b\": " + String(lastBlue) + "}");
}

// void handlePlain() {
//   if (server.method() != HTTP_POST) {
//     digitalWrite(led, 1);
//     server.send(405, "text/plain", "Method Not Allowed");
//     digitalWrite(led, 0);
//   } else {
//     digitalWrite(led, 1);
//     server.send(200, "text/plain", "POST body was:\n" + server.arg("plain"));
//     digitalWrite(led, 0);
//   }
// }

// void handleForm() {
//   if (server.method() != HTTP_POST) {
//     digitalWrite(led, 1);
//     server.send(405, "text/plain", "Method Not Allowed");
//     digitalWrite(led, 0);
//   } else {
//     digitalWrite(led, 1);
//     String message = "POST form was:\n";
//     for (uint8_t i = 0; i < server.args(); i++) {
//       message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
//     }
//     server.send(200, "text/plain", message);
//     digitalWrite(led, 0);
//   }
// }

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArgs: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void connectWifiClient() {
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print(F("Connecting to "));
  Serial.println(ENV_WIFI_SSID);
  Serial.flush();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ENV_WIFI_SSID, ENV_WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println();
  Serial.println(F("WiFi connected"));
}

void connectAndServeHTTP() {
  connectWifiClient();

  // start local dns server (coop-command.local)
  if (MDNS.begin("coop-command")) {
    Serial.println("MDNS responder started");
  }

  // mount server routes
  server.on("/", handleRoot);
  server.on("/api/leds", handleGetLEDs);
  // server.on("/postplain/", handlePlain);
  // server.on("/postform/", handleForm);
  server.onNotFound(handleNotFound);  

  // start server
  server.begin();
  Serial.println(F("Server started"));

  // Print the IP address
  Serial.println(WiFi.localIP());
}

void setup() {
  serialInit();
  ledInit();
  connectAdafruitIO();
  connectAndServeHTTP();
}

void loop() {
  // io.run(); is required for all sketches.
  // it should always be present at the top of your loop
  // function. it keeps the client connected to
  // io.adafruit.com, and processes any incoming data.
  io.run();

  // handle incoming http clients
  server.handleClient();

  // update local dns, just in case
  MDNS.update();
}

// TODO: OTA updates vvv
// https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WebServer/examples/WebUpdate/WebUpdate.ino

// TODO: bootstrap AP with network vvv
/*

/ Just a little test message.  Go to http://192.168.4.1 in a web browser
   connected to this access point to see it.
/
void handleRoot() {
  server.send(200, "text/html", "<h1>You are connected</h1>");
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();
  Serial.print("Configuring access point...");
  / You can remove the password parameter if you want the AP to be open. /
  WiFi.softAP(ssid, password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
*/