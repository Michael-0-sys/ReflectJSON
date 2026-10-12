#include "ReflectJSON_writer.h"

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  // 512 bytes are reserved for the JSON output.
  // The second argument sets the maximum nesting depth.
  JSONwriter writer(512, 6);

  // startObject(key) opens a nested object under the given key.
  writer.startObject("device");
  writer.write(cstr("Pico 2 W"), "name");
  writer.write(3, "id");

  // startArray(key) opens an array. Inside an array write(value) needs no key.
  writer.startArray("samples");
  writer.write(101);
  writer.write(102);
  writer.write(103);
  writer.endArray();

  // Objects can be nested inside other objects.
  writer.startObject("status");
  writer.write(true, "online");
  writer.write(41.28f, "voltage");
  writer.endObject();

  // Close the "device" object.
  writer.endObject();

  // Arrays may also contain complete objects.
  writer.startArray("events");

  // No key is used here because these objects are array elements.
  writer.startObject();
  writer.write(1, "id");
  writer.write(cstr("boot"), "name");
  writer.endObject();

  writer.startObject();
  writer.write(2, "id");
  writer.write(cstr("ready"), "name");
  writer.endObject();

  writer.endArray();

  // Close the automatically created root object.
  writer.finish();

  if(writer.hasError()) {
    Serial.print("JSON error code: ");
    Serial.println((int)writer.getErrorCode());
    Serial.print("Message: ");
    Serial.println(writer.getErrorMsg());
    return;
  }

  Serial.println(writer.getJSON());
}

void loop() {}

/*
Serial output:
{"device":{"name":"Pico 2 W","id":3,"samples":[101,102,103],"status":{"online":true,"voltage":41.28}},"events":[{"id":1,"name":"boot"},{"id":2,"name":"ready"}]}
*/
