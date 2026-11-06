#include <unity.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>
#include "ResidentLuaHeap.h"
extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

using Resident::LuaHeap;

// A backing heap over malloc that counts what it holds, and can be told to
// refuse new slabs.
struct Counted {
  size_t liveBytes = 0;
  int liveBlocks = 0;
  bool refuseSlabs = false;
};

static void* countedAligned(size_t align, size_t size, void* ctx) {
  Counted* c = static_cast<Counted*>(ctx);
  if (c->refuseSlabs) return nullptr;
  void* p = nullptr;
  if (posix_memalign(&p, align, size) != 0) return nullptr;
  c->liveBytes += size;
  c->liveBlocks++;
  return p;
}
static void* countedAlloc(size_t size, void* ctx) {
  Counted* c = static_cast<Counted*>(ctx);
  void* p = malloc(size);
  if (p) { c->liveBytes += size; c->liveBlocks++; }
  return p;
}
static void* countedRealloc(void* ptr, size_t oldSize, size_t newSize, void* ctx) {
  Counted* c = static_cast<Counted*>(ctx);
  void* p = realloc(ptr, newSize);
  if (p) c->liveBytes = c->liveBytes - oldSize + newSize;
  return p;
}
static void countedRelease(void* ptr, size_t size, void* ctx) {
  Counted* c = static_cast<Counted*>(ctx);
  free(ptr);
  c->liveBytes -= size;
  c->liveBlocks--;
}

static Counted g_counted;
static LuaHeap::Backing backing() {
  return {countedAligned, countedAlloc, countedRealloc, countedRelease, &g_counted};
}

void setUp(void) { g_counted = Counted{}; }
void tearDown(void) {}

void test_small_blocks_share_a_slab_and_are_aligned(void) {
  LuaHeap heap(backing());
  void* a = heap.reallocate(nullptr, 0, 24);
  void* b = heap.reallocate(nullptr, 0, 20);   // same 24-byte class
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_EQUAL_UINT(0, (uintptr_t)a % LuaHeap::kGrain);
  TEST_ASSERT_EQUAL_UINT(0, (uintptr_t)b % LuaHeap::kGrain);
  TEST_ASSERT_EQUAL_UINT32(1, heap.stats().slabs);
  TEST_ASSERT_EQUAL_UINT(48, heap.stats().smallBytes);
  heap.reallocate(a, 24, 0);
  heap.reallocate(b, 20, 0);
  TEST_ASSERT_EQUAL_UINT(0, heap.stats().smallBytes);
}

void test_a_freed_block_is_reused(void) {
  LuaHeap heap(backing());
  void* a = heap.reallocate(nullptr, 0, 32);
  heap.reallocate(a, 32, 0);
  void* b = heap.reallocate(nullptr, 0, 32);
  TEST_ASSERT_EQUAL_PTR(a, b);
  heap.reallocate(b, 32, 0);
}

void test_realloc_within_a_class_keeps_the_block(void) {
  LuaHeap heap(backing());
  void* a = heap.reallocate(nullptr, 0, 17);
  TEST_ASSERT_EQUAL_PTR(a, heap.reallocate(a, 17, 24));
  heap.reallocate(a, 24, 0);
}

void test_realloc_across_classes_and_sizes_keeps_the_contents(void) {
  LuaHeap heap(backing());
  char* p = static_cast<char*>(heap.reallocate(nullptr, 0, 16));
  memcpy(p, "0123456789abcde", 16);
  p = static_cast<char*>(heap.reallocate(p, 16, 200));       // small -> small
  TEST_ASSERT_EQUAL_STRING("0123456789abcde", p);
  p = static_cast<char*>(heap.reallocate(p, 200, 1000));     // small -> large
  TEST_ASSERT_EQUAL_STRING("0123456789abcde", p);
  TEST_ASSERT_EQUAL_UINT(1000, heap.stats().largeBytes);
  p = static_cast<char*>(heap.reallocate(p, 1000, 5000));    // large -> large
  TEST_ASSERT_EQUAL_STRING("0123456789abcde", p);
  TEST_ASSERT_EQUAL_UINT(5000, heap.stats().largeBytes);
  p = static_cast<char*>(heap.reallocate(p, 5000, 40));      // large -> small
  TEST_ASSERT_EQUAL_STRING("0123456789abcde", p);
  TEST_ASSERT_EQUAL_UINT(0, heap.stats().largeBytes);
  TEST_ASSERT_EQUAL_UINT(40, heap.stats().smallBytes);
  heap.reallocate(p, 40, 0);
}

// Fills a slab of the 256-byte class, so the next block opens a second.
static int fillSlab(LuaHeap& heap, void** out) {
  int n = 0;
  while (heap.stats().slabs < 2) out[n++] = heap.reallocate(nullptr, 0, 256);
  return n;
}

void test_an_emptied_slab_goes_back_unless_it_is_the_last_with_room(void) {
  LuaHeap heap(backing());
  void* blocks[64];
  const int n = fillSlab(heap, blocks);   // slab 1 full, slab 2 holds one
  TEST_ASSERT_EQUAL_UINT32(2, heap.stats().slabs);
  for (int i = 0; i < n - 1; i++) heap.reallocate(blocks[i], 256, 0);
  TEST_ASSERT_EQUAL_UINT32(1, heap.stats().slabs);   // slab 1 emptied, released
  heap.reallocate(blocks[n - 1], 256, 0);
  TEST_ASSERT_EQUAL_UINT32(1, heap.stats().slabs);   // slab 2 kept
  heap.releaseAll();
  TEST_ASSERT_EQUAL_UINT32(0, heap.stats().slabs);
  TEST_ASSERT_EQUAL_INT(0, g_counted.liveBlocks);
}

void test_a_full_slab_takes_blocks_again_once_one_is_freed(void) {
  LuaHeap heap(backing());
  void* blocks[64];
  const int n = fillSlab(heap, blocks);
  heap.reallocate(blocks[0], 256, 0);              // slab 1 has room again
  void* again = heap.reallocate(nullptr, 0, 256);
  TEST_ASSERT_EQUAL_PTR(blocks[0], again);
  blocks[0] = again;
  for (int i = 0; i < n; i++) heap.reallocate(blocks[i], 256, 0);
}

void test_a_refused_slab_is_an_allocation_failure(void) {
  LuaHeap heap(backing());
  g_counted.refuseSlabs = true;
  TEST_ASSERT_NULL(heap.reallocate(nullptr, 0, 64));
  TEST_ASSERT_EQUAL_UINT(0, heap.stats().smallBytes);
}

void test_without_slabs_every_block_goes_to_the_backing_heap(void) {
  LuaHeap heap(backing(), /*slabs=*/false);
  void* a = heap.reallocate(nullptr, 0, 24);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL_UINT32(0, heap.stats().slabs);
  TEST_ASSERT_EQUAL_UINT(24, heap.stats().largeBytes);
  TEST_ASSERT_EQUAL_UINT(24, g_counted.liveBytes);
  a = heap.reallocate(a, 24, 32);
  TEST_ASSERT_EQUAL_UINT(32, g_counted.liveBytes);
  heap.reallocate(a, 32, 0);
  TEST_ASSERT_EQUAL_INT(0, g_counted.liveBlocks);
}

void test_a_lua_state_runs_on_it_and_leaves_nothing(void) {
  {
    LuaHeap heap(backing());
    lua_State* L = lua_newstate(LuaHeap::luaAlloc, &heap);
    TEST_ASSERT_NOT_NULL(L);
    luaL_openlibs(L);
    const char* script =
        "local t = {} "
        "for i = 1, 20000 do t[i % 500 + 1] = {i, tostring(i), x = function() return i end} end "
        "local s = {} for i = 1, 2000 do s[#s + 1] = string.rep('ab', i % 300) end "
        "collectgarbage() "
        "return #t[1][2] + #table.concat(s, ','):sub(1, 10)";
    TEST_ASSERT_EQUAL_INT(LUA_OK, luaL_dostring(L, script));
    TEST_ASSERT_TRUE(lua_tointeger(L, -1) > 0);
    lua_close(L);
    TEST_ASSERT_EQUAL_UINT(0, heap.stats().smallBytes);
    TEST_ASSERT_EQUAL_UINT(0, heap.stats().largeBytes);
  }
  TEST_ASSERT_EQUAL_INT(0, g_counted.liveBlocks);
  TEST_ASSERT_EQUAL_UINT(0, g_counted.liveBytes);
}

void test_lua_sees_a_refused_slab_as_out_of_memory(void) {
  LuaHeap heap(backing());
  lua_State* L = lua_newstate(LuaHeap::luaAlloc, &heap);
  luaL_openlibs(L);
  TEST_ASSERT_EQUAL_INT(LUA_OK,
                        luaL_loadstring(L, "local t = {} for i = 1, 100000 do t[i] = {} end"));
  g_counted.refuseSlabs = true;
  TEST_ASSERT_EQUAL_INT(LUA_ERRMEM, lua_pcall(L, 0, 0, 0));
  g_counted.refuseSlabs = false;
  lua_close(L);
  TEST_ASSERT_EQUAL_UINT(0, heap.stats().smallBytes);
}

// ── Corruption ────────────────────────────────────────────────────────────

// Records what the heap reported and returns, so the heap carries on.
struct Reported {
  int count = 0;
  const char* what = nullptr;
  void* block = nullptr;
};

static void recordCorruption(const char* what, void* block, void* ctx) {
  Reported* r = static_cast<Reported*>(ctx);
  r->count++;
  r->what = what;
  r->block = block;
}

// Frees a, then b, so b heads its slab's free list and b's first word is the
// link to a: the word a write through a stale pointer to b would overwrite.
static void freeTwo(LuaHeap& heap, void* a, void* b, size_t n) {
  heap.reallocate(a, n, 0);
  heap.reallocate(b, n, 0);
}

static void expectLinkCaught(void* (*badLink)(void* a, void* b)) {
  LuaHeap heap(backing());
  Reported reported;
  heap.setCorruptionHandler(recordCorruption, &reported);
  void* keep = heap.reallocate(nullptr, 0, 32);   // keeps the slab alive
  void* a = heap.reallocate(nullptr, 0, 32);
  void* b = heap.reallocate(nullptr, 0, 32);
  void* tail = heap.reallocate(nullptr, 0, 32);   // so b's successor is handed out
  freeTwo(heap, a, b, 32);
  *static_cast<void**>(b) = badLink(a, b);

  void* p = heap.reallocate(nullptr, 0, 32);
  TEST_ASSERT_EQUAL_INT(1, reported.count);
  TEST_ASSERT_EQUAL_STRING("free list link is not a block of its slab", reported.what);
  TEST_ASSERT_EQUAL_PTR(b, reported.block);
  // Neither the overwritten block nor anything on its list comes back: the
  // block is a fresh slab's.
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_TRUE(p != a && p != b);
  TEST_ASSERT_EQUAL_UINT32(2, heap.stats().slabs);
  memset(p, 0xA5, 32);

  // The heap goes on working, and the damaged slab's live blocks still free.
  void* q = heap.reallocate(nullptr, 0, 32);
  TEST_ASSERT_NOT_NULL(q);
  heap.reallocate(q, 32, 0);
  heap.reallocate(p, 32, 0);
  heap.reallocate(tail, 32, 0);
  heap.reallocate(keep, 32, 0);
  TEST_ASSERT_EQUAL_INT(1, reported.count);
  TEST_ASSERT_EQUAL_UINT(0, heap.stats().smallBytes);
}

static void* linkOutsideTheSlab(void*, void*) {
  static uint64_t elsewhere;
  return &elsewhere;
}
static void* linkToTheSlabHeader(void*, void* b) {
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(b) &
                                 ~(uintptr_t)(LuaHeap::kSlabBytes - 1));
}
static void* linkBetweenTwoBlocks(void* a, void*) {
  return static_cast<char*>(a) + 8;
}
static void* linkIntoTheUnusedTail(void*, void* b) {
  return static_cast<char*>(b) + 8 * 32;   // a block never handed out
}
static void* linkIntoTheNextSlab(void*, void* b) {
  return static_cast<char*>(b) + LuaHeap::kSlabBytes;   // never dereferenced
}

void test_a_free_list_link_outside_its_slab_is_caught(void) {
  expectLinkCaught(linkOutsideTheSlab);
}
// Masking the link down to its slab would pass the next three.
void test_a_free_list_link_to_the_slab_header_is_caught(void) {
  expectLinkCaught(linkToTheSlabHeader);
}
void test_a_free_list_link_between_two_blocks_is_caught(void) {
  expectLinkCaught(linkBetweenTwoBlocks);
}
void test_a_free_list_link_to_a_block_never_handed_out_is_caught(void) {
  expectLinkCaught(linkIntoTheUnusedTail);
}
void test_a_free_list_link_into_the_next_slab_is_caught(void) {
  expectLinkCaught(linkIntoTheNextSlab);
}

void test_an_intact_free_list_reports_nothing(void) {
  LuaHeap heap(backing());
  Reported reported;
  heap.setCorruptionHandler(recordCorruption, &reported);
  void* blocks[64];
  const int n = fillSlab(heap, blocks);
  for (int i = 0; i < n; i += 2) heap.reallocate(blocks[i], 256, 0);
  for (int i = 0; i < n; i += 2) blocks[i] = heap.reallocate(nullptr, 0, 256);
  for (int i = 0; i < n; i++) heap.reallocate(blocks[i], 256, 0);
  TEST_ASSERT_EQUAL_INT(0, reported.count);
  TEST_ASSERT_EQUAL_UINT(0, heap.stats().smallBytes);
}

void test_a_block_freed_twice_is_caught_and_ignored(void) {
  LuaHeap heap(backing());
  Reported reported;
  heap.setCorruptionHandler(recordCorruption, &reported);
  void* keep = heap.reallocate(nullptr, 0, 48);
  void* a = heap.reallocate(nullptr, 0, 48);
  heap.reallocate(a, 48, 0);
  heap.reallocate(a, 48, 0);
  TEST_ASSERT_EQUAL_INT(1, reported.count);
  TEST_ASSERT_EQUAL_STRING("block freed twice", reported.what);
  // Ignored: a is handed out once, not twice.
  void* p = heap.reallocate(nullptr, 0, 48);
  void* q = heap.reallocate(nullptr, 0, 48);
  TEST_ASSERT_EQUAL_PTR(a, p);
  TEST_ASSERT_TRUE(q != a);
  heap.reallocate(p, 48, 0);
  heap.reallocate(q, 48, 0);
  heap.reallocate(keep, 48, 0);
  TEST_ASSERT_EQUAL_INT(1, reported.count);
}

void test_a_free_at_the_wrong_size_is_caught_and_ignored(void) {
  LuaHeap heap(backing());
  Reported reported;
  heap.setCorruptionHandler(recordCorruption, &reported);
  void* a = heap.reallocate(nullptr, 0, 64);
  heap.reallocate(a, 128, 0);
  TEST_ASSERT_EQUAL_INT(1, reported.count);
  TEST_ASSERT_EQUAL_PTR(a, reported.block);
  TEST_ASSERT_EQUAL_UINT(64, heap.stats().smallBytes);   // still live
  heap.reallocate(a, 64, 0);
  TEST_ASSERT_EQUAL_INT(1, reported.count);
  TEST_ASSERT_EQUAL_UINT(0, heap.stats().smallBytes);
}

void test_a_free_of_a_misaligned_address_is_caught(void) {
  LuaHeap heap(backing());
  Reported reported;
  heap.setCorruptionHandler(recordCorruption, &reported);
  char* a = static_cast<char*>(heap.reallocate(nullptr, 0, 64));
  heap.reallocate(a + 8, 64, 0);
  TEST_ASSERT_EQUAL_INT(1, reported.count);
  heap.reallocate(a, 64, 0);
  TEST_ASSERT_EQUAL_INT(1, reported.count);
}

// By default corruption aborts the process, so this runs in a child.
void test_by_default_a_corrupt_link_aborts(void) {
  fflush(stdout);
  const pid_t pid = fork();
  TEST_ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    if (!freopen("/dev/null", "w", stderr)) _exit(2);
    LuaHeap heap(backing());
    heap.reallocate(nullptr, 0, 32);   // keeps the slab alive
    void* a = heap.reallocate(nullptr, 0, 32);
    void* b = heap.reallocate(nullptr, 0, 32);
    freeTwo(heap, a, b, 32);
    *static_cast<void**>(b) = linkOutsideTheSlab(a, b);
    heap.reallocate(nullptr, 0, 32);
    _exit(0);   // reached only if the heap carried on
  }
  int status = 0;
  TEST_ASSERT_EQUAL_INT(pid, waitpid(pid, &status, 0));
  TEST_ASSERT_TRUE(WIFSIGNALED(status));
  TEST_ASSERT_EQUAL_INT(SIGABRT, WTERMSIG(status));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_small_blocks_share_a_slab_and_are_aligned);
  RUN_TEST(test_a_freed_block_is_reused);
  RUN_TEST(test_realloc_within_a_class_keeps_the_block);
  RUN_TEST(test_realloc_across_classes_and_sizes_keeps_the_contents);
  RUN_TEST(test_an_emptied_slab_goes_back_unless_it_is_the_last_with_room);
  RUN_TEST(test_a_full_slab_takes_blocks_again_once_one_is_freed);
  RUN_TEST(test_a_refused_slab_is_an_allocation_failure);
  RUN_TEST(test_without_slabs_every_block_goes_to_the_backing_heap);
  RUN_TEST(test_a_lua_state_runs_on_it_and_leaves_nothing);
  RUN_TEST(test_lua_sees_a_refused_slab_as_out_of_memory);
  RUN_TEST(test_a_free_list_link_outside_its_slab_is_caught);
  RUN_TEST(test_a_free_list_link_to_the_slab_header_is_caught);
  RUN_TEST(test_a_free_list_link_between_two_blocks_is_caught);
  RUN_TEST(test_a_free_list_link_to_a_block_never_handed_out_is_caught);
  RUN_TEST(test_a_free_list_link_into_the_next_slab_is_caught);
  RUN_TEST(test_an_intact_free_list_reports_nothing);
  RUN_TEST(test_a_block_freed_twice_is_caught_and_ignored);
  RUN_TEST(test_a_free_at_the_wrong_size_is_caught_and_ignored);
  RUN_TEST(test_a_free_of_a_misaligned_address_is_caught);
  RUN_TEST(test_by_default_a_corrupt_link_aborts);
  return UNITY_END();
}
