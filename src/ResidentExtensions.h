// src/ResidentExtensions.h
#ifndef RESIDENT_EXTENSIONS_H
#define RESIDENT_EXTENSIONS_H

#include <cstdint>
#include <initializer_list>
#include "ResidentExtension.h"

namespace Resident {

struct Extensions {
  // MAX must stay below 256 because count is uint8_t. 16 (was 12, was 8):
  // a fully loaded device filled all 12 — its drivers plus the graphics
  // modules plus the platform's own appended modules — leaving no slot for
  // a new one. A slot is one pointer here and one in Sandbox::_lifecycle.
  // Sandbox::_lifecycle is sized Extensions::MAX + 4 (role slots), so it
  // grows with this constant.
  static constexpr int MAX = 16;
  Extension* items[MAX] = {};
  uint8_t count = 0;

  Extensions() = default;
  Extensions(std::initializer_list<Extension*> list) {
    for (auto e : list) {
      if (count >= MAX) break;
      items[count++] = e;
    }
  }
};

} // namespace Resident

#endif // RESIDENT_EXTENSIONS_H
