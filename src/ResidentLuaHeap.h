#ifndef RESIDENT_LUA_HEAP_H
#define RESIDENT_LUA_HEAP_H

// The Lua state's allocator. Lua allocates and frees small blocks at a high
// rate (a table header, its array, a closure, a short string), and the
// general heap pays a lock and a free-list search for every one of them. Here
// a small block comes from a slab: a 4 KB aligned page holding blocks of one
// size class (8-byte steps up to 256 bytes), with a free list of its own.
// Larger blocks go straight to the backing heap.
//
// Lua passes a block's size back on every free and realloc, so a block needs
// no header: the size gives the class, and the address rounded down to the
// slab size gives the slab. A slab that empties goes back to the backing heap
// unless it is its class's only slab with room, so a burst of garbage does not
// hold memory after it is collected and a steady churn does not thrash pages.
//
// Slabs are for a roomy backing heap (PSRAM): each is a 4 KB aligned block,
// which internal RAM can rarely spare. Constructed without them, every block
// goes straight to the backing heap.
//
// Pure: the backing heap is a set of functions, so the native tests run it
// over malloc and the device over heap_caps. Not thread-safe; a lua_State is
// used from one task.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Resident {

class LuaHeap {
public:
  // The backing heap. alignedAlloc returns `size` bytes aligned to `align` (a
  // power of two), or nullptr; realloc resizes a large block as C's realloc
  // does; release frees what any of them returned. Each is told the block's
  // size, so a backing heap can keep its own accounts.
  struct Backing {
    void* (*alignedAlloc)(size_t align, size_t size, void* ctx);
    void* (*alloc)(size_t size, void* ctx);
    void* (*realloc)(void* ptr, size_t oldSize, size_t newSize, void* ctx);
    void (*release)(void* ptr, size_t size, void* ctx);
    void* ctx;
  };

  struct Stats {
    size_t smallBytes;   // live small blocks, at their class size
    size_t largeBytes;   // live large blocks
    size_t slabBytes;    // slabs held from the backing heap
    uint32_t slabs;
  };

  static constexpr size_t kGrain = 8;
  static constexpr size_t kSmallMax = 256;
  static constexpr size_t kClasses = kSmallMax / kGrain;
  static constexpr size_t kSlabBytes = 4096;

  explicit LuaHeap(const Backing& backing, bool slabs = true)
      : _backing(backing), _smallMax(slabs ? kSmallMax : 0) {}
  LuaHeap(const LuaHeap&) = delete;
  LuaHeap& operator=(const LuaHeap&) = delete;
  ~LuaHeap() { releaseAll(); }

  // lua_Alloc, with the LuaHeap as `ud`.
  static void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    return static_cast<LuaHeap*>(ud)->reallocate(ptr, osize, nsize);
  }

  // With ptr == nullptr, osize is Lua's object type tag rather than a size, and
  // is not read.
  void* reallocate(void* ptr, size_t osize, size_t nsize) {
    if (nsize == 0) {
      if (ptr) release(ptr, osize);
      return nullptr;
    }
    if (!ptr) return allocate(nsize);
    if (isSmall(osize) && isSmall(nsize)) {
      if (classOf(osize) == classOf(nsize)) {
        _stats.smallBytes += classSize(classOf(nsize));
        _stats.smallBytes -= classSize(classOf(osize));
        return ptr;
      }
    } else if (!isSmall(osize) && !isSmall(nsize)) {
      void* p = _backing.realloc(ptr, osize, nsize, _backing.ctx);
      if (p) _stats.largeBytes = _stats.largeBytes - osize + nsize;
      return p;
    }
    void* p = allocate(nsize);
    if (!p) return nullptr;
    memcpy(p, ptr, osize < nsize ? osize : nsize);
    release(ptr, osize);
    return p;
  }

  // Every slab back to the backing heap. Only valid with no live small block,
  // as after lua_close.
  void releaseAll() {
    for (size_t c = 0; c < kClasses; c++) {
      while (Slab* s = _open[c]) {
        unlink(s);
        freeSlab(s);
      }
    }
    // Full slabs are on no list; with no live block there are none.
  }

  const Stats& stats() const { return _stats; }

private:
  struct Slab {
    Slab* prev;
    Slab* next;
    void* freeList;     // freed blocks of this slab
    uint16_t bumped;    // blocks handed out from the never-used tail
    uint16_t inUse;
    uint16_t capacity;
    uint8_t cls;
    bool open;          // on its class's list of slabs with room
  };
  static constexpr size_t kHeader = (sizeof(Slab) + kGrain - 1) / kGrain * kGrain;

  bool isSmall(size_t n) const { return n <= _smallMax; }
  static size_t classOf(size_t n) { return (n + kGrain - 1) / kGrain - 1; }
  static size_t classSize(size_t c) { return (c + 1) * kGrain; }
  static Slab* slabOf(void* p) {
    return reinterpret_cast<Slab*>(reinterpret_cast<uintptr_t>(p) & ~(uintptr_t)(kSlabBytes - 1));
  }
  uint8_t* blocks(Slab* s) const { return reinterpret_cast<uint8_t*>(s) + kHeader; }

  void* allocate(size_t n) {
    if (!isSmall(n)) {
      void* p = _backing.alloc(n, _backing.ctx);
      if (p) _stats.largeBytes += n;
      return p;
    }
    const size_t c = classOf(n);
    Slab* s = _open[c];
    if (!s) {
      s = newSlab(c);
      if (!s) return nullptr;
    }
    void* p;
    if (s->freeList) {
      p = s->freeList;
      s->freeList = *static_cast<void**>(p);
    } else {
      p = blocks(s) + (size_t)s->bumped * classSize(c);
      s->bumped++;
    }
    s->inUse++;
    if (s->inUse == s->capacity) unlink(s);
    _stats.smallBytes += classSize(c);
    return p;
  }

  void release(void* p, size_t n) {
    if (!isSmall(n)) {
      _backing.release(p, n, _backing.ctx);
      _stats.largeBytes -= n;
      return;
    }
    Slab* s = slabOf(p);
    *static_cast<void**>(p) = s->freeList;
    s->freeList = p;
    s->inUse--;
    _stats.smallBytes -= classSize(s->cls);
    if (!s->open) {
      push(s);
    } else if (s->inUse == 0 && (s->prev || s->next)) {
      unlink(s);
      freeSlab(s);
    }
  }

  Slab* newSlab(size_t c) {
    void* mem = _backing.alignedAlloc(kSlabBytes, kSlabBytes, _backing.ctx);
    if (!mem) return nullptr;
    Slab* s = static_cast<Slab*>(mem);
    s->prev = s->next = nullptr;
    s->freeList = nullptr;
    s->bumped = 0;
    s->inUse = 0;
    s->capacity = (uint16_t)((kSlabBytes - kHeader) / classSize(c));
    s->cls = (uint8_t)c;
    s->open = false;
    push(s);
    _stats.slabBytes += kSlabBytes;
    _stats.slabs++;
    return s;
  }

  void freeSlab(Slab* s) {
    _backing.release(s, kSlabBytes, _backing.ctx);
    _stats.slabBytes -= kSlabBytes;
    _stats.slabs--;
  }

  void push(Slab* s) {
    s->prev = nullptr;
    s->next = _open[s->cls];
    if (s->next) s->next->prev = s;
    _open[s->cls] = s;
    s->open = true;
  }

  void unlink(Slab* s) {
    if (s->prev) s->prev->next = s->next;
    else _open[s->cls] = s->next;
    if (s->next) s->next->prev = s->prev;
    s->prev = s->next = nullptr;
    s->open = false;
  }

  Backing _backing;
  size_t _smallMax;   // 0 without slabs: no block is small
  Slab* _open[kClasses] = {};
  Stats _stats = {};
};

}  // namespace Resident

#endif  // RESIDENT_LUA_HEAP_H
