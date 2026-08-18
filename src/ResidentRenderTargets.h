// src/ResidentRenderTargets.h — the render-target registry (arc R17).
//
// The single place a board's drawable surfaces are declared. Both graphics
// modules feed it — LgfxModule::addDisplay and LvglModule::addDisplay
// register/update an entry as a side effect — and the device hello reads it
// (Sandbox::sendHello's `surfaces` array), so a host learns every drawable
// surface, its geometry, its shape, and which Lua modules can draw on it
// without assuming any of it.
//
// Embedded-friendly by construction: a static-capacity array of POD entries,
// no std::map, no allocation. Names and shapes are stored as pointers — the
// registrant keeps them alive (string literals in firmware, as with
// LgfxModule's display names). Registering the same name again merges:
// geometry and shape refresh, module bits OR together — which is exactly what
// happens when lgfx and lvgl both drive one panel.
//
// Board-lifetime by design: entries persist across app loads and sandbox
// re-setup (surfaces are hardware, not app state). clear() exists for tests.
#ifndef RESIDENT_RENDER_TARGETS_H
#define RESIDENT_RENDER_TARGETS_H

#include <cstdint>
#include <cstring>

namespace Resident {

class RenderTargets {
public:
  static constexpr int MAX = 8;

  // Module bits for Entry::modules (reported as strings in the hello).
  static constexpr uint8_t MODULE_LGFX = 1 << 0;
  static constexpr uint8_t MODULE_LVGL = 1 << 1;

  struct Entry {
    const char* name = nullptr;
    int32_t w = 0;
    int32_t h = 0;
    const char* shape = "rect";   // "rect" | "round"
    uint8_t modules = 0;          // MODULE_* bitmask
  };

  // Register or update a surface. Same name merges (geometry/shape refresh,
  // module bit ORed in); shape == nullptr keeps the existing/default shape.
  // Returns false when the table is full or name is null.
  static bool add(const char* name, int32_t w, int32_t h,
                  const char* shape, uint8_t module) {
    if (!name) return false;
    Entry* e = entries();
    for (int i = 0; i < countRef(); i++) {
      if (strcmp(e[i].name, name) == 0) {
        e[i].w = w;
        e[i].h = h;
        if (shape) e[i].shape = shape;
        e[i].modules |= module;
        return true;
      }
    }
    if (countRef() >= MAX) return false;
    Entry& slot = e[countRef()++];
    slot.name = name;
    slot.w = w;
    slot.h = h;
    slot.shape = shape ? shape : "rect";
    slot.modules = module;
    return true;
  }

  static int count() { return countRef(); }

  // Valid for 0 <= i < count().
  static const Entry& entry(int i) { return entries()[i]; }

  // Tests only: surfaces are board-lifetime on hardware.
  static void clear() { countRef() = 0; }

private:
  // Function-local statics: header-only (one definition across TUs) without
  // tripping clang's NSDMI-in-inline-static-initializer limitation.
  static Entry* entries() {
    static Entry list[MAX];
    return list;
  }
  static int& countRef() {
    static int n = 0;
    return n;
  }
};

} // namespace Resident

#endif // RESIDENT_RENDER_TARGETS_H
