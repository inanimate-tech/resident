// test/unit/include/Esp.h
//
// Minimal ESP class stub for native unit tests. chipstring.h calls
// ESP.getEfuseMac() to derive the device ID; any stable nonzero value works.
#pragma once
#include <cstdint>

struct EspClass {
    uint64_t getEfuseMac() { return 0xA4CF1200CAFEULL; }
};

// Inline (C++17) so all TUs share one instance, like Arduino's global.
inline EspClass ESP;
