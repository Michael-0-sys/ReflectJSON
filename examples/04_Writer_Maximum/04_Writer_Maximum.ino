#include "ReflectJSON_writer.h"
#include <optional>

// This example combines most Writer features in one sketch.

enum class DeviceState : uint8_t {
  Off = 0,
  Ready = 1,
  Error = 2
};

struct Calibration {
  float offset = -0.1254f;
  float scale = 1.00234f;

  // fn() controls the number of decimal places written to JSON.
  REFLECT(
    MEMBER(offset, fn(offset, 3)),
    MEMBER(scale, fn(scale, 4))
  )
};

struct Sensor {
  uint8_t id = 0;
  float value = 0.0f;

  REFLECT(
    MEMBER(id),
    MEMBER(value, fn(value, 2))
  )
};

struct Telemetry {
  uint32_t uptime = 123456;
  DeviceState state = DeviceState::Ready;
  char channel = 'A';
  float temperature = 22.876f;
  float voltage = 41.275f;
  double distance = 1234.56789;
  bool charging = false;
  const char* text = "quotes: \"hello\", slash: \\";
  const char* nullableText = nullptr;
  std::optional<int32_t> errorCode = std::nullopt;
  std::optional<int16_t> optionalValues[3] = {12, std::nullopt, 14};
  int16_t samples[4] = {101, 104, 99, 102};
  Sensor sensors[2] = {{1, 12.345f}, {2, 67.891f}};
  Calibration calibration;

  REFLECT(
    MEMBER(uptime),
    MEMBER(state),
    MEMBER(channel),
    MEMBER(temperature, fn(temperature, 2)),

    // voltage is represented as an integer number of millivolts in JSON.
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

void printWriter(JSONwriter &writer) {
  if(writer.hasError()) {
    Serial.print("Writer error code: ");
    Serial.println((int)writer.getErrorCode());
    Serial.print("Writer error: ");
    Serial.println(writer.getErrorMsg());
    return;
  }

  // The compact JSON is the normal transmission/storage form.
  Serial.println("Compact JSON:");
  Serial.println(writer.getJSON());
  Serial.print("Compact bytes: ");
  Serial.println(writer.getJSONSize());

  // getJSONSize(true) only calculates the required pretty-print size.
  // It does not allocate the separate pretty-print buffer.
  Serial.print("Pretty bytes: ");
  Serial.println(writer.getJSONSize(true));

  // getJSON(true) creates the pretty-print buffer lazily and returns formatted JSON.
  // The compact JSON returned by getJSON() remains unchanged.
  Serial.println("Pretty JSON:");
  Serial.println(writer.getJSON(true));
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  Telemetry data;

  // Large output buffer and enough nesting depth for this example.
  JSONwriter writer(2048, 8);

  // A complete reflected struct can be written with a single call.
  writer.write(data, "telemetry");

  // Manual objects and arrays can be mixed with reflected structs.
  writer.startObject("meta");
  writer.write(cstr("1.0"), "protocol");
  writer.write(20261012u, "build");

  // Arduino String is supported directly when compiling for Arduino.
  String boardName = "Pico 2 W";
  writer.write(boardName, "board");
  writer.endObject();

  writer.startArray("messages");
  writer.write(cstr("boot"));
  writer.write(cstr("connected"));

  // Null values inside arrays are always preserved, even when explicitNulls=false.
  writer.write(nullptr);
  writer.endArray();

  writer.finish();
  printWriter(writer);

  // The same writer can be reused without reallocating its internal buffers.
  Serial.println();
  Serial.println("Reused writer:");

  writer.resetJSON();
  writer.write(999, "counter");
  writer.write(cstr("second document"), "text");
  writer.finish();
  printWriter(writer);
}

void loop() {}

/*
The sketch prints both forms of each document:

Compact JSON:
{"telemetry":{...},"meta":{...},"messages":[...]}

Pretty JSON:
{
  "telemetry": {
    ...
  },
  "meta": {
    ...
  },
  "messages": [
    ...
  ]
}

getJSON() always keeps returning the compact form, even after getJSON(true) was used.
*/
