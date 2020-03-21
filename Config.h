#pragma once

#include <map>
#include "Logger.h"

struct FVariant {
  enum { ENumber, EString } Type;
  union { float F32; byte Str[sizeof(String)]; } Data;

  FVariant(const FVariant& Rhs) {
    this->~FVariant();
    if (Rhs.Type == ENumber) new (this) FVariant(Rhs.AsNumber());
    if (Rhs.Type == EString) new (this) FVariant(Rhs.AsString());
  }
  FVariant(float Number) : Type(ENumber) {
    Data.F32 = Number;
  }
  FVariant(String&& Str) : Type(EString) {
    new (Data.Str) String(std::move(Str));
  }
  FVariant(const String& Str) : Type(EString) {
    new (Data.Str) String(Str);
  }
  ~FVariant() {
    if (Type == EString) ((String*)Data.Str)->~String();
  }

  String AsString() const {
    if (Type == ENumber) return String(Data.F32, 5);
    return *((String*)Data.Str);
  }
  float AsNumber() const {
    if (Type == EString) return 0;
    return Data.F32;
  }

  operator String() const {
    return AsString();
  }

  operator float() const {
    return AsNumber();
  }
  
  static const FVariant Zero;
};

const FVariant FVariant::Zero{0.0};

struct FConfig {
  const FVariant& operator[](const String& Key) const {
    auto it = Data.find(Key);
    if (it != Data.end()) return it->second;
    return FVariant::Zero;
  }
  void InitFromFile(File& InFile) {
    GLog.Info("Loading config from file %s", InFile.name());
    start: for (;;) {
      if (InFile.peek() == '#') do { 
        auto c = InFile.read();
        if (c == '\n') break;
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
      if (Value.length() > 0 && Value[0] >= '0' && Value[0] <= '9') {
        Data.insert({Key, Value.toFloat()});
      }
      else {
        Data.insert({Key, Value});
      }
    }
    finish: GLog.Info("Finished loading config: %d entries", Data.size());
  }
//protected:
  std::map<String, FVariant> Data;
};
