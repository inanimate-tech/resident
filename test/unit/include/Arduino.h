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
#include <string>

// Arduino's String — aliased to std::string for native test builds. Headers
// like ResidentSandboxConfig.h declare `std::map<String, String>` aliases
// that need a String type at parse time (even if unused at runtime). This is
// purely additive: Esp32Lua's Lua.cpp wrapper doesn't reference String, so
// the alias can't regress existing Lua tests.
using String = std::string;

// GPIO constants
static constexpr int OUTPUT      = 0x03;
static constexpr int INPUT       = 0x01;
static constexpr int INPUT_PULLUP = 0x05;

// Stub timing — returns 0 by design; tests needing real elapsed time should
// use the raw C clock API directly or inject a clock abstraction instead.
inline unsigned long millis() { return 0; }

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
