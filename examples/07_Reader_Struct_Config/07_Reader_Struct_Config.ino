#include "ReflectJSON_reader.h"
#include <optional>

// REFLECT() allows the Reader to map a JSON object directly into a C++ struct.
struct DeviceData {
  // These initial values are also the values kept when a key is missing
  // and explicitNulls=false is used.
  int32_t id = 99;
  float temperature = 20.0f;
  float batteryVoltage = 0.0f;
  bool online = false;
  int16_t samples[3] = {0, 0, 0};
  std::optional<int32_t> errorCode = std::nullopt;
  const char* note = nullptr;

  REFLECT(
    MEMBER(id),
    MEMBER(temperature, fn(temperature, 1)),

    // The JSON contains an integer in millivolts. fx(..., 3) converts it back to volts.
    MEMBER_AS("battery_mV", fx(batteryVoltage, 3)),

    MEMBER(online),
    MEMBER(samples),
    MEMBER(errorCode),
    MEMBER(note)
  )
};

void printDevice(const DeviceData &d) {
  Serial.print("id          = "); Serial.println(d.id);
  Serial.print("temperature = "); Serial.println(d.temperature);
  Serial.print("battery     = "); Serial.println(d.batteryVoltage, 3);
  Serial.print("online      = "); Serial.println(d.online);
  Serial.print("samples     = ");
  Serial.print(d.samples[0]); Serial.print(", ");
  Serial.print(d.samples[1]); Serial.print(", ");
  Serial.println(d.samples[2]);
  Serial.print("errorCode   = ");
  if(d.errorCode.has_value()) Serial.println(*d.errorCode); else Serial.println("null");
  Serial.print("note        = "); Serial.println(d.note ? d.note : "null");
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  // "note" and "errorCode" are missing and "futureField" is unknown.
  // The default JsonConfig accepts both situations:
  //   ignoreUnknownKeys = true
  //   explicitNulls     = false
  const char* json =
    "{\"device\":{"
      "\"id\":7,"
      "\"temperature\":23.4,"
      "\"battery_mV\":41287,"
      "\"online\":true,"
      "\"samples\":[120,121,119],"
      "\"futureField\":123"
    "}}";

  JSONreader reader(64);
  DeviceData data;

  // Reading a complete object into a reflected struct requires only one readExact() call.
  if(!reader.parse(json) || !reader.readExact(data, "device")) {
    Serial.print("Error: ");
    Serial.println(reader.getErrorMsg());
    return;
  }

  Serial.println("Default JsonConfig:");
  printDevice(data);

  // strict() changes the Reader to:
  //   ignoreUnknownKeys = false
  //   coerceInputValues = false
  //   explicitNulls     = true
  // The same JSON therefore fails because required struct members are missing.
  reader.setConfig(JsonConfig::strict());
  DeviceData strictData;

  Serial.println();
  Serial.println("Strict JsonConfig:");
  if(!reader.readExact(strictData, "device")) {
    Serial.print("Expected error: ");
    Serial.println(reader.getErrorMsg());
  } else {
    printDevice(strictData);
  }
}

void loop() {}

/*
Serial output:
Default JsonConfig:
id          = 7
temperature = 23.4
battery     = 41.287
online      = 1
samples     = 120, 121, 119
errorCode   = null
note        = null

Strict JsonConfig:
Expected error: int32_t 'errorCode' missing (in object 'device')
*/
