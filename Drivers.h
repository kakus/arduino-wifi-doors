#pragma once
#include <memory>
#include <deque>
#include "Ticker.h"
#include "Logger.h"
#include "Config.h"

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
