#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <BlynkSimpleEsp8266.h>
#include "FS.h"

#include "HTML.h"

#define VERSION "1.0"
#define HOSTNAME "Irrigation"
#define CONFIG "/conf.txt"

#define BLYNK_PRINT Serial

char* auth = "YourAuthToken";

const int WEBSERVER_PORT = 80;
char* www_username = "admin";
char* www_password = "password";

String OTA_Password = "password";

const int externalLight = LED_BUILTIN;
const int relay = D2;
const int flowSensor = D5;
const int moistureSensor = A0;

const int calibrationValue0 = 866;
const int calibrationValue100 = 416;

int triggerValue;
int targetValue;
int flow;

int currentMoisture;

boolean manualOverride = false;

long previousMillisMoistureSensor = 0;
long intervalMoistureSensor = 6000;

ESP8266WebServer server(WEBSERVER_PORT);

void setup() {
  Serial.begin(115200);
  SPIFFS.begin();
  delay(10);

  Serial.println();
  pinMode(externalLight, OUTPUT);
  pinMode(relay, OUTPUT);
  pinMode(flowSensor, INPUT);

  readSettings();

  parseHomePage();
  parseConfigurePage();
  parseControlPage();

  WiFiManager wifiManager;

  String hostname(HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);
  if (!wifiManager.autoConnect((const char *)hostname.c_str())) {// new addition
    delay(3000);
    WiFi.disconnect(true);
    ESP.reset();
    delay(5000);
  }

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  if (OTA_Password != "") {
    ArduinoOTA.setPassword(((const char *)OTA_Password.c_str()));
  }
  ArduinoOTA.begin();

  Serial.println("WEBSERVER_ENABLED");
  server.on("/Home", HTTP_GET, handleRoot);
  server.on("/Configure", handleConfigure);
  server.on("/updateConfig", handleUpdateConfigure);
  server.on("/updateControl", handleUpdateControl);
  server.on("/Control", handleControl);
  server.on("/FactoryReset", handleSystemReset);
  server.on("/WifiReset", handleWifiReset);
  server.onNotFound(handleRoot);
  server.begin();
  Serial.println("Server Started");
  String webAddress = "http://" + WiFi.localIP().toString() + ":" + String(WEBSERVER_PORT) + "/";
  Serial.println("Use this URL : " + webAddress);

  Blynk.config(auth);
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  Blynk.run();

  delay(1);
  if (digitalRead(relay) == LOW) {
    if (manualOverride) {
      digitalWrite(relay, HIGH);
    } else {
      if (currentMoisture < triggerValue) {
        digitalWrite(relay, HIGH);
      }
    }
  }

  if (digitalRead(relay) == HIGH && currentMoisture >= targetValue && !manualOverride) {
    digitalWrite(relay, LOW);
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisMoistureSensor > intervalMoistureSensor) {
    previousMillisMoistureSensor = currentMillis;

    currentMoisture = readMoistureSensor();
  }
}

void handleSystemReset() {
  if (!server.authenticate(www_username, www_password)) {
    return server.requestAuthentication();
  }
  Serial.println("Reset System Configuration");
  if (SPIFFS.remove(CONFIG)) {
    handleRoot();
    ESP.restart();
  }
}

void handleWifiReset() {
  if (!server.authenticate(www_username, www_password)) {
    return server.requestAuthentication();
  }
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  handleRoot();
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  ESP.restart();
}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality() {
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100) {
    return 0;
  } else if (dbm >= -50) {
    return 100;
  } else {
    return 2 * (dbm + 100);
  }
}

void writeSettings() {
  // Save decoded message to SPIFFS file for playback on power up.
  File f = SPIFFS.open(CONFIG, "w");
  if (!f) {
    Serial.println("File open failed!");
  } else {
    Serial.println("Saving settings now...");
    f.println("www_username=" + String(www_username));
    f.println("www_password=" + String(www_password));
    f.println("ota_password=" + String(OTA_Password));
    f.println("auth=" + String(auth));
    f.println("targetvalue=" + String(targetValue));
    f.println("triggervalue=" + String(triggerValue));
  }

  f.close();
  readSettings();
}

void handleUpdateConfigure() {
  if (!server.authenticate(www_username, www_password)) {
    return server.requestAuthentication();
  }

  String temp = server.arg("userid");
  temp.toCharArray(www_username, sizeof(temp));
  temp = server.arg("stationpassword");
  temp.toCharArray(www_password, sizeof(temp));
  OTA_Password = server.arg("otapassword");

  writeSettings();
  handleConfigureNoPassword();
}

void readSettings() {
  if (SPIFFS.exists(CONFIG) == false) {
    Serial.println("Settings File does not yet exists.");
    writeSettings();
    return;
  }
  File fr = SPIFFS.open(CONFIG, "r");
  String line;
  while (fr.available()) {
    line = fr.readStringUntil('\n');

    if (line.indexOf("www_username=") >= 0) {
      String temp = line.substring(line.lastIndexOf("www_username=") + 13);
      temp.trim();
      temp.toCharArray(www_username, sizeof(temp));
      Serial.println("www_username=" + String(www_username));
    }
    if (line.indexOf("www_password=") >= 0) {
      String temp = line.substring(line.lastIndexOf("www_password=") + 13);
      temp.trim();
      temp.toCharArray(www_password, sizeof(temp));
      Serial.println("www_password=" + String(www_password));
    }
    if (line.indexOf("ota_password=") >= 0) {
      OTA_Password = line.substring(line.lastIndexOf("ota_password=") + 13);
      Serial.println("ota_password=" + String(OTA_Password));
    }
    if (line.indexOf("auth=") >= 0) {
      String temp = line.substring(line.lastIndexOf("auth=") + 5);
      temp.trim();
      temp.toCharArray(auth, sizeof(temp));
      Serial.println("auth=" + String(auth));
    }
    if (line.indexOf("targetvalue=") >= 0) {
      targetValue = line.substring(line.lastIndexOf("targetvalue=") + 12).toInt();
      Serial.println("targetvalue=" + String(targetValue));
    }
    if (line.indexOf("triggervalue=") >= 0) {
      triggerValue = line.substring(line.lastIndexOf("triggervalue=") + 13).toInt();
      Serial.println("triggervalue=" + String(triggerValue));
    }
  }
  fr.close();
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URL in the request
}

void handleRoot() {
  String form = parseHomePage();
  form.replace("%MOISTURELEVEL%", String(currentMoisture));
  server.send(200, "text/html", form);  // Home webpage for the cloud
}

void handleConfigure() {
  if (!server.authenticate(www_username, www_password)) {
    return server.requestAuthentication();
  }
  String form = parseConfigurePage();
  form.replace("%USERID%", www_username);
  form.replace("%STATIONPASSWORD%", www_password);
  form.replace("%OTAPASSWORD%", OTA_Password);
  form.replace("%BLYNKAUTHENTICATOR%", auth);

  server.send(200, "text/html", form);  // Configure portal for the cloud
}

void handleConfigureNoPassword() {
  String form = parseConfigurePage();
  form.replace("%USERID%", www_username);
  form.replace("%STATIONPASSWORD%", www_password);
  form.replace("%OTAPASSWORD%", OTA_Password);
  form.replace("%BLYNKAUTHENTICATOR%", auth);

  server.send(200, "text/html", form);  // Configure portal for the cloud
}

void handleControl() {
  if (!server.authenticate(www_username, www_password)) {
    return server.requestAuthentication();
  }
  String form = parseControlPage();
  form.replace("%TARGETVALUE%", String(targetValue));
  form.replace("%TRIGGERVALUE%", String(triggerValue));

  server.send(200, "text/html", form);
}

void handleUpdateControl() {
  if (!server.authenticate(www_username, www_password)) {
    return server.requestAuthentication();
  }

  triggerValue = server.arg("trigger").toInt();
  targetValue = server.arg("target").toInt();

  writeSettings();
  handleControlNoPassword();
}

void handleControlNoPassword() {
  String form = parseControlPage();

  form.replace("%TARGETVALUE%", String(targetValue));
  form.replace("%TRIGGERVALUE%", String(triggerValue));

  server.send(200, "text/html", form);
}

int readMoistureSensor() {
  int val;
  val = analogRead(A0);
  Serial.println(val);

  int calculation = 100 - (int)(100*(float)(val - calibrationValue100) / (float)(calibrationValue0 - calibrationValue100));
  Serial.println(calculation);

  return calculation;
}
