// src/ResidentRenderTargets.h — the render-target registry.
//
// The single place a board's drawable surfaces are declared, and the seam
// between "the panel, addressable" (the board's job) and the machinery a
// graphics library needs to draw on it (the module's job).
//
// A board registers ONE library-agnostic target per panel:
//
//   class MyPanel : public Resident::PanelTarget {
//     int32_t width() const override  { return 240; }
//     int32_t height() const override { return 135; }
//     void blit(int32_t x, int32_t y, int32_t w, int32_t h,
//               const uint16_t* px) override { /* push pixels */ }
//   };
//   Resident::RenderTargets::addPanel("main", &myPanel, "rect");
//
// Both graphics modules then build their own machinery over that one target
// when Lua binds it: LgfxModule allocates/owns a full-frame sprite and
// presents it with a single blit; LvglModule creates/owns the lv_display_t
// and its partial draw buffers and wires the flush callback to the same
// blit. Neither needs anything else from the board.
//
// BIND IS THE CLAIM (R19). Two libraries cannot both drive one panel — the
// result is a strobing war between a full-frame push and partial flushes.
// So the registry holds an owner bit per target: `lgfx.bind(name)` /
// `lvgl.bind(name)` are both the app's library DECLARATION and its ownership
// CLAIM (last bind wins, either module), and the NON-owner's present path
// stands down silently — an lgfx flip is dropped, an lvgl flush returns
// without touching the glass. `onAppReset` releases every claim, so each
// app's first bind is a clean claim (and a wiped, unowned LVGL tree cannot
// race a blank frame onto the panel behind the incoming app).
//
// Out of the arbitration by design: the system/status display role and
// overlays. Those write through the board's own path (the driver that owns
// the status text and its sprite), not through a bound library handle.
//
// Embedded-friendly by construction: a static-capacity array of POD entries,
// no std::map, no allocation. Names and shapes are stored as pointers — the
// registrant keeps them alive (string literals in firmware). Registering the
// same name again merges: geometry and shape refresh, panel pointer updates,
// module bits OR together.
//
// Board-lifetime by design: entries persist across app loads and sandbox
// re-setup (surfaces are hardware, not app state); only OWNERSHIP is app
// state. clear() exists for tests.
#ifndef RESIDENT_RENDER_TARGETS_H
#define RESIDENT_RENDER_TARGETS_H

#include <cstdint>
#include <cstring>

namespace Resident {

// ── The library-agnostic panel: geometry + raw blit ──────────────────────
//
// Pixels are RGB565, BIG-ENDIAN (high byte first) — the byte order SPI
// panels take on the wire, which is also LovyanGFX's internal 16-bit sprite
// format (`swap565`). A caller whose pixels are little-endian (LVGL's
// native RGB565 on a little-endian MCU) swaps before calling; LvglModule's
// flush does exactly that.
class PanelTarget {
public:
  virtual ~PanelTarget() = default;

  virtual int32_t width() const = 0;
  virtual int32_t height() const = 0;

  // Push a w×h rectangle of pixels at (x, y). Synchronous: the caller may
  // reuse the buffer as soon as this returns.
  virtual void blit(int32_t x, int32_t y, int32_t w, int32_t h,
                    const uint16_t* px) = 0;
};

class RenderTargets {
public:
  static constexpr int MAX = 8;

  // Module bits for Entry::modules and Entry::owner.
  static constexpr uint8_t MODULE_LGFX = 1 << 0;
  static constexpr uint8_t MODULE_LVGL = 1 << 1;

  struct Entry {
    const char* name = nullptr;
    int32_t w = 0;
    int32_t h = 0;
    const char* shape = "rect";   // "rect" | "round"
    uint8_t modules = 0;          // MODULE_* bitmask: who CAN draw here
    uint8_t owner = 0;            // MODULE_* single bit: who IS driving (0 = nobody)
    PanelTarget* panel = nullptr; // the board's addressable panel (may be null)
  };

  // Register or update a surface. Same name merges (geometry/shape refresh,
  // module bit ORed in); shape == nullptr keeps the existing/default shape.
  // Returns false when the table is full or name is null.
  static bool add(const char* name, int32_t w, int32_t h,
                  const char* shape, uint8_t module) {
    Entry* e = slot(name);
    if (!e) return false;
    e->w = w;
    e->h = h;
    if (shape) e->shape = shape;
    e->modules |= module;
    return true;
  }

  // A module's declaration: "I can draw on this target." Nothing else.
  //
  // No geometry, because a module declares at firmware-setup time — the same
  // static-init window addPanel lives in, where asking anything its size is a
  // crash. And NO SHAPE: whether a panel is round is the board's fact, stated
  // once by addPanel. A module that also asserted a shape would overwrite it
  // with its own default, which is exactly what happened — lgfx's `shape =
  // "rect"` default silently flattened a board's "round" on the line after it
  // was declared, and every consumer downstream believed it.
  // `shape` is accepted but applied ONLY when no board panel is registered
  // for this name — a bare sprite, where the module is the only thing that
  // can say. The registry decides, not the caller, because the caller does
  // not know whether a board got there first.
  static bool declare(const char* name, uint8_t module,
                      const char* shape = nullptr) {
    Entry* e = slot(name);
    if (!e) return false;
    e->modules |= module;
    if (shape && !e->panel) e->shape = shape;
    return true;
  }

  // The board's registration: one panel, geometry read from it. Modules are
  // NOT declared here — a module declares itself when the board hands it the
  // target name (LgfxModule::addDisplay / LvglModule::addDisplay).
  // Registration does NOT ask the panel how big it is. A board registers in
  // its config function, which commonly runs during static init — before
  // M5.begin(), before any display driver's begin() — and a panel asked for
  // its width there is a panel whose hardware object is not constructed yet.
  // On an ESP32-S3 with Arduino core 3.3.9 that is a LoadProhibited crash in
  // global-ctor time, with no serial output to say why.
  //
  // So geometry is READ, never cached: size() below asks the panel at call
  // time. Registration is now safe from anywhere.
  static bool addPanel(const char* name, PanelTarget* p,
                       const char* shape = "rect") {
    if (!p) return false;
    Entry* e = slot(name);
    if (!e) return false;
    e->panel = p;
    if (shape) e->shape = shape;
    return true;
  }

  // Geometry for an entry, from the panel itself whenever it has one. The
  // cached w/h are only for targets registered through add() by a graphics
  // module (sprite-backed ones, which have no panel to ask).
  static void size(const Entry& e, int32_t& w, int32_t& h) {
    w = e.w;
    h = e.h;
    if (!e.panel) return;
    const int32_t pw = e.panel->width();
    const int32_t ph = e.panel->height();
    if (pw > 0 && ph > 0) { w = pw; h = ph; }
  }

  static int count() { return countRef(); }

  // Valid for 0 <= i < count().
  static const Entry& entry(int i) { return entries()[i]; }

  // -1 when unknown.
  static int indexOf(const char* name) {
    if (!name) return -1;
    Entry* e = entries();
    for (int i = 0; i < countRef(); i++) {
      if (e[i].name && strcmp(e[i].name, name) == 0) return i;
    }
    return -1;
  }

  // The board's panel for a target, or null (unknown name, or a target
  // registered by a module that brought its own present path).
  static PanelTarget* panel(const char* name) {
    int i = indexOf(name);
    return i < 0 ? nullptr : entries()[i].panel;
  }

  // ── Ownership: bind is the claim ───────────────────────────────────────

  // Claim a target for a module. Last claim wins — the previous owner's
  // present path stands down from here. Returns false for unknown names.
  static bool claim(const char* name, uint8_t module) {
    int i = indexOf(name);
    if (i < 0) return false;
    entries()[i].owner = module;
    return true;
  }

  // 0 when nobody has claimed it (fresh boot, or after an app reset).
  static uint8_t owner(const char* name) {
    int i = indexOf(name);
    return i < 0 ? 0 : entries()[i].owner;
  }

  // The present-path gate. Deliberately strict: an UNOWNED target answers
  // false for everyone, so a wiped-but-unclaimed library cannot flush.
  static bool isOwner(const char* name, uint8_t module) {
    int i = indexOf(name);
    return i >= 0 && entries()[i].owner != 0 && entries()[i].owner == module;
  }

  // Release every target this module owns (its onAppReset). Returns the
  // number of targets released.
  static int release(uint8_t module) {
    int n = 0;
    Entry* e = entries();
    for (int i = 0; i < countRef(); i++) {
      if (e[i].owner == module) { e[i].owner = 0; n++; }
    }
    return n;
  }

  // Release everything, whoever owns it.
  static void releaseAll() {
    Entry* e = entries();
    for (int i = 0; i < countRef(); i++) e[i].owner = 0;
  }

  // Tests only: surfaces are board-lifetime on hardware.
  static void clear() { countRef() = 0; }

private:
  // Find-or-create the entry for a name; null when full or name is null.
  static Entry* slot(const char* name) {
    int i = indexOf(name);
    if (i >= 0) return &entries()[i];
    if (!name || countRef() >= MAX) return nullptr;
    Entry& e = entries()[countRef()++];
    e = Entry{};
    e.name = name;
    return &e;
  }

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
