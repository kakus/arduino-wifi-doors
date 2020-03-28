#pragma once
#include <type_traits>
constexpr int MAX_LOG_FILE_SIZE = 10 * 1024;
extern NTPClient GTimeClient;

struct FSerialSink {
  void Init(int BaudRate) {
    Serial.begin(BaudRate);
  }
  void Process(const char* Msg) {
    Serial.print(Msg);
  }
};

struct FFileSink {
  File LogFile;
  void Init(const File& InFile) { 
    LogFile = InFile;
  }

  void Process(const char* Msg) {
    if (LogFile.isFile()) {
      if (LogFile.position() > MAX_LOG_FILE_SIZE) {
        LogFile.flush();
        LogFile.truncate(MAX_LOG_FILE_SIZE);
        LogFile.seek(0, SeekSet);
      }
      LogFile.print(Msg);
    }
  }
};

template<typename T>
inline T& LogConvert(T& Arg) { return Arg; }
inline const char* LogConvert(const String& Arg) { return Arg.c_str(); }

struct FMsgSinkVisitor {
  template<typename T>
  void Accept(T& Sink) {
    Sink.Process(Msg);
  }
  void PrintTimeTag() {
    Print(F("%2d:%02d:%02d:%03d "),
      GTimeClient.getHours(),
      GTimeClient.getMinutes(),
      GTimeClient.getSeconds(),
      millis() % 1000);
  }
  template<typename... Ts>
  inline void Print(const __FlashStringHelper* Fmt, const Ts&... ts) {
    int SprintfLen = snprintf_P(GetStr(), sizeof(Msg) - MsgLen - 1, (PGM_P)Fmt, LogConvert(ts)...);
    if (SprintfLen > 0) {
      MsgLen = max(0, MsgLen - 1); // we consume 0 of last string;
      MsgLen = min(MsgLen + SprintfLen + 1, int(sizeof(Msg) - 1)); // keep space for \n;
    }
  }
  inline void PrintLn() {
    if (MsgLen < sizeof(Msg) - 1) {
      *GetStr() = '\n';
      MsgLen += 1;
      *GetStr() = '\0';
    }
  }

private:
  inline char* GetStr() {
    return Msg + max(MsgLen - 1, 0);
  }

  char Msg[512];
  //  used bytes of Msg including '\0'
  int MsgLen = 0;
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
  void Log(const __FlashStringHelper* Fmt, const Ts&... ts) {
    FMsgSinkVisitor Visitor;
	Visitor.PrintTimeTag();
	Visitor.Print(Fmt, ts...);
    Visitor.PrintLn();
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
    Sink.Init(Buffer);
  }
  const TData& Buffer;
};

class FLogger {
public:
  template<typename TSink, typename TData>
  void InitSink(const TData& Buffer) {
    FIniter<TSink, TData> Visitor{Buffer};
    Impl.Visit(Visitor);
  }
  
  template<typename... Ts>
  inline void Info(const __FlashStringHelper* Fmt, const Ts&... ts) {
    Impl.Log(Fmt, ts...);
  }

protected:
  FLoggerImpl<FSerialSink, FFileSink> Impl;
};

extern FLogger GLog;
