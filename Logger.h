#pragma once
#include <type_traits>

struct FSerialSink {
  void Process(const char* Msg) {
    Serial.println(Msg);
  }
};

struct FFileSink {
  void Init(const File& InFile) { 
    _File = InFile;
  }
  void Process(const char* Msg) {}
  File _File;
};

struct FMsgSinkVisitor {
  template<typename T>
  void Accept(T& Sink) { Sink.Process(Msg); }
  char Msg[1024];
};

template<typename... TSinks>
class FLoggerImpl {
public:
  template<int I = 0, typename TVisitor>
  inline
  typename std::enable_if<I < sizeof...(TSinks)>::type
  Visit(TVisitor& Visitor) {
      Visitor.Accept(std::get<I>(Sinks));
      Visit<I + 1>(Visitor);
  }
  template<int I>
  inline
  typename std::enable_if<I == sizeof...(TSinks)>::type
  Visit(...) {  }
  
  template<typename... Ts>
  void Log(const char* Fmt, Ts&&... ts) {
    FMsgSinkVisitor Visitor;
    snprintf(Visitor.Msg, sizeof(Visitor.Msg), Fmt, std::forward<Ts>(ts)...);
    Visit(Visitor);
  }

protected:
  std::tuple<TSinks...> Sinks;  
};

class FLogger {
public:
  template<typename TSink, typename TData>
  void InitSink(const TData& Data) {
    struct FIniter {
      void Accept(...) { }
      void Accept(TSink& Cons) {
        Cons.Init(Data);
      }
      const TData& Data;
    };
    FIniter Visitor{Data};
    Impl.Visit(Visitor);
  }
  
  template<typename... Ts>
  inline void Info(const char* Fmt, Ts&&... ts) {
    Impl.Log(Fmt, std::forward<Ts>(ts)...);
  }

protected:
  FLoggerImpl<FSerialSink, FFileSink> Impl;
};

extern FLogger GLog;
