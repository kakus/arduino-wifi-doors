#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <memory>
#include "Config.h"
#include "Logger.h"

#ifndef STASSID
#define STASSID "Tech_D0048174"
#define STAPSK  "UYKHRYJR"
#endif

FLogger GLog;
FConfig GConfig;

class FApp;
class FAbstractState
{
public:
  virtual ~FAbstractState() = default;
  virtual void Setup(FApp* App) = 0;
  virtual void Loop() { };
};

using FStatePtr = std::unique_ptr<FAbstractState>;

class FApp
{
public:
  void Setup() {
    Serial.begin(115200);
    Serial.println();
    if (!SPIFFS.begin()) {
      GLog.Info("Failed to mount SPIFFS");
    }
    
    if (auto file = SPIFFS.open("/cfg.txt", "r")) {
      GConfig.InitFromFile(file);
      file.close();
      for (const auto& p : GConfig.Data) {
        GLog.Info("Key = %s, Value = %s, Type = %d", 
          p.first.c_str(), p.second.AsString().c_str(), p.second.Type);
      }
    } else {
      Serial.println("Failed to init config");
    }
    
    if (State) State->Setup(this);
    bAlreadyStarted = true;
  }

  void Loop() {
    if (State) State->Loop();
  }
  
  void ChangeState(FAbstractState* NewState) {
    State.reset(NewState);
    if (bAlreadyStarted) State->Setup(this);
  }
  
private:
  bool bAlreadyStarted = false;
  FStatePtr State = nullptr;
};

class FTryConnectWifi: public FAbstractState
{
public:
  virtual void Setup(FApp* App) override {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(GConfig[F("wifi_ssid")], GConfig[F("wifi_pswd")]);

    Serial.println();
    auto b = WiFi.softAP("BalconyAP");
    Serial.print("softAp: ");
    Serial.println(b ? "true" : "false");

    
    // Wait for connection
    
  }

  virtual void Loop() override {
    if (!bConnected) {
      if (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        return;
      }
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(STASSID);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    
      if (MDNS.begin("esp8266")) {
        Serial.println("MDNS responder started");
      }
      WiFi.mode(WIFI_STA);
      bConnected = true;
    }
  }

  bool bConnected = false;
};




const char* ssid = STASSID;
const char* password = STAPSK;

ESP8266WebServer server(80);

const int led = 13;
const int enA = 5;
const int in1 = 4;
const int in2 = 0;

void handleRoot() {
  digitalWrite(led, 1);
  server.send(200, "text/plain", "hello from esp8266!");
  WiFi.begin(STASSID, STAPSK);
  digitalWrite(led, 0);
}

void handleNotFound() {
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}

FApp GApp;
void setup(void) {

  GApp.Setup();
  GApp.ChangeState(new FTryConnectWifi());

  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });
  
  pinMode(12, OUTPUT);
  server.on("/on", []{
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
    analogWrite(enA, 1024);
    server.send(200, "text/plain", "on");
  });
  server.on("/stop", []{
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
    analogWrite(enA, 0);
    server.send(200, "text/plain", "on");
  });

  server.on("/off", []{
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
    analogWrite(enA, 1024);
    server.send(200, "text/plain", "off");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop(void) {
  GApp.Loop();
  server.handleClient();
  MDNS.update();
}
