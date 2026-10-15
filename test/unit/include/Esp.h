// test/unit/include/Esp.h
//
// Minimal ESP class stub for native unit tests. chipstring.h calls
// ESP.getEfuseMac() to derive the device ID. Like the real one, it returns
// the base MAC packed little-endian; the default is a4:cf:12:00:ca:fe, and a
// test can set efuseMac to any other.
#pragma once
#include <cstdint>

struct EspClass {
    uint64_t efuseMac = 0xFECA0012CFA4ULL;
    uint64_t getEfuseMac() { return efuseMac; }
};

// Inline (C++17) so all TUs share one instance, like Arduino's global.
inline EspClass ESP;
