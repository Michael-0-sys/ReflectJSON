#include "ReflectJSON_writer.h"
#include <cmath>

// This example intentionally creates Writer errors.
// It demonstrates the numeric status code, the human-readable message and the error path.
// Detailed error information can be disabled when firmware only needs the status code.

void printWriterError(JSONwriter &writer) {
  Serial.print("Error code : ");
  Serial.println((int)writer.getErrorCode());
  Serial.print("Error text : ");
  Serial.println(writer.getErrorMsg() ? writer.getErrorMsg() : "<none>");
  Serial.print("Error path : ");
  Serial.println(writer.getErrorPath() ? writer.getErrorPath() : "<none>");
  Serial.println();
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  JSONwriter writer(256);

  Serial.println("=== 1. Duplicate key ===");

  // A JSON object must not contain the same key twice.
  writer.write(10, "speed");
  writer.write(20, "speed");
  printWriterError(writer);

  // resetJSON() clears the previous error and starts a new root object.
  writer.resetJSON();

  Serial.println("=== 2. Invalid floating-point number ===");

  // JSON has no representation for NaN or Infinity.
  const float invalidNumber = NAN;
  writer.write(invalidNumber, "temperature");
  printWriterError(writer);

  writer.resetJSON();

  Serial.println("=== 3. Output buffer too small ===");

  // The Writer constructor enforces a minimum buffer size of 100 bytes.
  // A 90-character value plus JSON syntax is large enough to exceed that buffer.
  JSONwriter smallWriter(100);
  char longText[91];
  for(size_t i = 0; i < sizeof(longText) - 1; i++) longText[i] = 'A';
  longText[sizeof(longText) - 1] = '\0';

  smallWriter.write(cstr(longText), "text");
  printWriterError(smallWriter);

  Serial.println("=== 4. finish() while an array is still open ===");

  // Every manually opened array/object must be closed before finish() is called.
  writer.startArray("values");
  writer.write(1);
  writer.write(2);
  writer.finish();
  printWriterError(writer);

  writer.resetJSON();

  Serial.println("=== 5. Key used inside an array ===");

  // Array elements do not have keys. Passing a key in array context is an API error.
  // The path therefore points to the array element where the invalid write was attempted.
  writer.startArray("values");
  writer.write(1, "notAllowed");
  printWriterError(writer);

  writer.resetJSON();

  Serial.println("=== 6. Primitive value without a key in an object ===");

  // Reflected structs may be written without a key and are then flattened into the current object.
  // Primitive values still require a key when the current context is an object.
  writer.write(123);
  printWriterError(writer);

  writer.resetJSON();

  Serial.println("=== 7. Nested error path ===");

  // getErrorPath() becomes especially useful for nested JSON structures.
  writer.startObject("status");
  writer.startObject("motor");
  writer.write(invalidNumber, "speed");
  printWriterError(writer);

  writer.resetJSON();

  Serial.println("=== 8. Disable detailed error information ===");

  // Error details can be disabled when firmware only needs the numeric status code.
  // Error detection still works normally, but getErrorMsg() and getErrorPath() return nullptr.
  writer.setCreateErrorInfo(false);
  writer.write(10, "speed");
  writer.write(20, "speed");
  printWriterError(writer);

  // Detailed information can be enabled again at runtime.
  writer.resetJSON();
  writer.setCreateErrorInfo(true);

  Serial.println("=== 9. Disable error information in the constructor ===");

  // The Writer has a convenience overload for this common case:
  // JSONwriter(bufferSize, createErrorInfo)
  JSONwriter codeOnlyWriter(256, false);
  codeOnlyWriter.write(10, "speed");
  codeOnlyWriter.write(20, "speed");
  printWriterError(codeOnlyWriter);
}

void loop() {}

/*
Typical Serial output:

=== 1. Duplicate key ===
Error code : 11
Error text : duplicate key ('speed')
Error path : root.speed

=== 2. Invalid floating-point number ===
Error code : 3
Error text : invalid number for key 'temperature'
Error path : root.temperature

=== 3. Output buffer too small ===
Error code : 5
Error text : buffer capacity exceeded (limit=100)
Error path : root.text

=== 4. finish() while an array is still open ===
Error code : 7
Error text : array still open at finish
Error path : root.values

=== 5. Key used inside an array ===
Error code : 12
Error text : keys not allowed in arrays (key='notAllowed')
Error path : root.values[0]

=== 6. Primitive value without a key in an object ===
Error code : 12
Error text : value without key not allowed in object
Error path : root

=== 7. Nested error path ===
Error code : 3
Error text : invalid number for key 'speed'
Error path : root.status.motor.speed

=== 8. Disable detailed error information ===
Error code : 11
Error text : <none>
Error path : <none>

=== 9. Disable error information in the constructor ===
Error code : 11
Error text : <none>
Error path : <none>

The numeric status code is intended for program logic.
getErrorMsg() and getErrorPath() are intended for logs and debugging.
If detailed diagnostics are not needed, disable them with setCreateErrorInfo(false)
or with the createErrorInfo constructor parameter.
*/
