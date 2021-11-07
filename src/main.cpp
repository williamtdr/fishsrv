// fishsrv
// feeds fish automatically using a servo motor on a timer.
// web server for control and additional functionality.

// twitter @williamtdr

#include <Arduino.h>
#include <NTPClient.h>
#include <Preferences.h>

// ntp dependency
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32Servo.h>
#include "ESPAsyncWebServer.h"

/////// environment ////////
// GPIO the servo is attached to
static const int servoPin = 13;

// TODO: fill in your network ssid/password
const char* ssid = "ssid";
const char* password = "password";
#define WEB_SRV_PORT 80
#define TZ_UTC_OFFSET (-8 * 60 * 60) // PST
const int FILL_DEGREE = 87; // starting hole location, fill slider with fish food
const int DISPENSE_DEGREE = 58; // dispensing hole location


//// fish configuration ///////
#define FEED_NUM_TIMES 2
#define FASTING_DAY_IDX_DEFAULT 0 // sunday

// skip every Xth day (persists);
bool doFastingDay = true;
bool doAutoFeed = true;
uint8_t fastingDayIndex = FASTING_DAY_IDX_DEFAULT;
uint8_t feedHour1 = 8;
uint8_t feedHour2 = 16; // TODO

//////// state /////////
uint8_t feedIndex = 0;
bool connectedToWifi = false;

// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, TZ_UTC_OFFSET);
Servo actuator;
AsyncWebServer server(WEB_SRV_PORT);
Preferences prefs;

void writePersistentStore() {
  Serial.println("writing configuration to persistent store.");
  
  prefs.putBool("doFastingDay", doFastingDay);
  prefs.putUInt("fastingDayIndex", fastingDayIndex);
  prefs.putUInt("feedHour1", feedHour1);
  prefs.putUInt("feedHour2", feedHour2);
  prefs.putUInt("feedIndex", feedIndex);
  prefs.putUInt("doAutoFeed", doAutoFeed);
}

void readPeristentStore() {
  Serial.println("reading configuration from persistent store.");

  doFastingDay = prefs.getBool("doFastingDay", doFastingDay);

  Serial.print("doFastingDay: ");
  Serial.println(doFastingDay ? "yes" : "no");

  fastingDayIndex = prefs.getUInt("fastingDayIndex", fastingDayIndex);
  Serial.print("fastingDayIndex: ");
  Serial.println(fastingDayIndex);

  feedHour1 = prefs.getUInt("feedHour1", feedHour1);
  Serial.print("feedHour1: ");
  Serial.println(feedHour1);

  feedHour2 = prefs.getUInt("feedHour2", feedHour2);
  Serial.print("feedHour2: ");
  Serial.println(feedHour2);

  feedIndex = prefs.getUInt("feedIndex", feedIndex);
  Serial.print("feedIndex: ");
  Serial.println(feedIndex);

  doAutoFeed = prefs.getBool("doAutoFeed", doAutoFeed);
  Serial.print("doAutoFeed: ");
  Serial.println(doAutoFeed ? "yes" : "no");
}

void initHardware() {
  Serial.begin(115200);
  Serial.println("---");

  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(12, OUTPUT);
  digitalWrite(12, HIGH);

  // it doesn't need to be fast
  setCpuFrequencyMhz(80);

  actuator.attach(servoPin);
  actuator.write(FILL_DEGREE); // tell servo to go to fill position
  delay(1000); // sanity check, in case we started in a bad state
  // turn power off
  actuator.detach();

  prefs.begin("fishsrv", false);
  readPeristentStore();
}

bool lockIsFeeding = false;

void feed() {
  if (lockIsFeeding)
  {
    return;
  }

  lockIsFeeding = true;
  Serial.println("feed()");
  actuator.attach(servoPin);
  
  actuator.write(DISPENSE_DEGREE - 5); // overshoot to left
  delay(450);
  actuator.write(DISPENSE_DEGREE + 6);
  // this should be as quick as possible to shake food loose:
  delay(200);
  actuator.write(DISPENSE_DEGREE);
  delay(500); // wait for food to fall
  
  actuator.write(FILL_DEGREE);

  delay(500);
  // turn power off
  actuator.detach();

  lockIsFeeding = false;
}

void handleRoot(AsyncWebServerRequest* request) {
  String text = "";
  text += "<!DOCTYPE html><html>\n";
  text += "<head>\n";
  text += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  text += "<title>fishsrv</title>\n";
  text += "</head>\n";
  text += "<body style=\"margin-left: 20px; line-height: 1.6\">\n";
        
  text += "<h1>fishsrv</h1>\n";

  text += "local machine time is <b>\n";
  text += timeClient.getFormattedTime();
  text += "</b>, day=<b>\n";
  text += timeClient.getDay();
  text += "</b><br>\n";

  text += "have fed <b>\n";
  text += feedIndex;
  text += "</b> of <b>\n";
  text += FEED_NUM_TIMES;
  text += "</b> times thus far today.<br><br>";

  text += "feed automatically:  <b>";
  text += doAutoFeed ? "yes" : "no";
  text += "</b> (<a href=\"#\" data-endpoint=\"'/api/toggleAutoFeed'\">toggle</a>)<br>\n";

  text += "enforce fasting day:  <b>";
  text += doFastingDay ? "yes" : "no";
  text += "</b> (<a href=\"#\" data-endpoint=\"'/api/toggleFasting'\">toggle</a>)<br>\n";

  text += "fasting day index:  <b>";
  text += fastingDayIndex;
  text += "</b> (sunday = 0, new value: <input id=\"fastingIdx\" type=\"number\" min=\"0\" max=\"6\" style=\"width: 30px\" value=\"";
  text += fastingDayIndex;
  text += "\"/>, <a href=\"#\" data-endpoint=\"'/api/setFastingIndex?x=' + document.getElementById('fastingIdx').value\">apply</a>)<br>\n";

  text += "first feeding hour:  <b>";
  text += feedHour1;
  text += "</b> (new value: <input id=\"feedHour1Input\" type=\"number\" min=\"0\" max=\"23\" style=\"width: 30px\" value=\"";
  text += feedHour1;
  text += "\"/>, <a href=\"#\" data-endpoint=\"'/api/setFeedHour1?x=' + document.getElementById('feedHour1Input').value\">apply</a>)<br>\n";

  text += "second feeding hour:  <b>";
  text += feedHour2;
  text += "</b> (new value: <input id=\"feedHour2Input\" type=\"number\" min=\"0\" max=\"23\" style=\"width: 30px\" value=\"";
  text += feedHour2;
  text += "\"/>, <a href=\"#\" data-endpoint=\"'/api/setFeedHour2?x=' + document.getElementById('feedHour2Input').value\">apply</a>)<br>\n";

  text += "<br><a href=\"#\" data-endpoint=\"'/api/feed'\">feed now!</a><br>\n";

  text += "<script>";
  text += "var anchors = document.getElementsByTagName(\"a\");";
  text += "for (const a of anchors) {";
  text += "    a.onpointerup = () => {";
  text += "        a.innerText = \"loading...\";\n";
  text += "        a.style.pointerEvents = \"none\";\n";
  text += "        fetch(eval(a.dataset.endpoint), { method: 'POST', cache: 'no-cache' })\n";
  text += "        .then(() => window.location.reload());\n";
  text += "    }\n";
  text += "}\n";
  text += "</script>\n";

  text += "</body></html>\n";   

  request->send(200, "text/html", text); 
}

void connect() {
  Serial.println("connecting to wifi...");
  WiFi.begin(ssid, password);

  int maxAttempts = 30;
  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    i++;

    if (i == maxAttempts) break;
  }

  connectedToWifi = WiFi.status() == WL_CONNECTED;

  if(connectedToWifi) {
    Serial.println("");
    Serial.print("connected. ip address: ");
    Serial.println(WiFi.localIP());
    // enable modem sleep
    WiFi.setSleep(WIFI_PS_MIN_MODEM);

    // connect to time server
    timeClient.begin();

    server.on("/", handleRoot);
    server.on("/api/feed", HTTP_POST, [](AsyncWebServerRequest *request) {
      feed();

      request->redirect("/");
    });
    server.on("/api/toggleFasting", HTTP_POST, [](AsyncWebServerRequest *request) {
      doFastingDay = !doFastingDay;
      writePersistentStore();
      request->send(200, "text/plain", "ok");
    });
    server.on("/api/toggleAutoFeed", HTTP_POST, [](AsyncWebServerRequest *request) {
      doAutoFeed = !doAutoFeed;
      writePersistentStore();
      request->send(200, "text/plain", "ok");
    });
    server.on("/api/setFastingIndex", HTTP_POST, [](AsyncWebServerRequest *request) {
      if(request->hasParam("x")) {
        AsyncWebParameter* p = request->getParam("x");
        uint8_t newFastingDay = (uint8_t) atoi(p->value().c_str());

        if (newFastingDay >= 0 && newFastingDay <= 6) {
          fastingDayIndex = newFastingDay;
        }

        writePersistentStore();
      }

      request->send(200, "text/plain", "ok");
    });
    server.on("/api/setFeedHour1", HTTP_POST, [](AsyncWebServerRequest *request) {
      if(request->hasParam("x")) {
        AsyncWebParameter* p = request->getParam("x");
        uint8_t newVal = (uint8_t) atoi(p->value().c_str());

        if (newVal >= 0 && newVal <= 23) {
          feedHour1 = newVal;
        }

        writePersistentStore();
      }

      request->send(200, "text/plain", "ok");
    });
    server.on("/api/setFeedHour2", HTTP_POST, [](AsyncWebServerRequest *request) {
      if(request->hasParam("x")) {
        AsyncWebParameter* p = request->getParam("x");
        uint8_t newVal = (uint8_t) atoi(p->value().c_str());

        if (newVal >= 0 && newVal <= 23) {
          feedHour2 = newVal;
        }

        writePersistentStore();
      }

      request->send(200, "text/plain", "ok");
    });
  
    // start web server
    server.begin();
  } else {
    Serial.println("failed to get a network connection, ** ENTERING BACKUP ROUTINE**");

    // wait 8h, feed, reboot
    delay(8 * 60 * 60 * 1000);
    feed();
    ESP.restart();
  }
}

void setup() {
  initHardware();
  connect();
}

void loop() {
  timeClient.update();

  if (doAutoFeed) {
    if (timeClient.getDay() == fastingDayIndex && doFastingDay) {
      Serial.println("it's fasting day.");
    } else {
      if (timeClient.getHours() == feedHour1 && feedIndex == 0) {
        // feed for first time today!
        Serial.println("feeding first time!");
        feedIndex++;
        writePersistentStore();
        feed();
      } else if (timeClient.getHours() == feedHour2 && feedIndex == 1) {
        Serial.println("feeding second time!");
        // feed for second time today!
        feedIndex++;
        writePersistentStore();
        feed();
      }

      // reset feedIndex at midnight
      if (timeClient.getHours() == 0 && timeClient.getMinutes() < 5 && feedIndex != 0) {
        Serial.println("resetting.");
        feedIndex = 0;
        writePersistentStore();
      }
    }
  }

  delay(5000);
  // wish we could esp_set_light_sleep + wakeup, but then can't maintain wifi connection / server
  // buffer isn't flushed correctly
}
