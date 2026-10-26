#include <unity.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
  return UNITY_END();
}
