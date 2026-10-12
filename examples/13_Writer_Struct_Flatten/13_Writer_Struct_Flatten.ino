#include "ReflectJSON_writer.h"

struct Status {
  float speed = 18.64f;
  uint8_t assist = 3;
  bool light = true;

  REFLECT(
    MEMBER(speed, fn(speed, 1)),
    MEMBER(assist),
    MEMBER(light)
  )
};

void printDocument(const char* title, JSONwriter &writer) {
  Serial.println(title);
  if(writer.hasError()) {
    Serial.print("Writer error: ");
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

  Status status;
  JSONwriter writer(512, 6);

  // 1) A reflected struct WITH a key becomes a nested JSON object.
  writer.write(status, "status").finish();
  printDocument("1. Struct with a key:", writer);

  writer.resetJSON();

  // 2) A reflected struct WITHOUT a key is flattened into the current object.
  // Its reflected members become normal members of the root object.
  writer.write(status).finish();
  printDocument("2. Struct without a key (flattened):", writer);

  writer.resetJSON();

  // 3) Flattening can be mixed with members written before and after the struct.
  // The normal duplicate-key and key-limit checks still apply to all flattened members.
  writer.write(42, "id");
  writer.write(status);
  writer.write(cstr("ok"), "state");
  writer.finish();
  printDocument("3. Flattened into an existing object:", writer);

  writer.resetJSON();

  // 4) Inside an array, write(struct) has the normal array-element meaning.
  // The struct is NOT flattened; each struct becomes its own JSON object element.
  writer.startArray("history");
  writer.write(status);
  status.assist = 4;
  status.speed = 21.23f;
  writer.write(status);
  writer.endArray();
  writer.finish();
  printDocument("4. Struct without a key inside an array:", writer);

  writer.resetJSON();

  // 5) Flattened members share the current object's key namespace.
  // This intentionally creates a duplicate key and demonstrates that the normal
  // duplicate-key protection still works when a struct is flattened.
  writer.write(99.0f, "speed");
  writer.write(status);
  writer.finish();
  printDocument("5. Duplicate key while flattening:", writer);
}

void loop() {}

/*
Typical Serial output:

1. Struct with a key:
{"status":{"speed":18.6,"assist":3,"light":true}}

2. Struct without a key (flattened):
{"speed":18.6,"assist":3,"light":true}

3. Flattened into an existing object:
{"id":42,"speed":18.6,"assist":3,"light":true,"state":"ok"}

4. Struct without a key inside an array:
{"history":[{"speed":18.6,"assist":3,"light":true},{"speed":21.2,"assist":4,"light":true}]}

5. Duplicate key while flattening:
Writer error: duplicate key ('speed')
*/
