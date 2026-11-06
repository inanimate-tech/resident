// Behavioural tests for NvsPersistentStore::save — replacing the persisted
// app on a partition that cannot hold two copies of it (#60).
//
// ResidentNvsStore.h compiles only on ESP32, so this test claims ESP_PLATFORM
// for that one header and supplies the NVS calls itself (declared by the
// stubs in test/unit/include/nvs.h) over FakeNvs, a partition modelled the
// way ESP-IDF's NVS spends it:
//   - a page is 126 32-byte entries, and one page is always held back for
//     garbage collection;
//   - a blob costs one index entry plus, per page it spans (4000 bytes of
//     data a page at most), a chunk header and its data entries;
//   - nvs_set_blob on an existing key writes the new copy first and erases
//     the old one after, so a replace needs space for both; a write that does
//     not fit fails with ESP_ERR_NVS_NOT_ENOUGH_SPACE and changes nothing;
//   - nvs_get_stats counts erased entries as free.
#define ESP_PLATFORM 1
#include <unity.h>
#include <map>
#include <string>
#include <vector>

#include "ResidentNvsStore.h"

namespace {

struct FakeNvs {
  static constexpr size_t kPerPage = 126;
  size_t pages = 5;          // a 20 KB partition
  size_t otherEntries = 0;   // other namespaces sharing the partition
  std::map<std::string, std::string> blobs;   // the "resident" namespace
  std::vector<std::string> log;               // set/erase calls, in order
  int opens = 0, closes = 0, commits = 0;
  int setCalls = 0;
  int failSetCall = 0;       // fail this nvs_set_blob call (1-based) with ESP_FAIL
  bool statsFail = false;

  static size_t entriesFor(size_t len) {
    size_t chunks = len == 0 ? 1 : (len + 3999) / 4000;
    return 1 + chunks + (len + 31) / 32;
  }
  size_t used() const {
    size_t n = otherEntries;
    for (const auto& kv : blobs) n += entriesFor(kv.second.size());
    return n;
  }
  size_t total() const { return pages * kPerPage; }
  size_t usable() const { return total() - kPerPage; }
};

FakeNvs* nvs = nullptr;

} // namespace

esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t* out) {
  nvs->opens++;
  *out = 1;
  return ESP_OK;
}
void nvs_close(nvs_handle_t) { nvs->closes++; }

esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* value, size_t len) {
  nvs->log.push_back(std::string("set ") + key);
  if (++nvs->setCalls == nvs->failSetCall) return ESP_FAIL;
  std::string data(static_cast<const char*>(value), len);
  auto it = nvs->blobs.find(key);
  if (it != nvs->blobs.end() && it->second == data) return ESP_OK;
  // The old copy (if any) is still counted while the new one is written.
  if (nvs->used() + FakeNvs::entriesFor(len) > nvs->usable()) {
    return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
  }
  nvs->blobs[key] = data;
  return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* out, size_t* len) {
  auto it = nvs->blobs.find(key);
  if (it == nvs->blobs.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (out) {
    if (*len < it->second.size()) return ESP_FAIL;
    memcpy(out, it->second.data(), it->second.size());
  }
  *len = it->second.size();
  return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t, const char* key) {
  nvs->log.push_back(std::string("erase ") + key);
  return nvs->blobs.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_commit(nvs_handle_t) { nvs->commits++; return ESP_OK; }

esp_err_t nvs_get_stats(const char*, nvs_stats_t* st) {
  if (nvs->statsFail) return ESP_FAIL;
  st->total_entries = nvs->total();
  st->used_entries = nvs->used();
  st->free_entries = nvs->total() - nvs->used();
  st->namespace_count = 2;
  return ESP_OK;
}

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }

namespace {

Resident::NvsPersistentStore* store = nullptr;

std::string app(size_t len, char fill) { return std::string(len, fill); }

bool save(const std::string& s) { return store->save(s.data(), s.size()); }

// A partition laid out like #60's: 20 KB, other namespaces using 80 entries,
// a small Lua store blob beside the app, and 7000-byte apps — one fits, two
// do not.
void fillLikeIssue60() {
  nvs->otherEntries = 80;
  nvs->blobs["store"] = app(200, 's');
}

void resetLog() { nvs->log.clear(); nvs->setCalls = 0; nvs->commits = 0; }

} // namespace

void setUp() {
  nvs = new FakeNvs();
  store = new Resident::NvsPersistentStore();
}

void tearDown() {
  delete store; store = nullptr;
  delete nvs; nvs = nullptr;
}

// The partition really is the one #60 describes: one copy fits, two do not.
void test_partition_holds_one_app_not_two() {
  fillLikeIssue60();
  size_t one = FakeNvs::entriesFor(7000);
  TEST_ASSERT_TRUE(nvs->used() + one <= nvs->usable());
  TEST_ASSERT_TRUE(nvs->used() + 2 * one > nvs->usable());
}

void test_first_save_persists_and_commits() {
  TEST_ASSERT_TRUE(save("function init() end"));
  TEST_ASSERT_EQUAL_STRING("function init() end", store->load().c_str());
  TEST_ASSERT_EQUAL(1, nvs->commits);
  TEST_ASSERT_EQUAL(nvs->opens, nvs->closes);
}

// With space for both copies, NVS's own atomic replace runs: no erase, so no
// moment without an app.
void test_replace_with_space_for_both_never_erases() {
  TEST_ASSERT_TRUE(save(app(2000, 'a')));
  resetLog();
  TEST_ASSERT_TRUE(save(app(2000, 'b')));
  TEST_ASSERT_EQUAL(1, (int)nvs->log.size());
  TEST_ASSERT_EQUAL_STRING("set app", nvs->log[0].c_str());
  TEST_ASSERT_EQUAL_STRING(app(2000, 'b').c_str(), store->load().c_str());
}

// #60: NVS is too full for two copies. The old copy is erased and the write
// retried, and the new app is what persists — every time, not every other.
void test_replace_when_full_erases_old_then_writes_new() {
  fillLikeIssue60();
  TEST_ASSERT_TRUE(save(app(7000, 'a')));
  for (char c : {'b', 'c', 'd', 'e', 'f', 'g', 'h'}) {
    resetLog();
    TEST_ASSERT_TRUE(save(app(7000, c)));
    TEST_ASSERT_EQUAL(3, (int)nvs->log.size());
    TEST_ASSERT_EQUAL_STRING("set app", nvs->log[0].c_str());
    TEST_ASSERT_EQUAL_STRING("erase app", nvs->log[1].c_str());
    TEST_ASSERT_EQUAL_STRING("set app", nvs->log[2].c_str());
    TEST_ASSERT_EQUAL(1, nvs->commits);
    TEST_ASSERT_EQUAL_STRING(app(7000, c).c_str(), store->load().c_str());
  }
  // Only the app's own key was touched.
  TEST_ASSERT_EQUAL_STRING(app(200, 's').c_str(), nvs->blobs["store"].c_str());
  TEST_ASSERT_EQUAL(nvs->opens, nvs->closes);
}

// The retry fails after the erase (a flash write error, standing in for any
// failure inside the window). save() reports failure, and what is left is no
// app: neither the old copy nor a partial new one.
void test_replace_when_full_retry_fails_leaves_no_app() {
  fillLikeIssue60();
  TEST_ASSERT_TRUE(save(app(7000, 'a')));
  resetLog();
  nvs->failSetCall = 2;
  TEST_ASSERT_FALSE(save(app(7000, 'b')));
  TEST_ASSERT_EQUAL(3, (int)nvs->log.size());
  TEST_ASSERT_EQUAL_STRING("erase app", nvs->log[1].c_str());
  TEST_ASSERT_TRUE(store->load().empty());
  TEST_ASSERT_EQUAL(1, nvs->commits);   // the erase is committed, as reported
  TEST_ASSERT_EQUAL_STRING(app(200, 's').c_str(), nvs->blobs["store"].c_str());
  TEST_ASSERT_EQUAL(nvs->opens, nvs->closes);
}

// An app too big to fit even in the old copy's place is refused before
// anything is erased: the old app stays, as an oversized app always left it.
void test_app_too_big_even_alone_keeps_old_app() {
  fillLikeIssue60();
  TEST_ASSERT_TRUE(save(app(7000, 'a')));
  resetLog();
  TEST_ASSERT_TRUE(nvs->used() - FakeNvs::entriesFor(7000) +
                   FakeNvs::entriesFor(14000) > nvs->usable());
  TEST_ASSERT_FALSE(save(app(14000, 'b')));
  TEST_ASSERT_EQUAL(1, (int)nvs->log.size());
  TEST_ASSERT_EQUAL_STRING("set app", nvs->log[0].c_str());
  TEST_ASSERT_EQUAL_STRING(app(7000, 'a').c_str(), store->load().c_str());
  TEST_ASSERT_EQUAL(nvs->opens, nvs->closes);
}

// Full, but no app persisted yet: there is nothing of ours to erase, and
// other keys are never sacrificed.
void test_full_with_no_old_app_fails_without_erasing() {
  nvs->otherEntries = 400;
  nvs->blobs["store"] = app(200, 's');
  TEST_ASSERT_FALSE(save(app(7000, 'a')));
  TEST_ASSERT_EQUAL(1, (int)nvs->log.size());
  TEST_ASSERT_TRUE(store->load().empty());
  TEST_ASSERT_EQUAL_STRING(app(200, 's').c_str(), nvs->blobs["store"].c_str());
}

// Without partition stats the size check cannot rule the replace out, so it
// erases and retries: #60 stays fixed.
void test_replace_when_full_without_stats_still_replaces() {
  fillLikeIssue60();
  TEST_ASSERT_TRUE(save(app(7000, 'a')));
  nvs->statsFail = true;
  TEST_ASSERT_TRUE(save(app(7000, 'b')));
  TEST_ASSERT_EQUAL_STRING(app(7000, 'b').c_str(), store->load().c_str());
}

// Saving the app that is already persisted changes nothing.
void test_resaving_same_app_when_full_is_a_no_op() {
  fillLikeIssue60();
  TEST_ASSERT_TRUE(save(app(7000, 'a')));
  resetLog();
  TEST_ASSERT_TRUE(save(app(7000, 'a')));
  TEST_ASSERT_EQUAL(1, (int)nvs->log.size());
  TEST_ASSERT_EQUAL_STRING(app(7000, 'a').c_str(), store->load().c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_partition_holds_one_app_not_two);
  RUN_TEST(test_first_save_persists_and_commits);
  RUN_TEST(test_replace_with_space_for_both_never_erases);
  RUN_TEST(test_replace_when_full_erases_old_then_writes_new);
  RUN_TEST(test_replace_when_full_retry_fails_leaves_no_app);
  RUN_TEST(test_app_too_big_even_alone_keeps_old_app);
  RUN_TEST(test_full_with_no_old_app_fails_without_erasing);
  RUN_TEST(test_replace_when_full_without_stats_still_replaces);
  RUN_TEST(test_resaving_same_app_when_full_is_a_no_op);
  return UNITY_END();
}
