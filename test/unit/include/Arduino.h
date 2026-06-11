// Minimal Arduino stub for native (non-Arduino) builds.
// Allows Esp32Lua's Lua.cpp wrapper to compile under the native PlatformIO
// platform without the ESP32 toolchain.  Only the types and symbols that
// appear in Lua.cpp need to be present here.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cassert>
#include <cctype>
#include <string>

// Arduino's String — a thin std::string subclass for native test builds.
// Headers like ResidentSandboxConfig.h declare `std::map<String, String>`
// aliases that need a String type at parse time, and ResidentSandbox.cpp /
// chipstring.h use the Arduino-only surface (numeric-with-base constructor,
// toLowerCase, substring, isEmpty). Inheriting from std::string keeps the
// std interop (comparisons, operator+, map ordering) that earlier tests
// relied on when this was a plain alias.
class String : public std::string {
public:
    String() = default;
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(unsigned long long value, int base = 10) {
        char buf[24];
        snprintf(buf, sizeof(buf), base == 16 ? "%llx" : "%llu", value);
        assign(buf);
    }
    unsigned int length() const { return (unsigned int)size(); }
    bool isEmpty() const { return empty(); }
    String substring(size_t from) const { return String(substr(from)); }
    String substring(size_t from, size_t to) const {
        return String(substr(from, to - from));
    }
    void toLowerCase() {
        for (auto& c : *this) c = (char)tolower((unsigned char)c);
    }
};

// Numeric base constants (Arduino.h defines these as macros)
#define DEC 10
#define HEX 16

// GPIO constants
static constexpr int OUTPUT      = 0x03;
static constexpr int INPUT       = 0x01;
static constexpr int INPUT_PULLUP = 0x05;

// Stub timing — a settable fake clock. Defaults to 0 (the old fixed-zero
// behavior); tests that need elapsed time (e.g. Sandbox's TICK_INTERVAL
// gate) advance it directly: `testMillis() += 200;`. Reset it in setUp().
inline unsigned long& testMillis() { static unsigned long now = 0; return now; }
inline unsigned long millis() { return testMillis(); }

// Stub GPIO
inline void     pinMode(int, int)       {}
inline int      digitalRead(int)        { return 0; }

// Stub esp_random (normally from esp_system.h)
inline uint32_t esp_random()            { return 0; }

// Minimal Print interface (Serial derives from it in real Arduino)
struct Print {
    virtual void write(const char* s) { fputs(s, stdout); }
    virtual void println(const char* s = "") { puts(s); }
    virtual void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
};

// Serial stub — inline (C++17) so all TUs share one Print sink.
inline Print Serial;
