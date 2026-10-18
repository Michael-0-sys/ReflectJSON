/*
 ReflectJSON - JSON writer and reader with struct reflection for Arduino / C++17
 Copyright (c) 2026 Michael
 Commercial licensing available under separate written agreement.
 See LICENSE and COMMERCIAL.md.
*/

#pragma once
#include "ReflectJSON_tools.h"

// Error and status codes returned by JSONreader.
//
// These codes cover parsing, validation, and data access errors.
// JSON_READER_OK (0) indicates successful parsing or reading with no errors.
enum class JsonReaderStatus{
  // general
  JSON_READER_OK = 0,               // No error
  JSON_ERR_EMPTY_JSON,              // Input JSON is empty
  JSON_ERR_JSON_NOT_PARSED,         // Access attempted before parsing
  JSON_ERR_JSON_CHANGED,            // JSON content changed after parsing
  JSON_ERR_INVALID_KEY,             // Invalid or malformed key
  JSON_ERR_INVALID_PATH,            // Invalid or malformed path key

  // parsing errors
  JSON_ERR_UNEXPECTED_CHAR,         // Unexpected character in input
  JSON_ERR_ILLEGAL_NUMBER,          // Invalid number format
  JSON_ERR_ILLEGAL_BOOL,            // Invalid boolean literal
  JSON_ERR_ILLEGAL_NULL,            // Invalid null literal
  JSON_ERR_ILLEGAL_ESCAPE,          // Invalid escape sequence
  JSON_ERR_INVALID_UTF8,            // Invalid UTF-8 sequence
  JSON_ERR_MISSING_QUOTE,           // Missing closing quote
  JSON_ERR_MISSING_COLON,           // Missing ':' after object key
  JSON_ERR_BRACE_MISMATCH,          // Mismatched braces or brackets
  JSON_ERR_TOKEN_LIMIT,             // Token limit exceeded
  JSON_ERR_KEY_LIMIT,               // Global key limit exceeded
  JSON_ERR_KEY_LEN_LIMIT,           // Key length exceeds limit
  JSON_ERR_STRING_LEN_LIMIT,        // String length exceeds limit

  // structural / mapping errors
  JSON_ERR_KEY_MISSING,             // Struct member missing in JSON
  JSON_ERR_EXTRA_KEY,               // JSON contains a key not present in the struct
  JSON_ERR_TYPE_MISMATCH,           // JSON value type does not match target type
  JSON_ERR_KEY_FOUND_TYPE_MISSING,  // Key found but value type is missing or invalid
  JSON_ERR_TYPE_UNDERFLOW,          // Value is smaller than target type allows
  JSON_ERR_TYPE_OVERFLOW,           // Value exceeds target type range
  JSON_ERR_NOT_FOUND,               // Requested value or path not found
  JSON_ERR_INVALID_VALUE,           // Value is semantically invalid

  // --- array errors ---
  JSON_ERR_ARRAY_TOO_MANY_ELEMENTS, // JSON array has more elements than target array
  JSON_ERR_ARRAY_TOO_FEW_ELEMENTS,  // Target array expects more elements than JSON array

  // --- added later (appended so existing numeric values stay unchanged) ---
  JSON_ERR_JSON_TOO_LARGE,          // JSON is longer than JSON_READER_MAX_JSON_LEN bytes
  JSON_ERR_OUT_OF_MEMORY,           // The internal buffers could not be allocated (constructor)
  JSON_ERR_DUPLICATE_KEY            // The same key appears twice in one object (parse error, same rule as JSONwriter)
};
// Defines how members are matched when reading values from a JSON object.
enum class SearchMode {
  // Matches strictly by key.
  // Fails immediately if the key exists but the value type does not match.
  MatchByKey,
  // Matches by value type.
  // Continues searching for another key with a compatible type
  // if a type mismatch occurs.
  MatchByType
};

// Maximum number of tokens (token indices are stored as int16_t).
constexpr uint16_t JSON_READER_MAX_TOKENS = 32767;
// Maximum length of a JSON document in bytes (token offsets are stored as uint16_t).
constexpr size_t JSON_READER_MAX_JSON_LEN = 65535;

// JSONreader parses a JSON document into an internal token representation
// and provides typed access to values via reflection-based mapping.
//
// The reader performs tokenization and structural validation during parsing.
// Parsed tokens, keys, and runtime state are stored internally.
//
// Memory for tokens and key caches is allocated dynamically during construction.
// The instance can be reused for multiple JSON documents
class JSONreader {
public:
    // Constructs a JSONreader with a specified maximum token capacity.
    //
    // maxTokens defines the maximum number of JSON tokens that can be parsed.
    // A minimum of 10 and a maximum of JSON_READER_MAX_TOKENS (32767) is enforced internally.
    // Objects, arrays, keys and values each count as one token.
    //
    // createErrorInfo enables or disables creation of extended error information.
    // When disabled, only the error code and basic status flags
    // are recorded. No error message strings, keys, paths or other
    // contextual error information are generated.
    //
    // Allocates internal token and key caches and initializes all runtime state.
    // If the allocation fails, parse() returns false with JSON_ERR_OUT_OF_MEMORY.
    explicit JSONreader(uint16_t maxTokens, const JsonConfig &config, bool createErrorInfo = true)
      : JSONreader(maxTokens, createErrorInfo){
      _config = config;
    }
    explicit JSONreader(uint16_t maxTokens, bool createErrorInfo = true){
      _storage.tokens = nullptr;
      _storage.keyCache = nullptr;
      setAllRuntimeStatusToDefault();

      _storage.createErrorInfo = createErrorInfo;
      _storage.verifyBuffer = true;
      _storage.maxTokens = maxTokens < 10 ? 10 : (maxTokens > JSON_READER_MAX_TOKENS ? JSON_READER_MAX_TOKENS : maxTokens);
      _storage.tokens = new (std::nothrow) JsonToken[_storage.maxTokens];
      _storage.keyCache = new (std::nothrow) uint16_t[JSON_MAX_KEYS];
    }
    // Settings shared with JSONwriter (see JsonConfig): ignoreUnknownKeys, coerceInputValues,
    // explicitNulls. Applies to all following read() calls.
    JSONreader &setConfig(const JsonConfig &config){ _config = config; return *this; }
    const JsonConfig &getConfig() const{ return _config; }

    // Destroys the JSONReader and releases all allocated resources.
    ~JSONreader(){
      delete[] _storage.tokens;
      delete[] _storage.keyCache;
    }
    JSONreader(const JSONreader&) = delete;
    JSONreader &operator=(const JSONreader&) = delete;

    // Returns the number of parsed tokens.
    // Returns 0 if a parse error has occurred.
    uint16_t getTokenCount() const{ return _runtimeStatus.hasParseError ? 0 : _storage.tokenCount; }
    // Returns the configured maximum number of tokens.
    uint16_t getMaxToken() const{ return _storage.maxTokens; }
    // Returns the number of parsed keys.
    // Returns 0 if a parse error has occurred.
    uint16_t getKeyCount() const{ return _runtimeStatus.hasParseError ? 0 : _storage.keyCount; }
    // Returns the maximum number of keys allowed per json document.
    uint8_t getMaxKeys() const{ return uint8_t(JSON_MAX_KEYS); }
    // Returns the maximum number of keys allowed per object.
    uint8_t getMaxObjectKeys() const{ return uint8_t(JSON_MAX_OBJECT_KEYS); }

    // Enables or disables creation of extended error information.
    //
    // When disabled, only the error code and basic status flags
    // are recorded. No error message strings, keys, paths or other
    // contextual error information are generated.
    void setCreateErrorInfo(bool on){ _storage.createErrorInfo = on; }
    // Returns whether creation of extended error information is enabled.
    //
    // If false, error message are suppressed and only error codes and
    // positional information are available.
    bool isCreateErrorInfoEnabled() const{ return _storage.createErrorInfo; }

    // Enables or disables the integrity check of the JSON buffer on every read.
    //
    // When enabled (default), every read() / readExact() verifies that the buffer
    // was not modified after parse() and fails with JSON_ERR_JSON_CHANGED otherwise.
    // The check costs one pass over the JSON per read call. It can be disabled
    // when the buffer is guaranteed to stay untouched between parse() and the reads.
    void setBufferCheck(bool on){ _storage.verifyBuffer = on; }
    // Returns whether the buffer integrity check is enabled.
    bool isBufferCheckEnabled() const{ return _storage.verifyBuffer; }

    // Returns the error code of the last error that occurred,
    // or JSON_READER_OK (0) if no error is present.
    JsonReaderStatus getErrorCode() const{ return _runtimeStatus.code; }

    // Returns the human-readable message of the last read or parse error.
    // Returns nullptr if no error is present or if createErrorInfo is disabled.
    const char *getErrorMsg() const{ 
      if(!_storage.createErrorInfo) return nullptr;
      return _runtimeStatus.hasReadError || _runtimeStatus.hasParseError ? _runtimeStatus.message : nullptr;
    }

    // Returns the location of the last read error as a path, e.g.
    //   root.status.motor.speed      root.status.values[3].speed      root.devices[2].settings.limit
    // Array indices are shown as [n]; for a missing key the path ends with that key.
    //
    // The path is only recorded as a small snapshot when the error happens and is formatted
    // on request (no per-reader string buffer). The returned pointer refers to a buffer that is
    // shared by all readers/writers and stays valid until the next getErrorPath() call.
    //
    // Returns nullptr if there is no read error, if createErrorInfo is disabled, or for errors
    // that have no location (parse errors: use the position in the message, not parsed, invalid key).
    const char *getErrorPath() const{
      if(!_storage.createErrorInfo || !_runtimeStatus.hasReadError || !_errPath.valid) return nullptr;
      return formatErrorPath();
    }

    // Returns true if the JSON document was successfully parsed.
    bool isJSONParsed() const{ return _runtimeStatus.isParsed; }
    // Returns true if a parse error occurred during tokenization or validation.
    bool hasParseError() const{ return _runtimeStatus.hasParseError; }
    // Returns true if a read error occurred.
    bool hasReadError() const{ return _runtimeStatus.hasReadError; }


    // Reads a value using an exact key path.
    // All path elements must match exactly; no search or fallback is performed.
    // If no path keys are provided, only the root object is searched.
    // Fails if any key in the path is missing or has a mismatched type.
    //
    // On failure the target variable is left unchanged.
    //
    // Supported value types include primitives, structs, arrays, and types handled
    // via REFLECT or custom writer traits.
    template<typename T, typename K, typename... Keys>
    inline bool readExact(T &out, K &&key, Keys&&... path) {
      return readExactInternal(out, keyArg(key), keyArg(path)... );
    }
    // Reads a value using a key and a key path.
    // The search is limited to the provided key path object.
    // The search behavior is controlled by SearchMode.
    //
    // -MatchByKey: Searches for the given key within the key path.
    // -MatchByType: Searches for the given key within the key path and
    //                 continues searching if the value type does not match.
    //
    // Fails if the key cannot be found or no matching value with the expected type exists.
    //
    // Supported value types include primitives, structs, arrays, and types handled
    // via REFLECT or custom writer traits.
    template<typename T, typename K, typename KP>
    inline bool read(T &out, K &&key, KP &&keyPath, SearchMode mode = SearchMode::MatchByKey){
      return readInternal(out, keyArg(key), keyArg(keyPath), mode);
    }
    // Reads a value by key without providing a key path.
    // The entire JSON document is searched for the given key.
    // The search behavior is controlled by SearchMode:
    //
    // - MatchByKey : Returns the first matching key found.
    // - MatchByType: Searches for the given key and
    //                continues searching if the value type does not match.
    //
    // Fails if the key cannot be found or no matching value with the expected type exists.
    //
    // Supported value types include primitives, structs, arrays, and types handled
    // via REFLECT or custom writer traits.
    template<typename T, typename K>
    inline bool read(T &out, K &&key, SearchMode mode = SearchMode::MatchByKey){
      return readInternal(out, keyArg(key), nullptr, mode);
    }


    #ifdef ARDUINO
    // The JSON buffer is always writable when using Arduino String.
    // Zero-copy string views may be used internally.
    inline bool parse(String &json){
      return parse(json.c_str(), true);
    }
    #endif

    // Parses a zero-terminated JSON document (the root must be an object).
    //
    // If writableJSONBuffer is true, read() / readExact() may modify the JSON buffer internally
    // (e.g. in-place string termination / zero-copy string views).
    // IMPORTANT: Set to false for read-only or non-writable buffers
    // (e.g. string literals, Flash/rodata). Otherwise, undefined behavior (UB) will occur.
    //
    // Strings read into const char* members:
    // - writable buffer and no escape sequences: the pointer points into the JSON buffer
    //   and stays valid as long as the buffer exists.
    // - otherwise the string is copied into one of ROTATING_STR_BUFFER_COUNT shared static
    //   buffers. The pointer is only valid until that many further strings have been read
    //   (by any JSONreader instance). Copy the string if you need it longer.
    inline bool parse(const char* json, bool writableJSONBuffer = false){
      setAllRuntimeStatusToDefault();

      if(!_storage.tokens || !_storage.keyCache) return makeParseError(JsonReaderStatus::JSON_ERR_OUT_OF_MEMORY, nullptr);
      if(isStrNullOrEmpty(json)) return makeParseError(JsonReaderStatus::JSON_ERR_EMPTY_JSON, nullptr);

      _storage.json = json;
      _storage.writableJSONBuffer = writableJSONBuffer;

      uint32_t checksum;
      const size_t jsonLen = jsonLengthAndChecksum(json, checksum);   // length + checksum in a single pass
      if(jsonLen > JSON_READER_MAX_JSON_LEN) return makeParseError(JsonReaderStatus::JSON_ERR_JSON_TOO_LARGE, nullptr);
      _storage.jsonLen = (uint16_t)jsonLen;

      JsonToken *const tokens = _storage.tokens;
      int16_t parent = -1;
      int16_t stack = 0;   // checks nesting
      uint16_t allowedMask = A_LBRACE;
      bool isKey = false;

      // Duplicate check: one 32-bit bloom filter per open object (up to 8 levels) over the key signatures.
      // A comparison only happens if the bit of a new key is already set (or with escapes / deeper nesting).
      constexpr int16_t BLOOM_LEVELS = 8;
      uint32_t bloom[BLOOM_LEVELS][4];   // 128 bits per level

      for(const char *p = json; *p; ++p){
        const char c = *p;
        if(JSON_IS_SPACE(c)) continue;

        switch(checkAllowedChars(allowedMask, c)){
          case TOKEN_STRUCT:{   // structure characters
            switch (c) {
              case ',':
                if (parent < 0) return makeParseError(JsonReaderStatus::JSON_ERR_UNEXPECTED_CHAR, p);

                if(tokens[parent].type == TOKEN_OBJECT){
                  allowedMask = A_QUOTE;
                  isKey = true;
                }else{
                  allowedMask = A_LBRACE | A_LBRACKET | A_VALUE;
                  isKey = false;
                }
                break;
              case ':':
                allowedMask = A_LBRACE | A_LBRACKET | A_VALUE;
                isKey = false;
                break;
              case '{': // OBJECT
              case '[':{ // ARRAY
                if(_storage.tokenCount >= _storage.maxTokens) return makeParseError(JsonReaderStatus::JSON_ERR_TOKEN_LIMIT, p);

                fillToken(tokens[_storage.tokenCount], c == '{' ? TOKEN_OBJECT : TOKEN_ARRAY, uint16_t(p - json), 1, parent, 0);

                parent = int16_t(_storage.tokenCount);
                _storage.tokenCount++;
                stack++;
                if(stack <= BLOOM_LEVELS) bloom[stack - 1][0] = bloom[stack - 1][1] = bloom[stack - 1][2] = bloom[stack - 1][3] = 0;

                if(c == '{'){
                  allowedMask = A_QUOTE | A_RBRACE;
                  isKey = true;
                }else{
                  allowedMask = A_LBRACE | A_LBRACKET | A_RBRACKET | A_VALUE;
                  isKey = false;
                }
                break;
              }
              case '}':
              case ']':
                if (stack <= 0 || tokens[parent].type != (c == '}' ? TOKEN_OBJECT : TOKEN_ARRAY)){
                  return makeParseError(JsonReaderStatus::JSON_ERR_BRACE_MISMATCH, p);
                }
                tokens[parent].end = int16_t(_storage.tokenCount);

                stack--;
                parent = tokens[parent].parent;

                if (parent == -1) {
                  allowedMask = 0;   // nothing allowed anymore
                } else {
                  allowedMask = A_COMMA | A_RBRACE | A_RBRACKET;
                }
                isKey = false;
                break;
            }
            break;
          }
          // keys and values
          case TOKEN_STRING:{
            if(_storage.tokenCount >= _storage.maxTokens) return makeParseError(JsonReaderStatus::JSON_ERR_TOKEN_LIMIT, p);

            const char *start = p + 1; // p still points at the opening " here
            const char *q = start;
            uint8_t flags = 0;

            const JsonReaderStatus err = validateString(q, isKey, flags);
            if(err != JsonReaderStatus::JSON_READER_OK) return makeParseError(err, q);

            JsonToken &t = tokens[_storage.tokenCount];
            if(isKey){
              if(tokens[parent].objectKeyCount >= JSON_MAX_OBJECT_KEYS || _storage.keyCount >= JSON_MAX_KEYS){
                return makeParseError(JsonReaderStatus::JSON_ERR_KEY_LIMIT, p);
              }
              tokens[parent].objectKeyCount++;
              _storage.keyCache[_storage.keyCount++] = _storage.tokenCount;
              if(flags & FLAG_ESCAPES) _storage.hasEscapedKeys = true;
              fillToken(t, TOKEN_KEY, uint16_t(start - json), uint16_t(q - start), parent, flags);
              if(checkKeyUnique(t, parent, stack <= BLOOM_LEVELS ? bloom[stack - 1] : nullptr)){
                return makeParseError(JsonReaderStatus::JSON_ERR_DUPLICATE_KEY, p, &t);
              }
            }else{
              fillToken(t, TOKEN_STRING, uint16_t(start - json), uint16_t(q - start), parent, flags);
            }

            _storage.tokenCount++;
            allowedMask = isKey ? A_COLON : (tokens[parent].type == TOKEN_ARRAY ? (A_COMMA | A_RBRACKET) : (A_COMMA | A_RBRACE));
            p = q;  // q points at the closing "
            break;
          }
          case TOKEN_NUMBER:{
            if(_storage.tokenCount >= _storage.maxTokens) return makeParseError(JsonReaderStatus::JSON_ERR_TOKEN_LIMIT, p);

            const char *q = p;
            uint8_t flags = 0;
            JsonToken &t = tokens[_storage.tokenCount];

            if(!validateNumber(q, flags)){
              const char *errPos = q;
              if(*q) q++;   // ++ to include the offending character in the error
              fillToken(t, TOKEN_NUMBER, uint16_t(p - json), uint16_t(q - p), parent, 0);
              return makeParseError(JsonReaderStatus::JSON_ERR_ILLEGAL_NUMBER, errPos, &t);
            }else if(!isLiteralEnd(*q)){
              fillToken(t, TOKEN_NUMBER, uint16_t(p - json), uint16_t(q - p), parent, 0);
              return makeParseError(JsonReaderStatus::JSON_ERR_UNEXPECTED_CHAR, q, &t);
            }

            fillToken(t, TOKEN_NUMBER, uint16_t(p - json), uint16_t(q - p), parent, flags);
            _storage.tokenCount++;
              
            allowedMask = (tokens[parent].type == TOKEN_ARRAY ? (A_COMMA | A_RBRACKET) : (A_COMMA | A_RBRACE));
            p = q - 1;
            break;
          }
          case TOKEN_BOOL:
          case TOKEN_NULL:{
            if(_storage.tokenCount >= _storage.maxTokens) return makeParseError(JsonReaderStatus::JSON_ERR_TOKEN_LIMIT, p);

            const char *kw;
            uint8_t kwLen;
            uint8_t type;
            JsonReaderStatus errCode;
            if(c == 't'){        kw = "true";  kwLen = 4; type = TOKEN_BOOL; errCode = JsonReaderStatus::JSON_ERR_ILLEGAL_BOOL; }
            else if(c == 'f'){   kw = "false"; kwLen = 5; type = TOKEN_BOOL; errCode = JsonReaderStatus::JSON_ERR_ILLEGAL_BOOL; }
            else{                kw = "null";  kwLen = 4; type = TOKEN_NULL; errCode = JsonReaderStatus::JSON_ERR_ILLEGAL_NULL; }

            uint8_t i = 0;
            while(i < kwLen && p[i] == kw[i]) i++;

            JsonToken &t = tokens[_storage.tokenCount];
            const char *q = p + i;
            if(i == kwLen && isLiteralEnd(*q)){
              fillToken(t, type, uint16_t(p - json), kwLen, parent, 0);
              _storage.tokenCount++;
              allowedMask = (tokens[parent].type == TOKEN_ARRAY ? (A_COMMA | A_RBRACKET) : (A_COMMA | A_RBRACE));
              p = q - 1;
            }else{
              // read the rest of the literal for the error message
              while(*q && !isLiteralEnd(*q)) q++;
              fillToken(t, type, uint16_t(p - json), uint16_t(q - p), parent, 0);
              return makeParseError(errCode, p + i, &t);
            }
            break;
          }
          case TOKEN_UNKNOWN:
          default:
            // After a key only ':' is allowed: any other character means the colon is missing
            if(allowedMask == A_COLON) return makeParseError(JsonReaderStatus::JSON_ERR_MISSING_COLON, p);
            return makeParseError(JsonReaderStatus::JSON_ERR_UNEXPECTED_CHAR, p);
        }
      }

      // All { / [ must be closed again
      if (stack != 0) return makeParseError(JsonReaderStatus::JSON_ERR_BRACE_MISMATCH, json + jsonLen);

      _storage.checksum = checksum;
      _runtimeStatus.isParsed = true;

      return true;
    }

private:
  REGION(Deklaration)
    enum ActionMask{
      A_QUOTE    = 1 << 0,  // "
      A_LBRACE   = 1 << 1,  // {
      A_LBRACKET = 1 << 2,  // [
      A_COMMA    = 1 << 3,  // ,
      A_COLON    = 1 << 4,  // :
      A_RBRACE   = 1 << 5,  // }
      A_RBRACKET = 1 << 6,  // ]
      A_VALUE    = 1 << 7   // "value" (string/number/bool/null)
    };
    enum TokenType : uint8_t{
      TOKEN_OBJECT,
      TOKEN_ARRAY,
      TOKEN_KEY,
      TOKEN_STRING,
      TOKEN_NUMBER,
      TOKEN_BOOL,
      TOKEN_NULL,

      TOKEN_STRUCT,
      TOKEN_UNKNOWN
    };
    enum TokenFlags : uint8_t{
      FLAG_ESCAPES = 1,   // string contains escape sequences
      FLAG_FLOAT   = 2    // number contains '.', 'e' or 'E'
    };
    struct JsonToken{
      int16_t parent;
      uint16_t start;
      uint16_t length;
      uint8_t type;
      uint8_t flags;
      int16_t end;
      uint8_t objectKeyCount;
    };

    enum PathType {
      PATH_OBJECT,
      PATH_MEMBER,
      PATH_ARRAYINDEX,
      PATH_ARRAYKEY
    };

    struct PathContext {
      struct PathEntry {
        PathType type;
        union {
          int16_t tokenIndex;  // object key or object token index
          int16_t arrayIndex;       // Array-Index
        };
        uint16_t expectedCount = 0; // arrays only
      } entries[JSON_ERR_MAX_PATH];
      uint8_t depth = 0;
      uint8_t overflow = 0;   // entries that could not be stored anymore

      void addPathEntry(PathType type, int16_t index, uint16_t expectedCount = 0){
        if (depth >= JSON_ERR_MAX_PATH){
          if(overflow < 255) overflow++;
          return;
        }

        auto &e = entries[depth++];
        e.type = type;
        if(type == PATH_ARRAYINDEX){
          e.arrayIndex = index;
        }else{
          e.tokenIndex = index;
        }
        e.expectedCount = expectedCount;
      }
      void removePathEntry(){
        if(overflow > 0) overflow--;
        else if(depth > 0) depth--;
      }
    } _path;

    // Compact snapshot of the error location (only filled when an error happens, the path string is built in getErrorPath()).
    // anchor: last token with a known position (path = chain of ancestors of this token in the token tree).
    // Followed by array indices without a token (element errors) and optionally a key that is not in the JSON (missing key).
    struct ErrorPathSnap{
      int16_t anchor;
      uint16_t idx[4];
      uint8_t idxCount;
      bool idxTruncated;
      bool overflow;      // path stack was full: path possibly incomplete
      bool hasLeaf;
      bool valid;
      char leaf[JSON_MAX_KEY_LEN + 1];
    } _errPath = {};

    enum ErrorPriority{
      PARSE = 100,
      STRUCT = 90,
      OBJECT = 80,
      ARRAY = 70,
      VALUE = 60,
      MISSING_KEY = 50,
      NONE = 0
    };
    struct JsonReaderResult{
      JsonReaderStatus code;     // error code
      ErrorPriority priority;
      char message[JSON_ERR_MAX_MSG] = {0};
      bool isParsed;
      bool hasParseError;
      bool hasReadError;
    } _runtimeStatus;

    struct ReaderStorage {
      const char* json;
      JsonToken* tokens;
      uint16_t* keyCache;

      uint16_t maxTokens;
      uint16_t tokenCount;
      uint16_t keyCount;
      bool hasEscapedKeys;    // at least one key contains escapes (rare): comparison then goes through decoding

      uint16_t jsonLen;
      uint32_t checksum;
      uint16_t strPosAdjust;

      bool writableJSONBuffer;
      bool verifyBuffer;
      bool createErrorInfo;
    } _storage;
    JsonConfig _config;
  ENDREGION()
  
  REGION(parse)
    static inline void fillToken(JsonToken &t, uint8_t type, uint16_t start, uint16_t length, int16_t parent, uint8_t flags){
      t.parent = parent;
      t.start = start;
      t.length = length;
      t.type = type;
      t.flags = flags;
      t.end = -1;
      t.objectKeyCount = 0;
    }

    static inline bool isLiteralEnd(const char c){
      return c == '\0' || c == ',' || c == ']' || c == '}' || JSON_IS_SPACE(c);
    }

    inline TokenType checkAllowedChars(uint16_t mask, char c) const {
      if (mask & A_VALUE) {
        switch (c) {
          case '"': return TOKEN_STRING;
          case '-': return TOKEN_NUMBER;
          case 't':
          case 'f': return TOKEN_BOOL;
          case 'n': return TOKEN_NULL;
          default: if (JSON_IS_DIGIT(c)) return TOKEN_NUMBER;
        }
      }

      // Strukturzeichen
      if ((mask & A_QUOTE)    && c == '"') return TOKEN_STRING;
      if ((mask & A_LBRACE)   && c == '{') return TOKEN_STRUCT;
      if ((mask & A_LBRACKET) && c == '[') return TOKEN_STRUCT;
      if ((mask & A_COMMA)    && c == ',') return TOKEN_STRUCT;
      if ((mask & A_COLON)    && c == ':') return TOKEN_STRUCT;
      if ((mask & A_RBRACE)   && c == '}') return TOKEN_STRUCT;
      if ((mask & A_RBRACKET) && c == ']') return TOKEN_STRUCT;

      return TOKEN_UNKNOWN;  // character not allowed
    }

    // Compares two key tokens (keys with escapes are decoded first: "a" == "a")
    inline bool keyTokensEqual(const JsonToken &a, const JsonToken &b) const {
      if(!((a.flags | b.flags) & FLAG_ESCAPES)){
        return a.length == b.length && bytesEqual(_storage.json + a.start, _storage.json + b.start, a.length);
      }
      char ta[JSON_MAX_KEY_LEN + 1];
      char tb[JSON_MAX_KEY_LEN + 1];
      size_t na = 0, nb = 0;
      if(!decodeJsonString(a, ta, sizeof(ta), na) || !decodeJsonString(b, tb, sizeof(tb), nb)) return false;
      return na == nb && bytesEqual(ta, tb, na);
    }
    // Short signature of an (unescaped) key from its length, first and last character
    static FORCE_INLINE uint8_t keySignature(const char *s, const uint16_t len){
      return len ? uint8_t(len * 7u + (uint8_t)s[0] * 3u + (uint8_t)s[len - 1]) : uint8_t(0);
    }
    // Checks the key t that was just created for duplicates in its object. bloom points to the bloom filter of the
    // object (nullptr with deep nesting). Returns true = duplicate.
    JSON_NOINLINE bool checkKeyUnique(const JsonToken &t, const int16_t parent, uint32_t *bloom) const {
      const bool first = _storage.tokens[parent].objectKeyCount <= 1;
      bool maybeDuplicate = !first;
      if(bloom && !_storage.hasEscapedKeys){
        const uint8_t h = keySignature(_storage.json + t.start, t.length) & 127u;
        const uint32_t bit = uint32_t(1) << (h & 31u);
        uint32_t &word = bloom[h >> 5];
        maybeDuplicate = !first && (word & bit) != 0;
        word |= bit;
      }
      return maybeDuplicate && hasDuplicateKey(t, parent);
    }

    // Is there already a key like t among the earlier keys of the same object? t is the most recently created key.
    // Slow path: only called when the bloom filter in the parser reports a possible hit.
    inline bool hasDuplicateKey(const JsonToken &t, const int16_t parent) const {
      const JsonToken *const tokens = _storage.tokens;
      const uint16_t *const cache = _storage.keyCache;
      const char *const tp = _storage.json + t.start;
      const bool plain = !_storage.hasEscapedKeys;      // without escapes length + byte comparison is enough
      uint8_t remaining = uint8_t(tokens[parent].objectKeyCount - 1);   // earlier keys of this object

      for(int16_t kk = int16_t(_storage.keyCount) - 2; kk >= 0 && remaining > 0; kk--){
        const JsonToken &o = tokens[cache[kk]];
        if(o.parent != parent) continue;
        remaining--;
        if(plain){
          if(o.length == t.length && bytesEqual(_storage.json + o.start, tp, t.length)) return true;
        }else if(keyTokensEqual(o, t)){
          return true;
        }
      }
      return false;
    }

    // Validates a string from p (after the opening ") up to the closing ".
    // The length limit applies to the decoded length in bytes.
    // On success p points at the closing ".
    inline JsonReaderStatus validateString(const char *&p, const bool isKey, uint8_t &flags){
      const size_t maxLen = isKey ? JSON_MAX_KEY_LEN : JSON_MAX_STRING_LEN;
      size_t len = 0;
      flags = 0;

      for(;;){
        const unsigned char uc = (unsigned char)*p;

        if(uc == '"') break;
        if(uc == '\0') return JsonReaderStatus::JSON_ERR_MISSING_QUOTE;

        if(uc == '\\'){
          flags |= FLAG_ESCAPES;
          p++;

          switch(*p){
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
              len++;
              _storage.strPosAdjust++;  // an escape sequence counts as 1 character
              break;
            case 'u':{
              const char *before = p;
              const uint8_t n = jsonDecodeUnicodeEscape(p, nullptr);
              if(n == 0) return JsonReaderStatus::JSON_ERR_ILLEGAL_ESCAPE;
              len += n;
              _storage.strPosAdjust += uint16_t((p - before) + 1);
              break;
            }
            case '\0':
              return JsonReaderStatus::JSON_ERR_MISSING_QUOTE;
            default:
              return JsonReaderStatus::JSON_ERR_ILLEGAL_ESCAPE;
          }
        }else if(uc < 0x20){
          return JsonReaderStatus::JSON_ERR_UNEXPECTED_CHAR;   // control characters must be escaped
        }else if(uc < 0x80){
          len++;
        }else{
          const uint8_t n = jsonUtf8SeqLen((const unsigned char*)p);
          if(n == 0) return JsonReaderStatus::JSON_ERR_INVALID_UTF8;
          len += n;
          _storage.strPosAdjust += uint16_t(n - 1);  // UTF-8 multibyte = 1 char
          p += n - 1;
        }

        p++;
        if(len > maxLen) return isKey ? JsonReaderStatus::JSON_ERR_KEY_LEN_LIMIT : JsonReaderStatus::JSON_ERR_STRING_LEN_LIMIT;
      }

      return JsonReaderStatus::JSON_READER_OK;
    }

    // Validates a number according to the JSON grammar. Afterwards p points behind the number (or at the error).
    inline bool validateNumber(const char *&p, uint8_t &flags) const {
      flags = 0;

      if(*p == '-'){
        p++;
        if(!JSON_IS_DIGIT(*p)){
          p--;
          return false;  // -- not allowed
        }
      }

      if(*p == '0'){
        p++;
        if(JSON_IS_DIGIT(*p)){
          p--;
          return false; // leading 0 not allowed
        }
      }

      while(JSON_IS_DIGIT(*p)) p++;
      if(*p == '.'){  // decimal places
        flags |= FLAG_FLOAT;
        p++;
        if(!JSON_IS_DIGIT(*p)){
          p--;
          return false; // no digit after .
        }
        while(JSON_IS_DIGIT(*p)) p++;
      }

      if (*p == 'e' || *p == 'E') {
        flags |= FLAG_FLOAT;
        p++;
        if (*p == '+' || *p == '-') {
          p++;
        }
        if (!JSON_IS_DIGIT(*p)) return false;
        while (JSON_IS_DIGIT(*p)) p++;
      }

      return true;
    }

    // Position-weighted sum over 32-bit words: a change of a single byte is always
    // detected and can be tracked in O(1) (see jsonChecksumZeroByte / assignString).
    // Layout: bytes up to the next 4-byte boundary, then whole words, then the remaining bytes.
    // Words instead of bytes: about 3-4x faster on small MCUs.
    static inline size_t jsonChecksumPrefix(const char *s, const size_t n){
      const size_t pre = (size_t)((4u - ((uintptr_t)s & 3u)) & 3u);
      return pre < n ? pre : n;
    }
    static inline uint32_t jsonLoadWord(const char *alignedPtr){
      uint32_t v;
#if defined(__GNUC__)
      alignedPtr = (const char*)__builtin_assume_aligned(alignedPtr, 4);
#endif
      memcpy(&v, alignedPtr, sizeof(v));
      return v;
    }
    // Determines length and checksum in ONE pass (replaces strlen + jsonChecksum).
    // Result is identical to jsonChecksum(s, strlen(s)). Stops as soon as the length exceeds JSON_READER_MAX_JSON_LEN
    // (the return value is then > JSON_READER_MAX_JSON_LEN and the sum is unusable).
    // Note: like strlen() this function reads whole, aligned words. The last word may contain bytes behind
    // the terminator; an aligned word always lies completely inside the same memory region.
#if defined(__clang__)
    __attribute__((no_sanitize("address")))
#elif defined(__GNUC__) && (__GNUC__ >= 8)
    __attribute__((no_sanitize_address))
#endif
    static inline size_t jsonLengthAndChecksum(const char *s, uint32_t &outSum){
      uint32_t sum = 0;
      uint32_t w = 1;
      size_t i = 0;
      const size_t pre = (size_t)((4u - ((uintptr_t)s & 3u)) & 3u);

      for(; i < pre; i++){
        const uint8_t c = (uint8_t)s[i];
        if(c == 0){
          outSum = sum;
          return i;
        }
        sum += (uint32_t)c * w;
        w += 2;
      }

      const char *p = s + pre;
      w = 1;
      for(;;){
        uint32_t v;
        memcpy(&v, __builtin_assume_aligned(p, 4), sizeof(v));
        if(((v - 0x01010101u) & ~v & 0x80808080u) != 0) break;   // contains a 0 byte
        sum += v * w;
        w += 2;
        p += 4;
        i += 4;
        if(i > JSON_READER_MAX_JSON_LEN){
          outSum = sum;
          return i;
        }
      }

      w = 1;
      while(*p){
        sum += (uint32_t)(uint8_t)*p * w;
        w += 2;
        p++;
        i++;
      }
      outSum = sum;
      return i;
    }
    static inline uint32_t jsonChecksum(const char *s, const size_t n){
      const size_t pre = jsonChecksumPrefix(s, n);
      const size_t words = (n - pre) / 4;
      uint32_t sum = 0;
      uint32_t w = 1;
      for(size_t i = 0; i < pre; i++){
        sum += (uint32_t)(uint8_t)s[i] * w;
        w += 2;
      }
      const char *p = s + pre;
      w = 1;
      for(size_t k = 0; k < words; k++){
        sum += jsonLoadWord(p) * w;
        p += 4;
        w += 2;
      }
      w = 1;
      for(size_t i = pre + words * 4; i < n; i++){
        sum += (uint32_t)(uint8_t)s[i] * w;
        w += 2;
      }
      return sum;
    }
    // Returns the checksum that results when s[pos] (currently non-zero) is set to 0.
    // Must be called BEFORE overwriting. Independent of byte order.
    static inline uint32_t jsonChecksumZeroByte(uint32_t sum, const char *s, const size_t n, const size_t pos){
      const size_t pre = jsonChecksumPrefix(s, n);
      const size_t words = (n - pre) / 4;
      if(pos < pre){
        return sum - (uint32_t)(uint8_t)s[pos] * (uint32_t)(2 * pos + 1);
      }
      const size_t q = pos - pre;
      if(q < words * 4){
        const size_t k = q / 4;
        const uint32_t before = jsonLoadWord(s + pre + k * 4);
        uint32_t after = before;
        ((uint8_t*)&after)[q & 3] = 0;
        return sum - (before - after) * (uint32_t)(2 * k + 1);
      }
      const size_t j = q - words * 4;
      return sum - (uint32_t)(uint8_t)s[pos] * (uint32_t)(2 * j + 1);
    }
  ENDREGION()

  REGION(read)
    #ifdef ARDUINO
    inline const char* keyArg(const String &k) {
      return k.c_str();
    }
    #endif
    inline const char* keyArg(const char *k) {
      return k;
    }

    // Common precheck of all read functions
    inline bool beginRead(const char *key){
      if(_runtimeStatus.hasParseError) return false;

      setReadRuntimeStatusToDefault();

      if(!_runtimeStatus.isParsed){
        return makeError(JsonReaderStatus::JSON_ERR_JSON_NOT_PARSED, PARSE, nullptr);
      }else if(_storage.verifyBuffer && _storage.checksum != jsonChecksum(_storage.json, _storage.jsonLen)){
        return makeError(JsonReaderStatus::JSON_ERR_JSON_CHANGED, PARSE, nullptr);
      }else if(!key){
        return makeError(JsonReaderStatus::JSON_ERR_INVALID_KEY, STRUCT, "key");
      }else if(_storage.tokenCount == 0 || _storage.tokens[0].type != TOKEN_OBJECT){
        return makeError(JsonReaderStatus::JSON_ERR_NOT_FOUND, STRUCT, "object", "root");
      }
      return true;
    }

    template<typename T, typename... Keys>
    bool readExactInternal(T &out, const char *key, Keys... path) {
      if(!beginRead(key)) return false;

      int16_t parentTok = 0;

      if constexpr (sizeof...(path) > 0){
        // every path element must be an object directly below the previous one
        auto resolveObject = [&](const char* objKey) -> bool {
          if(!objKey){
            return makeError(JsonReaderStatus::JSON_ERR_INVALID_PATH, STRUCT, "object");
          }

          uint16_t from = 0;
          int16_t lastBad = -1;
          uint8_t mismatchTyp = 0;
          const int16_t tok = findValue(objKey, parentTok, PATH_OBJECT, SearchMode::MatchByKey, from, lastBad, mismatchTyp);
          if(tok < 0) return failFind(tok, PATH_OBJECT, lastBad, mismatchTyp, "object", objKey);

          parentTok = tok;
          return true;
        };

        if(!(resolveObject(path) && ...)) return false;
      }

      static_assert(!is_json_fixed_v<remove_cvref_t<T>> && !is_json_number_v<remove_cvref_t<T>>,
                    "JsonFixed/JsonNumber cannot be read directly (they only reference a variable). "
                    "Use a struct member instead: struct Data { float x; REFLECT(MEMBER(x, fx(x, 2))) };");

      if constexpr (is_json_fixed_v<remove_cvref_t<T>> || is_json_number_v<remove_cvref_t<T>>){
        return false;   // never reached: the static_assert above already aborts earlier (prevents follow-up errors)
      }else{
        T tmpOut{};
        copyValue(tmpOut, out);

        if(!getData(tmpOut, key, parentTok, SearchMode::MatchByKey)) return false;

        copyValue(out, tmpOut);
        return true;
      }
    }
    template<typename T>
    bool readInternal(T &out, const char *key, const char* keyPath = nullptr, SearchMode mode = SearchMode::MatchByKey) {
      if(!beginRead(key)) return false;

      int16_t parentTok = -1;  // search the whole json

      if(keyPath){
        uint16_t from = 0;
        int16_t lastBad = -1;
        uint8_t mismatchTyp = 0;
        parentTok = findValue(keyPath, -1, PATH_OBJECT, mode, from, lastBad, mismatchTyp);
        if(parentTok < 0) return failFind(parentTok, PATH_OBJECT, lastBad, mismatchTyp, "object", keyPath);
      }

      static_assert(!is_json_fixed_v<remove_cvref_t<T>> && !is_json_number_v<remove_cvref_t<T>>,
                    "JsonFixed/JsonNumber cannot be read directly (they only reference a variable). "
                    "Use a struct member instead: struct Data { float x; REFLECT(MEMBER(x, fx(x, 2))) };");

      if constexpr (is_json_fixed_v<remove_cvref_t<T>> || is_json_number_v<remove_cvref_t<T>>){
        return false;   // never reached: the static_assert above already aborts earlier (prevents follow-up errors)
      }else{
        T tmpOut{};
        copyValue(tmpOut, out);

        if(!getData(tmpOut, key, parentTok, mode)) return false;

        copyValue(out, tmpOut);
        return true;
      }
    }

    // Reports the result of a failed findValue() search
    inline bool failFind(const int16_t rc, const PathType want, const int16_t lastBad, const uint8_t mismatchTyp, const char *typeName, const char *key){
      if(rc == -2){
        // Key found, but wrong type (MatchByKey). The path entry was created in findValue()
        return makeError(JsonReaderStatus::JSON_ERR_TYPE_MISMATCH, VALUE, typeName, key, jsonTypeName(mismatchTyp));
      }
      if(lastBad >= 0){
        // Key found, but no value with a matching type (MatchByType)
        _path.addPathEntry(want, lastBad);
        return makeError(JsonReaderStatus::JSON_ERR_KEY_FOUND_TYPE_MISSING, VALUE, typeName, key, jsonTypeName(_storage.tokens[lastBad].type));
      }
      _path.addPathEntry(want, -1);
      return makeError(JsonReaderStatus::JSON_ERR_NOT_FOUND, STRUCT, typeName, key);
    }

    template<typename T>
    inline bool getData(T &out, const char *key, int16_t parentToken, SearchMode mode) {
      uint8_t mismatchTyp = 0;
      uint16_t from = 0;
      int16_t lastBad = -1;

      if constexpr (is_json_optional_v<T>){
        // std::optional<V>: null -> empty. Otherwise like V: MatchByKey takes the first key with this name,
        // MatchByType tries keys with the same name one after another until one matches the type V.
        using V = typename json_optional_value<T>::type;
        constexpr CTypeKind kv = deduceCType<V>();
        const size_t keyLen = strlen(key);
        FieldRef<T&> fr{key, uint8_t(keyLen > JSON_MAX_KEY_LEN ? JSON_MAX_KEY_LEN : keyLen), out};

        for(uint16_t kk = 0; kk < _storage.keyCount; kk++){
          const JsonToken &kt = _storage.tokens[_storage.keyCache[kk]];
          if(parentToken >= 0 && kt.parent != parentToken) continue;
          if(!keyEquals(kt, key, keyLen)) continue;

          const int16_t tok = int16_t(_storage.keyCache[kk] + 1);
          const JsonToken &val = _storage.tokens[tok];

          if(val.type == TOKEN_NULL){
            out.reset();
            return true;
          }
          if(mode != SearchMode::MatchByType) return readMemberAt(fr, tok);

          if constexpr (kv == CTypeKind::Object || kv == CTypeKind::Array){
            if(val.type != (kv == CTypeKind::Object ? TOKEN_OBJECT : TOKEN_ARRAY)){
              lastBad = tok;
              continue;
            }
            return readMemberAt(fr, tok);
          }else{
            V tmp = out.has_value() ? *out : V{};
            bool retry = false;
            if(readScalar(tmp, tok, key, true, retry)){
              out = tmp;
              return true;
            }
            if(!retry) return false;
            lastBad = tok;
          }
        }
        return failFind(-1, PATH_MEMBER, lastBad, 0, cppTypeName<T>(), key);
      }else{
      constexpr CTypeKind k = deduceCType<T>();

      if constexpr (k == CTypeKind::Object) {
        const int16_t tok = findValue(key, parentToken, PATH_OBJECT, mode, from, lastBad, mismatchTyp);
        if(tok < 0) return failFind(tok, PATH_OBJECT, lastBad, mismatchTyp, cppTypeName<T>(true), key);

        return readStruct<T>(tok, out);
      }else if constexpr (k == CTypeKind::Array) {
        constexpr size_t N = std::extent_v<T>;
        using Elem = std::remove_extent_t<T>;

        const int16_t tok = findValue(key, parentToken, PATH_ARRAYKEY, mode, from, lastBad, mismatchTyp);
        if(tok < 0) return failFind(tok, PATH_ARRAYKEY, lastBad, mismatchTyp, cppTypeName<T>(true), key);

        return readArray<Elem, N>(tok, out);
      }else{
        for(;;){
          const int16_t tok = findValue(key, parentToken, PATH_MEMBER, mode, from, lastBad, mismatchTyp);
          if(tok < 0) return failFind(tok, PATH_MEMBER, lastBad, mismatchTyp, cppTypeName<T>(), key);

          bool retry = false;
          if(readScalar(out, tok, key, mode == SearchMode::MatchByType, retry)) return true;
          if(!retry) return false;

          // MatchByType: try the next key with the same name (from already points behind the hit)
          lastBad = tok;
        }
      }
      }
    }

    // Reads a scalar value (everything except struct/array) from token valTok.
    //
    // key == nullptr means: array element.
    // allowRetry (MatchByType): on a type conflict no error, but retry = true and return false.
    template<typename T>
    bool readScalar(T &out, const int16_t valTok, const char *key, const bool allowRetry, bool &retry){
      const JsonToken &val = _storage.tokens[valTok];

      auto mismatch = [&](const char *jsonTyp) -> bool {
        if(allowRetry){
          retry = true;
          return false;
        }
        if(key) _path.addPathEntry(PATH_MEMBER, valTok);
        return makeError(JsonReaderStatus::JSON_ERR_TYPE_MISMATCH, VALUE, cppTypeName<T>(), key, jsonTyp);
      };
      auto fail = [&](JsonReaderStatus code) -> bool {
        if(key) _path.addPathEntry(PATH_MEMBER, valTok);
        return makeError(code, VALUE, cppTypeName<T>(), key, jsonTypeName(val.type));
      };

      constexpr CTypeKind k = deduceCType<T>();
      if constexpr (k == CTypeKind::Bool) {
        if(val.type != TOKEN_BOOL) return mismatch(jsonTypeName(val.type));
        out = (_storage.json[val.start] == 't');
      }else if constexpr (k == CTypeKind::Char) {
        if(val.type != TOKEN_STRING) return mismatch(jsonTypeName(val.type));

        if(!decodeSingleChar(val, out)){
          if(allowRetry){
            retry = true;
            return false;
          }
          return fail(JsonReaderStatus::JSON_ERR_INVALID_VALUE);
        }
      }else if constexpr (k == CTypeKind::String) {
        if(val.type == TOKEN_NULL){
          out = nullptr;
          return true;
        }
        if(val.type != TOKEN_STRING) return mismatch(jsonTypeName(val.type));

        if(!assignString(out, val)) return fail(JsonReaderStatus::JSON_ERR_INVALID_VALUE);
      #ifdef ARDUINO
      }else if constexpr (k == CTypeKind::StringArduino){
        if(val.type != TOKEN_STRING) return mismatch(jsonTypeName(val.type));

        const char *tmp = nullptr;
        if(!assignString(tmp, val)) return fail(JsonReaderStatus::JSON_ERR_INVALID_VALUE);
        out = tmp;
      #endif
      }else if constexpr (k == CTypeKind::Enum || k == CTypeKind::Int){
        if(val.type != TOKEN_NUMBER) return mismatch(jsonTypeName(val.type));
        if(val.flags & FLAG_FLOAT) return mismatch("float");

        int8_t rc;
        if constexpr (k == CTypeKind::Enum){
          using U = std::underlying_type_t<T>;
          U tmp = 0;
          rc = assignInteger(tmp, val);
          if(rc == 0) out = static_cast<T>(tmp);
        }else{
          rc = assignInteger(out, val);
        }
        if(rc != 0) return fail(rc < 0 ? JsonReaderStatus::JSON_ERR_TYPE_UNDERFLOW : JsonReaderStatus::JSON_ERR_TYPE_OVERFLOW);
      }else if constexpr (k == CTypeKind::Float || k == CTypeKind::FloatDefault || k == CTypeKind::FloatFixedPoint) {
        if(val.type != TOKEN_NUMBER) return mismatch(jsonTypeName(val.type));
        // fixed-point is stored in the JSON as an integer
        if(k == CTypeKind::FloatFixedPoint && (val.flags & FLAG_FLOAT)) return mismatch("float");

        const int8_t rc = assignFloat(out, val);
        if(rc != 0) return fail(rc < 0 ? JsonReaderStatus::JSON_ERR_TYPE_UNDERFLOW : JsonReaderStatus::JSON_ERR_TYPE_OVERFLOW);
      }else{
        static_assert(always_false<T>, "readScalar: unsupported type");
      }

      return true;
    }

    // Reads an array element (any type) from token tok
    template<typename Elem>
    inline bool readElement(Elem &out, const int16_t tok){
      const JsonToken &val = _storage.tokens[tok];

      if constexpr (is_json_optional_v<Elem>){
        using V = typename json_optional_value<Elem>::type;
        if(val.type == TOKEN_NULL){
          out.reset();
          return true;
        }
        V tmp = out.has_value() ? *out : V{};
        if(!readElement<V>(tmp, tok)) return false;
        out = tmp;
        return true;
      }else{
      constexpr CTypeKind k = deduceCType<Elem>();
      if constexpr (k == CTypeKind::Object){
        if(val.type != TOKEN_OBJECT){
          return makeError(JsonReaderStatus::JSON_ERR_TYPE_MISMATCH, VALUE, cppTypeName<Elem>(), nullptr, jsonTypeName(val.type));
        }
        return readStruct<Elem>(tok, out);
      }else if constexpr (k == CTypeKind::Array){
        using Inner = std::remove_extent_t<Elem>;
        constexpr size_t M = std::extent_v<Elem>;

        if(val.type != TOKEN_ARRAY){
          return makeError(JsonReaderStatus::JSON_ERR_TYPE_MISMATCH, VALUE, cppTypeName<Inner>(), nullptr, jsonTypeName(val.type));
        }
        return readArray<Inner, M>(tok, out);
      }else{
        bool retry = false;
        return readScalar(out, tok, nullptr, false, retry);
      }
      }
    }

    // Reads a struct member (the key is searched from hint to the end, then from the start)
    template<typename FR>
    inline bool readMember(const int16_t parentTok, FR &f, uint16_t &hint){
      if constexpr (is_reflect_comment_v<FR>){
        (void)parentTok; (void)f; (void)hint;
        return true;                    // COMMENT(...) is for INI only
      }else{
        return readMemberField(parentTok, f, hint);
      }
    }
    template<typename FR>
    inline bool readMemberField(const int16_t parentTok, FR &f, uint16_t &hint){
      using Member = remove_cvref_t<decltype(f.value)>;

      const int16_t valTok = findMember(f.key, f.keyLen, parentTok, hint);
      if(valTok < 0){
        // explicitNulls == false: a missing key is not an error, the variable keeps its default
        if(!_config.explicitNulls) return true;
        _path.addPathEntry(PATH_MEMBER, -1);
        return makeError(JsonReaderStatus::JSON_ERR_KEY_MISSING, MISSING_KEY, cppTypeName<Member>(), f.key);
      }
      return readMemberAt(f, valTok);
    }

    // Reads the value in token valTok into the struct member f.
    template<typename FR>
    inline bool readMemberAt(FR &f, const int16_t valTok){
      using Member = remove_cvref_t<decltype(f.value)>;
      Member &out = f.value;
      const JsonToken &val = _storage.tokens[valTok];

      if constexpr (is_json_optional_v<Member>){
        // nullable type: null -> empty, otherwise read the value (on a copy, so an error does not change the member)
        using V = typename json_optional_value<Member>::type;
        if(val.type == TOKEN_NULL){
          out.reset();
          return true;
        }
        V tmp = out.has_value() ? *out : V{};
        FieldRef<V&> inner{f.key, f.keyLen, tmp};
        if(!readMemberAt(inner, valTok)) return false;
        out = tmp;
        return true;
      }else{
        constexpr CTypeKind k = deduceCType<Member>();

        // coerceInputValues: null for a non-nullable member -> keep default (const char* takes null itself as nullptr)
        if(val.type == TOKEN_NULL && k != CTypeKind::String && _config.coerceInputValues) return true;

        if constexpr (k == CTypeKind::Object){
          if(val.type != TOKEN_OBJECT){
            _path.addPathEntry(PATH_MEMBER, valTok);
            return makeError(JsonReaderStatus::JSON_ERR_TYPE_MISMATCH, VALUE, cppTypeName<Member>(), f.key, jsonTypeName(val.type));
          }
          _path.addPathEntry(PATH_OBJECT, valTok);
          const bool ok = readStruct<Member>(valTok, out);
          _path.removePathEntry();
          return ok;
        }else if constexpr (k == CTypeKind::Array){
          using Elem = std::remove_extent_t<Member>;
          constexpr size_t N = std::extent_v<Member>;

          if(val.type != TOKEN_ARRAY){
            _path.addPathEntry(PATH_MEMBER, valTok);
            return makeError(JsonReaderStatus::JSON_ERR_TYPE_MISMATCH, VALUE, cppTypeName<Elem>(), f.key, jsonTypeName(val.type));
          }

          _path.addPathEntry(PATH_ARRAYKEY, valTok);
          const bool ok = readArray<Elem, N>(valTok, out);
          _path.removePathEntry();
          return ok;
        }else{
          bool retry = false;
          return readScalar(out, valTok, f.key, false, retry);
        }
      }
    }

    template<typename T>
    inline bool readStruct(const int16_t parentTok, T &out){
      uint8_t structMemberCount = 0;
      bool ok = true;
      uint16_t hint = 0;

      out.reflect([&](auto&&... fields){
        static_assert(reflect_member_count_v<decltype(fields)...> <= JSON_MAX_OBJECT_KEYS, "Struct exceeds JSON_MAX_OBJECT_KEYS");
        structMemberCount = reflect_member_count_v<decltype(fields)...>;

        ok = (... && (!_runtimeStatus.hasReadError && readMember(parentTok, fields, hint)));
      });

      if(!ok) return false;
      if(_config.ignoreUnknownKeys) return true;
      // Without missing keys (explicitNulls == true) the same count means: there are no unknown keys
      if(_config.explicitNulls && _storage.tokens[parentTok].objectKeyCount == structMemberCount) return true;
      return checkExtraKeys(parentTok, out);
    }

    template<typename Elem, size_t N>
    inline bool readArray(const int16_t parentTok, Elem (&arr)[N]) {
      size_t count = 0;
      const int16_t end = _storage.tokens[parentTok].end;
      int16_t i = parentTok + 1;

      while(i < end){
        const JsonToken &val = _storage.tokens[i];
        // Skip nested containers: only visit direct children
        const int16_t next = (val.type == TOKEN_OBJECT || val.type == TOKEN_ARRAY) ? val.end : int16_t(i + 1);

        if(count < N){
          _path.addPathEntry(PATH_ARRAYINDEX, (int16_t)count, (uint16_t)N);
          const bool ok = readElement<Elem>(arr[count], i);
          _path.removePathEntry();
          if(!ok) return false;
        }
        count++;   // keep counting for the error message
        i = next;
      }

      if (count < N){
        _path.addPathEntry(PATH_ARRAYINDEX, (int16_t)count, (uint16_t)N);
        return makeError(JsonReaderStatus::JSON_ERR_ARRAY_TOO_FEW_ELEMENTS, ARRAY, "array", nullptr, nullptr);
      }else if(count > N){
        _path.addPathEntry(PATH_ARRAYINDEX, (int16_t)N, (uint16_t)N);
        return makeError(JsonReaderStatus::JSON_ERR_ARRAY_TOO_MANY_ELEMENTS, ARRAY, "array", nullptr, nullptr);
      }

      return true;
    }

    template<typename T>
    inline bool checkExtraKeys(const int16_t parentTok, const T &out) {
      const char* structKeys[JSON_MAX_OBJECT_KEYS];
      uint8_t structKeyLen[JSON_MAX_OBJECT_KEYS];
      uint8_t structMemberCount = 0;

      out.reflect([&](auto&&... fields) {
        ([&](const auto &f){
          if constexpr (!is_reflect_comment_v<decltype(f)>){
            structKeys[structMemberCount] = f.key;
            structKeyLen[structMemberCount++] = f.keyLen;
          }
        }(fields), ...);
      });

      const int16_t end = _storage.tokens[parentTok].end;
      int16_t i = parentTok + 1;

      // Children of an object alternate between key and value
      while(i < end){
        const JsonToken &keyTok = _storage.tokens[i];
        const int16_t valIdx = i + 1;
        const JsonToken &valTok = _storage.tokens[valIdx];

        bool known = false;
        for (uint16_t s = 0; s < structMemberCount; s++) {
          if (keyEquals(keyTok, structKeys[s], structKeyLen[s])) {
            known = true;
            break;
          }
        }

        if(!known){
          char keyBuf[64];
          const char* jsonKey = getTokenValStr(keyTok);
          strncpy(keyBuf, jsonKey, sizeof(keyBuf) - 1);
          keyBuf[sizeof(keyBuf) - 1] = '\0';

          _path.addPathEntry(PATH_MEMBER, valIdx);
          return makeError(JsonReaderStatus::JSON_ERR_EXTRA_KEY, STRUCT, "key", keyBuf, jsonTypeName(valTok.type));
        }

        i = (valTok.type == TOKEN_OBJECT || valTok.type == TOKEN_ARRAY) ? valTok.end : int16_t(valIdx + 1);
      }

      return true;
    }

    template<typename X>
    inline void copyValue(X &dst, const X &src) {
      dst = src;
    }
    template<typename E, size_t N>
    inline void copyValue(E (&dst)[N], const E (&src)[N]) {
      if constexpr (std::is_trivially_copyable_v<E>) {
        memcpy(dst, src, sizeof(E) * N);
      } else {
        for(size_t i = 0; i < N; ++i) {
          dst[i] = src[i];
        }
      }
    }
  ENDREGION()
    
  REGION(assign)
    // Returns: 0 = ok, -1 = underflow, 1 = overflow
    template<typename T, typename = std::enable_if_t<std::is_floating_point_v<T>>>
    inline int8_t assignFloat(T &out, const JsonToken &t) const {
      const double v = parseDouble(t);
      if(v > (double)(std::numeric_limits<T>::max)()) return 1;
      if(v < (double)std::numeric_limits<T>::lowest()) return -1;   // also catches -inf
      out = static_cast<T>(v);
      return 0;
    }
    template<typename T>
    inline int8_t assignFloat(JsonNumber<T> &out, const JsonToken &t) const {
      double v = parseDouble(t);
      if(v > (double)(std::numeric_limits<T>::max)()) return 1;
      if(v < (double)std::numeric_limits<T>::lowest()) return -1;

      const double scale = JSON_POW10[jsonClampPrecision(out.precision)];
      const double s = v * scale;
      // round to the requested number of decimal places (rounding is pointless for huge values)
      if(s > -9.0e18 && s < 9.0e18){
        v = (double)(int64_t)(s + (s >= 0 ? 0.5 : -0.5)) / scale;
      }

      out.value = static_cast<T>(v);
      return 0;
    }
    template<typename T>
    inline int8_t assignFloat(JsonFixed<T> &out, const JsonToken &t) const {
      bool neg = false;
      uint64_t mag = 0;
      if(!parseMagnitude(t, neg, mag)) return neg ? -1 : 1;

      const double raw = neg ? -(double)mag : (double)mag;
      out.value = static_cast<T>(raw / JSON_POW10[jsonClampPrecision(out.precision)]);
      return 0;
    }

    // Returns: 0 = ok, -1 = underflow, 1 = overflow. out stays unchanged on error.
    template<typename T>
    inline int8_t assignInteger(T &out, const JsonToken &t) const {
      bool neg = false;
      uint64_t mag = 0;
      if(!parseMagnitude(t, neg, mag)) return neg ? -1 : 1;

      constexpr uint64_t maxv = (uint64_t)(std::numeric_limits<T>::max)();

      if constexpr (std::is_unsigned_v<T>){
        if(neg && mag != 0) return -1;     // "-0" is allowed
        if(mag > maxv) return 1;
        out = static_cast<T>(mag);
      }else{
        if(neg){
          if(mag > maxv + 1) return -1;
          out = (mag == maxv + 1) ? (std::numeric_limits<T>::min)() : static_cast<T>(-(int64_t)mag);
        }else{
          if(mag > maxv) return 1;
          out = static_cast<T>(mag);
        }
      }
      return 0;
    }

    inline bool assignString(const char *&out, const JsonToken &t) {
      // FAST PATH: no escape, writable buffer -> pointer directly into the JSON buffer
      if (!(t.flags & FLAG_ESCAPES) && _storage.writableJSONBuffer) {
        char* p = const_cast<char*>(_storage.json) + t.start;
        char* e = p + t.length;
        if(*e != '\0'){
          // overwrite the closing " and update the checksum (the new byte is 0)
          _storage.checksum = jsonChecksumZeroByte(_storage.checksum, _storage.json, _storage.jsonLen, (size_t)(t.start + t.length));
          *e = '\0';
        }
        out = p;
        return true;
      }
      
      // SLOW PATH: escapes / read-only buffer -> copy into a rotating buffer
      static char buffers[ROTATING_STR_BUFFER_COUNT][JSON_MAX_STRING_LEN + 1];
      static size_t idx = 0;

      char* dest = buffers[idx];
      idx = (idx + 1 >= ROTATING_STR_BUFFER_COUNT) ? 0 : idx + 1;

      size_t outLen;
      if (!decodeJsonString(t, dest, JSON_MAX_STRING_LEN + 1, outLen)) return false;

      out = dest;
      return true;
    }
  ENDREGION()

  REGION(helper)
    static const char* jsonTypeName(uint8_t t) {
      switch (t) {
        case TOKEN_BOOL:   return "bool";
        case TOKEN_NUMBER: return "number";
        case TOKEN_STRING: return "string";
        case TOKEN_OBJECT: return "object";
        case TOKEN_ARRAY:  return "array";
        case TOKEN_KEY:    return "key";
        case TOKEN_NULL:   return "null";
        default:           return "unknown";
      }
    }
    
    inline const char* getTokenValStr(const JsonToken& t) const {
      static char buf[64];
      uint16_t l = t.length > 63 ? 63 : t.length;

      memcpy(buf, _storage.json + t.start, l);
      buf[l] = '\0';

      return buf;
    }
    inline bool isStrNullOrEmpty(const char* s){
      if (!s) return true;

      while (*s){
        if (!JSON_IS_SPACE((unsigned char)*s)) return false;
        s++;
      }
      return true;
    }

    // Compares a key token with key. Keys with escapes ("name") are decoded first,
    // so they mean the same as their unescaped spelling.
    inline bool keyEqualsEscaped(const JsonToken &kt, const char *key, const size_t keyLen) const {
      char tmp[JSON_MAX_KEY_LEN + 1];
      size_t n = 0;
      if(!decodeJsonString(kt, tmp, sizeof(tmp), n)) return false;
      return n == keyLen && memcmp(tmp, key, keyLen) == 0;
    }
    // Keys are short (max. JSON_MAX_KEY_LEN): a simple loop is cheaper than the memcmp call
    static FORCE_INLINE bool bytesEqual(const char *a, const char *b, size_t n){
      while(n--){
        if(*a++ != *b++) return false;
      }
      return true;
    }
    FORCE_INLINE bool keyEquals(const JsonToken &kt, const char *key, const size_t keyLen) const {
      if(!_storage.hasEscapedKeys){
        return kt.length == keyLen && bytesEqual(_storage.json + kt.start, key, keyLen);
      }
      if(!(kt.flags & FLAG_ESCAPES)){
        return kt.length == keyLen && bytesEqual(_storage.json + kt.start, key, keyLen);
      }
      // raw length >= decoded length: an escaped key can only match if it is at least that long
      return kt.length >= keyLen && keyEqualsEscaped(kt, key, keyLen);
    }

    // Looks up a struct member. Searches from hint to the end and then from the start,
    // so the order of the keys in the JSON does not matter (the same order is only faster).
    inline int16_t findMember(const char* key, const size_t keyLen, const int16_t parentTok, uint16_t &hint) {
      const uint16_t n = _storage.keyCount;

      for(uint8_t pass = 0; pass < 2; pass++){
        uint16_t k    = pass == 0 ? hint : 0;
        const uint16_t kend = pass == 0 ? n : (hint < n ? hint : n);

        for (; k < kend; k++) {
          const uint16_t i = _storage.keyCache[k];
          const JsonToken &kt = _storage.tokens[i];

          if(kt.parent != parentTok) continue;
          if(!keyEquals(kt, key, keyLen)) continue;

          hint = k + 1;
          return int16_t(i + 1);
        }
      }

      return -1;
    }

    // Looks up key below parentTok (parentTok < 0: in the whole document), starting at position from in the key cache.
    //
    // Returns:
    //   >= 0  token of the value, from then points behind the hit (for another call)
    //   -1    no (further) hit; lastBad >= 0 if keys with a wrong type were skipped
    //   -2    key found, but wrong type (MatchByKey only)
    inline int16_t findValue(const char* key, const int16_t parentTok, const PathType want, const SearchMode mode,
                             uint16_t &from, int16_t &lastBad, uint8_t &mismatchTyp) {
      const size_t keyLen = strlen(key);

      for (uint16_t k = from; k < _storage.keyCount; k++) {
        const uint16_t i = _storage.keyCache[k];
        const JsonToken &kt = _storage.tokens[i];

        if (parentTok >= 0 && kt.parent != parentTok) continue;
        if (!keyEquals(kt, key, keyLen)) continue;

        const int16_t valTok = int16_t(i + 1);
        const uint8_t vt = _storage.tokens[valTok].type;

        bool match;
        if(want == PATH_OBJECT)        match = (vt == TOKEN_OBJECT);
        else if(want == PATH_ARRAYKEY) match = (vt == TOKEN_ARRAY);
        else                           match = (vt != TOKEN_OBJECT && vt != TOKEN_ARRAY);

        if(match){
          if(want != PATH_MEMBER) _path.addPathEntry(want, valTok);
          from = k + 1;
          return valTok;
        }

        if(mode == SearchMode::MatchByType){
          lastBad = valTok;
          continue;
        }

        _path.addPathEntry(vt == TOKEN_OBJECT ? PATH_OBJECT : (vt == TOKEN_ARRAY ? PATH_ARRAYKEY : PATH_MEMBER), valTok);
        mismatchTyp = vt;
        return -2;
      }

      from = _storage.keyCount;
      return -1;
    }

    // Reads magnitude and sign of a JSON integer.
    // Returns false for more than 64 bits (neg then tells the direction).
    inline bool parseMagnitude(const JsonToken &t, bool &neg, uint64_t &mag) const {
      const char* s = _storage.json + t.start;
      size_t len = t.length;

      neg = (*s == '-');
      if(neg){
        s++;
        len--;
      }

      if(len > 20) return false;

      if(len <= 9){   // fits into 32 bit, fast path
        uint32_t r = 0;
        for(size_t i = 0; i < len; ++i) r = r * 10u + (uint32_t)(s[i] - '0');
        mag = r;
        return true;
      }

      uint64_t r = 0;
      if(len <= 18){  // cannot overflow
        // Two 32-bit parts and only ONE 64-bit multiplication (a 64-bit multiplication per digit is expensive on 32-bit MCUs)
        const size_t hiLen = len - 9;
        uint32_t hi = 0, lo = 0;
        for(size_t i = 0; i < hiLen; ++i) hi = hi * 10u + (uint32_t)(s[i] - '0');
        for(size_t i = hiLen; i < len; ++i) lo = lo * 10u + (uint32_t)(s[i] - '0');
        r = (uint64_t)hi * 1000000000u + lo;
      }else{
        // UINT64_MAX = 18446744073709551615
        constexpr uint64_t lim = 1844674407370955161ull;
        for(size_t i = 0; i < len; ++i){
          const uint64_t d = (uint64_t)(s[i] - '0');
          if(r > lim || (r == lim && d > 5)) return false;
          r = r * 10ull + d;
        }
      }
      mag = r;
      return true;
    }

    inline double parseDouble(const JsonToken &t) const {
      const char *p   = _storage.json + t.start;
      const char *end = p + t.length;

      bool neg = false;
      if(*p == '-'){
        neg = true;
        ++p;
      }

      // Mantissa: at most 18 significant digits, the rest is compensated through the exponent
      // Up to 9 digits in 32 bit, 64 bit only after that (64-bit arithmetic is slow on 32-bit MCUs)
      uint32_t m32 = 0;
      uint8_t n32 = 0;
      uint64_t mantissa = 0;
      bool wide = false;
      int scale = 0;

      // true if the digit was taken into the mantissa
      auto addDigit = [&](const uint32_t d) -> bool {
        if(!wide){
          if(n32 < 9){
            m32 = m32 * 10u + d;
            n32++;
            return true;
          }
          wide = true;
          mantissa = m32;
        }
        if(mantissa < 1000000000000000000ull){
          mantissa = mantissa * 10u + d;
          return true;
        }
        return false;
      };

      while(p < end && JSON_IS_DIGIT(*p)){
        if(!addDigit((uint32_t)(*p - '0'))){
          scale++;      // digit before the decimal point that no longer fits into the mantissa
        }
        ++p;
      }

      if(p < end && *p == '.'){
        ++p;
        while(p < end && JSON_IS_DIGIT(*p)){
          if(addDigit((uint32_t)(*p - '0'))) --scale;
          ++p;
        }
      }

      if(p < end && (*p == 'e' || *p == 'E')){
        ++p;

        int expSign = 1;
        if(*p == '-') { expSign = -1; ++p; }
        else if(*p == '+') { ++p; }

        int exp = 0;
        while(p < end && JSON_IS_DIGIT(*p)){
          if(exp < 100000) exp = exp * 10 + (*p - '0');   // limit to prevent overflow
          ++p;
        }

        scale += expSign * exp;
      }

      if(!wide) mantissa = m32;
      if(mantissa == 0) return neg ? -0.0 : 0.0;

      double val = (double)mantissa;

      if(scale > 0){
        val *= (scale <= JSON_POW10_MAX_EXACT) ? JSON_POW10[scale] : pow(10.0, scale);
      }else if(scale < 0){
        // Division is more exact than multiplying with the reciprocal (for an exact power of ten)
        val /= (-scale <= JSON_POW10_MAX_EXACT) ? JSON_POW10[-scale] : pow(10.0, -scale);
      }

      return neg ? -val : val;
    }

    // Decodes the content of a string token into out (UTF-8, incl. \uXXXX).
    // Returns false if the result (incl. '\0') does not fit into maxLen bytes.
    inline bool decodeJsonString(const JsonToken &t, char *out, const size_t maxLen, size_t &outLen) const {
      const char *p = _storage.json + t.start;
      const char *end = p + t.length;
      outLen = 0;

      if(!(t.flags & FLAG_ESCAPES)){
        const size_t len = t.length;
        if (len + 1 > maxLen) return false;
        memcpy(out, p, len);
        out[len] = '\0';
        outLen = len;
        return true;
      }

      size_t o = 0;
      while (p < end) {
        const char c = *p++;

        if(c != '\\'){
          if (o + 1 >= maxLen) return false;
          out[o++] = c;
          continue;
        }

        // p points at the character behind the backslash (already validated while parsing)
        char decoded;
        switch (*p) {
          case '"':  decoded = '"';  break;
          case '\\': decoded = '\\'; break;
          case '/':  decoded = '/';  break;
          case 'b':  decoded = '\b'; break;
          case 'f':  decoded = '\f'; break;
          case 'n':  decoded = '\n'; break;
          case 'r':  decoded = '\r'; break;
          case 't':  decoded = '\t'; break;
          case 'u': {
            char tmp[4];
            const uint8_t n = jsonDecodeUnicodeEscape(p, tmp);   // p then points at the last character
            if (n == 0 || o + n >= maxLen) return false;
            memcpy(out + o, tmp, n);
            o += n;
            ++p;
            continue;
          }
          default: return false;
        }

        if (o + 1 >= maxLen) return false;
        out[o++] = decoded;
        ++p;
      }

      out[o] = '\0';
      outLen = o;
      return true;
    }
    // A string token with exactly one character (or empty -> '\0')
    inline bool decodeSingleChar(const JsonToken &t, char &out) const {
      char tmp[8];
      size_t n = 0;
      if(!decodeJsonString(t, tmp, sizeof(tmp), n)) return false;

      if(n == 0){
        out = '\0';
        return true;
      }
      if(n == 1){
        out = tmp[0];
        return true;
      }
      return false;
    }
  ENDREGION()
    
  REGION(Error)
    void setAllRuntimeStatusToDefault(){
      setReadRuntimeStatusToDefault();

      _storage.json = nullptr;
      _storage.jsonLen = 0;
      _storage.checksum = 0;
      _storage.writableJSONBuffer = false;
      _storage.tokenCount = 0;
      _storage.strPosAdjust = 0;
      _storage.keyCount = 0;
      _storage.hasEscapedKeys = false;

      _runtimeStatus.isParsed = false;
      _runtimeStatus.hasParseError = false;
    }

    bool makeParseError(JsonReaderStatus code, const char* p, const JsonToken* token = nullptr){
      int32_t parseErrorPos = 0;
      char errorChar = ' ';

      if(p && _storage.json){
        parseErrorPos = ((int32_t)(p - _storage.json)) - (int32_t)_storage.strPosAdjust;
        errorChar = *p ? *p : ' ';
      }

      if(_storage.createErrorInfo){
        const char *valString = token ? getTokenValStr(*token) : "???";

        using S = JsonReaderStatus;
        switch (code) {
        case S::JSON_ERR_EMPTY_JSON:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "empty JSON buffer");
          break;
        case S::JSON_ERR_JSON_TOO_LARGE:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "JSON too large (limit=%u bytes)", unsigned(JSON_READER_MAX_JSON_LEN));
          break;
        case S::JSON_ERR_OUT_OF_MEMORY:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "out of memory");
          break;
        case S::JSON_ERR_UNEXPECTED_CHAR:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "SyntaxError: unexpected char '%c' at pos %ld", errorChar, (long)parseErrorPos);
          break;
        case S::JSON_ERR_INVALID_UTF8:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "invalid UTF8 at pos %ld", (long)parseErrorPos);
          break;
        case S::JSON_ERR_ILLEGAL_ESCAPE:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "illegal escape char '%c' at pos %ld", errorChar, (long)parseErrorPos);
          break;
        case S::JSON_ERR_MISSING_QUOTE:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "SyntaxError: missing '\"' at pos %ld", (long)parseErrorPos);
          break;
        case S::JSON_ERR_DUPLICATE_KEY: {
          char keyName[JSON_MAX_KEY_LEN + 1] = "?";
          char objName[JSON_MAX_KEY_LEN + 1] = "root";
          size_t n = 0;
          if(token) decodeJsonString(*token, keyName, sizeof(keyName), n);
          if(token && token->parent > 0){
            const JsonToken &pt = _storage.tokens[token->parent - 1];    // key under which the object hangs
            if(pt.type == TOKEN_KEY) decodeJsonString(pt, objName, sizeof(objName), n);
            else strcpy(objName, "array");
          }
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "duplicate key '%s' in object '%s' (pos %ld)", keyName, objName, (long)parseErrorPos);
          break;
        }
        case S::JSON_ERR_MISSING_COLON:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "SyntaxError: missing ':' at pos %ld", (long)parseErrorPos);
          break;
        case S::JSON_ERR_ILLEGAL_NUMBER:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "illegal number at pos %ld got %s", (long)parseErrorPos, valString);
          break;
        case S::JSON_ERR_ILLEGAL_BOOL:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "invalid bool keyword at pos %ld got %s", (long)parseErrorPos, valString);
          break;
        case S::JSON_ERR_ILLEGAL_NULL:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "invalid null keyword at pos %ld got %s", (long)parseErrorPos, valString);
          break;
        case S::JSON_ERR_BRACE_MISMATCH:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "SyntaxError: brace mismatch at pos %ld", (long)parseErrorPos);
          break;
        case S::JSON_ERR_TOKEN_LIMIT:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "token limit exceeded (limit=%d)", _storage.maxTokens);
          break;
        case S::JSON_ERR_KEY_LIMIT:
          if(_storage.keyCount >= JSON_MAX_KEYS){
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "key limit exceeded (limit=%u)", unsigned(JSON_MAX_KEYS));
          }else{
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "object key limit exceeded (limit=%u)", unsigned(JSON_MAX_OBJECT_KEYS));
          }
          break;
        case S::JSON_ERR_KEY_LEN_LIMIT:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "key len exceeded (limit=%u)", unsigned(JSON_MAX_KEY_LEN));
          break;
        case S::JSON_ERR_STRING_LEN_LIMIT:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "string len exceeded (limit=%u)", unsigned(JSON_MAX_STRING_LEN));
          break;
        default:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "Undefined parse error code: %d", (int)code);
          break;
        }
      }
      _runtimeStatus.code = code;
      _runtimeStatus.priority = PARSE;
      _runtimeStatus.hasParseError = true;
      _runtimeStatus.isParsed = false;

      return false;
    }

    void setReadRuntimeStatusToDefault(){
      _runtimeStatus.hasReadError = false;

      _runtimeStatus.code = JsonReaderStatus::JSON_READER_OK;
      _runtimeStatus.priority = NONE;
      _runtimeStatus.message[0] = '\0';

      _path.depth = 0;
      _path.overflow = 0;
      _errPath.valid = false;
    }

    // Snapshot of the current _path stack (see ErrorPathSnap). Only called for errors that win.
    void captureErrorPath(const JsonReaderStatus code, const char *key){
      using S = JsonReaderStatus;
      ErrorPathSnap &e = _errPath;
      e.valid = false;
      if(code == S::JSON_ERR_JSON_NOT_PARSED || code == S::JSON_ERR_JSON_CHANGED ||
         code == S::JSON_ERR_INVALID_KEY || code == S::JSON_ERR_INVALID_PATH) return;

      e.anchor = 0;               // root object
      e.idxCount = 0;
      e.idxTruncated = false;
      e.hasLeaf = false;
      e.overflow = _path.overflow > 0;

      // Find the last entry with a known token
      int8_t lastTok = -1;
      for(int8_t i = int8_t(_path.depth) - 1; i >= 0; --i){
        if(_path.entries[i].type != PATH_ARRAYINDEX && _path.entries[i].tokenIndex >= 0){ lastTok = i; break; }
      }
      if(lastTok >= 0) e.anchor = _path.entries[lastTok].tokenIndex;

      // Entries after it: array indices and the key of a value that does not exist
      for(uint8_t i = uint8_t(lastTok + 1); i < _path.depth; i++){
        const auto &p = _path.entries[i];
        if(p.type == PATH_ARRAYINDEX){
          if(e.idxCount < 4) e.idx[e.idxCount++] = uint16_t(p.arrayIndex); else e.idxTruncated = true;
        }else if(key && _path.depth > 0){
          e.hasLeaf = true;
        }
      }
      if(e.hasLeaf){
        size_t n = 0;
        while(n < JSON_MAX_KEY_LEN && key[n]) { e.leaf[n] = key[n]; n++; }
        e.leaf[n] = '\0';
      }
      e.valid = true;
    }

    // Formats the snapshot: root + ancestor chain of the anchor + array indices + missing key
    const char *formatErrorPath() const{
      const JsonToken *tk = _storage.tokens;
      const ErrorPathSnap &e = _errPath;

      // collect the ancestor chain (from the anchor up to the root)
      int16_t chain[24];
      uint8_t n = 0;
      bool tooDeep = false;
      for(int16_t t = e.anchor; t > 0 && t < int16_t(_storage.tokenCount); t = tk[t].parent){
        if(n >= 24){ tooDeep = true; break; }
        chain[n++] = t;
      }

      JsonPathOut out;
      out.putStr("root");
      if(tooDeep) out.putStr("...");
      while(n){
        const int16_t t = chain[--n];
        const int16_t p = tk[t].parent;
        if(p < 0) continue;
        if(tk[p].type == TOKEN_ARRAY){
          // position among the direct children of the array
          unsigned idx = 0;
          for(int16_t c = int16_t(p + 1); c < t; ){
            c = (tk[c].type == TOKEN_OBJECT || tk[c].type == TOKEN_ARRAY) ? tk[c].end : int16_t(c + 1);
            idx++;
          }
          out.putIndex(idx);
        }else if(t > 0 && tk[t - 1].type == TOKEN_KEY && tk[t - 1].parent == p){
          const JsonToken &kt = tk[t - 1];
          out.putKey(_storage.json + kt.start, kt.length);
        }
      }
      for(uint8_t i = 0; i < e.idxCount; i++) out.putIndex(e.idx[i]);
      if(e.idxTruncated || e.overflow) out.putStr("...");
      if(e.hasLeaf){
        out.put('.');
        out.putStr(e.leaf);
      }
      return out.end();
    }


    //<CONTEXT>(<PATH>) <problem>: expected <C++>, got <JSON>
    bool makeError(JsonReaderStatus code, ErrorPriority priority, const char* expectedTyp, const char* key = nullptr, const char *JSONTyp = nullptr){
      if(priority <= _runtimeStatus.priority) return false;

      if(_storage.createErrorInfo){
        const char* finalerrorKey   = nullptr;
        const char* finalparentKey  = nullptr;
        const char* finalparentTyp  = nullptr;  // object/array
        const char* finaljsonTyp    = nullptr;
        bool isArray = false;
        bool isParentArray = false;

        char arrKeyIndex[64]  = {0};
        char arrKeyCount[64]  = {0};

        analyzePath(finalparentKey, finalparentTyp, finaljsonTyp, isArray, isParentArray);
        if(isArray) createArrayStr(arrKeyIndex, sizeof(arrKeyIndex), arrKeyCount, sizeof(arrKeyCount));

        // ---------- errorKey ----------
        finalerrorKey = safeStr(key ? key : "???");

        // ---------- parent ----------
        if(isParentArray){
          createArrayStr(arrKeyIndex, sizeof(arrKeyIndex), arrKeyCount, sizeof(arrKeyCount));
          finalparentKey = arrKeyIndex;
          finalparentTyp = safeStr(finalparentTyp ? finalparentTyp : "array");
        }else{
          finalparentKey = safeStr(finalparentKey ? finalparentKey : "root");
          finalparentTyp = safeStr(finalparentTyp ? finalparentTyp : "object");
        }

        // ---------- types ----------
        finaljsonTyp   = JSONTyp ? JSONTyp : (finaljsonTyp ? finaljsonTyp : "???");
        expectedTyp    = expectedTyp ? expectedTyp : "???";

        using S = JsonReaderStatus;
        switch (code) {
        case S::JSON_ERR_JSON_NOT_PARSED:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "JSON has not been parsed");
          break;
        case S::JSON_ERR_JSON_CHANGED:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "JSON buffer was modified after parsing");
          break;
        case S::JSON_ERR_INVALID_VALUE:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "'%s' (%s) invalid value (in %s '%s')", 
                  isArray ? arrKeyIndex : finalerrorKey, finaljsonTyp, finalparentTyp, finalparentKey);
          break;
        case S::JSON_ERR_INVALID_KEY:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "invalid key, nullptr not allowed");
          break;
        case S::JSON_ERR_INVALID_PATH:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "invalid path key, nullptr not allowed");
          break;
        case S::JSON_ERR_NOT_FOUND:
          if(_path.depth <= 0){  // if not even root was found, should never happen
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "'%s' not found", finalerrorKey); 
          }else{
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "%s '%s' not found (in %s '%s')", 
                    expectedTyp, finalerrorKey, finalparentTyp, finalparentKey); 
          }
          break;
        case S::JSON_ERR_KEY_FOUND_TYPE_MISSING:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "key '%s' found, but no matching type (in %s '%s')", 
                  finalerrorKey, finalparentTyp, finalparentKey); 
          break;
        case S::JSON_ERR_KEY_MISSING:
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "%s '%s' missing (in %s '%s')", 
                    expectedTyp, finalerrorKey, finalparentTyp, finalparentKey);
          break;
        case S::JSON_ERR_EXTRA_KEY:
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "extra %s '%s' (%s) in %s '%s'", 
                    expectedTyp, finalerrorKey, finaljsonTyp, finalparentTyp, finalparentKey);
          break;
        case S::JSON_ERR_TYPE_MISMATCH:
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "'%s' type mismatch: expected %s, got %s (in %s '%s')", 
                    isArray ? arrKeyIndex : finalerrorKey, expectedTyp, finaljsonTyp, finalparentTyp, finalparentKey);
          break;
        case S::JSON_ERR_TYPE_UNDERFLOW:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "value underflow for %s '%s' (in %s '%s')",
                  expectedTyp, isArray ? arrKeyIndex : finalerrorKey, finalparentTyp, finalparentKey);
          break;
        case S::JSON_ERR_TYPE_OVERFLOW:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "value overflow for %s '%s' (in %s '%s')",
                  expectedTyp, isArray ? arrKeyIndex : finalerrorKey, finalparentTyp, finalparentKey);
          break;
        case S::JSON_ERR_ARRAY_TOO_MANY_ELEMENTS:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "array too many elements at index '%s' (in %s '%s')",
                  arrKeyIndex, finalparentTyp, finalparentKey);
          break;
        case S::JSON_ERR_ARRAY_TOO_FEW_ELEMENTS:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "array too few elements at index '%s' (in %s '%s')",
                  arrKeyIndex, finalparentTyp, finalparentKey);
          break;
        default:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "Undefined error code: %d", (int)code);
          break;
        }
      }

      if(_storage.createErrorInfo) captureErrorPath(code, key);

      _runtimeStatus.code = code;
      _runtimeStatus.priority = priority;
      _runtimeStatus.hasReadError = true;

      return false;
    }

    bool analyzePath(const char* &parentKey, const char* &parentTyp, const char* &jsonTyp, bool &isArray, bool &isParentArray) {
      if (_path.depth <= 0) return false;

      static char parentKeyBuf[64];
      int8_t culpritIndex = -1;

      // 1) Read from the back: find array context and "culprit"
      for (int8_t i = int8_t(_path.depth) - 1; i >= 0; --i) {
        const auto& p = _path.entries[i];

        // Array-Frame
        if (p.type == PATH_ARRAYINDEX) {
          continue;
        }
        // "Culprit": MEMBER/OBJECT/ARRAYKEY - even if tokenIndex == -1
        if (jsonTyp == nullptr) {
          if (p.type == PATH_MEMBER) {
            jsonTyp  = "key";  
            culpritIndex = i;
          } else if (p.type == PATH_OBJECT) {
            jsonTyp  = "object";
            culpritIndex = i;
          } else if (p.type == PATH_ARRAYKEY) {
            jsonTyp  = "array";
            isArray = true;     // the ARRAY is the error
            culpritIndex = i;
          }
          // keep going - determine the parent
          continue;
        }

        // 2) determine the parent
        if (culpritIndex >= 0) {
          for (int8_t j = culpritIndex - 1; j >= 0; --j) {
            const auto& pp = _path.entries[j];

            // direct array parent
            if (pp.type == PATH_ARRAYINDEX) {
              parentTyp = "array";
              isParentArray = true;
              return true;
            }

            // Object / Member
            if (pp.type == PATH_OBJECT || pp.type == PATH_MEMBER) {
              parentTyp = "object";
              
              parentKey = findTokenKeyBackwards(pp.tokenIndex);
              if (parentKey) {
                size_t len = strlen(parentKey);
                if (len >= sizeof(parentKeyBuf)) len = sizeof(parentKeyBuf) - 1;
                memcpy(parentKeyBuf, parentKey, len);
                parentKeyBuf[len] = '\0';
                parentKey = parentKeyBuf;
              }
              return true;
            }
          }
        }

        // Without array: the parent is the next OBJECT/MEMBER to the left
        if (p.type == PATH_OBJECT || p.type == PATH_MEMBER) {
          parentTyp = "object";
          const char* src = findTokenKeyBackwards(p.tokenIndex);
          if (src) {
            size_t len = strlen(src);
            if (len >= sizeof(parentKeyBuf)) len = sizeof(parentKeyBuf) - 1;
            memcpy(parentKeyBuf, src, len);
            parentKeyBuf[len] = '\0';
            parentKey = parentKeyBuf;
          }
          return true;
        }
      }

      // fallback
      if(!parentTyp) parentTyp = "object";
      if(!parentKey) parentKey = "root";
      return true;
    }

    const char* findTokenKeyBackwards(int16_t tokIndex) {
      if (tokIndex <= 0) return nullptr;

      int16_t keyIndex = tokIndex - 1;
      if (_storage.tokens[keyIndex].type != TOKEN_KEY) return nullptr;
      if (_storage.tokens[keyIndex].parent != _storage.tokens[tokIndex].parent) return nullptr;

      return getTokenValStr(_storage.tokens[keyIndex]);
    }

    bool createArrayStr(char* outIndex, size_t outIndexSize, char* outCount, size_t outCountSize){
      if (!outIndex || !outCount || outIndexSize < 2 || outCountSize < 2 || _path.depth == 0)
        return false;

      char* pIdx = outIndex + outIndexSize - 1;
      char* pCnt = outCount + outCountSize - 1;
      *pIdx = '\0';
      *pCnt = '\0';

      char tmp[12];

      // arrays first
      for (int8_t i = int8_t(_path.depth) - 1; i >= 0; --i) {
        if (_path.entries[i].type == PATH_ARRAYINDEX) {
          // -------- index --------
          int n = snprintf(tmp, sizeof(tmp), "[%d]", _path.entries[i].arrayIndex);
          if (n <= 0 || n > (pIdx - outIndex)) return false;
          pIdx -= n;
          memcpy(pIdx, tmp, n);

          // -------- count --------
          n = snprintf(tmp, sizeof(tmp), "[%d]", _path.entries[i].expectedCount);
          if (n <= 0 || n > (pCnt - outCount)) return false;
          pCnt -= n;
          memcpy(pCnt, tmp, n);
        }else if (_path.entries[i].type == PATH_ARRAYKEY) {
          const char* key = findTokenKeyBackwards(_path.entries[i].tokenIndex);
          if (!key) break;
          size_t len = strlen(key);
          if (len > (size_t)(pIdx - outIndex)) return false;
          if (len > (size_t)(pCnt - outCount)) return false;

          pIdx -= len;
          memcpy(pIdx, key, len);
          pCnt -= len;
          memcpy(pCnt, key, len);

          break;
        }
      }

      memmove(outIndex, pIdx, (outIndex + outIndexSize) - pIdx);
      memmove(outCount, pCnt, (outCount + outCountSize) - pCnt);
      return true;
    }

    inline const char* safeStr(const char* s, size_t maxLen = 64) {
      if (!s) return nullptr;

      for (size_t i = 0; i < maxLen; ++i) {
        if (s[i] == '\0') return s;
      }
      return "<truncated>";
    }
  ENDREGION()
};
