#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <AdafruitIO_WiFi.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Fonts/FreeMonoBold12pt7b.h>  // Add a custom font
#include <Fonts/FreeMono9pt7b.h>  // Add a custom font

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

const int pinBlue = _D7;
const int pinGreen = _D5;
const int pinRed = _D6;

int lastRed = 255;
int lastGreen = 255;
int lastBlue = 255;

// create instance of oled display
Adafruit_SSD1306 display(128, 64);

// Create an instance of the server
ESP8266WebServer server(ENV_PORT);

// Create an instance of Adafruit HTTP Api Client
AdafruitIO_WiFi io(ENV_AIO_USERNAME, ENV_AIO_KEY, ENV_WIFI_SSID, ENV_WIFI_PASS);

// Using that, connect to the digital feed
AdafruitIO_Feed *digitalFeed = io.feed("digital");

String rgbDisplay() {
  return String("rgb(" + String(lastRed) + ", " + String(lastGreen) + ", " + String(lastBlue) + ")");
}

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

  Serial.println(rgbDisplay());

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

void displayInit() {
  delay(100);  // This delay is needed to let the display to initialize

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // Initialize display with the I2C address of 0x3C
 
  display.clearDisplay();  // Clear the buffer

  display.setTextColor(WHITE);  // Set color of the text

  display.setRotation(0);  // Set orientation. Goes from 0, 1, 2 or 3

  //display.setTextWrap(false);  // By default, long lines of text are set to automatically “wrap” back to the leftmost column.
                               // To override this behavior (so text will run off the right side of the display - useful for
                               // scrolling marquee effects), use setTextWrap(false). The normal wrapping behavior is restored
                               // with setTextWrap(true).

  display.dim(0);  //Set brightness (0 is maximun and 1 is a little dim)
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

void renderDisplay() {
  display.clearDisplay();  // Clear the display so we can refresh
  display.setFont(&FreeMono9pt7b);  // Set a custom font
  display.setTextSize(0);  // Set text size. We are using a custom font so you should always use the text size of 0
  display.setCursor(0, 10);  // (x,y)
  display.println(WiFi.localIP());  // Text or value to print  
  display.setCursor(0, 30);
  display.println(rgbDisplay());
  display.display();  // Print everything we set previously
}

void setup() {
  serialInit();
  displayInit();
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

  // update oled display
  renderDisplay();
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