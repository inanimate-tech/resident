// src/ResidentExtensions.h
#ifndef RESIDENT_EXTENSIONS_H
#define RESIDENT_EXTENSIONS_H

#include <cstdint>
#include <initializer_list>
#include "ResidentExtension.h"

namespace Resident {

struct Extensions {
  // MAX must stay below 256 because count is uint8_t.
  static constexpr int MAX = 8;
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
