// src/OutrunExtensions.h
#ifndef OUTRUN_EXTENSIONS_H
#define OUTRUN_EXTENSIONS_H

#include <cstdint>
#include <initializer_list>
#include "OutrunExtension.h"

namespace Outrun {

struct Extensions {
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

} // namespace Outrun

#endif // OUTRUN_EXTENSIONS_H
