// JsonSpeedTest.ino
//
// Measures the speed of JSONwriter / JSONreader and prints the result to Serial (115200 baud).
// Send any character in the serial monitor to run the test again.
// Run it on each board you care about: timings differ a lot between MCUs.

#include <ReflectJSON.h>

#define ITERATIONS 1000   // iterations per measurement (reduce on very slow boards)
#define REPEATS    3      // number of measurements, the best one counts

struct Inner {
  int16_t a;
  bool b;
  REFLECT(MEMBER(a), MEMBER(b))
};

struct Data {
  int32_t  i32;
  uint32_t u32;
  int64_t  i64;
  bool     flag;
  float    fl;
  double   dv;
  const char* name;
  const char* text;
  int      arr[8];
  Inner    inner;
  Inner    list[3];
  REFLECT(MEMBER(i32), MEMBER(u32), MEMBER(i64), MEMBER(flag), MEMBER(fl), MEMBER(dv),
          MEMBER(name), MEMBER(text), MEMBER(arr), MEMBER(inner), MEMBER(list))
};

static const char kJson[] =
  "{\"doc\":{\"i32\":123456,\"u32\":4000000000,\"i64\":-1234567890123,\"flag\":true,\"fl\":3.14,\"dv\":2.7183,"
  "\"name\":\"sensor-01\",\"text\":\"hello world 123\",\"arr\":[1,2,3,4,5,6,7,8],\"inner\":{\"a\":-5,\"b\":false},"
  "\"list\":[{\"a\":1,\"b\":true},{\"a\":2,\"b\":false},{\"a\":3,\"b\":true}]}}";

JSONwriter writer(2048, 5);
JSONreader reader(200);

static Data data;
static Data parsed;
static volatile uint32_t sink = 0;     // keeps the compiler from optimizing the loops away

// print the result as "xx.xx us" (integer math only, works on boards without FPU)
static void printResult(const char *name, uint32_t totalUs) {
  uint32_t x100 = (uint32_t)(((uint64_t)totalUs * 100ULL) / ITERATIONS);
  Serial.print(name);
  for (size_t n = strlen(name); n < 50; n++) Serial.print(' ');
  Serial.print(x100 / 100);
  Serial.print('.');
  uint32_t frac = x100 % 100;
  if (frac < 10) Serial.print('0');
  Serial.print(frac);
  Serial.println(" us");
}

// the code runs ITERATIONS times, repeated REPEATS times, the best value is printed
#define BENCH(name, ...)                                              \
  do {                                                                \
    uint32_t best = 0xFFFFFFFFUL;                                     \
    for (uint8_t rep = 0; rep < REPEATS; rep++) {                     \
      uint32_t t0 = micros();                                         \
      for (uint16_t i = 0; i < ITERATIONS; i++) { __VA_ARGS__; }      \
      uint32_t dt = micros() - t0;                                    \
      if (dt < best) best = dt;                                       \
    }                                                                 \
    printResult(name, best);                                          \
  } while (0)

static void runAll() {
  Serial.println();
  Serial.println("=== JSON Speedtest ===");
#ifdef ARDUINO_BOARD
  Serial.print("Board:      "); Serial.println(ARDUINO_BOARD);
#endif
#ifdef F_CPU
  Serial.print("CPU MHz:    "); Serial.println((uint32_t)(F_CPU / 1000000UL));
#endif
  Serial.print("Iterations: "); Serial.print(ITERATIONS);
  Serial.print(" x "); Serial.print(REPEATS); Serial.println(" (best value)");
  Serial.println();

  // ---- prepare data ----
  data.i32 = 123456;
  data.u32 = 4000000000UL;
  data.i64 = -1234567890123LL;
  data.flag = true;
  data.fl = 3.14f;
  data.dv = 2.7183;
  data.name = "sensor-01";
  data.text = "hello world 123";
  for (int k = 0; k < 8; k++) data.arr[k] = k + 1;
  data.inner.a = -5;   data.inner.b = false;
  data.list[0].a = 1;  data.list[0].b = true;
  data.list[1].a = 2;  data.list[1].b = false;
  data.list[2].a = 3;  data.list[2].b = true;

  static char longStr[201];
  static char escStr[201];
  for (int k = 0; k < 200; k++) longStr[k] = 'a';
  longStr[200] = '\0';
  for (int k = 0; k < 200; k += 4) { escStr[k] = 'a'; escStr[k + 1] = '"'; escStr[k + 2] = 'b'; escStr[k + 3] = '\n'; }
  escStr[200] = '\0';

  // ---- correctness ----
  writer.resetJSON();
  writer.write(data, "doc").finish();
  bool writerOk = !writer.hasError() && writer.getJSON() && strcmp(writer.getJSON(), kJson) == 0;
  Serial.print("Writer output as expected:   ");
  Serial.println(writerOk ? "YES" : "NO");
  if (!writerOk) {
    Serial.println(writer.getJSON() ? writer.getJSON() : "(no JSON)");
    if (writer.getErrorMsg()) Serial.println(writer.getErrorMsg());
  }

  bool parseOk = reader.parse(kJson, false);
  memset(&parsed, 0, sizeof(parsed));
  bool readOk = parseOk && reader.read(parsed, "doc");
  bool valuesOk = readOk && parsed.i32 == 123456 && parsed.u32 == 4000000000UL && parsed.i64 == -1234567890123LL &&
                  parsed.flag && parsed.inner.a == -5 && parsed.list[2].a == 3 && strcmp(parsed.name, "sensor-01") == 0;
  Serial.print("Reader reads all values:      ");
  Serial.println(valuesOk ? "YES" : "NO");
  if (!valuesOk && reader.getErrorMsg()) Serial.println(reader.getErrorMsg());
  Serial.println();

  // ---- Writer ----
  Serial.println("--- Writer (time per iteration) ---");

  BENCH("Writer: struct (11 fields, array, nested)",
        writer.resetJSON(); writer.write(data, "doc").finish(); sink += writer.getJSONSize());

  BENCH("Writer: 100 x int32 in array",
        writer.resetJSON(); writer.startArray("v");
        for (int k = 0; k < 100; k++) writer.write((int32_t)(i * 31 + k * 1000003L));
        writer.endArray().finish(); sink += writer.getJSONSize());

  BENCH("Writer: 100 x float (2 decimals)",
        writer.resetJSON(); writer.startArray("v");
        for (int k = 0; k < 100; k++) writer.write((float)(i % 1000) + k * 0.37f);
        writer.endArray().finish(); sink += writer.getJSONSize());

  BENCH("Writer: 200 char string, no escapes",
        writer.resetJSON(); writer.write((const char*)longStr, "s").finish(); sink += writer.getJSONSize());

  BENCH("Writer: 200 char string, many escapes",
        writer.resetJSON(); writer.write((const char*)escStr, "s").finish(); sink += writer.getJSONSize());

  // ---- Reader ----
  Serial.println();
  Serial.println("--- Reader (time per iteration) ---");

  BENCH("Reader: parse (about 200 bytes)",
        reader.parse(kJson, false); sink += reader.getTokenCount());

  reader.parse(kJson, false);
  BENCH("Reader: read struct (parsed once before)",
        reader.read(parsed, "doc"); sink += parsed.i32);

  BENCH("Reader: parse + read struct",
        reader.parse(kJson, false); reader.read(parsed, "doc"); sink += parsed.i32);

  Serial.println();
  Serial.print("(sink="); Serial.print(sink); Serial.println(")");
  Serial.println("Done. Send any character for another run.");
}

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) { }   // on USB boards wait briefly for the monitor
  delay(200);
  runAll();
}

void loop() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    runAll();
  }
}
