#pragma once
#include <type_traits>

struct FSerialLogConsumer {
  void Consume(const char* Msg) {
    Serial.println(Msg);
  }
};

template<typename... TConsumers>
class FLoggerImpl {
public:
  template<typename... Ts>
  void Log(const char* Fmt, Ts&&... ts) {
    char Msg[1024];
    snprintf(Msg, sizeof(Msg), Fmt, std::forward<Ts>(ts)...);
    Consume<>(Msg);
  }

protected:
  template<int I = 0>
  inline
  typename std::enable_if<I < sizeof...(TConsumers)>::type
  Consume(const char* Msg) {
      std::get<I>(Consumers).Consume(Msg);
      Consume<I + 1>(Msg);
  }
  template<int I>
  inline
  typename std::enable_if<I == sizeof...(TConsumers)>::type
  Consume(const char* Msg) {  }

  std::tuple<TConsumers...> Consumers;  
};


class FLogger {
public:
  template<typename... Ts>
  inline void Info(const char* Fmt, Ts&&... ts) {
    Impl.Log(Fmt, std::forward<Ts>(ts)...);
  }
protected:
  FLoggerImpl<FSerialLogConsumer> Impl;
};

extern FLogger GLog;
