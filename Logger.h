#pragma once
#include <type_traits>

struct FSerialSink {
  void Init(int BaudRate) {
    Serial.begin(BaudRate);
  }
  void Process(const char* Msg) {
    Serial.print(Msg);
  }
};

struct FFileSink {
  File file;
  void Init(const File& InFile) { 
    file = InFile;
  }
  void Process(const char* Msg) {
    if (file.isFile()) {
      if (file.position() > 1024 * 10) {
        file.flush();
        file.seek(0, SeekSet);
      }
      file.print(Msg);
    }
  }
};

struct FMsgSinkVisitor {
  template<typename T>
  void Accept(T& Sink) { Sink.Process(Msg); }
  char Msg[512];
  int MsgLen = 0;
};

template<typename T>
inline T&& LogConvert(T&& Arg) { return std::forward<T>(Arg); }
inline const char* LogConvert(const String& Arg) { Arg.c_str(); }
inline const char* LogConvert(String&& Arg) { Arg.c_str(); }

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
    int MsgLen = snprintf(Visitor.Msg, sizeof(Visitor.Msg) - 1, Fmt, LogConvert(std::forward<Ts>(ts))...);
    Visitor.Msg[MsgLen] = '\n';
    Visitor.MsgLen = MsgLen + 1;
    Visit(Visitor);
  }

protected:
  std::tuple<TSinks...> Sinks;  
};

template<typename TSink, typename TData>
struct FIniter {
  template<typename T>
  void Accept(T& Sink) { }
  void Accept(TSink& Sink) {
    Sink.Init(Data);
  }
  const TData& Data;
};

class FLogger {
public:
  template<typename TSink, typename TData>
  void InitSink(const TData& Data) {
    FIniter<TSink, TData> Visitor{Data};
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
