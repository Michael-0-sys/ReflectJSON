// ReaderBasic - parse a JSON document and read it into structs.

#include <ReflectJSON.h>

struct Motor {
  int rpm;
  float temperature;
  REFLECT(MEMBER(rpm), MEMBER(temperature))
};

struct Status {
  Motor motor;
  int values[3];
  const char *name;
  REFLECT(MEMBER(motor), MEMBER(values), MEMBER(name))
};

JSONreader reader(100);   // room for 100 tokens (objects, arrays, keys and values each count as one)

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  // The buffer is writable, so the reader may hand out strings without copying.
  char json[] = "{\"status\":{\"motor\":{\"rpm\":1200,\"temperature\":41.5},\"values\":[1,2,3],\"name\":\"pump\"}}";

  if (!reader.parse(json, true)) {
    Serial.println(reader.getErrorMsg());
    return;
  }

  Status status{};
  if (!reader.read(status, "status")) {
    Serial.println(reader.getErrorMsg());
    Serial.println(reader.getErrorPath());   // e.g. root.status.motor.rpm
    return;
  }

  Serial.println(status.name);
  Serial.println(status.motor.rpm);
  Serial.println(status.values[2]);

  // A single value can be read by key from anywhere in the document ...
  int rpm = 0;
  if (reader.read(rpm, "rpm")) Serial.println(rpm);
  // ... or exactly along a path
  float temp = 0;
  if (reader.readExact(temp, "temperature", "status", "motor")) Serial.println(temp);
}

void loop() {}
