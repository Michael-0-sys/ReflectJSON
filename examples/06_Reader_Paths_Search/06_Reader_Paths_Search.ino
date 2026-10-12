#include "ReflectJSON_reader.h"

void printError(JSONreader &reader) {
  Serial.print("Error code: ");
  Serial.println((int)reader.getErrorCode());
  Serial.print("Message: ");
  Serial.println(reader.getErrorMsg());
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  // The same key name may exist in different objects.
  // MatchByType can skip a value with the wrong type and continue searching.
  const char* json =
    "{"
      "\"legacy\":{\"power\":\"unknown\"},"
      "\"bike\":{"
        "\"telemetry\":{\"speed\":18.6,\"power\":650},"
        "\"settings\":{\"speedLimit\":25}"
      "}"
    "}";

  JSONreader reader(64);
  if(!reader.parse(json)) {
    printError(reader);
    return;
  }

  float speed = 0;
  int32_t limit = 0;
  int32_t power = 0;

  // Exact nested path: root -> bike -> telemetry -> speed.
  // Path arguments are written from the outer object to the inner object.
  if(!reader.readExact(speed, "speed", "bike", "telemetry")) {
    printError(reader);
    return;
  }

  // read(value, key, keyPath) searches for the named object and then reads inside it.
  if(!reader.read(limit, "speedLimit", "settings")) {
    printError(reader);
    return;
  }

  // Global MatchByType search:
  // legacy.power is a string, so it is skipped.
  // bike.telemetry.power is an integer, so it is accepted.
  if(!reader.read(power, "power", SearchMode::MatchByType)) {
    printError(reader);
    return;
  }

  Serial.print("speed      = "); Serial.println(speed);
  Serial.print("speedLimit = "); Serial.println(limit);
  Serial.print("power      = "); Serial.println(power);
}

void loop() {}

/*
Serial output:
speed      = 18.6
speedLimit = 25
power      = 650
*/
