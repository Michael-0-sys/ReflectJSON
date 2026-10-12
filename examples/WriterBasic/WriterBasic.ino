// WriterBasic - build a JSON document from values and structs.
//
// Expected output:
//   {"id":123,"speed":18.6,"assist":3,"light":true,"state":"ok","values":[1,2,3]}

#include <ReflectJSON.h>

struct Status {
  float speed;
  int assist;
  bool light;
  REFLECT(MEMBER(speed, fn(speed, 1)), MEMBER(assist), MEMBER(light))   // fn(): 1 decimal place
};

JSONwriter writer(256);   // 256 byte output buffer, default nesting depth 5

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  Status status{18.6f, 3, true};
  int values[3] = {1, 2, 3};

  writer.write(123, "id");
  writer.write(status);               // no key: the members are written into the current object
  writer.write(cstr("ok"), "state");  // cstr(): a string literal would otherwise be written as a char array
  writer.write(values, "values");
  writer.finish();

  if (writer.hasError()) {
    Serial.println(writer.getErrorMsg());
    Serial.println(writer.getErrorPath());   // e.g. root.status.speed
    return;
  }
  Serial.println(writer.getJSON());
}

void loop() {}
