/*
  ReflectJSON + ReflectINI example

  IMPORTANT:
  This example requires BOTH libraries to be installed before compiling:

    - ReflectJSON
    - ReflectINI

  The same REFLECT(...) struct is written and read as JSON and INI.
*/

#include "ReflectJSON.h"
#include "ReflectINI.h"
#include <optional>

// ReflectJSON and ReflectINI use the same ReflectCore.h.
// The same REFLECT() definition can therefore be used by both formats.
struct BikeStatus {
  uint32_t uptime = 0;
  float speed = 0;
  float voltage = 0;
  uint8_t assist = 0;
  bool light = false;
  std::optional<int32_t> error = std::nullopt;

  REFLECT(
    MEMBER(uptime),
    MEMBER(speed, fn(speed, 1)),
    MEMBER_AS("voltage_mV", fx(voltage, 3)),
    MEMBER(assist),
    MEMBER(light),
    MEMBER(error)
  )
};

void printStatus(const char* title, const BikeStatus &s) {
  Serial.println(title);
  Serial.print("uptime  = "); Serial.println(s.uptime);
  Serial.print("speed   = "); Serial.println(s.speed, 1);
  Serial.print("voltage = "); Serial.println(s.voltage, 3);
  Serial.print("assist  = "); Serial.println((int)s.assist);
  Serial.print("light   = "); Serial.println(s.light);
  Serial.print("error   = "); Serial.println(s.error.has_value() ? "value" : "null");
  Serial.println();
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  BikeStatus tx;
  tx.uptime = 12345;
  tx.speed = 18.64f;
  tx.voltage = 41.287f;
  tx.assist = 3;
  tx.light = true;

  // ----- JSON -----
  JSONwriter jsonWriter(512, 6);
  jsonWriter.write(tx, "status").finish();

  if(jsonWriter.hasError()) {
    Serial.print("JSON writer error: ");
    Serial.println(jsonWriter.getErrorMsg());
    return;
  }

  Serial.println("JSON:");
  Serial.println(jsonWriter.getJSON());
  Serial.println();

  JSONreader jsonReader(64);
  BikeStatus fromJson;
  if(!jsonReader.parse(jsonWriter.getJSON()) || !jsonReader.readExact(fromJson, "status")) {
    Serial.print("JSON reader error: ");
    Serial.println(jsonReader.getErrorMsg());
    return;
  }

  // ----- INI -----
  INIwriter iniWriter(512);
  iniWriter.write(tx, "status");

  if(iniWriter.hasError()) {
    Serial.print("INI writer error: ");
    Serial.println(iniWriter.getErrorMsg());
    return;
  }

  Serial.println("INI:");
  Serial.println(iniWriter.getINI());

  INIreader iniReader(32, 8);
  BikeStatus fromIni;
  if(!iniReader.parse(iniWriter.getINI()) || !iniReader.read(fromIni, "status")) {
    Serial.print("INI reader error: ");
    Serial.println(iniReader.getErrorMsg());
    return;
  }

  printStatus("Decoded from JSON:", fromJson);
  printStatus("Decoded from INI:", fromIni);
}

void loop() {}

/*
JSON:
{"status":{"uptime":12345,"speed":18.6,"voltage_mV":41287,"assist":3,"light":true}}

INI:
[status]
uptime = 12345
speed = 18.6
voltage_mV = 41287
assist = 3
light = true

Both decoders produce the same C++ values.
*/
