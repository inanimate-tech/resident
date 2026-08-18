// src/ResidentStoreModule.h — the store slot that survives loadApp.
//
// An internal Extension registering the Lua `store` module: an app-scoped,
// persistent KV slot of scalars — the foundation for device-canonical state.
// RAM-backed with write-through to the PersistentStore, DEBOUNCED: NVS is
// written at most once per STORE_DEBOUNCE_MS of mutation quiet, plus a
// forced flush on app unload (loadAppInternal) — NVS wear matters.
//
// Scoping/reset policy (the whole mechanism): the slot is namespaced by a
// server-provided app identity string (system {type:"app", storeNs:"..."}
// load messages; missing storeNs = the shared default "app"). Loading with
// the SAME namespace leaves the data intact — state survives loadApp and
// reboot. Loading with a DIFFERENT namespace clears the slot first.
// The namespace is persisted alongside the data.
//
// Unlike EventsModule, everything here is self-contained (no Sandbox
// callback needed), so all methods are defined inline — one header, one TU,
// same build story for native tests and the PlatformIO library.
#ifndef RESIDENT_STORE_MODULE_H
#define RESIDENT_STORE_MODULE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include "ResidentExtension.h"
#include "ResidentLuaModule.h"
#include "ResidentPersistentStore.h"

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

// Budget: total persisted size (namespace + keys + values, serialized as
// the NVS JSON blob) — store.set beyond this returns false with no partial
// write. Override with a build flag if a device can afford more.
#ifndef RESIDENT_STORE_JSON_MAX
#define RESIDENT_STORE_JSON_MAX 2048
#endif

namespace Resident {

class StoreModule : public Extension {
public:
  static constexpr unsigned long STORE_DEBOUNCE_MS = 2000;
  static constexpr unsigned long STORE_MAX_FLUSH_MS = 30000;
  static constexpr size_t STORE_NS_MAX = 32;

  const char* name() const override { return "store"; }

  void registerModule(LuaModule& m) override {
    m.method<StoreModule, &StoreModule::get>("get");
    m.method<StoreModule, &StoreModule::set>("set");
    m.method<StoreModule, &StoreModule::keys>("keys");
    m.method<StoreModule, &StoreModule::clear>("clear");
    m.method<StoreModule, &StoreModule::remaining>("remaining");
  }

  // Budget-rejection feedback (0.8): called with the key the first time a
  // set() is rejected over budget, once per key per app load — silent
  // state loss is impossible. The sandbox wires this to telemetry.
  using RejectCallback = std::function<void(const char* key)>;
  void onBudgetReject(RejectCallback cb) { _onReject = std::move(cb); }
  void resetRejections() { _rejectedKeys.clear(); }

  // Deliberately NOT clearing on onAppReset — surviving loadApp is the
  // point. Reset happens only via setNamespace (a different app identity)
  // or Lua store.clear().

  // ── Sandbox wiring ──────────────────────────────────────────────────────

  void attach(PersistentStore* store) { _store = store; }

  // Boot: hydrate RAM from the persisted blob {"ns":"...","kv":{...}}.
  void loadPersisted() {
    _kv.clear();
    _kv.to<JsonObject>();
    if (!_store) return;
    String blob = _store->loadStore();
    if (blob.length() == 0) return;
    JsonDocument doc;
    if (deserializeJson(doc, blob)) return;   // corrupt blob → start fresh
    const char* ns = doc["ns"] | "app";
    _ns = ns;
    if (doc["kv"].is<JsonObjectConst>()) _kv.set(doc["kv"]);
    if (!_kv.is<JsonObject>()) _kv.to<JsonObject>();
  }

  // App-identity switch (from the system app-load message; ≤32 chars,
  // longer is truncated). Same ns: no-op, state survives. Different ns:
  // clear the slot and persist the boundary IMMEDIATELY (not debounced) —
  // a reboot right after must not resurrect the old app's state.
  void setNamespace(const char* ns) {
    String n(ns && ns[0] ? ns : "app");
    if (n.length() > STORE_NS_MAX) n = n.substring(0, STORE_NS_MAX);
    if (n == _ns) return;
    Serial.printf("Resident::Sandbox: store namespace '%s' -> '%s'; slot cleared\n",
                  _ns.c_str(), n.c_str());
    _ns = n;
    _kv.clear();
    _kv.to<JsonObject>();
    _dirty = true;
    flush();
  }

  // Debounced write-through: called every Sandbox::loop(). Quiet apps
  // flush STORE_DEBOUNCE_MS after their last mutation; apps that NEVER
  // go quiet (a ticking clock mutates continuously, so the quiet window
  // never arrives) flush at least every STORE_MAX_FLUSH_MS while dirty —
  // found live 2026-08-11: a crash mid-tick lost a clock's entire boot
  // session because the quiet-only debounce had never fired. Worst-case
  // wear stays ~2 writes/min.
  void updateDebounce(unsigned long now) {
    if (!_dirty) { _dirtySinceMs = 0; return; }
    if (_dirtySinceMs == 0) _dirtySinceMs = now;
    if (now - _lastMutationMs >= STORE_DEBOUNCE_MS ||
        now - _dirtySinceMs >= STORE_MAX_FLUSH_MS) {
      _dirtySinceMs = 0;
      flush();
    }
  }

  // Persist now if dirty. Called by the debounce, on namespace switches,
  // and on app unload (loadAppInternal).
  void flush() {
    if (!_dirty) return;
    _dirty = false;                    // RAM-only builds: nothing to retry
    if (!_store) return;
    JsonDocument doc;
    doc["ns"] = _ns;
    doc["kv"] = _kv;
    size_t need = measureJson(doc) + 1;
    char* blob = (char*)malloc(need);
    if (!blob) return;
    serializeJson(doc, blob, need);
    if (!_store->saveStore(blob, need - 1)) {
      Serial.println("Resident::Sandbox: store persist failed");
    }
    free(blob);
  }

  const String& storeNamespace() const { return _ns; }

  // ── Lua: store.get(key) -> string|number|boolean|nil ───────────────────
  int get(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    JsonVariantConst v = _kv[key];
    if (v.isNull())               lua_pushnil(L);
    else if (v.is<bool>())        lua_pushboolean(L, v.as<bool>());
    else if (v.is<const char*>()) lua_pushstring(L, v.as<const char*>());
    else if (v.is<int64_t>())     lua_pushinteger(L, (lua_Integer)v.as<int64_t>());
    else if (v.is<double>())      lua_pushnumber(L, v.as<double>());
    else                          lua_pushnil(L);
    return 1;
  }

  // ── Lua: store.set(key, value) -> boolean. Scalars only; nil deletes. ──
  int set(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    if (!_kv.is<JsonObject>()) _kv.to<JsonObject>();
    JsonObject root = _kv.as<JsonObject>();
    int t = lua_gettop(L) < 2 ? LUA_TNIL : lua_type(L, 2);

    if (t == LUA_TNIL) {
      root.remove(key);
      markDirty();
      lua_pushboolean(L, true);
      return 1;
    }
    if (t != LUA_TSTRING && t != LUA_TNUMBER && t != LUA_TBOOLEAN) {
      Serial.println("Resident::Sandbox: store.set rejects non-scalar values");
      lua_pushboolean(L, false);
      return 1;
    }

    // Remember the previous value so an over-budget write can roll back —
    // no partial writes, the slot is unchanged on rejection.
    JsonDocument saved;
    bool existed = !root[key].isNull();
    if (existed) saved.set(root[key]);

    if (t == LUA_TSTRING)       root[key] = lua_tostring(L, 2);
    else if (t == LUA_TBOOLEAN) root[key] = (bool)lua_toboolean(L, 2);
    else if (lua_isinteger(L, 2)) root[key] = (int64_t)lua_tointeger(L, 2);
    else                          root[key] = lua_tonumber(L, 2);

    if (persistedSize() > RESIDENT_STORE_JSON_MAX) {
      if (existed) root[key] = saved.as<JsonVariantConst>();
      else         root.remove(key);
      Serial.println("Resident::Sandbox: store.set over budget; rejected");
      reportRejection(key);
      lua_pushboolean(L, false);
      return 1;
    }

    markDirty();
    lua_pushboolean(L, true);
    return 1;
  }

  // ── Lua: store.remaining() -> bytes left in the persisted budget ────────
  int remaining(lua_State* L) {
    size_t used = persistedSize();
    lua_pushinteger(L, used >= RESIDENT_STORE_JSON_MAX
                           ? 0
                           : (lua_Integer)(RESIDENT_STORE_JSON_MAX - used));
    return 1;
  }

  // ── Lua: store.keys() -> array of key strings ───────────────────────────
  int keys(lua_State* L) {
    JsonObjectConst root = _kv.as<JsonObjectConst>();
    lua_createtable(L, (int)root.size(), 0);
    lua_Integer i = 1;
    for (JsonPairConst kv : root) {
      lua_pushstring(L, kv.key().c_str());
      lua_rawseti(L, -2, i++);
    }
    return 1;
  }

  // ── Lua: store.clear() ──────────────────────────────────────────────────
  int clear(lua_State* L) {
    (void)L;
    _kv.clear();
    _kv.to<JsonObject>();
    markDirty();
    return 0;
  }

private:
  PersistentStore* _store = nullptr;
  JsonDocument _kv;              // root is always a JSON object of scalars
  String _ns = "app";            // shared default namespace
  bool _dirty = false;
  unsigned long _lastMutationMs = 0;
  unsigned long _dirtySinceMs = 0;   // first dirty moment (max-flush clock)
  RejectCallback _onReject;
  std::vector<String> _rejectedKeys;   // once-per-key-per-load gate

  void reportRejection(const char* key) {
    if (!_onReject) return;
    for (auto& k : _rejectedKeys) {
      if (k == key) return;
    }
    _rejectedKeys.push_back(String(key));
    _onReject(key);
  }

  void markDirty() {
    _dirty = true;
    _lastMutationMs = millis();
  }

  // Exact size of the persisted blob {"ns":"<ns>","kv":<kv>} without
  // building it: 15 fixed chars + ns + serialized kv (measureJson accounts
  // for escaping).
  size_t persistedSize() const {
    return 15 + _ns.length() + measureJson(_kv);
  }
};

} // namespace Resident

#endif // RESIDENT_STORE_MODULE_H
