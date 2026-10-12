#include "ReflectJSON_writer.h"

void setup() {
  // Start the serial monitor. The short delay is especially useful on RP2040/RP2350 boards,
  // because the USB serial connection may not be ready immediately after reset.
  Serial.begin(115200);
  delay(1000);

  // Create a writer with a 256 byte output buffer.
  // The root JSON object is created automatically.
  JSONwriter writer(256);

  // Add primitive values to the root object.
  // write(value, key) creates a JSON member with the given key.
  writer.write(42, "answer");
  writer.write(true, "ready");
  writer.write(23.75f, "temperature");

  // cstr() makes it explicit that a string literal is a C-style string value.
  writer.write(cstr("hello"), "text");

  // finish() closes the root object and makes getJSON() available.
  writer.finish();

  // Always check for an error before using the generated JSON.
  if(writer.hasError()) {
    Serial.print("JSON error: ");
    Serial.println(writer.getErrorMsg());
    return;
  }

  // getJSON() returns the finished zero-terminated JSON string.
  // getJSONSize() returns its length without the terminating '\0'.
  Serial.println(writer.getJSON());
  Serial.print("JSON bytes: ");
  Serial.println(writer.getJSONSize());
}

void loop() {}

/*
Serial output:
{"answer":42,"ready":true,"temperature":23.75,"text":"hello"}
JSON bytes: 61
*/
