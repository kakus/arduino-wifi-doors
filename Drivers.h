#pragma once
#include <memory>
#include <deque>

#include "OneWire.h"
#include "DallasTemperature.h"
#include "IRremoteESP8266.h"
#include "IRrecv.h"
#include "IRutils.h"

#include "Ticker.h"
#include "Logger.h"
#include "Config.h"
#include "Constants.h"

using FTickerPtr = std::unique_ptr<Ticker>;
class FDriver
{
public:
  struct FConfig {
    float UpdateInterval = 0;
    bool  bUpdate = false;
  };

public:
  void Mount() {
    Setup(Config);
    if (Config.bUpdate) {
      MyTicker.reset(new Ticker());
      MyTicker->attach(Config.UpdateInterval, [this] { Update(); });
    }
  }

  const FConfig& GetConfig() const {
    return Config;
  }

protected:
  virtual void Setup(FConfig& Config) { }
  virtual void Update() { }

private:
  FConfig Config;
  FTickerPtr MyTicker;
};

using FDriverPtr = std::unique_ptr<FDriver>;

class FAnalogReadDriver : public FDriver
{
protected:
  constexpr static const size_t CBUF_SIZE = 200;

  virtual void Setup(FConfig& Config) override {
    Config.bUpdate = true;
    Config.UpdateInterval = .01f;
  }

  void Update() override {
    if (Buffer.size() >= CBUF_SIZE) Buffer.pop_back();
    Buffer.push_front(analogRead(A0));
  }

public:
  static inline float ConvertReadToVolts(float Read) {
    auto v = 3.3f * Read / 1024.f;
    return v * 0.962 - 0.0428;
  }
  
  float GetVoltage() const {
    return ConvertReadToVolts(GetAvgOverTime(0.1f));
  }

  float GetAvgOverTime(float Span) const {
    float Avg = 0;
    auto it = Buffer.cbegin();
    for (; Span >= 0 && it != Buffer.cend(); Span -= GetConfig().UpdateInterval) {
      Avg += *it++;
    }
    auto n = max(1, (int)(it - Buffer.cbegin()));
    return Avg / n;
  }

  std::deque<int16_t> Buffer{ CBUF_SIZE };
};

class FThermometerDriver : public FDriver {
  
public:
  FThermometerDriver()
    : _Wire(EPin::D5)
    , _Sens(&_Wire)
  { }

  float GetTemperature() const { return _Temperature; }
  
protected:
  virtual void Setup(FConfig& Config) override final {
    Config.bUpdate = true;
    Config.UpdateInterval = 1.f;
    
    _Sens.begin();
    if (_Sens.getDeviceCount() == 0) {
      GLog.Info(F("FThermometerDriver: Failed to find thermometer!"));
    }
    _Sens.setWaitForConversion(false);
    _Sens.requestTemperatures();
  }
  
  virtual void Update() override final {
    _Temperature = _Sens.getTempCByIndex(0);
    _Sens.requestTemperatures();
  }

protected:
  float _Temperature = 100.f;
  OneWire _Wire;
  DallasTemperature _Sens;
};

class FIRDriver : public FDriver {
  
public:
  FIRDriver()
    : _IRrecv(EPin::D6)
  { }

  void BindAction(std::function<void()> Callback) {
    _Callback = Callback;
  }

protected:
  virtual void Setup(FConfig& Config) override final {
    if (!GConfig["IRRemote.Enable"]) {
      GLog.Info(F("FIRDriver: Remote controll will be disabled"));
      return;
    }
    
    Config.bUpdate = true;
    Config.UpdateInterval = 0.1f;
    _IRrecv.enableIRIn();
  }

  virtual void Update() override {
    decode_results Results;
    if (_IRrecv.decode(&Results)) {
      uint32_t Code = Results.value;
      // Red button on LG remote, Red button on Netia
      if (Code == 0x20DF4EB1 || Code == 0xC1CC629D) {
        GLog.Info(F("FIRDriver: Received button pressed %X"), Code);
        if (_Callback) _Callback();
      }
      
      _IRrecv.resume();
    }
  }

  IRrecv _IRrecv;
  std::function<void()> _Callback;
};

class FLockDriver : public FDriver {
public:
  static bool IsLocked() { return bIsLocked; }
protected:
  virtual void Setup(FConfig& Config) override final {
    Config.bUpdate = true;
    Config.UpdateInterval = 0.01f;
    pinMode(EPin::D7, INPUT);
  }
  virtual void Update() override final {
    if (bIsLocked != digitalRead(EPin::D7)) {
      bIsLocked = !bIsLocked;
      GLog.Info(F("FLockDriver: Doors are %s"), bIsLocked ? "LOCKED" : "UNLOCKED"); 
    }
  }
  
  static bool bIsLocked;
};

bool FLockDriver::bIsLocked = true;

class FMotorDriver : public FDriver {
public:
  enum ECommand { Open, Stop, Close };

  void ToggleState() {
    switch (_State) {
    case Opening:
    case Closing:
      ExecuteCommand(ECommand::Stop);
      break; 

    case Stop:
      if (_PreviousState == Opening)
        ExecuteCommand(ECommand::Close);
      else if (_PreviousState == Closing)
        ExecuteCommand(ECommand::Open);
      break;
    }
  }
  
  void ExecuteCommand(ECommand Command) {
    if (FLockDriver::IsLocked()) {
      GLog.Info(F("FLockDriver: Doors are locked, can't execute any commands"));
      return;
    }
    switch (Command) {
    case ECommand::Open:
      if (_State == Opening) break;
      if (_Progress < 1.f) {
        _PreviousState = _State;
        _State = Opening;
        GLog.Info(F("Opening the door. Current progress %.2f"), _Progress);
        // digitalWrite
      }
      break;

    case ECommand::Stop:
      if (_State != Stoped)  {
        _PreviousState = _State;
        _State = Stoped;
        GLog.Info(F("Stoping the door. Current progress %.2f"), _Progress);
        // digitalWrite
      }
      break;

    case ECommand::Close:
      if (_State == Closing) break;
      if (_Progress > 0.f) {
        _PreviousState = _State;
        _State = Closing;
        GLog.Info(F("Closing the door. Current progress %.2f"), _Progress);
        // digitalWrite
      }
      break;
    }
  }
protected:
  virtual void Setup(FConfig& Config) override final {
    Config.bUpdate = true;
    Config.UpdateInterval = 0.01f;
    _MaxWorkTime = GConfig["Servo.MaxWorkTime"];
    
    if (_MaxWorkTime <= 0) {
      GLog.Info(F("FMotorDriver: Wrong or invalid value for Servo.MaxWorkTime (%f)"),
        _MaxWorkTime);
        
      _MaxWorkTime = 30.f;
      GLog.Info(F("FMotorDriver: Setting Servo.MaxWorkTime to %f"), _MaxWorkTime);
    }
    
    pinMode(EPin::D7, INPUT);
  }
  virtual void Update() override final {
    const float Delta = GetConfig().UpdateInterval / _MaxWorkTime;
    switch (_State) {
    case Stoped:
      // nothing to do;
      break;
      
    case Opening:
      _Progress = min(_Progress + Delta, 1.f);
      if (_Progress == 1.f) {
        //GLog.Info(F("FMotorDriver: Doors fully open"));
        ExecuteCommand(ECommand::Stop);
      }
      break;

    case Closing:
      _Progress = max(_Progress - Delta, 0.f);
      if (_Progress == 0.f) {
        //GLog.Info(F("FMotorDriver: Doors fully closed"));
        ExecuteCommand(ECommand::Stop);
      }
      break;
    }
  }

  enum EState { Opening, Stoped, Closing };
  EState _State = Stoped;
  EState _PreviousState = Closing;
  float _Progress = 0.f;
  float _MaxWorkTime = 30.f;
};
