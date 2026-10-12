/*
 ReflectJSON - JSON writer and reader with struct reflection for Arduino / C++17
 Copyright (c) 2026 Michael
 Commercial licensing available under separate written agreement.
 See LICENSE and COMMERCIAL.md.
*/

#pragma once
#include "ReflectJSON_tools.h"

// Error and status codes returned by JSONwriter.
//
// These codes indicate structural, capacity, or usage errors
// encountered while building a JSON document.
// JSON_WRITER_OK (0) indicates successful writing with no errors.
enum class JsonWriterStatus {
  JSON_WRITER_OK = 0,               // No error

  // limits
  JSON_ERR_KEY_LEN_LIMIT,          // Key length exceeds allowed limit
  JSON_ERR_STRING_LEN_LIMIT,       // String length exceeds allowed limit
  JSON_ERR_INVALID_NUMBER,         // NaN, Infinity or a value that does not fit the requested number format

  // state errors
  JSON_ERR_FINISHED,                // Operation attempted after JSON was finished
  JSON_ERR_NO_SPACE,                // Output buffer has insufficient space

  // array state errors
  JSON_ERR_ARRAY_OPEN,              // Invalid array operation for the current context (e.g. key provided while in array context)
  JSON_ERR_FINISH_ARRAY_OPEN,       // finish() called while an array is still open
  JSON_ERR_ARRAY_NOT_OPEN,          // Operation requires an open array context, but the current context is not an array

  // depth / key limits
  JSON_ERR_MAX_DEPTH,               // Maximum nesting depth exceeded
  JSON_ERR_MAX_KEYS,                // Maximum number of keys per object or per document exceeded
  JSON_ERR_DUPLICATE_KEY,           // Duplicate key in the same object
  JSON_ERR_KEY,                     // Invalid or missing key

  // object state errors
  JSON_ERR_OBJECT_OPEN,             // Invalid object operation for the current context (e.g. key provided while in array context)
  JSON_ERR_FINISH_OBJECT_OPEN,      // finish() called while an object is still open
  JSON_ERR_OBJECT_NOT_OPEN,         // Operation requires an open object context, but the current context is not an object

  // memory
  JSON_ERR_OUT_OF_MEMORY            // The internal buffers could not be allocated (constructor)
};


// JSONwriter builds a JSON document incrementally into an output buffer.
//
// The writer enforces structural correctness (objects, arrays, keys)
// and reports detailed runtime errors if invalid operations occur.
//
// The maximum nesting depth and output buffer size are defined at construction.
// Internal state can be reset to reuse the instance for multiple documents.
class JSONwriter {
public:
    // Constructs a JSONwriter with a specified output buffer size and nesting depth.
    //
    // bufferSize defines the size of the internal output buffer in bytes.
    // A minimum buffer size of 100 bytes is enforced internally.
    //
    // maxDepth defines the maximum allowed nesting depth for objects and arrays.
    // A minimum depth of 1 is enforced internally.
    //
    // Allocates all required internal buffers and initializes the writer
    // in a valid root object state. If the allocation fails the writer
    // is put into the error state JSON_ERR_OUT_OF_MEMORY and every
    // further operation is ignored.
    // JSONwriter(256, false) means "no error info" (default depth); without these overloads the bool would
    // silently be taken as maxDepth.
    template<typename B, typename = std::enable_if_t<std::is_same_v<B, bool>>>
    explicit JSONwriter(size_t bufferSize, B createErrorInfo)
      : JSONwriter(bufferSize, uint8_t(5), createErrorInfo){}
    template<typename B, typename = std::enable_if_t<std::is_same_v<B, bool>>>
    explicit JSONwriter(size_t bufferSize, B createErrorInfo, const JsonConfig &config)
      : JSONwriter(bufferSize, uint8_t(5), config, createErrorInfo){}
    explicit JSONwriter(size_t bufferSize, uint8_t maxDepth, const JsonConfig &config, bool createErrorInfo = true)
      : JSONwriter(bufferSize, maxDepth, createErrorInfo){
      _config = config;
    }
    explicit JSONwriter(size_t bufferSize, uint8_t maxDepth = 5, bool createErrorInfo = true){
      _storage.maxDepth = maxDepth == 0 ? 1 : maxDepth;
      _storage.bufferMax = bufferSize < 100 ? 100 : bufferSize;
      _storage.createErrorInfo = createErrorInfo;
      _storage.writerPos = 0;
      _storage.depth = 0;
      _storage.ready = false;

      // one position for the closing '}' of the root object and one for '\0' always stay free
      _storage.limit = _storage.bufferMax - 2;

      _storage.jsonOut = new (std::nothrow) char[_storage.bufferMax];
      _writeContext = new (std::nothrow) WriteContext[_storage.maxDepth];
      _keyHashes = new (std::nothrow) uint32_t[size_t(_storage.maxDepth) * JSON_MAX_OBJECT_KEYS];

      setRuntimeStatusToDefault();

      if (!_storage.jsonOut || !_writeContext || !_keyHashes) {
        makeError(JsonWriterStatus::JSON_ERR_OUT_OF_MEMORY);
        return;
      }

      for (uint8_t i = 0; i < _storage.maxDepth; i++) {
        _writeContext[i].hashes = _keyHashes + size_t(i) * JSON_MAX_OBJECT_KEYS;
      }
      _storage.ready = true;
      resetJSON();
    }
    // Destroys the JSONwriter and releases all allocated resources.
    ~JSONwriter() {
      delete[] _prettyBuf;
      delete[] _storage.jsonOut;
      delete[] _keyHashes;
      delete[] _writeContext;
    }
    JSONwriter(const JSONwriter&) = delete;
    JSONwriter &operator=(const JSONwriter&) = delete;

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

    // Returns true if the writer is currently in an error state.
    bool hasError() const{ return _runtimeStatus.hasError(); }
    // Returns the error code of the last error that occurred,
    // or JSON_WRITER_OK (0) if no error is present.
    JsonWriterStatus getErrorCode() const{ return _runtimeStatus.code; }
    // Returns the error message of the last error that occurred,
    // or nullptr if no error is present.
    const char *getErrorMsg() const{
      if(!_storage.createErrorInfo) return nullptr;
      return _runtimeStatus.hasError() ? _runtimeStatus.message : nullptr;
    }

    // Returns the location of the last error as a path, same format as JSONreader::getErrorPath():
    //   root.status.motor.speed      root.status.values[3]      root.devices[2].settings.limit
    // It names the open containers at the time of the error plus the offending key (object) or the
    // element index [n] (array). Errors without a location (out of memory, JSON finished, ...) give "root"
    // or nullptr.
    //
    // The path is rebuilt on request from the already written buffer (no per-call cost while writing,
    // only the error position and the failing key are stored). The returned pointer refers to a buffer
    // that is shared by all readers/writers and stays valid until the next getErrorPath() call.
    //
    // Returns nullptr if there is no error or if createErrorInfo is disabled.
    const char *getErrorPath() const{
      if(!_storage.createErrorInfo || !_runtimeStatus.hasError() || !_runtimeStatus.pathValid) return nullptr;
      return formatErrorPath();
    }

    // Settings shared with JSONreader (see JsonConfig). For the writer explicitNulls matters:
    // false -> null values (nullptr, std::nullopt) are omitted in objects and written as null in arrays.
    // Applies to all following write() calls.
    JSONwriter &setConfig(const JsonConfig &config){ _config = config; return *this; }
    const JsonConfig &getConfig() const{ return _config; }

    // Returns the configured nesting depth.
    uint8_t getMaxDepth() const{ return _storage.maxDepth; }
    // Returns the maximum number of keys allowed per object.
    uint8_t getMaxObjectKeys() const{ return uint8_t(JSON_MAX_OBJECT_KEYS); }
    // Returns the maximum number of keys allowed per json document.
    uint8_t getMaxKeys() const{ return uint8_t(JSON_MAX_KEYS); }
    // Returns the size of the internal output buffer in bytes.
    size_t getBufferSize() const{ return _storage.bufferMax; }

    // Returns a pointer to the finalized JSON string,
    // or nullptr if the JSON is not yet finished.
    //
    // prettyPrint = true returns an indented, readable variant (created lazily in a
    // separate buffer, the compact buffer stays unchanged; getJSON(false) still returns
    // the compact JSON afterwards). The pointer stays valid until the next getJSON(true)/resetJSON()/destructor.
    // Returns nullptr if there is not enough memory.
    const char *getJSON(bool prettyPrint = false) const{
      if(!_runtimeStatus.isJSONFinished) return nullptr;
      if(!prettyPrint) return _storage.jsonOut;
      return createPrettyJSON() ? _prettyBuf : nullptr;
    }
    // Returns the size of the finalized JSON in bytes (without the terminating '\0'),
    // or 0 if the JSON is not yet finished. getJSONSize(true) returns the length of the pretty variant
    // without allocating memory for it (it only counts; getJSON(true) creates the buffer).
    size_t getJSONSize(bool prettyPrint = false) const{
      if(!_runtimeStatus.isJSONFinished) return 0;
      if(!prettyPrint) return _storage.writerPos;
      if(_prettyValid) return _prettySize;
      // only count, no memory needed (the pretty buffer is allocated by getJSON(true))
      return prettyPass<false>(_storage.jsonOut, _storage.writerPos, nullptr);
    }

    // Finishes the JSON document.
    //
    // Closes the root object and finalizes the JSON output buffer.
    // This must be called once after all objects and arrays have been
    // properly closed.
    //
    // Fails if:
    // - an error has already occurred
    // - the JSON was already finished
    // - there are still open objects or arrays
    //
    // On success, appends the closing '}' for the root object,
    // null-terminates the buffer, and marks the JSON as finished.
    // An empty document ("{}") is valid.
    inline JSONwriter &finish(){
      if(_runtimeStatus.hasError()){
        return *this;
      }else if(_runtimeStatus.isJSONFinished){
        makeError(JsonWriterStatus::JSON_ERR_FINISHED);
        return *this;
      }else if(_storage.depth > 0){
        makeError(_writeContext[_storage.depth].inArray ? JsonWriterStatus::JSON_ERR_FINISH_ARRAY_OPEN
                                                        : JsonWriterStatus::JSON_ERR_FINISH_OBJECT_OPEN);
        return *this;
      }

      // space for '}' and '\0' is always reserved by _storage.limit
      _storage.jsonOut[_storage.writerPos++] = '}';
      _storage.jsonOut[_storage.writerPos] = '\0';

      _runtimeStatus.isJSONFinished = true;
      return *this;
    }
    // Resets the JSON writer to its initial state.
    //
    // Clears all runtime state, depth tracking, and key counters,
    // then starts a new empty root object.
    //
    // Always starts a new JSON document with '{' already written.
    // Has no effect if the allocation in the constructor failed.
    inline JSONwriter &resetJSON(){
      if(!_storage.ready) return *this;

      _writeContext[0].inArray = false;
      _writeContext[0].firstAtDepth = true;
      _writeContext[0].count = 0;

      _storage.depth = 0;
      _storage.keyTotal = 0;
      _storage.jsonOut[0] = '{';
      _storage.jsonOut[1] = '\0';
      _storage.writerPos = 1;
      _prettyValid = false;

      setRuntimeStatusToDefault();

      return *this;
    }


  #ifdef ARDUINO
    // Starts a new JSON object using an Arduino String as the key.
    // This overload is functionally identical to startObject(const char*).
    inline JSONwriter &startObject(const String &key){
      return startObject(key.c_str());
    }
    // Starts a new JSON array using an Arduino String as the key.
    // This overload is functionally identical to startArray(const char*).
    inline JSONwriter &startArray(const String &key){
      return startArray(key.c_str());
    }

    // Writes a value to the current JSON context using an Arduino String as the key.
    // This overload is functionally identical to write(const T&, const char*).
    template<typename T>
    inline JSONwriter &write(const T &value, const String &key){
      return write(value, key.c_str());
    }
  #endif

    // Starts a new JSON object.
    // If a key is provided, the object is added as a member of the current object.
    // If no key is provided, the object is added as an array element.
    //
    // The object must be closed by calling endObject().
    //
    // Fails if called in an invalid context (e.g. key is null but the current context is not an array,
    // duplicate key, maximum depth exceeded, or JSON already finished).
    inline JSONwriter &startObject(const char *key = nullptr){
      return openContainer(false, key);
    }
    // Ends the current JSON object.
    // Closes the most recently opened object and returns to the parent context.
    //
    // Fails if no object is currently open, the JSON is already finished,
    // or the resulting JSON structure would become invalid.
    inline JSONwriter &endObject(){
      if(_runtimeStatus.hasError()) return *this;

      if(_runtimeStatus.isJSONFinished){
        makeError(JsonWriterStatus::JSON_ERR_FINISHED);
      }else if(_writeContext[_storage.depth].inArray) {
        makeError(JsonWriterStatus::JSON_ERR_ARRAY_OPEN);
      }else if(_storage.depth == 0){
        makeError(JsonWriterStatus::JSON_ERR_OBJECT_NOT_OPEN);
      }else{
        _pendingKey = nullptr;
        appendChar('}');
        _storage.depth--;
      }
      return *this;
    }

    // Starts a new JSON array.
    // If a key is provided, the array is added as a member of the current object.
    // If no key is provided, the array is added as an array element.
    //
    // The array must be closed by calling endArray().
    //
    // Fails if called in an invalid context (e.g. key is null but the current context is not an array,
    // duplicate key, maximum depth exceeded, or JSON already finished).
    inline JSONwriter &startArray(const char *key = nullptr){
      return openContainer(true, key);
    }
    // Ends the current JSON array.
    // Closes the most recently opened array and returns to the parent context.
    //
    // Fails if no array is currently open, the JSON is already finished,
    // or the resulting JSON structure would become invalid.
    inline JSONwriter &endArray(){
      if(_runtimeStatus.hasError()) return *this;

      if(_runtimeStatus.isJSONFinished){
        makeError(JsonWriterStatus::JSON_ERR_FINISHED);
      }else if(!_writeContext[_storage.depth].inArray){
        makeError(JsonWriterStatus::JSON_ERR_ARRAY_NOT_OPEN);
      }else{
        _pendingKey = nullptr;
        appendChar(']');
        _storage.depth--;
      }
      return *this;
    }

    // Writes a value to the current JSON context.
    // If a key is provided, the value is written as a member of the current object.
    // If no key is provided, the value is written as an array element.
    // A REFLECT struct without a key inside an open object is "flattened": its members are written directly
    // into the current object (write(123,"id").write(status).write("ok","state")
    // -> {"id":123,"speed":18.6,"assist":3,"light":true,"state":"ok"}). Key limits and the duplicate check
    // apply to the current object. std::optional<Struct> without a value and without a key -> JSON_ERR_KEY.
    //
    // Fails if called in an invalid context (e.g. key is null outside of an array,
    // duplicate key, type not supported, maximum depth/keys exceeded, or JSON already finished).
    //
    // Supported value types include primitives, structs, arrays, and types handled
    // via REFLECT or custom writer traits.
    template<typename T>
    inline JSONwriter &write(const T &value, const char *key = nullptr){
      if(_runtimeStatus.hasError()) return *this;

      if(_runtimeStatus.isJSONFinished){
        makeError(JsonWriterStatus::JSON_ERR_FINISHED);
        return *this;
      }

      setData(value, key);
      return *this;
    }
private:
  REGION(deklaration)
    struct WriteContext{
      uint32_t *hashes;     // points into the shared _keyHashes pool
      uint16_t count;       // number of keys in this object
      bool inArray;
      bool firstAtDepth;
    } *_writeContext;
    uint32_t *_keyHashes;
    JsonConfig _config;
    // Key of the value currently being written (only valid during the current write()/start...() call).
    // Used for the error path of errors that happen after beginValue() (e.g. JSON_ERR_NO_SPACE).
    const char *_pendingKey = nullptr;

    // Lazy pretty-print cache (only filled by the first getJSON(true) after finish())
    mutable char *_prettyBuf = nullptr;
    mutable size_t _prettyCap = 0;
    mutable size_t _prettySize = 0;
    mutable bool _prettyValid = false;

    // One pass over the compact JSON. Write=false: only count the length, Write=true: write.
    // Strings (incl. \" and \\) are copied unchanged, empty containers stay "{}" / "[]".
    template<bool Write>
    static size_t prettyPass(const char *in, size_t n, char *out){
      size_t o = 0;
      unsigned level = 0;
      size_t i = 0;
      #define JW_PUT(c) do{ if(Write) out[o] = (c); o++; }while(0)
      #define JW_NL() do{ JW_PUT('\n'); for(unsigned k = level * JSON_PRETTY_INDENT; k; k--) JW_PUT(' '); }while(0)
      while(i < n){
        const char c = in[i];
        switch(c){
          case '"': {
            size_t j = i + 1;
            while(j < n && in[j] != '"'){ j += (in[j] == '\\') ? 2 : 1; }
            if(j >= n) j = n - 1;
            const size_t len = j - i + 1;
            if(Write) memcpy(out + o, in + i, len);
            o += len;
            i = j + 1;
            break;
          }
          case '{': case '[':
            JW_PUT(c);
            if(i + 1 < n && (in[i + 1] == '}' || in[i + 1] == ']')){
              JW_PUT(in[i + 1]);
              i += 2;
            }else{
              level++;
              JW_NL();
              i++;
            }
            break;
          case '}': case ']':
            level--;
            JW_NL();
            JW_PUT(c);
            i++;
            break;
          case ',':
            JW_PUT(',');
            JW_NL();
            i++;
            break;
          case ':':
            JW_PUT(':');
            JW_PUT(' ');
            i++;
            break;
          default:
            JW_PUT(c);
            i++;
        }
      }
      #undef JW_NL
      #undef JW_PUT
      return o;
    }

    bool createPrettyJSON() const{
      if(_prettyValid) return true;
      const size_t need = prettyPass<false>(_storage.jsonOut, _storage.writerPos, nullptr);
      if(need + 1 > _prettyCap){
        char *nb = new (std::nothrow) char[need + 1];
        if(!nb) return false;
        delete[] _prettyBuf;
        _prettyBuf = nb;
        _prettyCap = need + 1;
      }
      prettyPass<true>(_storage.jsonOut, _storage.writerPos, _prettyBuf);
      _prettyBuf[need] = '\0';
      _prettySize = need;
      _prettyValid = true;
      return true;
    }

    struct JsonWriterResult{
      JsonWriterStatus code;
      char message[JSON_ERR_MAX_MSG] = {0};
      // Error location for getErrorPath(): write position + offending key (only if createErrorInfo)
      size_t errPos = 0;
      char errKey[JSON_MAX_KEY_LEN + 1] = {0};
      bool pathValid = false;
      bool pathLeaf = false;     // append the key / array index of the offending element
      bool isJSONFinished;
      bool hasError() const{ return code != JsonWriterStatus::JSON_WRITER_OK; }
    } _runtimeStatus;

    struct writerStorage{
      char* jsonOut;
      size_t bufferMax;
      size_t limit;           // bufferMax - 2: last position for regular content
      uint8_t maxDepth;

      size_t writerPos;
      uint16_t keyTotal;      // number of keys in the whole document (limit JSON_MAX_KEYS)
      uint8_t depth;

      bool createErrorInfo;
      bool ready;             // false if the allocation failed
    } _storage;
  ENDREGION()

  REGION(write)
    // Common implementation of startObject / startArray
    inline JSONwriter &openContainer(const bool isArray, const char *key){
      if(_runtimeStatus.hasError()) return *this;

      if(_runtimeStatus.isJSONFinished){
        makeError(JsonWriterStatus::JSON_ERR_FINISHED);
        return *this;
      }

      const JsonWriterStatus ctxErr = isArray ? JsonWriterStatus::JSON_ERR_ARRAY_OPEN : JsonWriterStatus::JSON_ERR_OBJECT_OPEN;
      const WriteContext &ctx = _writeContext[_storage.depth];

      if(key){
        if(ctx.inArray){
          makeError(ctxErr, key);
          return *this;
        }
      }else if(!ctx.inArray){
        makeError(ctxErr);
        return *this;
      }

      openInternal(isArray, key);
      return *this;
    }

    template<typename T>
    bool setData(const T &value, const char *key) {
      if(_runtimeStatus.hasError()) return false;

      if constexpr (is_json_optional_v<T>){
        if(value.has_value()) return setData(*value, key);
        return writeNull(key);
      }else{
        constexpr CTypeKind k = deduceCType<T>();
        if constexpr (k == CTypeKind::Object) {
          return addStruct(value, key);
        }else if constexpr (k == CTypeKind::Array) {
          return addArray(value, key);
        }else if constexpr (k == CTypeKind::Null) {
          return writeNull(key);
        }else if constexpr (k == CTypeKind::String) {
          if constexpr (std::is_pointer_v<remove_cvref_t<T>>){
            if(value == nullptr) return writeNull(key);
          }
          if(!beginValue(key)) return false;
          return writePrimitive(value, key);
        }else{
          if(!beginValue(key)) return false;
          return writePrimitive(value, key);
        }
      }
    }

    // Writes null. With explicitNulls == false a null in an object (key != nullptr) is omitted.
    inline bool writeNull(const char *key){
      if(key && !_config.explicitNulls) return true;
      if(!beginValue(key)) return false;
      appendLit("null");
      return !_runtimeStatus.hasError();
    }

    template<typename T>
    inline bool addStruct(const T &value, const char *key){
      // Without a key inside an open object: write the members directly into the current object (flatten)
      if(!key && !_writeContext[_storage.depth].inArray){
        bool okFlat = true;
        value.reflect([&](auto&&... fields) {
          okFlat = (... && setData(fields.value, fields.key));
        });
        return okFlat && !_runtimeStatus.hasError();
      }
      if(!openInternal(false, key)) return false;

      bool ok = true;
      value.reflect([&](auto&&... fields) {
        ok = (... && setData(fields.value, fields.key));
      });

      if(!ok) return false;

      closeInternal(false);
      return !_runtimeStatus.hasError();
    }
    template<typename T, size_t N>
    inline bool addArray(const T (&arr)[N], const char *key){
      if(!openInternal(true, key)) return false;

      for (size_t i = 0; i < N; i++){
        if(!setData(arr[i], nullptr)) return false;
      }

      closeInternal(true);
      return !_runtimeStatus.hasError();
    }
    template<typename T>
    FORCE_INLINE bool writePrimitive(const T &value, [[maybe_unused]] const char *key) {
      constexpr CTypeKind k = deduceCType<T>();
      if constexpr (k == CTypeKind::Null){
        appendLit("null");
      }else if constexpr (k == CTypeKind::Bool){
        if(static_cast<bool>(value)) appendLit("true"); else appendLit("false");
      }else if constexpr (k == CTypeKind::Char){
        appendChar('"');
        if(value != '\0'){
          const char tmp = value;
          appendEscaped(&tmp, 1);
        }
        appendChar('"');
      }else if constexpr (k == CTypeKind::String){
        if(value == nullptr){
          appendLit("null");
        }else{
          appendQuoted(value, key);
        }
      #ifdef ARDUINO
      }else if constexpr (k == CTypeKind::StringArduino){
        appendQuoted(value.c_str(), key);
      #endif
      }else if constexpr (k == CTypeKind::Enum){
        appendInt(static_cast<std::underlying_type_t<T>>(value));
      }else if constexpr (k == CTypeKind::Int){
        appendInt(value);
      }else if constexpr (k == CTypeKind::FloatFixedPoint){
        // JsonFixed<T> -> integer in the JSON (value * 10^precision)
        const double scaled = (double)value.value * JSON_POW10[jsonClampPrecision(value.precision)];

        // Also catches NaN/Inf. 9.0e18 is safely below INT64_MAX
        if(!(scaled > -9.0e18 && scaled < 9.0e18)){
          return makeError(JsonWriterStatus::JSON_ERR_INVALID_NUMBER, key);
        }

        const int64_t r = (int64_t)(scaled + (scaled >= 0 ? 0.5 : -0.5));
        if(r >= INT32_MIN && r <= INT32_MAX){
          appendInt((int32_t)r);    // 32-bit path is much faster on MCUs
        }else{
          appendInt(r);
        }
      }else if constexpr (k == CTypeKind::FloatDefault){
        if(!std::isfinite(value)) return makeError(JsonWriterStatus::JSON_ERR_INVALID_NUMBER, key);
        appendReal(value, defaultPrecision<T>());
      }else if constexpr (k == CTypeKind::Float){
        if(!std::isfinite(value.value)) return makeError(JsonWriterStatus::JSON_ERR_INVALID_NUMBER, key);
        appendReal(value.value, value.precision);
      }else{
        static_assert(always_false<T>, "writePrimitive: unsupported type");
      }

      return !_runtimeStatus.hasError();
    }

    // Opens an object/array as a value (key or array element)
    inline bool openInternal(const bool isArray, const char *key){
      if(_storage.depth + 1 >= _storage.maxDepth){
        return makeError(JsonWriterStatus::JSON_ERR_MAX_DEPTH, key);
      }
      if(!beginValue(key)) return false;

      appendChar(isArray ? '[' : '{');
      if(_runtimeStatus.hasError()) return false;

      _storage.depth++;
      WriteContext &ctx = _writeContext[_storage.depth];
      ctx.inArray = isArray;
      ctx.firstAtDepth = true;
      ctx.count = 0;
      return true;
    }
    inline void closeInternal(const bool isArray){
      _pendingKey = nullptr;
      appendChar(isArray ? ']' : '}');
      _storage.depth--;
    }

    // Writes the separating comma and (for objects) the key. Checks context, limit and duplicates.
    inline bool beginValue(const char *key) {
      WriteContext &ctx = _writeContext[_storage.depth];
      _pendingKey = key;    // lets makeError() name the key if a later append of this value runs out of space

      uint32_t keyHash = 0;
      size_t keyLen = 0;

      if(key){
        if(ctx.inArray){
          return makeError(JsonWriterStatus::JSON_ERR_KEY, key);
        }else if(ctx.count >= JSON_MAX_OBJECT_KEYS || _storage.keyTotal >= JSON_MAX_KEYS){
          return makeError(JsonWriterStatus::JSON_ERR_MAX_KEYS, key);
        }

        // check the length and compute the hash in one pass
        keyHash = 2166136261u;
        while(keyLen <= JSON_MAX_KEY_LEN && key[keyLen]){
          keyHash ^= (uint8_t)key[keyLen];
          keyHash *= 16777619u;
          keyLen++;
        }
        if(keyLen > JSON_MAX_KEY_LEN){
          return makeError(JsonWriterStatus::JSON_ERR_KEY_LEN_LIMIT, key);
        }

        for(uint16_t i = 0; i < ctx.count; i++){
          if(ctx.hashes[i] == keyHash) return makeError(JsonWriterStatus::JSON_ERR_DUPLICATE_KEY, key);
        }
      }else if(!ctx.inArray){
        return makeError(JsonWriterStatus::JSON_ERR_KEY);
      }

      if(!ctx.firstAtDepth) appendChar(',');
      ctx.firstAtDepth = false;

      if(key){
        ctx.hashes[ctx.count++] = keyHash;
        _storage.keyTotal++;

        appendChar('"');
        appendEscaped(key, keyLen);
        appendChar('"');
        appendChar(':');
      }

      return !_runtimeStatus.hasError();
    }
  ENDREGION()

  REGION(append)
    FORCE_INLINE void appendChar(const char c){
      if (_storage.writerPos >= _storage.limit){
        makeError(JsonWriterStatus::JSON_ERR_NO_SPACE);
      }else{
        _storage.jsonOut[_storage.writerPos++] = c;
      }
    }
    // Reserves n bytes in the output buffer and returns the write position.
    // The caller must then write exactly n bytes.
    // If there is not enough space the error is set and nullptr is returned.
    FORCE_INLINE char *reserve(const size_t n){
      if (n > _storage.limit - _storage.writerPos){
        makeError(JsonWriterStatus::JSON_ERR_NO_SPACE);
        return nullptr;
      }
      char *const p = _storage.jsonOut + _storage.writerPos;
      _storage.writerPos += n;
      return p;
    }
    // Write two digits ("00".."99") with byte access. memcpy would be a real function call on small MCUs.
    static FORCE_INLINE void putPair(char *p, const uint32_t v){
      const char *const d = JSON_DIGIT_PAIRS + v * 2;
      p[0] = d[0];
      p[1] = d[1];
    }
    inline void appendRaw(const char *s, const size_t n){
      char *const p = reserve(n);
      if(p) memcpy(p, s, n);
    }
    // Literal with a length known at compile time ("null", "true", ...)
    template<size_t N>
    FORCE_INLINE void appendLit(const char (&s)[N]){
      char *const p = reserve(N - 1);
      if(p){
        for(size_t k = 0; k < N - 1; k++) p[k] = s[k];   // gets unrolled into single instructions, no memcpy call
      }
    }
    // Writes s (content only, without quotes) with JSON escaping.
    inline void appendEscaped(const char *s, const size_t len){
      static constexpr char hex[] = "0123456789abcdef";
      const uint8_t *in = (const uint8_t*)s;
      const uint8_t *const inEnd = in + len;

      if(len * 6 <= _storage.limit - _storage.writerPos){
        // Fast path: even if every character is written as \u00XX the text fits.
        // So no individual checks are necessary.
        char *o = _storage.jsonOut + _storage.writerPos;
        while(in < inEnd){
          const uint8_t c = *in++;
          const uint8_t e = JSON_ESC.v[c];
          if(!e){
            *o++ = (char)c;
            continue;
          }
          *o++ = '\\';
          *o++ = (char)e;
          if(e == 'u'){
            *o++ = '0';
            *o++ = '0';
            *o++ = hex[c >> 4];
            *o++ = hex[c & 0x0F];
          }
        }
        _storage.writerPos = size_t(o - _storage.jsonOut);
        return;
      }

      // Tight buffer: check every character individually
      while(in < inEnd){
        const uint8_t c = *in++;
        const uint8_t e = JSON_ESC.v[c];
        if(!e){
          appendChar((char)c);
          continue;
        }
        appendChar('\\');
        appendChar((char)e);
        if(e == 'u'){
          appendChar('0');
          appendChar('0');
          appendChar(hex[c >> 4]);
          appendChar(hex[c & 0x0F]);
        }
      }
    }
    // Writes a complete string value including the quotes
    inline void appendQuoted(const char *s, const char *key = nullptr){
      const size_t len = jsonBoundedLen(s, JSON_MAX_STRING_LEN);
      if(len > JSON_MAX_STRING_LEN){
        makeError(JsonWriterStatus::JSON_ERR_STRING_LEN_LIMIT, key);
        return;
      }
      appendChar('"');
      appendEscaped(s, len);
      appendChar('"');
    }

    template<typename I>
    inline void appendInt(const I v){
      static_assert(std::is_integral_v<I>, "appendInt: integral type expected");

      if constexpr (sizeof(I) <= sizeof(uint32_t)){
        uint32_t mag;
        if constexpr (std::is_signed_v<I>){
          if(v < 0){
            appendChar('-');
            mag = uint32_t(0) - (uint32_t)(int32_t)v;
          }else{
            mag = (uint32_t)v;
          }
        }else{
          mag = (uint32_t)v;
        }
        appendUnsigned32(mag);
      }else{
        uint64_t mag;
        if constexpr (std::is_signed_v<I>){
          if(v < 0){
            appendChar('-');
            mag = uint64_t(0) - (uint64_t)v;
          }else{
            mag = (uint64_t)v;
          }
        }else{
          mag = (uint64_t)v;
        }
        appendUnsigned64(mag);
      }
    }
    // Digits are written directly into the output buffer (count determined beforehand, only one space check)
    inline void appendUnsigned32(uint32_t x){
      // Branching search instead of 9 comparisons: much cheaper on 32-bit MCUs without division/FPU
      const uint8_t n = (x < 10000u)
        ? ((x < 100u) ? (uint8_t)(1 + (x >= 10u)) : (uint8_t)(3 + (x >= 1000u)))
        : ((x < 1000000u) ? (uint8_t)(5 + (x >= 100000u))
                          : ((x < 100000000u) ? (uint8_t)(7 + (x >= 10000000u)) : (uint8_t)(9 + (x >= 1000000000u))));
      char *const start = reserve(n);
      if(!start) return;
      char *p = start + n;

      while(x >= 100){
        const uint32_t q = x / 100;
        const uint32_t r = x - ((q << 6) + (q << 5) + (q << 2));   // x - q*100, without the compiler turning it into a second division
        p -= 2;
        putPair(p, r);
        x = q;
      }
      if(x >= 10){
        p -= 2;
        putPair(p, x);
      }else{
        *--p = char('0' + x);
      }
    }
    inline void appendUnsigned64(uint64_t x){
      if(x <= 0xFFFFFFFFull){
        appendUnsigned32((uint32_t)x);
        return;
      }

      // from here on at least 10 digits
      uint8_t n = 10;
      uint64_t lim = 10000000000ull;
      while(n < 20 && x >= lim){
        n++;
        lim *= 10;
      }

      char *const start = reserve(n);
      if(!start) return;
      char *p = start + n;

      // 64-bit division is slow on 32-bit MCUs: switch to 32 bit as early as possible
      while(x > 0xFFFFFFFFull){
        const uint64_t q = x / 100;
        const uint32_t r = (uint32_t)(x - q * 100);
        p -= 2;
        putPair(p, r);
        x = q;
      }

      uint32_t y = (uint32_t)x;
      while(y >= 100){
        const uint32_t q = y / 100;
        const uint32_t r = y - q * 100;
        p -= 2;
        putPair(p, r);
        y = q;
      }
      if(y >= 10){
        p -= 2;
        putPair(p, y);
      }else{
        *--p = char('0' + y);
      }
    }
    // Writes '.' and the decimal places (with leading zeros) directly into the buffer
    inline void appendFraction(uint32_t frac, const uint8_t decimals){
      char *const p = reserve(size_t(decimals) + 1);
      if(!p) return;
      p[0] = '.';
      for(uint8_t i = decimals; i > 0; i--){
        const uint32_t q = frac / 10;
        p[i] = char('0' + (frac - q * 10));
        frac = q;
      }
    }
    // Writes a number with a fixed number of decimal places (value must be finite).
    // The integer and fractional parts are rounded separately: value - integer part is exact in floating point,
    // so there is no rounding error from multiplying large values by 10^n,
    // and no division by 10^n is needed.
    template<typename F>
    FORCE_INLINE void appendReal(F value, uint8_t decimals){
      if(decimals > MAX_FLOAT_PRECISION) decimals = MAX_FLOAT_PRECISION;

      const bool neg = value < 0;
      if(neg) value = -value;

      const uint32_t p10 = (uint32_t)JSON_POW10[decimals];

      if(value < (F)4.0e9){                          // the integer part safely fits into 32 bit (fast path)
        uint32_t ip = (uint32_t)value;
        const F frac = value - (F)ip;
        uint32_t fp = (uint32_t)(frac * (F)p10 + (F)0.5);
        if(fp >= p10){                               // round up across the decimal point (0.996 -> 1.00)
          fp -= p10;
          ip++;
        }
        if(neg && (ip != 0 || fp != 0)) appendChar('-');   // no "-0.00"
        appendUnsigned32(ip);
        if(decimals) appendFraction(fp, decimals);
      }else{
        appendRealLarge(value, neg, decimals, p10);
      }
    }
    // Rare path (large values), deliberately not inline
    template<typename F>
    void appendRealLarge(const F value, const bool neg, const uint8_t decimals, const uint32_t p10){
      const F scaled = value * (F)p10 + (F)0.5;
      if(scaled < (F)1.8e19){                         // fits into 64 bit
        const uint64_t total = (uint64_t)scaled;
        if(neg) appendChar('-');
        const uint64_t ip = decimals ? total / p10 : total;
        appendUnsigned64(ip);
        if(decimals) appendFraction((uint32_t)(total - ip * p10), decimals);
      }else{
        // Huge values: exponential notation is valid JSON
        char buf[32];
        const int n = snprintf(buf, sizeof(buf), "%s%.*g", neg ? "-" : "", (int)(sizeof(F) <= 4 ? 9 : 17), (double)value);
        if(n > 0 && n < (int)sizeof(buf)) appendRaw(buf, size_t(n)); else makeError(JsonWriterStatus::JSON_ERR_INVALID_NUMBER);
      }
    }
  ENDREGION()

  REGION(status)
    // Formats the error path from the text written so far: the buffer contains all open containers
    // with their keys, array indices follow from the commas. Strings (incl. escapes) are skipped.
    const char *formatErrorPath() const{
      struct Frame{
        bool isArray;       // this container is an array
        bool viaIndex;      // reached via array index (otherwise via key)
        uint16_t idx;       // array: index of the current element; viaIndex: index in the parent array
        size_t keyPos;      // key start in the buffer (only for !viaIndex)
        size_t keyLen;
      };
      constexpr uint8_t MAXF = uint8_t(JSON_ERR_MAX_PATH);
      Frame f[MAXF];
      f[0] = {false, false, 0, 0, 0};          // root object
      uint16_t depth = 0;                       // depth of the current container (0 = root)
      const char *b = _storage.jsonOut;
      const size_t end = _runtimeStatus.errPos;
      size_t keyPos = 0, keyLen = 0;
      bool expectKey = true;

      for(size_t i = 1; i < end; ){
        const char c = b[i];
        if(c == '"'){
          size_t j = i + 1;
          while(j < end && b[j] != '"') j += (b[j] == '\\') ? 2 : 1;
          const bool isArr = depth < MAXF ? f[depth].isArray : false;
          if(expectKey && !isArr){ keyPos = i + 1; keyLen = (j < end ? j : end) - keyPos; }
          i = j + 1;
          continue;
        }
        switch(c){
          case ':': expectKey = false; break;
          case ',':
            if(depth < MAXF && f[depth].isArray) f[depth].idx++; else expectKey = true;
            break;
          case '{': case '[': {
            const bool parentArr = depth < MAXF ? f[depth].isArray : false;
            const uint16_t parentIdx = depth < MAXF ? f[depth].idx : 0;
            depth++;
            if(depth < MAXF){
              f[depth] = {c == '[', parentArr, parentArr ? parentIdx : uint16_t(0), keyPos, keyLen};
            }
            expectKey = (c == '{');
            break;
          }
          case '}': case ']':
            if(depth == 0) i = end;     // closing '}' of the root object
            else depth--;
            // after closing, the parent container is back in the "value done" state
            expectKey = false;
            break;
          default: break;
        }
        i++;
      }

      JsonPathOut out;
      out.putStr("root");
      const uint16_t shown = depth < MAXF ? depth : uint16_t(MAXF - 1);
      for(uint16_t d = 1; d <= shown; d++){
        if(f[d].viaIndex) out.putIndex(f[d].idx);
        else out.putKey(b + f[d].keyPos, f[d].keyLen);
      }
      if(depth > shown) out.putStr("...");

      if(_runtimeStatus.pathLeaf){
        if(depth < MAXF && f[depth].isArray) out.putIndex(f[depth].idx);
        else if(_runtimeStatus.errKey[0]) { out.put('.'); out.putStr(_runtimeStatus.errKey); }
      }
      return out.end();
    }

    void setRuntimeStatusToDefault(){
      _runtimeStatus.pathValid = false;
      _runtimeStatus.code = JsonWriterStatus::JSON_WRITER_OK;
      _runtimeStatus.message[0] = '\0';
      _runtimeStatus.isJSONFinished = false;
    }
    bool makeError(JsonWriterStatus code, const char *key = nullptr){
      if (_runtimeStatus.hasError()) return false;

      if(_storage.createErrorInfo){
        using S = JsonWriterStatus;
        if(_storage.ready && code != S::JSON_ERR_OUT_OF_MEMORY){
          _runtimeStatus.errPos = _storage.writerPos;
          size_t n = 0;
          // errors raised while a value is being appended know the key from beginValue()
          if(!key && (code == S::JSON_ERR_NO_SPACE || code == S::JSON_ERR_INVALID_NUMBER)) key = _pendingKey;
          if(key) while(n < JSON_MAX_KEY_LEN && key[n]){ _runtimeStatus.errKey[n] = key[n]; n++; }
          _runtimeStatus.errKey[n] = '\0';
          // Structure/state errors only point at the open container, value/key errors also at the element
          _runtimeStatus.pathLeaf = !(code == S::JSON_ERR_FINISHED || code == S::JSON_ERR_FINISH_ARRAY_OPEN ||
                                      code == S::JSON_ERR_FINISH_OBJECT_OPEN || code == S::JSON_ERR_ARRAY_NOT_OPEN ||
                                      code == S::JSON_ERR_OBJECT_NOT_OPEN);
          _runtimeStatus.pathValid = true;
        }
        const char *k = key ? key : "";
        switch(code){
        case S::JSON_ERR_FINISHED:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "JSON already finished");
          break;
        case S::JSON_ERR_KEY_LEN_LIMIT:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "key len exceeded (limit=%u)", unsigned(JSON_MAX_KEY_LEN));
          break;
        case S::JSON_ERR_STRING_LEN_LIMIT:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "string len exceeded (limit=%u)", unsigned(JSON_MAX_STRING_LEN));
          break;
        case S::JSON_ERR_INVALID_NUMBER:
          if(key){
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "invalid number for key '%s'", k);
          }else{
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "invalid number in array");
          }
          break;
        case S::JSON_ERR_ARRAY_OPEN:
          if(key)
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "startArray('%s') with key not allowed inside array", k);
          else
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "startArray() without key not allowed outside array");
          break;
        case S::JSON_ERR_FINISH_ARRAY_OPEN:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "array still open at finish");
          break;
        case S::JSON_ERR_ARRAY_NOT_OPEN:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "endArray not allowed: no open array");
          break;
        case S::JSON_ERR_MAX_DEPTH:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "maximum depth exceeded (limit=%u)", _storage.maxDepth);
          break;
        case S::JSON_ERR_KEY:
          if(key)
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "keys not allowed in arrays (key='%s')", k);
          else
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "value without key not allowed in object");
          break;
        case S::JSON_ERR_MAX_KEYS:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "maximum key count exceeded (limit=%u per object, %u per document)", unsigned(JSON_MAX_OBJECT_KEYS), unsigned(JSON_MAX_KEYS));
          break;
        case S::JSON_ERR_DUPLICATE_KEY:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "duplicate key ('%s')", k);
          break;
        case S::JSON_ERR_OBJECT_OPEN:
          if(key)
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "startObject('%s') with key not allowed inside array", k);
          else
            snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "startObject() without key not allowed outside array");
          break;
        case S::JSON_ERR_FINISH_OBJECT_OPEN:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "object still open at finish");
          break;
        case S::JSON_ERR_OBJECT_NOT_OPEN:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "endObject not allowed: no open object");
          break;
        case S::JSON_ERR_NO_SPACE:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "buffer capacity exceeded (limit=%u)", unsigned(_storage.bufferMax));
          break;
        case S::JSON_ERR_OUT_OF_MEMORY:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "out of memory");
          break;
        default:
          snprintf(_runtimeStatus.message, sizeof(_runtimeStatus.message), "Undefined error code: %d", (int)code);
          break;
        }
      }
      _runtimeStatus.code = code;

      return false;
    }
  ENDREGION()
};
