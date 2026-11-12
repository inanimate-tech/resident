#pragma once

// A small-object allocator for one thread's heap of short-lived objects — the
// Lua state's, and LVGL's.
//
// Why it exists. On the boards Resident runs on, both heaps live in PSRAM
// behind the system allocator (heap_caps over TLSF): every allocation takes
// the heap's lock, searches and splits a free block whose header is
// somewhere cold in PSRAM, and writes a canary either side of the block.
// Measured on an ESP32-S3 with a 730 KB Lua heap, a three-element table
// constructor — two allocations — cost 24 us with the collector stopped,
// against about 1 us for a loop iteration that allocates nothing. Lua apps
// and LVGL allocate constantly (every `obj:set{...}`, every draw task), so
// that was most of the time in an app's frame.
//
// What it does instead. Memory is taken from the system in REGIONS (256 KB),
// cut into 8 KB CHUNKS. A chunk serves one SIZE CLASS and hands out fixed
// slots; freed slots go on the chunk's own free list, most recent first, so
// the next allocation of that size reuses memory that was just touched and
// is still in the data cache. There is no per-object header (a slot's size
// is its chunk's class), no lock, and the only metadata is twelve bytes per
// chunk at the head of the region. A chunk with no live slots goes back to
// its region for any class to take, and a region with no live chunks goes
// back to the system, so a burst of one size does not pin memory for good.
//
// A freed slot keeps its free list link (a chunk offset) in its first two
// bytes, where a write through a stale pointer can overwrite it. Followed,
// such a link would hand the heap any address in the chunk, the metadata
// included. So a link is checked before it is followed: it must be a slot
// boundary below the chunk's never-used tail. A free is checked too: the
// pointer must be a slot boundary the chunk has handed out, in a chunk that
// serves a class and has live slots, and not the slot freed last. A failed
// check is heap corruption: the arena calls its corruption handler, which by
// default prints what it found and aborts. A handler that returns lets the
// arena carry on without the damage, leaking the memory involved.
//
// What it does not do. Anything over kMaxSmall bytes is not its business:
// the caller sends those to the system allocator. It is not thread-safe: one
// arena belongs to one task. Out of regions (the cap, or the system has no
// more), alloc() returns nullptr and the caller falls back to the system.
//
// Pure C++, no platform headers: the device, the sim and the native tests
// run the same code (test/unit/test/test_slab_alloc).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Resident {

class SlabArena {
public:
  static constexpr size_t kMaxSmall = 512;
  static constexpr size_t kChunkBytes = 8 * 1024;
  static constexpr size_t kChunksPerRegion = 32;
  static constexpr size_t kRegionBytes = kChunkBytes * kChunksPerRegion;   // 256 KB
  static constexpr int kMaxRegions = 8;
  static constexpr int kClasses = 32;

  // Where regions come from: the system allocator, at 8-byte alignment or
  // better. `free` takes what `alloc` returned.
  struct Backend {
    void* (*alloc)(size_t bytes) = nullptr;
    void (*free)(void* p) = nullptr;
  };

  struct Stats {
    size_t regions = 0;        // regions held
    size_t chunksInUse = 0;    // chunks serving a class
    size_t liveSlots = 0;      // allocations outstanding
    size_t liveBytes = 0;      // their slot sizes, summed
    uint32_t regionAllocs = 0; // regions taken from the system, ever
    uint32_t regionFrees = 0;  // regions given back, ever
    uint32_t refusals = 0;     // alloc() calls that returned nullptr
  };

  // Called with what was found and the slot or pointer involved. If it
  // returns, the arena carries on as described above.
  using CorruptionHandler = void (*)(const char* what, const void* block, void* ctx);

  SlabArena() {
    for (int i = 0; i < kClasses; i++) _partial[i] = kNone;
  }

  void setCorruptionHandler(CorruptionHandler h, void* ctx) {
    _onCorrupt = h ? h : abortOnCorruption;
    _onCorruptCtx = h ? ctx : nullptr;
  }

  static void abortOnCorruption(const char* what, const void* block, void*) {
    fprintf(stderr, "SlabArena: heap corruption: %s (block %p)\n", what, block);
    fflush(stderr);
    abort();
  }

  // `maxRegions` caps what the arena may hold (1..kMaxRegions).
  void begin(Backend backend, int maxRegions = kMaxRegions) {
    _backend = backend;
    _maxRegions = maxRegions < 1 ? 1 : (maxRegions > kMaxRegions ? kMaxRegions : maxRegions);
    for (int i = 0; i < kClasses; i++) _partial[i] = kNone;
  }

  bool ready() const { return _backend.alloc != nullptr; }

  // The size class's slot size for a request of n bytes (1..kMaxSmall).
  static size_t slotSize(size_t n) { return kSlot[classOf(n)]; }

  // n in 1..kMaxSmall. nullptr: no region to be had; ask the system.
  void* alloc(size_t n) {
    const int cls = classOf(n);
    uint16_t id;
    Meta* mp;
    uint8_t* base;
    for (;;) {
      id = _partial[cls];
      if (id == kNone) {
        id = takeChunk(cls);
        if (id == kNone) { _stats.refusals++; return nullptr; }
      }
      Region& r = _regions[id / kChunksPerRegion];
      mp = &r.meta()[id % kChunksPerRegion];
      base = r.chunk(id % kChunksPerRegion);
      if (mp->freeHead == kNoSlot) break;
      // The head was checked when it became the head; its link was written
      // into a freed slot and may not be what the arena left there.
      uint16_t next;
      memcpy(&next, base + mp->freeHead, sizeof(next));
      if (next == kNoSlot ||
          (next < mp->bump && next % kSlot[cls] == 0 && next != mp->freeHead)) {
        break;
      }
      corrupt("free list link is not a slot of its chunk", base + mp->freeHead);
      dropFreeList(cls, id, *mp);
    }
    Meta& m = *mp;
    uint8_t* p;
    if (m.freeHead != kNoSlot) {
      p = base + m.freeHead;
      uint16_t next;
      memcpy(&next, p, sizeof(next));
      m.freeHead = next;
    } else {
      p = base + m.bump;
      m.bump = (uint16_t)(m.bump + kSlot[cls]);
    }
    if (++m.used == kCap[cls]) unlinkPartial(cls, id, m);
    _stats.liveSlots++;
    _stats.liveBytes += kSlot[cls];
    return p;
  }

  // Is p a slot of this arena? False for nullptr and for system blocks.
  bool owns(const void* p) const {
    const uintptr_t a = (uintptr_t)p;
    for (int i = 0; i < _regionCount; i++) {
      if (a - (uintptr_t)_regions[i].base < kRegionBytes && _regions[i].base) return true;
    }
    return false;
  }

  // The slot size behind an owned pointer: what may be read or written
  // there, whatever was asked for.
  size_t sizeOf(const void* p) const {
    const uint16_t id = chunkOf(p);
    return kSlot[_regions[id / kChunksPerRegion].meta()[id % kChunksPerRegion].cls];
  }

  // p must be owned (owns(p)).
  void free(void* p) {
    const uint16_t id = chunkOf(p);
    if (id == kNone) { corrupt("free of a pointer the arena does not own", p); return; }
    if (id % kChunksPerRegion == 0) { corrupt("free inside a region's metadata", p); return; }
    const int ri = id / kChunksPerRegion;
    Region& r = _regions[ri];
    Meta& m = r.meta()[id % kChunksPerRegion];
    if (!m.inUse || m.used == 0) { corrupt("free in a chunk with no live slots", p); return; }
    const int cls = m.cls;
    uint8_t* base = r.chunk(id % kChunksPerRegion);
    const uint16_t off = (uint16_t)((uint8_t*)p - base);
    if (off >= m.bump || off % kSlot[cls] != 0) {
      corrupt("free of a slot this chunk did not hand out", p);
      return;
    }
    if (off == m.freeHead) { corrupt("slot freed twice", p); return; }
    const uint16_t head = m.freeHead;
    memcpy(p, &head, sizeof(head));
    m.freeHead = off;
    const bool wasFull = m.used == kCap[cls];
    _stats.liveSlots--;
    _stats.liveBytes -= kSlot[cls];
    if (--m.used == 0) {
      if (!wasFull) unlinkPartial(cls, id, m);
      releaseChunk(ri, id);
    } else if (wasFull) {
      linkPartial(cls, id, m);
    }
  }

  const Stats& stats() const { return _stats; }

  // Give every region back. Nothing allocated from the arena may be used
  // afterwards; begin() need not be called again.
  void reset() {
    for (int i = 0; i < _regionCount; i++) {
      if (_regions[i].base) { _backend.free(_regions[i].base); _stats.regionFrees++; }
      _regions[i] = Region();
    }
    _regionCount = 0;
    for (int i = 0; i < kClasses; i++) _partial[i] = kNone;
    _stats.regions = _stats.chunksInUse = _stats.liveSlots = _stats.liveBytes = 0;
  }

private:
  static constexpr uint16_t kNone = 0xFFFF;     // no chunk
  static constexpr uint16_t kNoSlot = 0xFFFF;   // no free slot (offsets are < kChunkBytes)

  // Twelve bytes a chunk, in the region's first chunk-sized block.
  struct Meta {
    uint16_t next, prev;   // this class's list of chunks with a free slot
    uint16_t freeHead;     // byte offset of the most recently freed slot
    uint16_t bump;         // byte offset of the first never-used slot
    uint16_t used;         // live slots
    uint8_t cls;
    uint8_t inUse;         // serving a class (not on the region's empty list)
  };

  // Chunk 0 of a region holds the metadata; chunks 1.. are handed out.
  struct Region {
    uint8_t* base = nullptr;
    uint16_t inUse = 0;      // chunks serving a class
    uint16_t fresh = 1;      // first chunk never handed out
    uint16_t emptyHead = kNone;   // chunks handed out before and empty now (linked through Meta::next)
    Meta* meta() const { return reinterpret_cast<Meta*>(base); }
    uint8_t* chunk(size_t i) const { return base + i * kChunkBytes; }
  };

  // 8-byte steps to 128, 16 to 256, 32 to 512.
  static int classOf(size_t n) {
    if (n <= 128) return n ? (int)((n - 1) >> 3) : 0;
    if (n <= 256) return 16 + (int)((n - 129) >> 4);
    return 24 + (int)((n - 257) >> 5);
  }

  static constexpr uint16_t kSlot[kClasses] = {
      8,   16,  24,  32,  40,  48,  56,  64,  72,  80,  88,  96,  104, 112, 120, 128,
      144, 160, 176, 192, 208, 224, 240, 256, 288, 320, 352, 384, 416, 448, 480, 512};
  static constexpr uint16_t kCap[kClasses] = {
      8192 / 8,   8192 / 16,  8192 / 24,  8192 / 32,  8192 / 40,  8192 / 48,  8192 / 56,
      8192 / 64,  8192 / 72,  8192 / 80,  8192 / 88,  8192 / 96,  8192 / 104, 8192 / 112,
      8192 / 120, 8192 / 128, 8192 / 144, 8192 / 160, 8192 / 176, 8192 / 192, 8192 / 208,
      8192 / 224, 8192 / 240, 8192 / 256, 8192 / 288, 8192 / 320, 8192 / 352, 8192 / 384,
      8192 / 416, 8192 / 448, 8192 / 480, 8192 / 512};

  uint16_t chunkOf(const void* p) const {
    const uintptr_t a = (uintptr_t)p;
    for (int i = 0; i < _regionCount; i++) {
      const uintptr_t d = a - (uintptr_t)_regions[i].base;
      if (d < kRegionBytes && _regions[i].base) {
        return (uint16_t)(i * kChunksPerRegion + d / kChunkBytes);
      }
    }
    return kNone;   // not reached for an owned pointer
  }

  void corrupt(const char* what, const void* block) { _onCorrupt(what, block, _onCorruptCtx); }

  // Forgets a free list that cannot be trusted, with every slot on it and the
  // never-used tail: the chunk counts as full, so it is never allocated from
  // again until a live slot is freed, and never empties.
  void dropFreeList(int cls, uint16_t id, Meta& m) {
    m.freeHead = kNoSlot;
    m.bump = (uint16_t)(kCap[cls] * kSlot[cls]);
    if (m.used != kCap[cls]) {
      unlinkPartial(cls, id, m);
      m.used = kCap[cls];
    }
  }

  Meta& metaOf(uint16_t id) {
    return _regions[id / kChunksPerRegion].meta()[id % kChunksPerRegion];
  }

  void linkPartial(int cls, uint16_t id, Meta& m) {
    m.prev = kNone;
    m.next = _partial[cls];
    if (m.next != kNone) metaOf(m.next).prev = id;
    _partial[cls] = id;
  }

  void unlinkPartial(int cls, uint16_t id, Meta& m) {
    if (m.prev != kNone) metaOf(m.prev).next = m.next;
    else _partial[cls] = m.next;
    if (m.next != kNone) metaOf(m.next).prev = m.prev;
    (void)id;
  }

  // An empty chunk for `cls`, at the head of its partial list: one emptied
  // earlier (its lines are likelier to be cached), else a never-used one,
  // else from a new region.
  uint16_t takeChunk(int cls) {
    int ri = -1;
    for (int i = 0; i < _regionCount && ri < 0; i++) {
      if (_regions[i].base && _regions[i].emptyHead != kNone) ri = i;
    }
    for (int i = 0; i < _regionCount && ri < 0; i++) {
      if (_regions[i].base && _regions[i].fresh < kChunksPerRegion) ri = i;
    }
    if (ri < 0) ri = addRegion();
    if (ri < 0) return kNone;

    Region& r = _regions[ri];
    uint16_t ci;
    if (r.emptyHead != kNone) {
      ci = r.emptyHead;
      r.emptyHead = r.meta()[ci].next;
    } else {
      ci = r.fresh++;
    }
    Meta& m = r.meta()[ci];
    m.cls = (uint8_t)cls;
    m.inUse = 1;
    m.used = 0;
    m.bump = 0;
    m.freeHead = kNoSlot;
    r.inUse++;
    _stats.chunksInUse++;
    const uint16_t id = (uint16_t)(ri * kChunksPerRegion + ci);
    linkPartial(cls, id, m);
    return id;
  }

  // The chunk has no live slot and is on no partial list.
  void releaseChunk(int ri, uint16_t id) {
    Region& r = _regions[ri];
    const uint16_t ci = id % kChunksPerRegion;
    Meta& m = r.meta()[ci];
    m.inUse = 0;
    m.next = r.emptyHead;
    r.emptyHead = ci;
    _stats.chunksInUse--;
    // The last region is kept: an app idling at a few objects must not take
    // and give back 256 KB on every allocation.
    if (--r.inUse == 0 && _stats.regions > 1) {
      _backend.free(r.base);
      r = Region();
      _stats.regions--;
      _stats.regionFrees++;
    }
  }

  int addRegion() {
    int slot = -1;
    for (int i = 0; i < _regionCount && slot < 0; i++) {
      if (!_regions[i].base) slot = i;
    }
    if (slot < 0) {
      if (_regionCount >= _maxRegions) return -1;
      slot = _regionCount;
    }
    if (!_backend.alloc) return -1;
    uint8_t* base = (uint8_t*)_backend.alloc(kRegionBytes);
    if (!base) return -1;
    if (slot == _regionCount) _regionCount++;
    _regions[slot] = Region();
    _regions[slot].base = base;
    _stats.regions++;
    _stats.regionAllocs++;
    return slot;
  }

  Backend _backend;
  int _maxRegions = kMaxRegions;
  int _regionCount = 0;       // slots of _regions ever used (some may be empty again)
  Region _regions[kMaxRegions];
  uint16_t _partial[kClasses];
  Stats _stats;
  CorruptionHandler _onCorrupt = abortOnCorruption;
  void* _onCorruptCtx = nullptr;
};

// The three calls a heap needs, over an arena and the system allocator behind
// it: small blocks from the arena, the rest (and everything once the arena is
// out of regions) from the system. `Sys` supplies
//   static void* alloc(size_t)            static void free(void*)
//   static void* realloc(void*, size_t)   static size_t sizeOf(void*)
// for system blocks; sizeOf may return more than was asked for, never less.
template <class Sys>
struct SlabHeap {
  static void* alloc(SlabArena& a, size_t n) {
    if (n == 0) n = 1;
    if (n <= SlabArena::kMaxSmall && a.ready()) {
      if (void* p = a.alloc(n)) return p;
    }
    return Sys::alloc(n);
  }

  static void free(SlabArena& a, void* p) {
    if (!p) return;
    if (a.owns(p)) a.free(p);
    else Sys::free(p);
  }

  // realloc's contract, including that a failure leaves p as it was. A
  // shrink never fails: a block that cannot move stays where it is.
  static void* realloc(SlabArena& a, void* p, size_t n) {
    if (!p) return alloc(a, n);
    if (n == 0) n = 1;
    if (a.owns(p)) {
      const size_t have = a.sizeOf(p);
      if (n <= have && SlabArena::slotSize(n) == have) return p;
      void* q = alloc(a, n);
      if (!q) return n <= have ? p : nullptr;
      memcpy(q, p, n < have ? n : have);
      a.free(p);
      return q;
    }
    if (n > SlabArena::kMaxSmall || !a.ready()) return Sys::realloc(p, n);
    // A system block shrinking into the arena's range.
    void* q = a.alloc(n);
    if (!q) return Sys::realloc(p, n);
    const size_t have = Sys::sizeOf(p);
    memcpy(q, p, n < have ? n : have);
    Sys::free(p);
    return q;
  }
};

}  // namespace Resident
