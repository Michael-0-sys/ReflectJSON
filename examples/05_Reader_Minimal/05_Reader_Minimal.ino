#include "ReflectJSON_reader.h"

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  // The Reader accepts a zero-terminated JSON string.
  // The root element must be a JSON object.
  const char* json = "{\"answer\":42,\"ready\":true,\"temperature\":23.75,\"text\":\"hello\"}";

  // Reserve space for up to 32 parsed tokens.
  JSONreader reader(32);

  // parse() validates and tokenizes the complete JSON document.
  if(!reader.parse(json)) {
    Serial.print("Parse error: ");
    Serial.println(reader.getErrorMsg());
    return;
  }

  // Target variables keep their original value if a read operation fails.
  int32_t answer = 0;
  bool ready = false;
  float temperature = 0;
  const char* text = nullptr;

  // readExact() reads a value with an exact key match.
  if(!reader.readExact(answer, "answer") ||
     !reader.readExact(ready, "ready") ||
     !reader.readExact(temperature, "temperature") ||
     !reader.readExact(text, "text")) {
    Serial.print("Read error: ");
    Serial.println(reader.getErrorMsg());
    return;
  }

  Serial.print("answer      = "); Serial.println(answer);
  Serial.print("ready       = "); Serial.println(ready);
  Serial.print("temperature = "); Serial.println(temperature);
  Serial.print("text        = "); Serial.println(text);
}

void loop() {}

/*
Serial output:
answer      = 42
ready       = 1
temperature = 23.75
text        = hello
*/
