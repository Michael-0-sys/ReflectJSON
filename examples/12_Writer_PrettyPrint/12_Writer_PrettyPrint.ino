// Define JSON_PRETTY_INDENT before including JSONwriter.h if you want a value other than 2.
// #define JSON_PRETTY_INDENT 4
#include "ReflectJSON_writer.h"

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  JSONwriter writer(512, 6);

  // Build a nested document. The Writer always creates the compact representation first.
  writer.write(7, "id");
  writer.startObject("status");
  writer.write(18.6f, "speed");
  writer.write(3, "assist");
  writer.write(true, "light");
  writer.endObject();

  writer.startArray("values");
  writer.write(10);
  writer.write(20);
  writer.startObject();
  writer.write(cstr("text with { } [ ] , : and \\\"quotes\\\""), "text");
  writer.endObject();
  writer.endArray();

  writer.finish();

  if(writer.hasError()) {
    Serial.print("Writer error: ");
    Serial.println(writer.getErrorMsg());
    return;
  }

  // getJSON() returns the compact form. This is normally best for transmission and storage.
  Serial.println("Compact JSON:");
  Serial.println(writer.getJSON());
  Serial.print("Compact bytes: ");
  Serial.println(writer.getJSONSize());

  // Asking only for the pretty size performs a count pass and does NOT allocate the pretty buffer.
  const size_t prettySize = writer.getJSONSize(true);
  Serial.print("Pretty bytes: ");
  Serial.println(prettySize);

  // getJSON(true) lazily creates a separate pretty-print buffer.
  // JSON syntax characters inside strings are kept untouched.
  Serial.println("Pretty JSON:");
  Serial.println(writer.getJSON(true));

  // Once the pretty buffer exists, getJSONSize(true) returns its cached size.
  Serial.print("Pretty bytes after getJSON(true): ");
  Serial.println(writer.getJSONSize(true));

  // Pretty printing never changes the compact representation.
  Serial.println("Compact JSON is still available:");
  Serial.println(writer.getJSON());
}

void loop() {}

/*
Typical Serial output (default JSON_PRETTY_INDENT = 2):

Compact JSON:
{"id":7,"status":{"speed":18.60,"assist":3,"light":true},"values":[10,20,{"text":"text with { } [ ] , : and \\\"quotes\\\""}]}

Pretty JSON:
{
  "id": 7,
  "status": {
    "speed": 18.60,
    "assist": 3,
    "light": true
  },
  "values": [
    10,
    20,
    {
      "text": "text with { } [ ] , : and \\\"quotes\\\""
    }
  ]
}
*/
