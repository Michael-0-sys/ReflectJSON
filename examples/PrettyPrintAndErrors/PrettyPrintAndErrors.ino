// PrettyPrintAndErrors - pretty printing and error locations (getErrorPath).

#include <ReflectJSON.h>

struct Limits {
  int max;
  REFLECT(MEMBER(max))
};
struct Device {
  Limits limits;
  REFLECT(MEMBER(limits))
};
JSONwriter writer(256);
JSONreader reader(100);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  // ---- pretty printing ----
  writer.startObject("status").write(18, "speed").write(true, "light").endObject();
  writer.startArray("values").write(1).write(2).write(3).endArray();
  writer.write(cstr("hello, { not: [structure] }"), "text");   // brackets inside strings are left alone
  writer.finish();

  Serial.println(writer.getJSON());        // compact
  Serial.println(writer.getJSONSize());    // length of the compact JSON
  Serial.println(writer.getJSON(true));    // pretty (created on demand in a separate buffer)
  Serial.println(writer.getJSONSize(true));   // length of the pretty JSON (does not allocate)

  // ---- error path of the reader ----
  char json[] = "{\"devices\":[{\"limits\":{\"max\":1}},{\"limits\":{\"max\":2}},{\"limits\":{\"max\":\"oops\"}}]}";
  Device devices[3];
  if (reader.parse(json, true) && !reader.read(devices, "devices")) {
    Serial.println(reader.getErrorMsg());
    Serial.println(reader.getErrorPath());   // root.devices[2].limits.max
  }

  // ---- error path of the writer ----
  JSONwriter small(100);
  small.startObject("status");
  char longText[260];
  memset(longText, 'a', 255);
  longText[255] = '\0';
  small.write(cstr(longText), "text");
  Serial.println(small.getErrorMsg());       // string len exceeded (limit=254)
  Serial.println(small.getErrorPath());      // root.status.text
}

void loop() {}
