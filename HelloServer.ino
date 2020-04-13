#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <memory>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "Config.h"
#include "Logger.h"
#include "Drivers.h"

static const char CLogFileName[] PROGMEM = "/log.txt";
static const char CConfigFileName[] PROGMEM = "/cfg.txt";

FLogger GLog;
FConfig GConfig;
fs::FS& GFS = SPIFFS;

WiFiUDP GNtpUDP;
NTPClient GTimeClient(GNtpUDP, /* utc offset (s) */3600);
ESP8266WebServer server{80};

/**
 * Entry point for this project, contains all configuration
 */
class FApp
{
public:
  
  void Setup() {
    InitLogAndConfig();
    InitDrivers();
    BindDrivers();
    InitWiFi();
    InitWebServer();
  }
  
  void InitLogAndConfig() {
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
      GLog.Info(F("Failed to init config"));
    }
  }

  void InitDrivers() {
    // Driver list, order matters
    Drivers.emplace_back(new FAnalogReadDriver());
    Drivers.emplace_back(new FThermometerDriver());
    Drivers.emplace_back(new FIRDriver());
    Drivers.emplace_back(new FLockDriver());
    Drivers.emplace_back(new FMotorDriver());
    for (auto& Driver : Drivers) Driver->Mount();
  }

  void BindDrivers();

  void InitWiFi() {
    String Ssid = GConfig["WiFi.ssid"];
    String Pswd = GConfig["WiFi.pswd"];

    GLog.Info(F("Trying to connect to Wifi with ssid: %s, pswd: %s"), 
      Ssid, Pswd);
      
    WiFi.mode(WIFI_STA);
    WiFi.begin(Ssid, Pswd);

  }

  void InitWebServer();

  template<typename T>
  T* GetDriver() const;

  void CheckWifi() {
    if (WiFi.status() != WL_CONNECTED) {
      if (_WifiConnected) {
        GLog.Info(F("Lost connection to WiFi"));
      }
      _WifiConnected = false;
      
      if (millis() - _WifiTimer0 > 10000) {
        _WifiTimer0 = millis();
        
        // WL_IDLE_STATUS      = 0, WL_NO_SSID_AVAIL    = 1, WL_SCAN_COMPLETED   = 2, WL_CONNECTED        = 3,
        // WL_CONNECT_FAILED   = 4, WL_CONNECTION_LOST  = 5, WL_DISCONNECTED     = 6
        static const char* Status[] = { "IDLE", "NO SSID", "SCAN", "CONNECTED", 
          "CONN_FAILED", "CONN_LOST", "DISCONNECTED" };
    
        GLog.Info(F("- Failed to connect: %s"), 
          Status[constrain(WiFi.status(), 0, 6)]);
      }
   }
   else if (!_WifiConnected)
   {
      GLog.Info(F("Connected to Wifi with address: %s"), 
        WiFi.localIP().toString());      

      String Name = GConfig["MDNS.name"];
      if (Name.length() > 0 && MDNS.begin(Name)) {
        GLog.Info(F("MDNS responder started with name: %s"), Name);
      }
      
      _WifiConnected = true;
    }
  }

  void Loop() {
    CheckWifi();
    server.handleClient();
    MDNS.update();
  }
  
private:
  uint32_t _WifiTimer0 = 0;
  bool _WifiConnected = false;
  std::vector<FDriverPtr> Drivers;
};

template<>
FAnalogReadDriver* FApp::GetDriver() const { return (FAnalogReadDriver*)Drivers[0].get(); }
template<>
FThermometerDriver* FApp::GetDriver() const { return (FThermometerDriver*)Drivers[1].get(); }
template<>
FIRDriver* FApp::GetDriver() const { return (FIRDriver*)Drivers[2].get(); }
template<>
FLockDriver* FApp::GetDriver() const { return (FLockDriver*)Drivers[3].get(); }
template<>
FMotorDriver* FApp::GetDriver() const { return (FMotorDriver*)Drivers[4].get(); }

void FApp::BindDrivers() {
    GetDriver<FIRDriver>()->BindAction([this] {
      auto Motor = GetDriver<FMotorDriver>();
      Motor->ToggleState();
    });
}

void FApp::InitWebServer() {
  
//  server.on("/", [] {
//    server.send(200, "text/plain", "hello world!");
//  });

  server.on("/A0", [this] {
    auto d = GetDriver<FAnalogReadDriver>();
    String json = "{\"A0\":" + String(d->GetVoltage(), 5) + "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  server.on("/C", [this] {
    auto d = GetDriver<FThermometerDriver>();
    String json = "{\"C\":" + String(d->GetTemperature(), 3) + "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  server.on("/values", [this] {
    String json = "{\n";
    {
      auto d = GetDriver<FThermometerDriver>();
      json += "\"C\":" + String(d->GetTemperature(), 3) + ",\n";
    }
    {
      auto d = GetDriver<FAnalogReadDriver>();
      json += "\"A0\":" + String(d->GetVoltage(), 5) + ",\n";
    }
    {
      json += "\"heap\":" + String(ESP.getFreeHeap()) + ",\n";
      json += "\"uptime\":" + String(millis() / 1000., 3) + ",\n";
      json += "\"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16))) + "\n";
    }
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  server.on("/log", []{
    File file = GFS.open("/log-live.html", "r");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    size_t sent = server.streamFile(file, "text/html"); // And send it to the client
    file.close();                                       // Then close the file again
  });
  
  server.on("/toggle", [this]{
    auto d = GetDriver<FMotorDriver>();
    d->ToggleState();
    server.send(200, "text/plain", d->GetStateAsString());
  });


  struct Local {
    static bool handleFileRead(String path) {
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
  };
  
  server.onNotFound([] {
    if (!Local::handleFileRead(server.uri())) {
      server.send(404, "text/plain", "File not found");
    }
  });
  

  server.begin();
  GLog.Info(F("HTTP server started"));
}

FApp GApp;
void setup(void) {
  GApp.Setup();
}

void loop(void) {
  GApp.Loop();
}
