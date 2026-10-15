#pragma once

// Minimal Arduino stand-in so the examples and tests can be built on a PC.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <climits>
#include <cstdint>
#include <chrono>
#include <string>

#define F_CPU 1000000000UL
#define ARDUINO_BOARD "PC mock"

inline uint32_t micros() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline uint32_t millis() { return micros() / 1000; }
inline void delay(unsigned) {}

class String {
public:
  String() {}
  String(const char *c) : s(c ? c : "") {}

  String &operator=(const char *c) {
    s = c ? c : "";
    return *this;
  }

  const char *c_str() const { return s.c_str(); }

private:
  std::string s;
};

struct SerialMock {
  explicit operator bool() const { return true; }

  void begin(unsigned) {}
  int available() { return 0; }
  int read() { return -1; }

  void print(const char *s) { fputs(s ? s : "", stdout); }
  void print(const String &s) { print(s.c_str()); }
  void print(char c) { fputc(c, stdout); }
  void print(bool v) { print(v ? "1" : "0"); }

  void print(int v) { printf("%d", v); }
  void print(unsigned v) { printf("%u", v); }
  void print(long v) { printf("%ld", v); }
  void print(unsigned long v) { printf("%lu", v); }
  void print(long long v) { printf("%lld", v); }
  void print(unsigned long long v) { printf("%llu", v); }

  void print(float v) { printf("%g", (double)v); }
  void print(double v) { printf("%g", v); }

  void print(float v, int digits) { printf("%.*f", digits, (double)v); }
  void print(double v, int digits) { printf("%.*f", digits, v); }

  void println() { fputc('\n', stdout); }

  template<typename T>
  void println(const T &v) {
    print(v);
    fputc('\n', stdout);
  }

  void println(float v, int digits) {
    print(v, digits);
    fputc('\n', stdout);
  }

  void println(double v, int digits) {
    print(v, digits);
    fputc('\n', stdout);
  }
};

static SerialMock Serial;

void setup();
void loop();
