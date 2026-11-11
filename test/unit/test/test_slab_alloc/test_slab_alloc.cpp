// SlabArena and SlabHeap (ResidentSlabAlloc.h): the Lua and LVGL heaps'
// small-object allocator. An allocator's bugs are memory corruption, so the
// core of this is a long random walk checked against a shadow of every live
// block: no two blocks overlap, and a block still holds what was written to
// it when it is read back, resized or freed.
#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "ResidentSlabAlloc.h"

using Resident::SlabArena;
using Resident::SlabHeap;

namespace {

int g_regionAllocs = 0, g_regionFrees = 0;
bool g_refuseRegions = false;

void* regionAlloc(size_t n) {
  if (g_refuseRegions) return nullptr;
  g_regionAllocs++;
  return malloc(n);
}
void regionFree(void* p) {
  g_regionFrees++;
  free(p);
}

SlabArena::Backend backend() {
  SlabArena::Backend b;
  b.alloc = regionAlloc;
  b.free = regionFree;
  return b;
}

// The system side of a SlabHeap, with its sizes kept so sizeOf is exact.
std::map<void*, size_t> g_sys;
struct Sys {
  static void* alloc(size_t n) {
    void* p = malloc(n);
    g_sys[p] = n;
    return p;
  }
  static void free(void* p) {
    TEST_ASSERT_TRUE(g_sys.count(p) == 1);
    g_sys.erase(p);
    ::free(p);
  }
  static void* realloc(void* p, size_t n) {
    TEST_ASSERT_TRUE(g_sys.count(p) == 1);
    g_sys.erase(p);
    void* q = ::realloc(p, n);
    g_sys[q] = n;
    return q;
  }
  static size_t sizeOf(void* p) { return g_sys.at(p); }
};

uint32_t g_rng = 0x1234567u;
uint32_t rnd() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

void fill(void* p, size_t n, uint8_t seed) {
  uint8_t* b = (uint8_t*)p;
  for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + i * 7);
}
bool holds(const void* p, size_t n, uint8_t seed) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) {
    if (b[i] != (uint8_t)(seed + i * 7)) return false;
  }
  return true;
}

void resetCounters() {
  g_regionAllocs = g_regionFrees = 0;
  g_refuseRegions = false;
  g_sys.clear();
}

}  // namespace

void setUp() { resetCounters(); }
void tearDown() {}

void test_slot_sizes_cover_their_requests() {
  for (size_t n = 1; n <= SlabArena::kMaxSmall; n++) {
    const size_t s = SlabArena::slotSize(n);
    TEST_ASSERT_TRUE(s >= n);
    TEST_ASSERT_EQUAL(0, s % 8);
    // No smaller class would have done.
    if (n > 1) TEST_ASSERT_TRUE(SlabArena::slotSize(n - 1) <= s);
    TEST_ASSERT_TRUE(s - n < 32);
  }
  TEST_ASSERT_EQUAL(8, SlabArena::slotSize(1));
  TEST_ASSERT_EQUAL(512, SlabArena::slotSize(512));
}

void test_slots_are_aligned_and_owned() {
  SlabArena a;
  a.begin(backend());
  for (size_t n : {1, 8, 9, 24, 100, 129, 300, 512}) {
    void* p = a.alloc(n);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(0, (uintptr_t)p % 8);
    TEST_ASSERT_TRUE(a.owns(p));
    TEST_ASSERT_EQUAL(SlabArena::slotSize(n), a.sizeOf(p));
  }
  int local = 0;
  TEST_ASSERT_FALSE(a.owns(&local));
  TEST_ASSERT_FALSE(a.owns(nullptr));
  a.reset();
  TEST_ASSERT_EQUAL(g_regionAllocs, g_regionFrees);
}

void test_a_freed_slot_is_the_next_one_handed_out() {
  SlabArena a;
  a.begin(backend());
  void* p = a.alloc(40);
  void* q = a.alloc(40);
  a.free(p);
  TEST_ASSERT_EQUAL_PTR(p, a.alloc(40));
  a.free(q);
  a.free(p);
  // Most recently freed first.
  TEST_ASSERT_EQUAL_PTR(p, a.alloc(40));
  TEST_ASSERT_EQUAL_PTR(q, a.alloc(40));
  a.reset();
}

void test_an_emptied_chunk_serves_another_class() {
  SlabArena a;
  a.begin(backend(), 1);
  // Fill the one region with 512-byte slots, then free them all: every
  // chunk must come back for 16-byte slots to use.
  std::vector<void*> big;
  while (void* p = a.alloc(512)) big.push_back(p);
  TEST_ASSERT_EQUAL((SlabArena::kChunksPerRegion - 1) * (SlabArena::kChunkBytes / 512), big.size());
  TEST_ASSERT_NULL(a.alloc(16));
  for (void* p : big) a.free(p);
  TEST_ASSERT_EQUAL(0, a.stats().liveSlots);
  TEST_ASSERT_EQUAL(0, a.stats().chunksInUse);
  size_t small = 0;
  std::vector<void*> smalls;
  while (void* p = a.alloc(16)) { smalls.push_back(p); small++; }
  TEST_ASSERT_EQUAL((SlabArena::kChunksPerRegion - 1) * (SlabArena::kChunkBytes / 16), small);
  for (void* p : smalls) a.free(p);
  a.reset();
}

void test_an_empty_region_goes_back_but_the_last_is_kept() {
  SlabArena a;
  a.begin(backend(), 4);
  std::vector<void*> v;
  const size_t perRegion = (SlabArena::kChunksPerRegion - 1) * (SlabArena::kChunkBytes / 256);
  for (size_t i = 0; i < perRegion * 3; i++) v.push_back(a.alloc(256));
  TEST_ASSERT_EQUAL(3, a.stats().regions);
  for (void* p : v) a.free(p);
  TEST_ASSERT_EQUAL(1, a.stats().regions);
  TEST_ASSERT_EQUAL(2, g_regionFrees);
  // And the kept region still serves.
  void* p = a.alloc(64);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL(3, g_regionAllocs);
  a.free(p);
  a.reset();
  TEST_ASSERT_EQUAL(g_regionAllocs, g_regionFrees);
}

void test_the_cap_and_a_refusing_system_return_null() {
  SlabArena a;
  a.begin(backend(), 1);
  size_t n = 0;
  std::vector<void*> v;
  while (void* p = a.alloc(64)) { v.push_back(p); n++; }
  TEST_ASSERT_EQUAL((SlabArena::kChunksPerRegion - 1) * (SlabArena::kChunkBytes / 64), n);
  TEST_ASSERT_TRUE(a.stats().refusals >= 1);
  for (void* p : v) a.free(p);
  a.reset();

  SlabArena b;
  b.begin(backend());
  g_refuseRegions = true;
  TEST_ASSERT_NULL(b.alloc(64));
  g_refuseRegions = false;
  void* p = b.alloc(64);
  TEST_ASSERT_NOT_NULL(p);
  b.free(p);
  b.reset();
}

// The random walk, through SlabHeap so system blocks and the moves between
// the two are in it.
void test_random_walk_against_a_shadow() {
  SlabArena a;
  a.begin(backend(), 3);
  struct Block { void* p; size_t n; uint8_t seed; };
  std::vector<Block> live;
  size_t peak = 0;

  auto check = [&](const Block& b) { TEST_ASSERT_TRUE(holds(b.p, b.n, b.seed)); };
  auto size = [&]() -> size_t {
    const uint32_t r = rnd() % 100;
    if (r < 60) return 1 + rnd() % 64;
    if (r < 90) return 1 + rnd() % 512;
    return 513 + rnd() % 3000;   // the system's
  };

  for (int step = 0; step < 400000; step++) {
    const uint32_t op = rnd() % 100;
    // Breathe: grow to a few thousand blocks, shrink to nothing, so chunks
    // and regions are taken and given back many times.
    const bool growing = (step / 20000) % 2 == 0;
    if (live.empty() || op < (growing ? 55u : 30u)) {
      Block b{nullptr, size(), (uint8_t)rnd()};
      b.p = SlabHeap<Sys>::alloc(a, b.n);
      TEST_ASSERT_NOT_NULL(b.p);
      if (b.n > SlabArena::kMaxSmall) TEST_ASSERT_FALSE(a.owns(b.p));
      fill(b.p, b.n, b.seed);
      live.push_back(b);
    } else if (op < 85) {
      const size_t i = rnd() % live.size();
      check(live[i]);
      SlabHeap<Sys>::free(a, live[i].p);
      live[i] = live.back();
      live.pop_back();
    } else {
      const size_t i = rnd() % live.size();
      Block& b = live[i];
      check(b);
      const size_t n = size();
      void* q = SlabHeap<Sys>::realloc(a, b.p, n);
      TEST_ASSERT_NOT_NULL(q);
      TEST_ASSERT_TRUE(holds(q, n < b.n ? n : b.n, b.seed));
      b.p = q;
      b.n = n;
      b.seed = (uint8_t)rnd();
      fill(b.p, b.n, b.seed);
    }
    if (live.size() > peak) peak = live.size();
    if (step % 50000 == 0) {
      for (const Block& b : live) check(b);
    }
  }
  TEST_ASSERT_TRUE(peak > 1000);
  for (const Block& b : live) {
    check(b);
    SlabHeap<Sys>::free(a, b.p);
  }
  TEST_ASSERT_EQUAL(0, a.stats().liveSlots);
  TEST_ASSERT_EQUAL(0, a.stats().liveBytes);
  TEST_ASSERT_EQUAL(0, a.stats().chunksInUse);
  TEST_ASSERT_EQUAL(0, g_sys.size());
  a.reset();
  TEST_ASSERT_EQUAL(g_regionAllocs, g_regionFrees);
}

// No two live slots share a byte, across classes and chunks.
void test_live_slots_never_overlap() {
  SlabArena a;
  a.begin(backend(), 2);
  std::map<uintptr_t, size_t> spans;   // start -> length
  std::vector<void*> v;
  for (int i = 0; i < 60000; i++) {
    if (!v.empty() && rnd() % 3 == 0) {
      const size_t k = rnd() % v.size();
      spans.erase((uintptr_t)v[k]);
      a.free(v[k]);
      v[k] = v.back();
      v.pop_back();
      continue;
    }
    const size_t n = 1 + rnd() % 512;
    void* p = a.alloc(n);
    if (!p) continue;
    const uintptr_t s = (uintptr_t)p;
    const size_t len = a.sizeOf(p);
    auto next = spans.lower_bound(s);
    if (next != spans.end()) TEST_ASSERT_TRUE(s + len <= next->first);
    if (next != spans.begin()) {
      auto prev = std::prev(next);
      TEST_ASSERT_TRUE(prev->first + prev->second <= s);
    }
    spans[s] = len;
    v.push_back(p);
  }
  for (void* p : v) a.free(p);
  a.reset();
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_slot_sizes_cover_their_requests);
  RUN_TEST(test_slots_are_aligned_and_owned);
  RUN_TEST(test_a_freed_slot_is_the_next_one_handed_out);
  RUN_TEST(test_an_emptied_chunk_serves_another_class);
  RUN_TEST(test_an_empty_region_goes_back_but_the_last_is_kept);
  RUN_TEST(test_the_cap_and_a_refusing_system_return_null);
  RUN_TEST(test_random_walk_against_a_shadow);
  RUN_TEST(test_live_slots_never_overlap);
  return UNITY_END();
}
