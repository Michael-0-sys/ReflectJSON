#include "ReflectJSON_reader.h"

// This example intentionally creates Reader errors.
// It demonstrates the numeric status code, the human-readable message and the error path.
// Parse errors have a character position in the message but no semantic JSON path.
// Read/mapping errors can provide a path such as root.status.motor.speed.

struct Settings {
  int32_t speed = 25;

  REFLECT(
    MEMBER(speed)
  )
};

struct MotorStatus {
  int32_t speed = 0;

  REFLECT(
    MEMBER(speed)
  )
};

struct MachineStatus {
  MotorStatus motor;

  REFLECT(
    MEMBER(motor)
  )
};

void printReaderError(JSONreader &reader) {
  Serial.print("Error code : ");
  Serial.println((int)reader.getErrorCode());
  Serial.print("Error text : ");
  Serial.println(reader.getErrorMsg() ? reader.getErrorMsg() : "<none>");
  Serial.print("Error path : ");
  Serial.println(reader.getErrorPath() ? reader.getErrorPath() : "<none>");
  Serial.println();
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  JSONreader reader(64);

  Serial.println("=== 1. Missing ':' after a key ===");

  // parse() detects JSON syntax errors before a semantic object path can be established.
  // The error message contains the character position; getErrorPath() returns nullptr.
  if(!reader.parse("{\"speed\" 25}")) printReaderError(reader);

  Serial.println("=== 2. Duplicate key ===");

  // Duplicate keys are also parse errors, so there is no read/mapping path.
  if(!reader.parse("{\"speed\":25,\"speed\":30}")) printReaderError(reader);

  Serial.println("=== 3. Type mismatch ===");

  // Parsing succeeds because the JSON itself is valid.
  // The error occurs while reading a string into an integer variable.
  if(reader.parse("{\"speed\":\"fast\"}")) {
    int32_t speed = 0;
    if(!reader.readExact(speed, "speed")) printReaderError(reader);
  }

  Serial.println("=== 4. Numeric overflow ===");

  // 500 cannot fit into an int8_t, so the Reader reports an overflow at root.value.
  if(reader.parse("{\"value\":500}")) {
    int8_t value = 0;
    if(!reader.readExact(value, "value")) printReaderError(reader);
  }

  Serial.println("=== 5. Missing struct member in strict mode ===");

  // strict() requires every reflected struct member to be present.
  reader.setConfig(JsonConfig::strict());
  if(reader.parse("{\"settings\":{}}")) {
    Settings settings;
    if(!reader.readExact(settings, "settings")) printReaderError(reader);
  }

  Serial.println("=== 6. Unknown struct member in strict mode ===");

  // strict() also rejects JSON keys which are not reflected by the target struct.
  if(reader.parse("{\"settings\":{\"speed\":25,\"unknown\":123}}")) {
    Settings settings;
    if(!reader.readExact(settings, "settings")) printReaderError(reader);
  }

  Serial.println("=== 7. Requested key not found ===");

  // Switch back to the default Kotlin-like configuration.
  reader.setConfig(JsonConfig{});
  if(reader.parse("{\"speed\":25}")) {
    int32_t power = 0;
    if(!reader.readExact(power, "power")) printReaderError(reader);
  }

  Serial.println("=== 8. Nested error path ===");

  // A reflected struct preserves the complete path to the failing nested member.
  if(reader.parse("{\"status\":{\"motor\":{\"speed\":\"fast\"}}}")) {
    MachineStatus status;
    if(!reader.readExact(status, "status")) printReaderError(reader);
  }

  Serial.println("=== 9. Disable detailed error information ===");

  // Error detection and the numeric status code remain available.
  // getErrorMsg() and getErrorPath() return nullptr when detailed information is disabled.
  reader.setCreateErrorInfo(false);
  if(reader.parse("{\"speed\":\"fast\"}")) {
    int32_t speed = 0;
    if(!reader.readExact(speed, "speed")) printReaderError(reader);
  }

  // Detailed information can be enabled again at runtime.
  reader.setCreateErrorInfo(true);

  Serial.println("=== 10. Disable error information in the constructor ===");

  // Parameters: maxTokens, createErrorInfo.
  JSONreader codeOnlyReader(64, false);
  if(codeOnlyReader.parse("{\"speed\":\"fast\"}")) {
    int32_t speed = 0;
    if(!codeOnlyReader.readExact(speed, "speed")) printReaderError(codeOnlyReader);
  }
}

void loop() {}

/*
Typical Serial output:

=== 1. Missing ':' after a key ===
Error code : 13
Error text : SyntaxError: missing ':' at pos 9
Error path : <none>

=== 2. Duplicate key ===
Error code : 31
Error text : duplicate key 'speed' in object 'root' (pos 12)
Error path : <none>

=== 3. Type mismatch ===
Error code : 21
Error text : 'speed' type mismatch: expected int32_t, got string (in object 'root')
Error path : root.speed

=== 4. Numeric overflow ===
Error code : 24
Error text : value overflow for int8_t 'value' (in object 'root')
Error path : root.value

=== 5. Missing struct member in strict mode ===
Error code : 19
Error text : int32_t 'speed' missing (in object 'settings')
Error path : root.settings.speed

=== 6. Unknown struct member in strict mode ===
Error code : 20
Error text : extra key 'unknown' (number) in object 'settings'
Error path : root.settings.unknown

=== 7. Requested key not found ===
Error code : 25
Error text : int32_t 'power' not found (in object 'root')
Error path : root.power

=== 8. Nested error path ===
Error code : 21
Error text : 'speed' type mismatch: expected int32_t, got string (in object 'motor')
Error path : root.status.motor.speed

=== 9. Disable detailed error information ===
Error code : 21
Error text : <none>
Error path : <none>

=== 10. Disable error information in the constructor ===
Error code : 21
Error text : <none>
Error path : <none>

The numeric status code is intended for program logic.
Parse errors use the position in getErrorMsg(); getErrorPath() is for read/mapping errors.
If detailed diagnostics are not needed, disable them with setCreateErrorInfo(false)
or with the createErrorInfo constructor parameter.
*/
