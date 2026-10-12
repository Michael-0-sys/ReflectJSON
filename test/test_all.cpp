// Host tests for ReflectJSON. Build: see test/Makefile. Exits with a non-zero code on failure.
#include <ReflectJSON.h>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a); const char *_b = (b); \
  if (!_a || strcmp(_a, _b) != 0) { printf("FAIL %s:%d: got '%s', expected '%s'\n", __FILE__, __LINE__, _a ? _a : "(null)", _b); fails++; } } while (0)

struct Status {
  double speed; int assist; bool light;
  REFLECT(MEMBER(speed, fn(speed, 1)), MEMBER(assist), MEMBER(light))
};
struct Motor { int rpm; double temp; REFLECT(MEMBER(rpm), MEMBER(temp)) };
struct Pump {
  Motor motor; int values[3]; const char *name;
  REFLECT(MEMBER(motor), MEMBER(values), MEMBER(name))
};
struct Opt {
  int x = 1; std::optional<int> o; int y = 2;
  REFLECT(MEMBER(x), MEMBER(o), MEMBER(y))
};
struct Wrap { Motor motor; REFLECT(MEMBER(motor)) };
struct Limits { int max; REFLECT(MEMBER(max)) };
struct Device { Limits limits; REFLECT(MEMBER(limits)) };

static void testWriterBasics() {
  JSONwriter w(256);
  Status s{18.6, 3, true};
  int v[3] = {1, 2, 3};
  w.write(123, "id").write(s).write(cstr("ok"), "state").write(v, "values").finish();
  CHECK(!w.hasError());
  CHECK_STR(w.getJSON(), "{\"id\":123,\"speed\":18.6,\"assist\":3,\"light\":true,\"state\":\"ok\",\"values\":[1,2,3]}");
  CHECK(w.getJSONSize() == strlen(w.getJSON()));

  JSONwriter a(256);                       // struct without key inside an array stays an object
  a.startArray("a").write(s).endArray().finish();
  CHECK_STR(a.getJSON(), "{\"a\":[{\"speed\":18.6,\"assist\":3,\"light\":true}]}");

  JSONwriter d(256);                       // flattened duplicate keys are detected
  d.write(s).write(s);
  CHECK(d.hasError() && d.getErrorCode() == JsonWriterStatus::JSON_ERR_DUPLICATE_KEY);

  JSONwriter n(256);                       // empty optional struct without key cannot be flattened
  n.write(std::optional<Status>());
  CHECK(n.hasError() && n.getErrorCode() == JsonWriterStatus::JSON_ERR_KEY);
}

static void testPrettyPrint() {
  JSONwriter w(300);
  w.startObject("status").write(18, "speed").write(true, "light").endObject();
  w.startArray("values").write(1).write(2).endArray();
  w.startArray("e").endArray().startObject("eo").endObject();
  w.write(cstr("hello, { test: [1,2] } \"q\" \\ end"), "text");
  CHECK(w.getJSON() == nullptr);           // not finished yet
  CHECK(w.getJSONSize(true) == 0);
  w.finish();
  const std::string compact = w.getJSON();
  const size_t pretty = w.getJSONSize(true);
  const char *p = w.getJSON(true);
  CHECK(p != nullptr && strlen(p) == pretty);
  CHECK_STR(w.getJSON(false), compact.c_str());   // compact buffer untouched
  CHECK(strstr(p, "\"text\": \"hello, { test: [1,2] } \\\"q\\\" \\\\ end\"") != nullptr);
  CHECK(strstr(p, "\"e\": []") != nullptr && strstr(p, "\"eo\": {}") != nullptr);
  CHECK(strstr(p, "{\n  \"status\": {\n    \"speed\": 18,") == p);
  // pretty and compact describe the same document
  JSONreader r(100);
  std::string pc = p;
  CHECK(r.parse(pc.c_str(), false));
  CHECK(r.getTokenCount() > 0);
  w.resetJSON();                            // a new document invalidates the pretty buffer
  CHECK(w.getJSON(true) == nullptr);
  w.write(1, "a").finish();
  CHECK_STR(w.getJSON(true), "{\n  \"a\": 1\n}");
}

static void testReader() {
  char json[] = "{\"pump\":{\"motor\":{\"rpm\":1200,\"temp\":41.5},\"values\":[1,2,3],\"name\":\"p\\u00fcmp\"}}";
  JSONreader r(100);
  CHECK(r.parse(json, true));
  Pump p{};
  CHECK(r.read(p, "pump"));
  CHECK(p.motor.rpm == 1200 && p.motor.temp == 41.5 && p.values[2] == 3);
  CHECK_STR(p.name, "p\xc3\xbcmp");
  int rpm = 0;
  CHECK(r.read(rpm, "rpm") && rpm == 1200);
  double t = 0;
  CHECK(r.readExact(t, "temp", "pump", "motor") && t == 41.5);
  CHECK(!r.readExact(t, "temp", "pump"));   // not directly below pump
}

static void testRoundtrip() {
  Pump in{{77, 1.25}, {7, 8, 9}, "name"};
  JSONwriter w(256);
  w.write(in, "pump").finish();
  CHECK(!w.hasError());
  std::string copy = w.getJSON();
  JSONreader r(100);
  CHECK(r.parse(copy.c_str(), false));
  Pump out{};
  CHECK(r.read(out, "pump"));
  CHECK(out.motor.rpm == 77 && out.values[1] == 8 && strcmp(out.name, "name") == 0);
}

static void testConfig() {
  char j1[] = "{\"o\":{\"x\":5,\"extra\":[1,2]}}";
  JSONreader lenient(50);                   // default JsonConfig = Kotlin-like settings
  CHECK(lenient.parse(j1, true));
  Opt o;
  CHECK(lenient.read(o, "o") && o.x == 5 && !o.o && o.y == 2);

  JSONreader strict(50, JsonConfig::strict());
  CHECK(strict.parse(j1, true));
  Opt o2;
  CHECK(!strict.read(o2, "o"));

  char j2[] = "{\"o\":{\"x\":null,\"o\":7}}";   // null for a non-nullable member keeps the default
  CHECK(lenient.parse(j2, true));
  Opt o3;
  CHECK(lenient.read(o3, "o") && o3.x == 1 && o3.o == 7);

  JSONwriter w(100);                        // explicitNulls = false: nulls are omitted in objects
  w.write(o, "o").finish();
  CHECK_STR(w.getJSON(), "{\"o\":{\"x\":5,\"y\":2}}");
}

static void testParseErrors() {
  JSONreader r(50);
  char dup[] = "{\"a\":1,\"a\":2}";
  CHECK(!r.parse(dup, true));
  CHECK(r.getErrorCode() == JsonReaderStatus::JSON_ERR_DUPLICATE_KEY);
  CHECK(r.getErrorPath() == nullptr);       // parse errors have a position, not a path
  char colon[] = "{\"a\" 1}";
  CHECK(!r.parse(colon, true));
  CHECK(r.getErrorCode() == JsonReaderStatus::JSON_ERR_MISSING_COLON);
}

static void testErrorPaths() {
  JSONreader r(100);
  char json[] = "{\"devices\":[{\"limits\":{\"max\":1}},{\"limits\":{\"max\":2}},{\"limits\":{\"max\":\"oops\"}}]}";
  CHECK(r.parse(json, true));
  Device d[3];
  CHECK(!r.read(d, "devices"));
  CHECK_STR(r.getErrorPath(), "root.devices[2].limits.max");

  char deep[] = "{\"a\":{\"b\":{\"speed\":\"x\"}}}";
  CHECK(r.parse(deep, true));
  double sp;
  CHECK(!r.read(sp, "speed"));
  CHECK_STR(r.getErrorPath(), "root.a.b.speed");

  char missing[] = "{\"s\":{\"motor\":{\"rpm\":1}}}";
  JSONreader strict(50, JsonConfig::strict());
  CHECK(strict.parse(missing, true));
  Wrap wrap{};
  CHECK(!strict.read(wrap, "s"));
  CHECK_STR(strict.getErrorPath(), "root.s.motor.temp");

  JSONreader quiet(50, false);              // createErrorInfo = false
  CHECK(quiet.parse(deep, true) && !quiet.read(sp, "speed"));
  CHECK(quiet.getErrorPath() == nullptr && quiet.getErrorMsg() == nullptr);

  JSONwriter w(1024);
  w.startObject("status");
  char text[260];
  memset(text, 'a', 255);
  text[255] = '\0';
  w.write(cstr(text), "text");
  CHECK(w.getErrorCode() == JsonWriterStatus::JSON_ERR_STRING_LEN_LIMIT);
  CHECK_STR(w.getErrorPath(), "root.status.text");

  JSONwriter dup(100);
  dup.startObject("status").startObject("motor").write(1, "speed").write(2, "speed");
  CHECK_STR(dup.getErrorPath(), "root.status.motor.speed");

  JSONwriter arr(100);
  arr.startArray("values").write(1).write(2).write(3);
  arr.write(std::numeric_limits<double>::infinity());
  CHECK_STR(arr.getErrorPath(), "root.values[3]");

  JSONwriter full(100);                     // out of space while writing a value
  char big[60];
  memset(big, 'b', 50);
  big[50] = '\0';
  full.startObject("status").write(cstr(big), "text").write(cstr(big), "more");
  CHECK(full.getErrorCode() == JsonWriterStatus::JSON_ERR_NO_SPACE);
  const char *pth = full.getErrorPath();
  CHECK(pth && (strcmp(pth, "root.status.more") == 0 || strcmp(pth, "root.status.text") == 0));

  JSONwriter ok(100);
  ok.write(1, "a");
  CHECK(ok.getErrorPath() == nullptr);
}

static void testConstructors() {
  JSONwriter a(256, false);                 // bool = createErrorInfo, not maxDepth
  CHECK(a.getMaxDepth() == 5 && !a.isCreateErrorInfoEnabled());
  JSONwriter b(256, 3);
  CHECK(b.getMaxDepth() == 3 && b.isCreateErrorInfoEnabled());
  JSONwriter c(256, 4, false);
  CHECK(c.getMaxDepth() == 4 && !c.isCreateErrorInfoEnabled());
  JSONreader r(64, false);
  CHECK(!r.isCreateErrorInfoEnabled());
}

int main() {
  testWriterBasics();
  testPrettyPrint();
  testReader();
  testRoundtrip();
  testConfig();
  testParseErrors();
  testErrorPaths();
  testConstructors();
  if (fails) { printf("%d check(s) failed\n", fails); return 1; }
  printf("all tests passed\n");
  return 0;
}
