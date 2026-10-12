#include "ReflectJSON_writer.h"
#include <optional>

// A normal C++ struct can be serialized automatically by adding REFLECT().
struct DeviceData {
  int32_t id = 7;
  float temperature = 23.456f;
  float batteryVoltage = 41.287f;
  bool online = true;
  int16_t samples[3] = {120, 121, 119};
  std::optional<int32_t> errorCode = std::nullopt;
  const char* note = nullptr;

  REFLECT(
    MEMBER(id),

    // fn(value, precision) writes a floating-point JSON number with a chosen precision.
    MEMBER(temperature, fn(temperature, 1)),

    // MEMBER_AS changes the JSON key.
    // fx(value, precision) stores a float as an integer scaled by 10^precision.
    MEMBER_AS("battery_mV", fx(batteryVoltage, 3)),

    MEMBER(online),
    MEMBER(samples),
    MEMBER(errorCode),
    MEMBER(note)
  )
};

void printResult(const char* title, JSONwriter &writer) {
  Serial.println(title);
  if(writer.hasError()) {
    Serial.print("Error: ");
    Serial.println(writer.getErrorMsg());
  } else {
    Serial.println(writer.getJSON());
  }
  Serial.println();
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  DeviceData data;
  JSONwriter writer(768, 6);

  // Default JsonConfig uses explicitNulls=false.
  // std::nullopt and nullptr object members are therefore omitted.
  writer.write(data, "device").finish();
  printResult("Default JsonConfig:", writer);

  // resetJSON() reuses the same writer and its already allocated buffers.
  writer.resetJSON();

  // strict() enables explicitNulls and also selects the strict reader settings.
  // For the writer this means null object members are written explicitly.
  writer.setConfig(JsonConfig::strict());
  writer.write(data, "device").finish();
  printResult("Strict JsonConfig:", writer);
}

void loop() {}

/*
Serial output:
Default JsonConfig:
{"device":{"id":7,"temperature":23.5,"battery_mV":41287,"online":true,"samples":[120,121,119]}}

Strict JsonConfig:
{"device":{"id":7,"temperature":23.5,"battery_mV":41287,"online":true,"samples":[120,121,119],"errorCode":null,"note":null}}
*/
