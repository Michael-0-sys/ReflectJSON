# ReflectJSON

Fast JSON **writer and reader for microcontrollers** (RP2040/RP2350, ESP32/ESP32-S3, Teensy 4.x, Arduino UNO R4) with **struct reflection**:
describe a struct once, and read or write it as JSON without hand-written serialization code.

```cpp
#include <ReflectJSON.h>

struct Status {
  float speed; int assist; bool light;
  REFLECT(MEMBER(speed, fn(speed, 1)), MEMBER(assist), MEMBER(light))
};

JSONwriter writer(256);

void setup() {
  Serial.begin(115200);
  Status status{18.6f, 3, true};

  writer.write(123, "id");
  writer.write(status);                 // no key: members go into the current object
  writer.write(cstr("ok"), "state");
  writer.finish();

  Serial.println(writer.getJSON());     // {"id":123,"speed":18.6,"assist":3,"light":true,"state":"ok"}
  Serial.println(writer.getJSON(true)); // same document, pretty printed
}
```

## Features

- **Reflection**: `REFLECT(MEMBER(a), MEMBER(b))` maps structs, nested structs and C arrays in both directions.
- **Writer**: buffer-based streaming writer with structural validation (nesting depth, key limits, duplicate keys,
  string length), `std::optional`, fixed-point / precision control (`fn()`, `fx()`), struct flattening.
- **Reader**: single-pass tokenizer with strict validation (JSON grammar, escapes, UTF-8, duplicate keys), reads
  into structs, arrays and single values by key or by exact path. Zero-copy strings when the buffer is writable.
- **Kotlin-style configuration** (`JsonConfig`) shared by reader and writer: `ignoreUnknownKeys`,
  `coerceInputValues`, `explicitNulls`.
- **Pretty printing** on demand: `writer.getJSON(true)`. Strings are never touched; the compact buffer stays valid.
- **Precise errors**: messages plus a location like `root.devices[2].limits.max` via `getErrorPath()`.
- **Made for small MCUs**: no exceptions, no RTTI, no `std::string`; heap use only for the buffers you size in the
  constructors; integer fast paths for 32-bit cores.

## Installation

- **Arduino IDE**: Sketch > Include Library > Add .ZIP Library... (use the repository zip), or copy the folder to
  `Arduino/libraries/ReflectJSON`.
- **PlatformIO**: add the repository URL to `lib_deps`. The library needs C++17 (`build_flags = -std=gnu++17`,
  `build_unflags = -std=gnu++11 -std=gnu++14`; ESP32 and arduino-pico already default to C++17 in recent cores).

Include everything with `#include <ReflectJSON.h>` (or the single headers `ReflectJSON_tools.h`, `ReflectJSON_writer.h`, `ReflectJSON_reader.h`).

## Writing

```cpp
JSONwriter writer(512);                 // output buffer in bytes, default nesting depth 5
JSONwriter writer2(512, 8);             // depth 8
JSONwriter quiet(512, false);           // no error messages / paths (saves time and a little RAM)

writer.startObject("status").write(18, "speed").write(true, "light").endObject();
writer.startArray("values").write(1).write(2).write(3).endArray();
writer.write(myStruct, "config");       // reflected struct under a key
writer.write(myStruct);                 // no key inside an object: flattened into the current object
writer.finish();                        // closes the root object

const char *json = writer.getJSON();
size_t size = writer.getJSONSize();
writer.resetJSON();                     // start a new document
```

- The root is always an object; `finish()` closes it. In arrays, values are written without a key.
- **String literals**: write them with `cstr("text")` or from a `const char*`. A plain `"text"` literal has type
  `const char[N]` and is written as an array of characters.
- Strings are limited to 254 characters, keys to 32.
- `std::optional` / `nullptr`: with the default `explicitNulls = false` null values are omitted in objects and written as
  `null` in arrays.

### Pretty printing

```cpp
Serial.println(writer.getJSON());        // compact
Serial.println(writer.getJSON(true));    // indented (JSON_PRETTY_INDENT spaces, default 2)
size_t n = writer.getJSONSize(true);     // length of the pretty text, does not allocate
```

The pretty text is built in a second buffer on the first `getJSON(true)` after `finish()` (two passes: count, then write).
The pointer stays valid until the next `getJSON(true)`, `resetJSON()` or the destructor. If memory is short,
`getJSON(true)` returns `nullptr` and the compact JSON stays available.

## Reading

```cpp
JSONreader reader(100);                  // up to 100 tokens (objects, arrays, keys and values count one each)

char json[] = "{\"status\":{\"speed\":18,\"light\":true}}";
if (!reader.parse(json, true)) {         // true = buffer is writable (enables zero-copy strings)
  Serial.println(reader.getErrorMsg());
  return;
}

Status status;
if (reader.read(status, "status")) { /* ... */ }
int speed;
reader.read(speed, "speed");                           // search the whole document for key "speed"
reader.readExact(speed, "speed", "status");            // exact path: root -> status -> speed
reader.read(speed, "speed", SearchMode::MatchByType);  // keep searching when a same-named key has the wrong type
```

- **Pass `false` for read-only buffers** (string literals, flash). With `true` the reader may modify the buffer
  while reading strings; passing a literal with `true` is undefined behaviour.
- `const char*` members point into the JSON buffer (writable, no escapes) or into one of 10 shared rotating buffers.
  Copy strings you need to keep.
- `std::optional<T>` and `const char*` are the nullable types.
- Structs must be declared at namespace scope (local structs cannot use `REFLECT`).

## Configuration

`JsonConfig` mirrors Kotlin's `Json { ... }` and works the same for reader and writer:

| Setting | Reader | Writer |
|---|---|---|
| `ignoreUnknownKeys = true` | unknown keys in the JSON are skipped | - |
| `coerceInputValues = true` | JSON `null` for a non-nullable member keeps the default | - |
| `explicitNulls = false` | missing keys keep their default | `null` values are omitted in objects |
| encode defaults | - | always on: every member is written |

```cpp
JSONreader reader(100);                         // defaults = the settings above
JSONreader strictReader(100, JsonConfig::strict());   // unknown/missing keys and nulls are errors
JSONwriter writer(256, 5, JsonConfig::strict());      // writes "key":null
```

## Errors

```cpp
if (!reader.read(devices, "devices")) {
  Serial.println(reader.getErrorMsg());   // 'max' type mismatch: expected int32_t, got string (in object 'limits')
  Serial.println(reader.getErrorPath());  // root.devices[2].limits.max
}
```

`getErrorCode()`, `getErrorMsg()` and `getErrorPath()` exist on reader and writer. `getErrorPath()` is built on request
into a buffer **shared by all readers and writers**; the pointer is valid until the next `getErrorPath()` call. Parse
errors report the position in the message instead of a path. With `createErrorInfo = false` messages and paths are not
generated (`nullptr`).

## Limits

| What | Limit |
|---|---|
| Keys per object / per document | 25 / 75 |
| Key length | 32 |
| String length | 254 |
| JSON size for the reader | 65535 bytes |
| Reader tokens | 32767 (you choose the capacity in the constructor) |
| Decimal places | 8 |
| Nesting depth (writer) | set in the constructor (default 5) |

The limits are constants at the top of `ReflectJSON_tools.h`.

## Notes

- Requires **C++17**. Intended targets: RP2040/RP2350 (arduino-pico), ESP32 / ESP32-S3, Teensy 4.x. Other cores may
  work if they compile as C++17. AVR is not supported.
- Not thread-safe: static buffers are shared (rotating string buffers, error path). Use one task/core at a time.
- Time-critical code: run `examples/JsonSpeedTest` on your board. Timings differ a lot between MCUs.

## Tests

```
make -C test            # unit tests on the PC, AddressSanitizer + UndefinedBehaviorSanitizer
make -C test examples   # builds the examples against a small Arduino stand-in
```

## License

Dual license:

- **Noncommercial use** (private, hobby, education, research, non-profit) is free under the
  [PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0).
- **Commercial use requires a separate written license**; see [COMMERCIAL.md](COMMERCIAL.md).

This is a source-available license, not an OSI-approved open-source license.
