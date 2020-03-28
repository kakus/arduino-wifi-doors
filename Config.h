#pragma once

#include <map>
#include "Logger.h"

struct FVariant {
  enum { ENumber, EString } Type = ENumber;
  union { float F32; byte Str[sizeof(String)]; } Buffer;

  FVariant(const FVariant& Rhs) {
    this->~FVariant();
    if (Rhs.Type == ENumber) new (this) FVariant(Rhs.AsNumber());
    if (Rhs.Type == EString) new (this) FVariant(Rhs.AsString());
  }
  FVariant(FVariant&& Rhs) {
    this->~FVariant();
    
    if (Rhs.Type == ENumber) 
      new (this) FVariant(Rhs.AsNumber());
    if (Rhs.Type == EString) 
      new (this) FVariant(std::move(Rhs.UnsafeGetString()));

    Rhs.Type == ENumber;
  }
  
  FVariant(float Number) : Type(ENumber) {
    Buffer.F32 = Number;
  }
  FVariant(String&& Str) : Type(EString) {
    new (Buffer.Str) String(std::move(Str));
  }
  FVariant(const String& Str) : Type(EString) {
    new (Buffer.Str) String(Str);
  }
  ~FVariant() {
    if (Type == EString) ((String*)Buffer.Str)->~String();
  }

  FVariant& operator=(const FVariant& Rhs) = delete;
  FVariant& operator=(FVariant&& Rhs) = delete;

  String AsString() const {
    if (Type == ENumber) return String(Buffer.F32, 5);
    return UnsafeGetString();
  }
  float AsNumber() const {
    if (Type == EString) return 0;
    return Buffer.F32;
  }

  operator String() const {
    return AsString();
  }

  operator float() const {
    return AsNumber();
  }
  
  static const FVariant Zero;
private:
  String& UnsafeGetString() const {
    return *((String*)Buffer.Str);
  }
};

const FVariant FVariant::Zero{0.0};

struct FConfig {
  const FVariant& operator[](const String& Key) const {
    auto it = Buffer.find(Key);
    if (it != Buffer.end()) return it->second;
    return FVariant::Zero;
  }
  void InitFromFile(File& InFile) {
    GLog.Info(F("Loading config from file %s"), InFile.name());
    
    start: for (;;) {
      if (InFile.peek() == '#') do { 
        auto c = InFile.read();
        if (c == '\n') goto start;
        if (c == -1) goto finish;
      } while(1);
        
      String Key;
      do {
        auto c = InFile.read();
        if (c == -1) goto finish;
        if (c == '=') break;
        if (c == '\n') goto start;
        Key += (char)c;
      } while(1);
      
      String Value;
      do {
        auto c = InFile.read();
        if (c == -1) break;
        if (c == '\n') break;
        Value += (char)c;
      } while(1);

      Key.trim();
      Value.trim();
      GLog.Info(F("> %s = %s"), Key, Value);

      if (Value.length() > 0 && Value[0] >= '0' && Value[0] <= '9') {
        Buffer.insert({Key, Value.toFloat()});
      }
      else {
        Buffer.insert({Key, Value});
      }
    }
    finish: GLog.Info(F("Finished loading config: %d entries"), Buffer.size());
  }
  
protected:
  std::map<String, FVariant> Buffer;
};

extern FConfig GConfig;
