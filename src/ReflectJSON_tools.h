/*
 ReflectJSON - JSON writer and reader with struct reflection for Arduino / C++17
 Copyright (c) 2026 Michael
 Commercial licensing available under separate written agreement.
 See LICENSE and COMMERCIAL.md.
*/

#pragma once

#define PM5_PRAGMA(x) _Pragma(#x)

#if defined(_MSC_VER)
  #define REGION(name)   PM5_PRAGMA(region name)
  #define ENDREGION()    PM5_PRAGMA(endregion)
#else
  #define REGION(name)   /* nothing */
  #define ENDREGION()    /* nothing */
#endif

#if defined(ESP8266) || defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_SAM)  || \
    defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_STM32)
  #error "JSONWriter/JSONReader requires a modern C++17-capable Arduino core (ESP32, RP2350/RP2040, Teensy 3.x). ESP8266, AVR, SAM/SAMD and STM32 Arduino cores are not supported."
#endif

#include <type_traits>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <climits>
#include <limits>
#include <cmath>
#include <utility>
#include <new>

#if defined(__has_include)
  #if __has_include(<optional>)
    #include <optional>
    #define JSON_HAS_OPTIONAL 1
  #endif
#endif

#ifdef ARDUINO
#include <Arduino.h>
#endif

constexpr size_t JSON_ERR_MAX_MSG = 176;
constexpr size_t JSON_ERR_MAX_PATH = 15;
// Maximum length of the error path of getErrorPath() (incl. '\0'); longer paths end with "..."
constexpr size_t JSON_ERR_MAX_PATH_LEN = 96;
constexpr size_t JSON_MAX_OBJECT_KEYS = 25;
constexpr size_t JSON_MAX_KEYS = 75;
constexpr size_t JSON_MAX_KEY_LEN = 32;
#ifndef JSON_PRETTY_INDENT
#define JSON_PRETTY_INDENT 2   // spaces per level for getJSON(true)
#endif
constexpr size_t JSON_MAX_STRING_LEN = 254;

constexpr size_t ROTATING_STR_BUFFER_COUNT = 10;
constexpr uint8_t MAX_FLOAT_PRECISION = 8;

static_assert(JSON_MAX_OBJECT_KEYS <= JSON_MAX_KEYS, "JSON_MAX_OBJECT_KEYS must not exceed JSON_MAX_KEYS");
static_assert(JSON_MAX_KEYS < 255, "JSON_MAX_KEYS must be below 255 (8-bit key indices in the reader)");
static_assert(JSON_MAX_KEY_LEN <= JSON_MAX_STRING_LEN, "JSON_MAX_KEY_LEN must not exceed JSON_MAX_STRING_LEN");
static_assert(JSON_MAX_STRING_LEN < 60000, "JSON_MAX_STRING_LEN too large");
static_assert(ROTATING_STR_BUFFER_COUNT >= 1, "ROTATING_STR_BUFFER_COUNT must be >= 1");


#if defined(__GNUC__)
    #define JSON_NOINLINE __attribute__((noinline))
    #define FORCE_INLINE __attribute__((always_inline)) inline
#else
    #define JSON_NOINLINE
    #define FORCE_INLINE inline
#endif


REGION(typen)
  template<typename T>
  class is_reflectable {
    private:
      template<typename U>
      static auto test(int) -> decltype(
          std::declval<const U&>().reflect( std::declval<void(*)(const char*, int)>() ),
          std::true_type{}
      );

      template<typename>
      static std::false_type test(...);

    public:
        static constexpr bool value = decltype(test<T>(0))::value;
  };

  template<typename T>
  constexpr bool is_reflectable_v = is_reflectable<T>::value;

  template<typename>
  inline constexpr bool always_false = false;

  template<typename T>
  using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;
ENDREGION()


/**
 * Shared settings for JSONreader AND JSONwriter (corresponds to the Kotlin configuration
 * Json { ignoreUnknownKeys = true; coerceInputValues = true; explicitNulls = false; encodeDefaults = true }).
 *
 *   JsonConfig cfg;                      // = the Kotlin settings above
 *   JSONreader reader(200, cfg);         // or reader.setConfig(cfg)
 *   JSONwriter writer(2048, 5, cfg);     // or writer.setConfig(cfg)
 *   JsonConfig::strict()                 // old, strict behaviour
 *
 * The "default" of a struct variable is the value the object had BEFORE read()
 * (default member initializer or a previously assigned value).
 *
 *   ignoreUnknownKeys   Reader: keys in the JSON that do not exist in the struct are ignored (otherwise error).
 *   coerceInputValues   Reader: a JSON null for a non-nullable variable leaves the variable at its
 *                       default (otherwise error). Nullable types are std::optional<T> and const char*.
 *   explicitNulls       false: writer omits null values in objects (arrays keep their nulls);
 *                       reader: missing keys are not an error, the variable keeps its default.
 *                       true:  writer writes "key":null; reader requires every key of the struct.
 *   encodeDefaults      is always true: the writer writes every member, even if it equals the default.
 */
struct JsonConfig {
  bool ignoreUnknownKeys = true;
  bool coerceInputValues = true;
  bool explicitNulls = false;

  static constexpr JsonConfig strict(){
    JsonConfig c;
    c.ignoreUnknownKeys = false;
    c.coerceInputValues = false;
    c.explicitNulls = true;
    return c;
  }
};

REGION(REFLECT)
  template<typename T>
  struct FieldRef {
      const char *key;
      uint8_t keyLen;
      T value;
  };

  template<size_t N>
  constexpr uint8_t litLen(const char (&)[N]) {
    static_assert(N - 1 <= JSON_MAX_KEY_LEN, "JSON key exceeds JSON_MAX_KEY_LEN");
    return uint8_t(N - 1);
  }

  #define MEMBER_GET_MACRO(_1,_2,NAME,...) NAME
  /**
  * @brief Declares a reflected struct field.
  *
  * Usage:
  *   MEMBER(field)
  *   MEMBER(field, expression)         //expression = fx() and fn()
  *
  * Expands to a FieldRef containing the key name, key length,
  * and a reference/expression to the field value.
  */
  #define MEMBER(...) MEMBER_GET_MACRO(__VA_ARGS__, MEMBER2, MEMBER1)(__VA_ARGS__)

  #define MEMBER1(field) FieldRef<decltype((this->field))>{ #field, litLen(#field), (this->field) }
  #define MEMBER2(field, expr) FieldRef<decltype(expr)>{ #field, litLen(#field), (expr) }

  /**
  * @brief Declares a reflected struct entry with a custom JSON key.
  *
  * Usage:
  *   MEMBER_AS(key, expr)
  *
  * The second argument may be any valid expression referring to a value
  * (e.g. a struct member, an indexed element, or a computed wrapper).
  *
  * Expands to a FieldRef containing the provided key, its length,
  * and the evaluated expression.
  */
  #define MEMBER_AS(key, expr) FieldRef<decltype((expr))>{ key, litLen(key), (expr) }
  /**
  * @brief Enables reflection for a struct.
  *
  * The reflect() function is used by JsonReader, JsonWriter
  * to iterate over all reflected members.
  * Defines two reflect() overloads (const and non-const)
  * that forward all reflected members to the provided callable.
  *
  * The visitor parameter has an unusual name (jsonVisitor_) so that it can
  * never collide with a struct member inside MEMBER(field, expression).
  *
  * Usage:
  *   struct Foo {
  *     int a;
  *     float b;
  *     bool c;
  *     REFLECT(
  *       MEMBER(a),                    //adds field (int a) to the JSON with key "a"
  *       MEMBER(b),                    //adds field (float b) to the JSON with key "b"
  *       MEMBER_AS("custom key", c)    //adds field (bool c) to the JSON with key "custom key"
  *     )
  *   };
  */
  #define REFLECT(...) \
  template<typename JsonVisitorT_> \
  void reflect(JsonVisitorT_&& jsonVisitor_) const { \
    jsonVisitor_(__VA_ARGS__); \
  } \
  template<typename JsonVisitorT_> \
  void reflect(JsonVisitorT_&& jsonVisitor_) { \
    jsonVisitor_(__VA_ARGS__); \
  }
ENDREGION()

REGION(numbers)
  /**
  * @brief Numeric wrapper for floating-point values with configurable precision.
  *
  * JsonNumber is used to control how floating-point values are
  * serialized to JSON (number of decimal places).
  *
  * Typically constructed via fn():
  *   fn(value, precision)
  *
  * This wrapper does not own the value; it stores a reference.
  */
  template<typename T>
  struct JsonNumber {
    static_assert(std::is_floating_point_v<remove_cvref_t<T>>, "JsonNumber is only for floating point types");

    T& value;             ///< referenced numeric value
    uint8_t precision;    ///< number of decimal places
  };
  template<typename T>
  JsonNumber(T&, uint8_t) -> JsonNumber<T>;

  /**
  * @brief Creates a JsonNumber wrapper with the given precision.
  *
  * Usage:
  *   MEMBER(field, fn(field, 2))
  *
  * Precision values above MAX_FLOAT_PRECISION (8) are clamped.
  */
  template<typename T>
  FORCE_INLINE JsonNumber<T> fn(T &v, uint8_t precision) {
    if(precision > MAX_FLOAT_PRECISION){
      precision = MAX_FLOAT_PRECISION;
    }
    return { v, precision };
  }


  /**
  * @brief Fixed-point numeric wrapper for floating-point values.
  *
  * JsonFixed represents floating-point values as fixed-point numbers
  * with a fixed number of decimal places. In the JSON the value is an
  * integer (value * 10^precision).
  *
  * Typically constructed via fx():
  *   fx(value, precision)
  *
  * This wrapper does not own the value; it stores a reference.
  */
  template<typename T>
  struct JsonFixed {
    static_assert(std::is_floating_point_v<remove_cvref_t<T>>, "JsonFixed is only for floating point types");

    T& value;             ///< referenced numeric value
    uint8_t precision;    ///< fixed decimal places
  };
  template<typename T>
  JsonFixed(T&, uint8_t) -> JsonFixed<T>;

  /**
  * @brief Creates a JsonFixed wrapper with the given precision.
  *
  * Usage:
  *   MEMBER(field, fx(field, 3))
  *
  * Precision values above MAX_FLOAT_PRECISION (8) are clamped.
  */
  template<typename T>
  FORCE_INLINE JsonFixed<T> fx(T &v, uint8_t precision) {
    if(precision > MAX_FLOAT_PRECISION){
      precision = MAX_FLOAT_PRECISION;
    }
    return { v, precision };
  }

  /**
  * @brief Returns the default JSON precision for a numeric type.
  *
  * Used when no explicit precision is specified.
  *
  * float  -> 2
  * double -> 4
  */
  template<typename T>
  constexpr uint8_t defaultPrecision() {
    using U = remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, float>)  return 2;
    if constexpr (std::is_same_v<U, double>) return 4;
    return 0;
  }

  template<typename>
  struct is_json_fixed : std::false_type {};
  template<typename T>
  struct is_json_fixed<JsonFixed<T>> : std::true_type {};

  // std::optional<T> = nullable type (like "T?" in Kotlin)
  template<typename T>
  struct is_json_optional : std::false_type {};
#ifdef JSON_HAS_OPTIONAL
  template<typename T>
  struct is_json_optional<std::optional<T>> : std::true_type {};
#endif
  template<typename T>
  inline constexpr bool is_json_optional_v = is_json_optional<remove_cvref_t<T>>::value;

  template<typename T>
  struct json_optional_value { using type = T; };
#ifdef JSON_HAS_OPTIONAL
  template<typename T>
  struct json_optional_value<std::optional<T>> { using type = T; };
#endif

  template<typename T>
  inline constexpr bool is_json_fixed_v = is_json_fixed<T>::value;

  template<typename>
  struct is_json_number : std::false_type {};
  template<typename T>
  struct is_json_number<JsonNumber<T>> : std::true_type {};
  template<typename T>
  inline constexpr bool is_json_number_v = is_json_number<T>::value;

  template<typename T>
  struct unwrap_numeric {
      using type = remove_cvref_t<T>;
  };

  template<typename T>
  struct unwrap_numeric<JsonFixed<T>>  { using type = remove_cvref_t<T>; };

  template<typename T>
  struct unwrap_numeric<JsonNumber<T>> { using type = remove_cvref_t<T>; };

  template<typename T>
  struct unwrap_numeric<FieldRef<T>>   { using type = typename unwrap_numeric<T>::type; };

  template<typename T>
  using unwrap_numeric_t = typename unwrap_numeric<T>::type;
ENDREGION()

REGION(helper)
  // Marks a value explicitly as a C-style string.
  //
  // This helper forces string literal arguments (e.g. "abc") to be treated
  // as strings instead of character arrays when passed to template-based
  // JSON writer functions.
  //
  // Use this when writing string literals via generic write/setData APIs.
  // Regular char[N] variables remain arrays and are not affected.
  FORCE_INLINE const char* cstr(const char* s) { return s; }

  // Overload for string literals (const char[N]).
  //
  // Converts a string literal reference into a const char* so that it is
  // serialized as a JSON string rather than a character array.
  template<size_t N>
  FORCE_INLINE const char* cstr(const char (&s)[N]) { return s; }

  // Internal helper macros (prefixed so they do not collide with other libraries)
  #define JSON_IS_SPACE(c) ((c)==' '||(c)=='\t'||(c)=='\n'||(c)=='\r')
  #define JSON_IS_DIGIT(c) ((unsigned)((c)-'0') <= 9u)
  #define JSON_IS_XDIGIT(c) (JSON_IS_DIGIT(c) || (((c) | 0x20) >= 'a' && ((c) | 0x20) <= 'f'))

  // 10^0 .. 10^22 are exactly representable as double
  // JsonNumber/JsonFixed can be constructed directly (without fn()/fx()): always limit the precision
  inline constexpr uint8_t jsonClampPrecision(const uint8_t p){ return p > MAX_FLOAT_PRECISION ? uint8_t(MAX_FLOAT_PRECISION) : p; }
  inline constexpr double JSON_POW10[] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
    1e20, 1e21, 1e22
  };
  constexpr uint8_t JSON_POW10_MAX_EXACT = 22;

  // Digit pairs "00".."99" for fast integer output
  inline constexpr char JSON_DIGIT_PAIRS[201] =
    "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
    "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

  // Escape table for JSON strings: 0 = character unchanged,
  // otherwise the letter behind the backslash ('u' means \u00XX).
  struct JsonEscapeTable {
    uint8_t v[256];
    constexpr JsonEscapeTable() : v() {
      for(int i = 0; i < 0x20; i++) v[i] = 'u';
      v[0x7F] = 'u';
      v[(int)'"'] = '"';
      v[(int)'\\'] = '\\';
      v[(int)'\n'] = 'n';
      v[(int)'\r'] = 'r';
      v[(int)'\t'] = 't';
      v[(int)'\b'] = 'b';
      v[(int)'\f'] = 'f';
    }
  };
  inline constexpr JsonEscapeTable JSON_ESC{};

  // Type names (for error messages only)
  template<typename T>
  constexpr const char* cppTypeName([[maybe_unused]] bool memberTypeName = false) {
      using U = unwrap_numeric_t<T>;
      using R = remove_cvref_t<U>;

      // optional -> name of the contained type
      if constexpr (is_json_optional_v<R>)
        return cppTypeName<typename json_optional_value<R>::type>(memberTypeName);

      //string pointers
      if constexpr (std::is_same_v<R, const char*> || std::is_same_v<R, char*>)
        return "string";

      //null / pointer
      if constexpr (std::is_same_v<R, std::nullptr_t>)
        return "null";
      else if constexpr (std::is_pointer_v<R>)
        return cppTypeName<std::remove_pointer_t<R>>(memberTypeName);

      //arrays
      if constexpr (std::is_array_v<R>) {
        if(memberTypeName) return "array";
        return cppTypeName<std::remove_extent_t<R>>(memberTypeName);
      }

      //floating
      if constexpr (std::is_same_v<R, float>)  return "float";
      if constexpr (std::is_same_v<R, double>) return "double";

      //bool
      if constexpr (std::is_same_v<R, bool>) return "bool";

      if constexpr (std::is_same_v<R, char>) return "char";
      if constexpr (std::is_same_v<R, signed char>) return "int8_t";
      if constexpr (std::is_same_v<R, unsigned char>) return "uint8_t";

      //ints
      if constexpr (std::is_integral_v<R>){
        if constexpr (std::is_signed_v<R>){
          if constexpr (sizeof(R) == 1) return "int8_t";
          if constexpr (sizeof(R) == 2) return "int16_t";
          if constexpr (sizeof(R) == 4) return "int32_t";
          if constexpr (sizeof(R) == 8) return "int64_t";
        }else{
          if constexpr (sizeof(R) == 1) return "uint8_t";
          if constexpr (sizeof(R) == 2) return "uint16_t";
          if constexpr (sizeof(R) == 4) return "uint32_t";
          if constexpr (sizeof(R) == 8) return "uint64_t";
        }
      }

      //enums
      if constexpr (std::is_enum_v<R>) return "enum";

      //objects
      if constexpr (is_reflectable_v<R>) return "struct";

      return "unknown";
  }

  // Length of s, but never scan more than maxLen + 1 characters.
  // Return value > maxLen means "too long".
  FORCE_INLINE size_t jsonBoundedLen(const char* s, size_t maxLen) {
    size_t n = 0;
    while (n <= maxLen && s[n]) n++;
    return n;
  }

  // Writes cp as UTF-8 to out (out == nullptr: only compute the length).
  // Returns: number of bytes (1..4)
  inline uint8_t jsonUtf8Encode(uint32_t cp, char* out) {
    if (cp < 0x80) {
      if (out) out[0] = (char)cp;
      return 1;
    }
    if (cp < 0x800) {
      if (out) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); }
      return 2;
    }
    if (cp < 0x10000) {
      if (out) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); }
      return 3;
    }
    if (out) {
      out[0] = (char)(0xF0 | (cp >> 18));
      out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
      out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
      out[3] = (char)(0x80 | (cp & 0x3F));
    }
    return 4;
  }

  // Reads exactly 4 hex characters. Safely stops at a '\0'.
  inline bool jsonParseHex4(const char* s, uint16_t& out) {
    uint16_t v = 0;
    for (uint8_t i = 0; i < 4; i++) {
      const char h = s[i];
      if (!JSON_IS_XDIGIT(h)) return false;
      v = (uint16_t)((v << 4) | (JSON_IS_DIGIT(h) ? (h - '0') : (((h | 0x20) - 'a') + 10)));
    }
    out = v;
    return true;
  }

  // p points at the 'u' behind the backslash.
  // Decodes \uXXXX (for a high surrogate including the following \uXXXX) to UTF-8.
  // Lone/broken surrogates become U+FFFD.
  // out == nullptr: only compute the length.
  // Returns: number of UTF-8 bytes, 0 for invalid hex characters.
  // p then points at the last consumed character.
  inline uint8_t jsonDecodeUnicodeEscape(const char*& p, char* out) {
    uint16_t hi = 0;
    if (!jsonParseHex4(p + 1, hi)) return 0;

    uint32_t cp = hi;
    const char* last = p + 4;
    if (hi >= 0xD800 && hi <= 0xDBFF) {
      uint16_t lo = 0;
      if (last[1] == '\\' && last[2] == 'u' && jsonParseHex4(last + 3, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000u + (((uint32_t)hi - 0xD800u) << 10) + ((uint32_t)lo - 0xDC00u);
        last += 6;
      } else {
        cp = 0xFFFD;
      }
    } else if (hi >= 0xDC00 && hi <= 0xDFFF) {
      cp = 0xFFFD;
    }

    p = last;
    return jsonUtf8Encode(cp, out);
  }

  // Strictly validated UTF-8 sequence (no overlongs, no surrogates, <= U+10FFFF).
  // Returns: length of the sequence, 0 if invalid. Never reads behind a '\0'.
  inline uint8_t jsonUtf8SeqLen(const unsigned char* u) {
    const unsigned char c0 = u[0];
    if (c0 < 0xC2) return 0;
    if (c0 < 0xE0) return ((u[1] & 0xC0) == 0x80) ? 2 : 0;
    if (c0 < 0xF0) {
      const unsigned char b1 = u[1];
      if (c0 == 0xE0) { if (b1 < 0xA0 || b1 > 0xBF) return 0; }
      else if (c0 == 0xED) { if (b1 < 0x80 || b1 > 0x9F) return 0; }
      else if ((b1 & 0xC0) != 0x80) return 0;
      return ((u[2] & 0xC0) == 0x80) ? 3 : 0;
    }
    if (c0 < 0xF5) {
      const unsigned char b1 = u[1];
      if (c0 == 0xF0) { if (b1 < 0x90 || b1 > 0xBF) return 0; }
      else if (c0 == 0xF4) { if (b1 < 0x80 || b1 > 0x8F) return 0; }
      else if ((b1 & 0xC0) != 0x80) return 0;
      if ((u[2] & 0xC0) != 0x80) return 0;
      return ((u[3] & 0xC0) == 0x80) ? 4 : 0;
    }
    return 0;
  }

  enum class CTypeKind : uint8_t {
    Null,
    Bool,
    Enum,

    Int,
    FloatFixedPoint,
    FloatDefault,
    Float,
    String,
    StringArduino,
    Char,

    Object,
    Array,

    Unknown
  };

  template<typename T>
  constexpr CTypeKind deduceCType() {
    using U = remove_cvref_t<T>;

    if constexpr (std::is_same_v<U, std::nullptr_t>){
      return CTypeKind::Null;
    }else if constexpr (std::is_same_v<U, bool>){
      return CTypeKind::Bool;
    }else if constexpr (std::is_same_v<U, char>){
      return CTypeKind::Char;
    }else if constexpr (std::is_enum_v<U>){
      return CTypeKind::Enum;
    }else if constexpr (std::is_integral_v<U>){
      return CTypeKind::Int;
    }else if constexpr (is_json_fixed_v<U>) {
      return CTypeKind::FloatFixedPoint;
    }else if constexpr (is_json_number_v<U>) {
      return CTypeKind::Float;
    }else if constexpr (std::is_floating_point_v<U>){
      return CTypeKind::FloatDefault;
    }else if constexpr (std::is_same_v<U, const char*> || std::is_same_v<U, char*>){
      return CTypeKind::String;
    #ifdef ARDUINO
    }else if constexpr (std::is_same_v<U, String>){
      return CTypeKind::StringArduino;
    #endif
    }else if constexpr (is_reflectable_v<U>){
      return CTypeKind::Object;
    }else if constexpr (std::is_array_v<U>){
      return CTypeKind::Array;
    }else if constexpr (std::is_class_v<U> || std::is_union_v<U>){
      static_assert(always_false<U>, "struct/class has no REFLECT(...) macro");
      return CTypeKind::Unknown;
    }else{
      static_assert(always_false<U>, "unsupported type");
      return CTypeKind::Unknown;
    }
  }
ENDREGION()

REGION(errorpath)
  // Shared output buffer for getErrorPath() of JSONreader and JSONwriter.
  // The path is only formatted on request: the pointer is valid until the next getErrorPath() call
  // (from any reader/writer). Copy the path if you need it longer.
  inline char *jsonPathBuffer(){
    static char buf[JSON_ERR_MAX_PATH_LEN];
    return buf;
  }

  // Writes a path like root.status.values[3].speed into jsonPathBuffer()
  struct JsonPathOut{
    char *b;
    size_t n = 0;
    bool full = false;

    JsonPathOut() : b(jsonPathBuffer()){}
    void put(const char c){
      if(n + 1 < JSON_ERR_MAX_PATH_LEN) b[n++] = c; else full = true;
    }
    void put(const char *s, size_t len){
      while(len--) put(*s++);
    }
    void putStr(const char *s){ while(*s) put(*s++); }
    void putIndex(unsigned i){
      char tmp[11];
      uint8_t k = 0;
      do{ tmp[k++] = char('0' + i % 10); i /= 10; }while(i);
      put('[');
      while(k) put(tmp[--k]);
      put(']');
    }
    void putKey(const char *s, size_t len){
      put('.');
      put(s, len);
    }
    const char *end(){
      if(full){
        n = JSON_ERR_MAX_PATH_LEN - 1;
        b[n - 1] = b[n - 2] = b[n - 3] = '.';
      }
      b[n] = '\0';
      return b;
    }
  };
ENDREGION()
