// Untrusted Lua loads as text only: an app, a chunk and the framework slot
// all compile through compileUntrusted (mode "t"), so a precompiled binary
// chunk is refused whatever its length; string.dump is gone; and errors name
// the chunk briefly ("app:1:") instead of quoting its source.
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* telemetry = nullptr;

// A Lua 5.4 binary chunk starts ESC 'L' 'u' 'a', then the version byte.
constexpr const char* kBinarySignature = "\x1bLua\x54";

void build(const char* framework = nullptr, bool openLibs = false) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.openUnsafeLibs = openLibs;
  if (framework) {
    Resident::SandboxConfig::FrameworkConfig fw;
    fw.name = "testfw";
    fw.version = 1;
    fw.source = framework;
    cfg.framework = fw;
  }
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setTelemetryCallback([](const char* json) {
    telemetry->push_back(json ? json : "");
  });
  sandbox->setup();
}

void loadApp(const char* code) {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = code;
  sandbox->injectMessage("test", "app", doc);
}

// True if a telemetry record called `name` carries `needle`.
bool telemetryWith(const char* name, const char* needle) {
  const std::string key = std::string("\"name\":\"") + name + "\"";
  for (auto& t : *telemetry)
    if (t.find(key) != std::string::npos && t.find(needle) != std::string::npos)
      return true;
  return false;
}

int dumpWriter(lua_State*, const void* p, size_t sz, void* ud) {
  static_cast<std::string*>(ud)->append(static_cast<const char*>(p), sz);
  return 0;
}

// What string.dump would give: a real binary chunk, every byte of it.
std::string dumpChunk(lua_State* L, const char* source) {
  std::string bytes;
  TEST_ASSERT_EQUAL_INT(LUA_OK, luaL_loadstring(L, source));
  lua_dump(L, dumpWriter, &bytes, 0);
  lua_pop(L, 1);
  return bytes;
}

}  // namespace

void setUp(void) {
  testMillis() = 0;
  telemetry = new std::vector<std::string>();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete telemetry; telemetry = nullptr;
}

void test_compile_untrusted_refuses_a_binary_chunk_with_its_length(void) {
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);
  const std::string bytes = dumpChunk(L, "return 42");

  // The format's NUL at byte 5 (the format byte, 0 for the official
  // format) is why a C-string path could never carry one whole.
  TEST_ASSERT_TRUE(bytes.size() > 5);
  TEST_ASSERT_EQUAL_MEMORY(kBinarySignature, bytes.data(), 5);
  TEST_ASSERT_EQUAL_INT(0, bytes[5]);

  // The bytes are a good chunk: a binary-capable load runs them.
  TEST_ASSERT_EQUAL_INT(LUA_OK, luaL_loadbufferx(L, bytes.data(), bytes.size(),
                                                 "=dump", "bt"));
  TEST_ASSERT_EQUAL_INT(LUA_OK, lua_pcall(L, 0, 1, 0));
  TEST_ASSERT_EQUAL_INT(42, lua_tointeger(L, -1));
  lua_pop(L, 1);

  // The untrusted loader refuses them, length and all.
  TEST_ASSERT_NOT_EQUAL(LUA_OK, Resident::compileUntrusted(L, bytes.data(), bytes.size(), "=app"));
  TEST_ASSERT_NOT_NULL(strstr(lua_tostring(L, -1), "attempt to load a binary chunk"));
  lua_pop(L, 1);

  // Text still compiles.
  TEST_ASSERT_EQUAL_INT(LUA_OK, Resident::compileUntrusted(L, "return 1", 8, "=app"));
  lua_close(L);
}

void test_app_with_binary_signature_is_refused(void) {
  build();
  loadApp(kBinarySignature);
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(telemetryWith("compile_error", "attempt to load a binary chunk"));
}

void test_chunk_with_binary_signature_is_refused(void) {
  build();
  loadApp("function on_tick(ctx, dt) end\n");
  TEST_ASSERT_FALSE(sandbox->loadChunk(kBinarySignature));
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(telemetryWith("chunk_error", "attempt to load a binary chunk"));
}

void test_framework_with_binary_signature_is_refused(void) {
  build(kBinarySignature);
  TEST_ASSERT_TRUE(telemetryWith("framework_error", "attempt to load a binary chunk"));
}

void test_errors_name_the_chunk_briefly(void) {
  build();
  loadApp("function on_tick(ctx, dt) end\nlocal x = = 1\n");
  TEST_ASSERT_TRUE(telemetryWith("compile_error", "app:2:"));
  TEST_ASSERT_FALSE(telemetryWith("compile_error", "[string"));

  loadApp("function on_tick(ctx, dt) end\n");
  TEST_ASSERT_FALSE(sandbox->loadChunk("error('boom')"));
  TEST_ASSERT_TRUE(telemetryWith("chunk_error", "chunk:1: boom"));
}

void test_string_dump_is_gone(void) {
  build();
  loadApp(
      "gone = (string.dump == nil) and (('').dump == nil)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("gone"));
}

void test_open_unsafe_libs_keeps_string_dump(void) {
  build(nullptr, /*openLibs=*/true);
  loadApp(
      "kept = (type(string.dump) == 'function')\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("kept"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_compile_untrusted_refuses_a_binary_chunk_with_its_length);
  RUN_TEST(test_app_with_binary_signature_is_refused);
  RUN_TEST(test_chunk_with_binary_signature_is_refused);
  RUN_TEST(test_framework_with_binary_signature_is_refused);
  RUN_TEST(test_errors_name_the_chunk_briefly);
  RUN_TEST(test_string_dump_is_gone);
  RUN_TEST(test_open_unsafe_libs_keeps_string_dump);
  return UNITY_END();
}
