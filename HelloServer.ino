#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <memory>
#include <Ticker.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "Config.h"
#include "Logger.h"
#include "Drivers.h"

#ifndef STASSID
#define STASSID "Tech_D0048174"
#define STAPSK  "UYKHRYJR"
#endif


static const char CLogFileName[] PROGMEM = "/log.txt";
static const char CConfigFileName[] PROGMEM = "/cfg.txt";

FLogger GLog;
FConfig GConfig;
fs::FS& GFS = SPIFFS;

WiFiUDP GNtpUDP;
NTPClient GTimeClient(GNtpUDP, /* utc offset (s) */3600);

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
    // Init baud rate
    GLog.InitSink<FSerialSink>(115200);
    GLog.Info(F(""));
    if (!GFS.begin()) {
      GLog.Info(F("Failed to mount GFS (File System)"));
    }
    if (auto file = GFS.open(FPSTR(CLogFileName), "w")) {
      GLog.InitSink<FFileSink>(file);
    }
    else {
      GLog.Info(F("Failed to open log.txt"));
    }

    if (auto file = GFS.open(FPSTR(CConfigFileName), "r")) {
      GConfig.InitFromFile(file);
    }
    else {
      Serial.println(F("Failed to init config"));
    }

    if (State) State->Setup(this);
    bAlreadyStarted = true;

    // Driver list, order matters
    Drivers.emplace_back(new FAnalogReadDriver());

    for (auto& Driver : Drivers) Driver->Mount();
  }

  template<typename T>
  T* GetDriver() const;

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
  std::vector<FDriverPtr> Drivers;
};

template<>
FAnalogReadDriver* FApp::GetDriver() const { return (FAnalogReadDriver*)Drivers[0].get(); }

FApp GApp;

class FMeasueVcc: public FAbstractState {
  uint ts = 0;
  void Setup(FApp*) { }
  void Loop() {
    if (millis() - ts < 500) return;
    ts = millis();
    
      auto d = GApp.GetDriver<FAnalogReadDriver>();
      //auto pp = std::unique_ptr<char>(new char[5000]);
       GLog.Info(F("%f -> %f, buff: %d"), d->GetAvgOverTime(0.1f), d->GetVoltage(), d->Buffer.size());
       uint32_t free;
       uint16_t max;
       uint8_t frag;
       ESP.getHeapStats(&free, &max, &frag);
       GLog.Info(F("%d/%d frag: %d"), free, max, frag);
  }
  Ticker t;
};

class FTryConnectWifi: public FAbstractState
{
public:
  virtual void Setup(FApp* App) override {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin();

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
        GLog.Info(F("MDNS responder started"));
      }
      WiFi.mode(WIFI_STA);
      bConnected = true;
      GApp.ChangeState(new FMeasueVcc());
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
  digitalWrite(led, 0);
}

bool handleFileRead(String path) {
  if (!path.endsWith(F("log.txt")))
    GLog.Info(F("File request: %s"), path);
    
  if (path.endsWith("/")) {
    path += "index.htm";
  }
  String contentType = String("text/") + (path.endsWith("html") ? "html" : "plain");
  String pathWithGz = path + ".gz";
  if (GFS.exists(pathWithGz) || GFS.exists(path)) {
    if (GFS.exists(pathWithGz)) {
      path += ".gz";
    }
    File file = GFS.open(path, "r");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  else {
    GLog.Info(F("Failed to found requested file"));
  }
  return false;
}


void setup(void) {

  GApp.Setup();
  GApp.ChangeState(new FTryConnectWifi());

  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

//  server.on("/A0", [] {
//    auto d = GApp.GetDriver<FAnalogReadDriver>();
//    String json = "{\"A0\":[";
//    for (auto r : d->Buffer) {
//      json += String(FAnalogReadDriver::ConvertReadToVolts(r), 5);
//      json += ",";
//    }
//    if (json.endsWith(",")) json.remove(json.length()-1,1);
//    json += "]}";
//    server.sendHeader("Access-Control-Allow-Origin", "*");
//    server.send(200, "application/json", json);
//  });
  server.on("/A0", [] {
    auto d = GApp.GetDriver<FAnalogReadDriver>();
    String json = "{\"A0\":" + String(d->GetVoltage(), 5) + "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  server.on("/log", []{
    File file = GFS.open("/log-live.html", "r");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    size_t sent = server.streamFile(file, "text/html"); // And send it to the client
    file.close();                                       // Then close the file again
  });
  
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

  server.onNotFound([] {
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "File not found");
    }
  });
  

  server.begin();
  Serial.println("HTTP server started");
}

void loop(void) {
  GApp.Loop();
  server.handleClient();
  MDNS.update();
}
