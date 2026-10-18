/*
 ReflectJSON - JSON writer and reader with struct reflection for Arduino / C++17
 Copyright (c) 2026 Michael
 Commercial licensing available under separate written agreement.
 See LICENSE and COMMERCIAL.md.
*/

#pragma once

// Shared reflection core (REFLECT, MEMBER, fn(), fx(), type traits, cppTypeName ...), also used by ReflectINI
#include "ReflectCore.h"
#if !defined(REFLECTCORE_VERSION) || REFLECTCORE_VERSION != 6
  #error "ReflectCore.h version mismatch: ReflectJSON and ReflectINI must ship the same ReflectCore.h"
#endif

constexpr size_t JSON_ERR_MAX_MSG = 176;
constexpr size_t JSON_ERR_MAX_PATH = 15;
// Maximum length of the error path of getErrorPath() (incl. '\0'); longer paths end with "..."
constexpr size_t JSON_ERR_MAX_PATH_LEN = 96;
constexpr size_t JSON_MAX_OBJECT_KEYS = 25;
constexpr size_t JSON_MAX_KEYS = 75;
constexpr size_t JSON_MAX_KEY_LEN = REFLECT_MAX_KEY_LEN;   // shared with ReflectINI (ReflectCore.h)
#ifndef JSON_PRETTY_INDENT
#define JSON_PRETTY_INDENT 2   // spaces per level for getJSON(true)
#endif
constexpr size_t JSON_MAX_STRING_LEN = 254;

constexpr size_t ROTATING_STR_BUFFER_COUNT = 10;

static_assert(JSON_MAX_OBJECT_KEYS <= JSON_MAX_KEYS, "JSON_MAX_OBJECT_KEYS must not exceed JSON_MAX_KEYS");
static_assert(JSON_MAX_KEYS < 255, "JSON_MAX_KEYS must be below 255 (8-bit key indices in the reader)");
static_assert(JSON_MAX_KEY_LEN <= JSON_MAX_STRING_LEN, "JSON_MAX_KEY_LEN must not exceed JSON_MAX_STRING_LEN");
static_assert(JSON_MAX_STRING_LEN < 60000, "JSON_MAX_STRING_LEN too large");
static_assert(ROTATING_STR_BUFFER_COUNT >= 1, "ROTATING_STR_BUFFER_COUNT must be >= 1");

#if defined(__GNUC__)
    #define JSON_NOINLINE __attribute__((noinline))
#else
    #define JSON_NOINLINE
#endif

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
  #define JSON_IS_XDIGIT(c) (JSON_IS_DIGIT(c) || (((c) | 0x20) >= 'a' && ((c) | 0x20) <= 'f'))   // JSON_IS_DIGIT: ReflectCore.h

  // JSON_DIGIT_PAIRS: ReflectCore.h (shared number formatting)

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
