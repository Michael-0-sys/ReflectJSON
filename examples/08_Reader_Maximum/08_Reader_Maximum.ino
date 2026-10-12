#include "ReflectJSON_reader.h"
#include <optional>

// This example combines most Reader features in one sketch.

enum class DeviceState : uint8_t {
  Off = 0,
  Ready = 1,
  Error = 2
};

struct Calibration {
  float offset = 0;
  float scale = 1;

  REFLECT(
    MEMBER(offset, fn(offset, 3)),
    MEMBER(scale, fn(scale, 4))
  )
};

struct Sensor {
  uint8_t id = 0;
  float value = 0;

  REFLECT(
    MEMBER(id),
    MEMBER(value, fn(value, 2))
  )
};

struct Telemetry {
  uint32_t uptime = 0;
  DeviceState state = DeviceState::Off;
  char channel = '\0';
  float temperature = 0;
  float voltage = 0;
  double distance = 0;
  bool charging = false;
  const char* text = nullptr;
  const char* nullableText = nullptr;
  std::optional<int32_t> errorCode = 123; // overwritten by JSON null
  std::optional<int16_t> optionalValues[3] = {1, 2, 3};
  int16_t samples[4] = {0, 0, 0, 0};
  Sensor sensors[2];
  Calibration calibration;

  REFLECT(
    MEMBER(uptime),
    MEMBER(state),
    MEMBER(channel),
    MEMBER(temperature, fn(temperature, 2)),

    // Convert the integer JSON value in millivolts back to a voltage float.
    MEMBER_AS("voltage_mV", fx(voltage, 3)),

    MEMBER(distance, fn(distance, 3)),
    MEMBER(charging),
    MEMBER(text),
    MEMBER(nullableText),
    MEMBER(errorCode),
    MEMBER(optionalValues),
    MEMBER(samples),
    MEMBER(sensors),
    MEMBER(calibration)
  )
};

void printReaderError(JSONreader &reader) {
  Serial.print("Reader error code: ");
  Serial.println((int)reader.getErrorCode());
  Serial.print("Reader error: ");
  Serial.println(reader.getErrorMsg());
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  // This buffer is writable. With parse(json, true), unescaped const char* values
  // may be returned as zero-copy pointers directly into this buffer.
  char json[] =
    "{"
      "\"telemetry\":{"
        "\"uptime\":123456,"
        "\"state\":1,"
        "\"channel\":\"A\","
        "\"temperature\":22.88,"
        "\"voltage_mV\":41275,"
        "\"distance\":1234.568,"
        "\"charging\":false,"
        "\"text\":\"hello\","
        "\"nullableText\":null,"
        "\"errorCode\":null,"
        "\"optionalValues\":[12,null,14],"
        "\"samples\":[101,104,99,102],"
        "\"sensors\":[{\"id\":1,\"value\":12.35},{\"id\":2,\"value\":67.89}],"
        "\"calibration\":{\"offset\":-0.125,\"scale\":1.0023}"
      "},"
      "\"legacy\":{\"power\":\"unknown\"},"
      "\"live\":{\"power\":650}"
    "}";

  // Reserve enough token slots for the complete document.
  JSONreader reader(160);

  // The second argument tells the Reader that the input buffer may be modified internally.
  if(!reader.parse(json, true)) {
    printReaderError(reader);
    return;
  }

  // These values are useful when sizing maxTokens for a real protocol.
  Serial.print("Tokens: "); Serial.println(reader.getTokenCount());
  Serial.print("Keys:   "); Serial.println(reader.getKeyCount());

  // Decode the complete telemetry object into the reflected struct.
  Telemetry data;
  if(!reader.readExact(data, "telemetry")) {
    printReaderError(reader);
    return;
  }

  Serial.println();
  Serial.println("Decoded struct:");
  Serial.print("uptime       = "); Serial.println(data.uptime);
  Serial.print("state        = "); Serial.println((int)data.state);
  Serial.print("channel      = "); Serial.println(data.channel);
  Serial.print("temperature  = "); Serial.println(data.temperature, 2);
  Serial.print("voltage      = "); Serial.println(data.voltage, 3);
  Serial.print("distance     = "); Serial.println(data.distance, 3);
  Serial.print("text         = "); Serial.println(data.text ? data.text : "null");
  Serial.print("nullableText = "); Serial.println(data.nullableText ? data.nullableText : "null");
  Serial.print("errorCode    = "); Serial.println(data.errorCode.has_value() ? "value" : "null");
  Serial.print("optional[1]  = "); Serial.println(data.optionalValues[1].has_value() ? "value" : "null");
  Serial.print("sensor[1]    = "); Serial.println(data.sensors[1].value, 2);

  // MatchByType searches for the same key again if the first matching key has the wrong type.
  // legacy.power is a string; live.power is the requested integer.
  int32_t power = 0;
  if(reader.read(power, "power", SearchMode::MatchByType)) {
    Serial.print("MatchByType power = ");
    Serial.println(power);
  } else {
    printReaderError(reader);
  }

  // readExact() also supports a chain of nested object names.
  float scale = 0;
  if(reader.readExact(scale, "scale", "telemetry", "calibration")) {
    Serial.print("Exact path scale   = ");
    Serial.println(scale, 4);
  }

  // By default every read verifies that the original JSON buffer is unchanged.
  // This can be disabled for extra speed when your application guarantees that the buffer is stable.
  reader.setBufferCheck(false);
  Serial.print("Buffer check: ");
  Serial.println(reader.isBufferCheckEnabled() ? "on" : "off");
}

void loop() {}

/*
Serial output:
Tokens: 60
Keys:   25

Decoded struct:
uptime       = 123456
state        = 1
channel      = A
temperature  = 22.88
voltage      = 41.275
distance     = 1234.568
text         = hello
nullableText = null
errorCode    = null
optional[1]  = null
sensor[1]    = 67.89
MatchByType power = 650
Exact path scale   = 1.0023
Buffer check: off
*/
