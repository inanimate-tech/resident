#include "ResidentSandbox.h"
#include <Arduino.h>
#include <ezTime.h>
#include <math.h>
#include <cassert>
#include <cstdlib>
#include "chipstring.h"
#include "ResidentNvsStore.h"   // device-only; no-op on native

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"

// Custom Lua allocator. Prefers PSRAM, but transparently falls back to
// internal RAM on boards without PSRAM (e.g. ESP32-S3FN8). The capability is
// resolved once on first use from whether any SPIRAM is actually present.
static void* psramLuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  (void)ud;
  (void)osize;
  static const uint32_t kLuaHeapCaps =
      (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) ? MALLOC_CAP_SPIRAM
                                                        : MALLOC_CAP_8BIT;
  if (nsize == 0) {
    heap_caps_free(ptr);
    return NULL;
  }
  if (ptr == NULL) {
    return heap_caps_malloc(nsize, kLuaHeapCaps);
  }
  return heap_caps_realloc(ptr, nsize, kLuaHeapCaps);
}
#endif

namespace Resident {

// Registry key for accessing the sandbox instance from Lua C functions
static const char* REGISTRY_KEY = "ResidentSandbox_instance";

Sandbox::Sandbox() {
  memset(_events, 0, sizeof(_events));
  _eventsModule.bind(this);
}

Sandbox::Sandbox(const SandboxConfig& config) : Sandbox() {
  configure(config);
  _deviceId = ::getDeviceId();

  if (_config.network.has_value()) {
    // Courier 0.6+ delivers Client::onMessage from the default transport's
    // lane only ("receive parallels send" — see Courier::Client::dispatchJSON).
    // The sandbox's channel router receives exclusively through that
    // callback, so a networked Sandbox always needs a default lane or its
    // entire inbound path is dead. Mirror Courier's own auto-WS heuristic
    // (auto-registers WebSocketTransport as "ws" when host is set and
    // defaultTransport is unset/"ws"): default to "ws" here too when the
    // caller left it null/empty and set a host. A literal has static storage
    // duration, so storing it in the const char* is safe. Callers who
    // explicitly set a different defaultTransport (e.g. "mqtt") keep it.
    Courier::Config& net = *_config.network;
    bool wsIsDefault = !net.defaultTransport || net.defaultTransport[0] == '\0';
    if (wsIsDefault && net.host && net.host[0] != '\0') {
      net.defaultTransport = "ws";
    }
    _courier.emplace(net);
    _ws = &_courier->transport<Courier::WebSocketTransport>("ws");
  }
}

Courier::Client& Sandbox::courier() {
  // hasNetwork() == false would mean cfg.network was unset — programming
  // error; assert rather than return a dangling reference.
  assert(_courier.has_value());
  return *_courier;
}

Courier::WebSocketTransport& Sandbox::ws() {
  assert(_ws != nullptr);
  return *_ws;
}

bool Sandbox::isConnected() const {
  return _courier.has_value() &&
         _courier->getState() == Courier::State::Connected;
}

bool Sandbox::isTimeSynced() const {
  return _courier.has_value() && _courier->isTimeSynced();
}

void Sandbox::configure(const SandboxConfig& config) {
  _config = config;
  if (_config.telemetry) _telemetryCb = _config.telemetry;
  if (_config.timezone)  setTimezone(_config.timezone);
}

Sandbox::~Sandbox()
{
  if (_deferredLoadJson) {
    free(_deferredLoadJson);
    _deferredLoadJson = nullptr;
  }
  if (_lua) {
    if (_initFuncRef != LUA_NOREF)
      luaL_unref(_lua, LUA_REGISTRYINDEX, _initFuncRef);
    if (_onTickFuncRef != LUA_NOREF)
      luaL_unref(_lua, LUA_REGISTRYINDEX, _onTickFuncRef);
    if (_onEventFuncRef != LUA_NOREF)
      luaL_unref(_lua, LUA_REGISTRYINDEX, _onEventFuncRef);

    lua_close(_lua);
    _lua = nullptr;
  }
}

void Sandbox::setTimezone(const char* ianaZone)
{
  if (!ianaZone || !*ianaZone) {
    _hasTimezone = false;
    return;
  }
  bool ok = _tz.setLocation(ianaZone);
  _hasTimezone = ok;
  if (!ok) {
    Serial.printf("[time] detectedTimezone=%s not recognised, staying on UTC\n",
        ianaZone);
  } else {
    Serial.printf("[time] timezone set to %s\n", ianaZone);
  }
}

bool Sandbox::luaGlobalBoolForTest(const char* name)
{
  if (!_lua || !name) return false;
  lua_getglobal(_lua, name);
  bool result = lua_toboolean(_lua, -1);
  lua_pop(_lua, 1);
  return result;
}

int Sandbox::luaGlobalIntForTest(const char* name)
{
  if (!_lua || !name) return 0;
  lua_getglobal(_lua, name);
  int result = (int)lua_tointeger(_lua, -1);
  lua_pop(_lua, 1);
  return result;
}


void Sandbox::addLifecycle(Extension* e)
{
  if (!e) return;
  for (uint8_t i = 0; i < _lifecycleCount; i++) {
    if (_lifecycle[i] == e) return;   // already present — de-dup
  }
  if (_lifecycleCount < (Extensions::MAX + 4)) {
    _lifecycle[_lifecycleCount++] = e;
  }
}

void Sandbox::buildLifecycleSet()
{
  _lifecycleCount = 0;
  for (uint8_t i = 0; i < _config.extensions.count; i++) {
    addLifecycle(_config.extensions.items[i]);
  }
  // Driver role slots upcast to Extension*. Append any not already in
  // extensions[] so an assigned-but-unlisted peripheral is still begun and
  // updated. (systemMic is not a Driver: the mic pump owns its begin()/end(),
  // so capture hardware is held only while streaming.)
  addLifecycle(systemDisplay());
  addLifecycle(systemLED());
  addLifecycle(_config.systemButton);
}

bool Sandbox::isSystemExtension(Extension* e) const
{
  return e == static_cast<Extension*>(systemDisplay())
      || e == static_cast<Extension*>(systemLED())
      || e == static_cast<Extension*>(_config.systemButton);
}

Resident::SystemDisplay* Sandbox::systemDisplay() const
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  return _config.systemDisplay ? _config.systemDisplay : _config.statusDisplay;
#pragma GCC diagnostic pop
}

Resident::SystemLED* Sandbox::systemLED() const
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  return _config.systemLED ? _config.systemLED : _config.statusLED;
#pragma GCC diagnostic pop
}

void Sandbox::initialize()
{
  Serial.println("Initializing Resident::Sandbox");

#ifdef ESP_PLATFORM
  _lua = lua_newstate(psramLuaAlloc, NULL);
#else
  _lua = luaL_newstate();
#endif

  if (!_lua) {
    Serial.println("Error: Failed to create Lua state");
    return;
  }

  luaL_openlibs(_lua);

  // Store sandbox instance in registry
  lua_pushlightuserdata(_lua, this);
  lua_setfield(_lua, LUA_REGISTRYINDEX, REGISTRY_KEY);

  // Initialize function refs to LUA_NOREF
  _initFuncRef = LUA_NOREF;
  _onTickFuncRef = LUA_NOREF;
  _onEventFuncRef = LUA_NOREF;

  setupLuaEnvironment();

  // Build the de-duped lifecycle set (extensions[] + role slots).
  buildLifecycleSet();

  // Pass 1 — Lifecycle: wire event sink (so begin() can safely sendEvent()),
  // then begin(). Covers all managed objects: declared extensions and any
  // role-slot peripherals not also in extensions[].
  for (uint8_t i = 0; i < _lifecycleCount; i++) {
    Extension* ext = _lifecycle[i];
    Serial.printf("  Initializing extension: %s\n", ext->name());

    // Wire event sink first so a Driver's begin() can safely sendEvent().
    Driver* driver = ext->asDriver();
    if (driver) {
      driver->setEventSink(driverEventHandler, this);
    }

    Extension::beginExtension(*ext);
  }

  // Pass 2 — Lua modules: register globals for declared extensions only.
  // A role-slot peripheral that is NOT in extensions[] (e.g. a TFTSystemDisplay
  // assigned only to cfg.systemDisplay) must NOT get a Lua global — it has no
  // API surface to expose. A role object that also wants a Lua module must be
  // listed in extensions[] explicitly.
  for (uint8_t i = 0; i < _config.extensions.count; i++) {
    Extension* ext = _config.extensions.items[i];
    lua_newtable(_lua);
    LuaModule m(_lua, ext);
    ext->registerModule(m);
    lua_setglobal(_lua, ext->name());
  }

  // Internal modules — same registration shape as declared extensions.
  lua_newtable(_lua);
  {
    LuaModule m(_lua, &_eventsModule);
    _eventsModule.registerModule(m);
  }
  lua_setglobal(_lua, _eventsModule.name());

  _triggerResetTime = millis();
  _lastTickTime = millis();

  Serial.println("Resident::Sandbox initialized");
}

void Sandbox::setupLuaEnvironment()
{
  // Shader-compatible global functions
  lua_register(_lua, "rgb", lua_rgb);
  lua_register(_lua, "fract", lua_fract);
  lua_register(_lua, "beat", lua_beat);
  lua_register(_lua, "noise2d", lua_noise2d);

  // Math globals (bare functions for shader expression compatibility)
  lua_register(_lua, "floor", lua_math_floor);
  lua_register(_lua, "ceil", lua_math_ceil);
  lua_register(_lua, "abs", lua_math_abs);
  lua_register(_lua, "sin", lua_math_sin);
  lua_register(_lua, "cos", lua_math_cos);
  lua_register(_lua, "tan", lua_math_tan);
  lua_register(_lua, "sqrt", lua_math_sqrt);
  lua_register(_lua, "min", lua_math_min);
  lua_register(_lua, "max", lua_math_max);
  lua_register(_lua, "fmod", lua_math_fmod);

  // log module
  lua_newtable(_lua);
  lua_pushcfunction(_lua, lua_log_info);
  lua_setfield(_lua, -2, "info");
  lua_pushcfunction(_lua, lua_log_warn);
  lua_setfield(_lua, -2, "warn");
  lua_pushcfunction(_lua, lua_log_error);
  lua_setfield(_lua, -2, "error");
  lua_setglobal(_lua, "log");

  // time module
  lua_newtable(_lua);
  lua_pushcfunction(_lua, lua_time_is_valid);
  lua_setfield(_lua, -2, "is_valid");
  lua_pushcfunction(_lua, lua_time_hour);
  lua_setfield(_lua, -2, "hour");
  lua_pushcfunction(_lua, lua_time_minute);
  lua_setfield(_lua, -2, "minute");
  lua_pushcfunction(_lua, lua_time_second);
  lua_setfield(_lua, -2, "second");
  lua_pushcfunction(_lua, lua_time_day_id);
  lua_setfield(_lua, -2, "day_id");
  lua_pushcfunction(_lua, lua_time_has_timezone);
  lua_setfield(_lua, -2, "has_timezone");
  lua_setglobal(_lua, "time");
}

void Sandbox::setup()
{
  if (_initialized) return;
  _initialized = true;

  // deviceId is derived from the chip MAC and needed for the boot countdown
  // even in standalone (networkless) mode.
  _deviceId = ::getDeviceId();

  if (_courier.has_value()) {
    // 1. User's onConfigureNetwork — first; lets them register transports,
    //    set certs, etc., before any Courier setup runs.
    if (_onConfigureNetwork) {
      _onConfigureNetwork(*_courier);
    }

    // 2. Wire Resident's internal handlers onto Courier. These run before
    //    user callbacks (status indicators, reserved-type routing) and then
    //    delegate.
    wireInternalCourierHooks();

    // 3. AP name for WiFi config portal.
    String apName = String(getDeviceType());
    if (apName.length() > 0) apName[0] = toupper(apName[0]);
    String idSuffix = _deviceId.substring(0, 4);
    _apName = String("Resident ") + apName + " " + idSuffix;
    _courier->setAPName(_apName.c_str());

    Serial.printf("[resident] Device: %s (%s)\n",
                  getDeviceType(), _deviceId.c_str());
  }

  // 4. Sandbox internals (Lua state, extensions + role slots). Always.
  initialize();

  // Explicit override wins; otherwise use the platform default (NVS on
  // device). On native (neither macro defined) _store stays null unless a
  // test injected one.
  _store = _config.persistentStore;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (!_store && _config.persistApps) {
    static NvsPersistentStore s_defaultStore;
    if (s_defaultStore.begin()) _store = &s_defaultStore;
  }
#endif

  // Load any persisted app source. It is not armed here — the identity screen
  // and its countdown appear only once the device is ready to show them: on
  // first connection (networked), or right below (standalone). A networked
  // device that never connects stays on the connection-status screen.
  if (_config.persistApps && _store) {
    _pendingPersistedSource = _store->load();
  }

  // 6. Kick off Courier (WiFi + transports). The idle screen is then shown on
  // first connection. Standalone has no connection step, so enter it now.
  if (_courier.has_value()) {
    _courier->setup();
  } else {
    enterIdleScreen();
  }
}

void Sandbox::wireInternalCourierHooks()
{
  _courier->onMessage([this](const char* tn, const char* type, JsonDocument& d) {
    onCourierMessage(tn, type, d);
  });
  _courier->onConnectionChange([this](Courier::State s) {
    onCourierConnectionChange(s);
  });
  _courier->onTransportsWillConnect([this]() {
    onCourierTransportsWillConnect();
  });
  _courier->onConnected([this]() {
    onCourierConnected();
  });
}

void Sandbox::injectMessage(const char* transportName, const char* type,
                            JsonDocument& doc)
{
  onCourierMessage(transportName, type, doc);
}

void Sandbox::deferAppLoads(bool defer)
{
  _deferLoads = defer;
  if (defer || !_deferredLoadJson) return;

  // Apply the stashed load now. Hand ownership to a local first: loadApp /
  // loadShader may allocate heavily, and re-entrant stashing must see a
  // clean slot.
  char* payload = _deferredLoadJson;
  _deferredLoadJson = nullptr;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  free(payload);
  if (err) {
    Serial.println("Resident::Sandbox: deferred load unparseable; dropped");
    return;
  }
  const char* type = doc["type"] | "";
  // Route directly, skipping the filter (already applied at receipt) and the
  // deferral guard (_deferLoads is now false).
  dispatchMessage("", type, doc);
}

void Sandbox::stashDeferredLoad(JsonDocument& doc)
{
  size_t need = measureJson(doc) + 1;
  char* stash = (char*)malloc(need);
  if (!stash) {
    Serial.println("Resident::Sandbox: deferred load alloc failed; dropped");
    return;
  }
  serializeJson(doc, stash, need);
  if (_deferredLoadJson) free(_deferredLoadJson);
  _deferredLoadJson = stash;
}

void Sandbox::onCourierMessage(const char* transportName,
                                const char* type, JsonDocument& doc)
{
  const char* channel = doc["channel"] | "";
  if (channel[0]) {
    if (strcmp(channel, "app") == 0)    { handleAppMessage(transportName, type, doc); return; }
    // Runtime channel (framework events: heard, errors, agent status) rides
    // the same queue as app frames — delivered to on_event with
    // e.channel == "runtime". Reserved names will ride it later.
    if (strcmp(channel, "runtime") == 0) { handleAppMessage(transportName, type, doc); return; }
    if (strcmp(channel, "system") == 0) { handleSystemMessage(transportName, type, doc); return; }
    MessageCallback* cb = lookupChannelSlot(channel);
    if (cb && *cb) { (*cb)(transportName, type, doc); return; }
    Serial.printf("Resident::Sandbox: no handler for channel '%s' (type '%s'); dropped\n",
                  channel, type);
    return;
  }

  // ── Legacy un-channelled path (deprecated) ──
  Serial.printf("[deprecated] un-channelled '%s' message; sender should stamp channel\n", type);
  if (_messageFilter && !_messageFilter(transportName, type, doc)) return;
  bool isLoad = strcmp(type, "app") == 0 || strcmp(type, "shader") == 0;
  if (isLoad) maybeShowDescription(doc);
  if (_deferLoads && isLoad) {
    stashDeferredLoad(doc);
    return;
  }
  dispatchMessage(transportName, type, doc);
}

void Sandbox::handleAppMessage(const char* transportName, const char* type,
                               JsonDocument& doc)
{
  (void)transportName;
  const char* name = type;
  if (strcmp(type, "app_event") == 0) {
    // Legacy envelope on the data plane — removed when the shims retire.
    Serial.println("[deprecated] app_event wrapper; send channel:\"app\" with type=<event name>");
    name = doc["name"] | "";
    if (!name[0]) return;
  }
  const char* from = doc["from"] | "";
  if (from[0] && _deviceId.equals(from)) return;   // self-echo (multicast loopback)
  const char* nonce = doc["nonce"] | "";
  if (nonce[0] && isDuplicateNonce(nonce)) return;
  // Same gate as sendAppEvent (see its comment — "deliberate"): no app loaded,
  // or no on_event handler → drop. The event ring is not reset on app load, so
  // queueing here would leak stale events into whatever app loads next.
  if (!isAppRunning() || !_onEventFuncRef) return;
  char dataJson[RESIDENT_EVENT_JSON_MAX] = "{}";
  if (doc["data"].is<JsonObject>()) {
    // Drop, don't truncate: a cut-off payload must never reach the ring.
    if (measureJson(doc["data"]) + 1 > sizeof(dataJson)) {
      Serial.printf("Resident::Sandbox: incoming data exceeds RESIDENT_EVENT_JSON_MAX; "
                    "app event '%s' dropped\n", name);
      return;
    }
    serializeJson(doc["data"], dataJson, sizeof(dataJson));
  }
  // Envelope fields carried through to Lua: channel ("app" or "runtime" —
  // both route here), and src/seq only when the frame stamped them.
  const char* channel = doc["channel"] | "app";
  const char* src = doc["src"] | "";
  bool hasSeq = doc["seq"].is<uint32_t>();
  uint32_t seq = doc["seq"] | 0U;
  pushAppEvent(name, dataJson, from, doc["ts_ms"] | millis(),
               channel, src, hasSeq, seq);
}

bool Sandbox::isDuplicateNonce(const char* nonce)
{
  if (!nonce || !nonce[0]) return false;

  for (int i = 0; i < DEDUP_RING_SIZE; i++) {
    if (_recentNonces[i][0] && strcmp(_recentNonces[i], nonce) == 0) {
      return true;
    }
  }

  strncpy(_recentNonces[_nonceRingPos], nonce, sizeof(_recentNonces[0]) - 1);
  _recentNonces[_nonceRingPos][sizeof(_recentNonces[0]) - 1] = '\0';
  _nonceRingPos = (_nonceRingPos + 1) % DEDUP_RING_SIZE;
  return false;
}

void Sandbox::handleSystemMessage(const char* transportName, const char* type,
                                  JsonDocument& doc)
{
  bool isLoad = strcmp(type, "app") == 0 || strcmp(type, "shader") == 0;
  if (isLoad) maybeShowDescription(doc);
  if (_deferLoads && isLoad) { stashDeferredLoad(doc); return; }
  if (strcmp(type, "app") == 0) {
    const char* code = doc["code"];
    if (code) loadApp(code);
    return;
  }
  if (strcmp(type, "shader") == 0) {
    ShaderFields fields;
    for (JsonPair kv : doc.as<JsonObject>()) {
      if (strcmp(kv.key().c_str(), "type") == 0) continue;
      if (strcmp(kv.key().c_str(), "channel") == 0) continue;
      if (kv.value().is<const char*>()) {
        fields[String(kv.key().c_str())] = String(kv.value().as<const char*>());
      }
    }
    loadShader(fields);
    return;
  }
  if (strcmp(type, "forget") == 0) { clearPersistedApp(); return; }

  MessageCallback* cb = lookupChannelSlot("system");
  if (cb && *cb) { (*cb)(transportName, type, doc); return; }
  Serial.printf("Resident::Sandbox: unhandled system message '%s'; dropped\n", type);
}

Sandbox::MessageCallback* Sandbox::lookupChannelSlot(const char* channel)
{
  for (int i = 0; i < _channelSlotCount; i++) {
    if (_channelSlots[i].name == channel) return &_channelSlots[i].cb;
  }
  return nullptr;
}

void Sandbox::onMessageWithChannel(const char* channel, MessageCallback cb)
{
  for (int i = 0; i < _channelSlotCount; i++) {
    if (_channelSlots[i].name == channel) { _channelSlots[i].cb = std::move(cb); return; }
  }
  if (_channelSlotCount >= MAX_CHANNEL_SLOTS) {
    Serial.println("Resident::Sandbox: channel slot registry full");
    return;
  }
  _channelSlots[_channelSlotCount].name = channel;
  _channelSlots[_channelSlotCount].cb = std::move(cb);
  _channelSlotCount++;
}

// Control-plane emit: stamps channel:"system" and sends via the default
// transport. Used for device control messages (voice start/end etc.) — the
// doc is stamped whether or not a network is configured, so callers can
// inspect the envelope even when send fails.
bool Sandbox::sendSystem(JsonDocument& doc)
{
  doc["channel"] = "system";
  if (!_courier.has_value()) return false;
  return _courier->send(doc);
}

// App/shader "description" field -> systemDisplay on load receipt. Called
// once per load, before deferral is applied — a deferred load's description
// was already shown here at receipt, so the deferred-apply path shows nothing.
void Sandbox::maybeShowDescription(JsonDocument& doc)
{
  if (!_showDescriptions || !systemDisplay()) return;
  const char* desc = doc["description"];
  if (desc && desc[0]) systemDisplay()->displayText(desc);
}

// Data-plane emit: builds the app-channel envelope and hands it to the
// event sink. Shared by events.send (EventsModule::send, below) and any
// C++ caller (e.g. a wrapper's room.announce alias).
bool Sandbox::publishEvent(const char* name, const char* dataJson)
{
  if (!name || !name[0]) return false;
  if (!_eventsModule.takeToken()) {
    Serial.println("Resident::Sandbox: publishEvent rate-limited; dropped");
    return false;
  }
  JsonDocument doc;
  doc["channel"] = "app";
  doc["type"] = name;
  JsonDocument data;
  if (dataJson && dataJson[0] && !deserializeJson(data, dataJson)) {
    doc["data"] = data;
  } else {
    doc["data"].to<JsonObject>();
  }
  doc["from"] = _deviceId;
  // Envelope source + per-sender monotonic sequence (per-boot, uint32). One
  // counter for the device as sender; the nonce reuses the same count, so
  // nonce suffix and seq stay in lockstep for a given frame.
  uint32_t seq = (uint32_t)++_eventNonceCounter;
  doc["src"] = "device";
  doc["seq"] = seq;
  char nonce[64];
  snprintf(nonce, sizeof(nonce), "%s:%lu", _deviceId.c_str(),
           (unsigned long)seq);
  doc["nonce"] = nonce;
  doc["ts_ms"] = millis();

  if (_eventSink) return _eventSink(doc);
  if (_courier.has_value()) return _courier->send(doc);
  return false;
}

// ── Lua table → JSON serializer (outgoing events.send data) ──────────────
//
// Successor to the flat serializer ported from RoomModule::announce. Adds:
//   - proper JSON string escaping (" \ and control chars) for keys AND values
//   - booleans (Lua true/false → JSON true/false)
//   - nested tables to LUA_JSON_MAX_DEPTH table levels: string-keyed tables
//     become objects; tables with a non-empty array part (lua_rawlen > 0)
//     become arrays of elements 1..rawlen (string keys of a mixed table are
//     ignored — don't mix). The TOP level is always an object: the wire
//     format is a JSON object in the envelope's `data` field, and top-level
//     integer keys are skipped exactly as before.
//   - overflow is an error, not a truncation: the writer sets a flag and the
//     caller drops the event instead of sending a cut-off payload.
//
// Kept identical for existing flat string/number payloads (byte-for-byte,
// escaping aside): pair order is lua_next order, integers print via %lld,
// floats via %g, unsupported values (functions, userdata, nil, too-deep
// tables) are skipped along with their key, no data → "{}".
//
// Test cases live in test/unit/test/test_events_module — serializer cases
// call serializeLuaTableToJson directly on a bare lua_State.
namespace {

constexpr int LUA_JSON_MAX_DEPTH = 3;  // table levels, counting the top one

// Bounded writer: never writes past cap, never truncates silently — once
// `overflow` is set every subsequent write is a no-op and the caller must
// discard the buffer.
struct JsonWriter {
  char* buf = nullptr;
  size_t cap = 0;
  size_t len = 0;
  bool overflow = false;

  void putc_(char c) {
    if (overflow) return;
    if (len + 1 >= cap) { overflow = true; return; }  // reserve NUL slot
    buf[len++] = c;
  }
  void puts_(const char* s) { while (*s) putc_(*s++); }
  void terminate() { buf[len < cap ? len : cap - 1] = '\0'; }
};

void writeJsonString(JsonWriter& w, const char* s) {
  w.putc_('"');
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    unsigned char c = *p;
    switch (c) {
      case '"':  w.puts_("\\\""); break;
      case '\\': w.puts_("\\\\"); break;
      case '\n': w.puts_("\\n");  break;
      case '\r': w.puts_("\\r");  break;
      case '\t': w.puts_("\\t");  break;
      default:
        if (c < 0x20) {
          char esc[8];
          snprintf(esc, sizeof(esc), "\\u%04x", c);
          w.puts_(esc);
        } else {
          w.putc_((char)c);
        }
    }
  }
  w.putc_('"');
}

void writeLuaObject(lua_State* L, int idx, JsonWriter& w, int depth);
void writeLuaArray(lua_State* L, int idx, JsonWriter& w, int depth);

// True iff the value at idx can be serialized. `depth` is the depth of the
// CONTAINING table; a nested table is only supported while depth <
// LUA_JSON_MAX_DEPTH. Checked BEFORE writing an object key so unsupported
// values are skipped without leaving a dangling `"key":`.
bool luaValueSupported(lua_State* L, int idx, int depth) {
  switch (lua_type(L, idx)) {
    case LUA_TSTRING:
    case LUA_TNUMBER:
    case LUA_TBOOLEAN: return true;
    case LUA_TTABLE:   return depth < LUA_JSON_MAX_DEPTH;
    default:           return false;
  }
}

// Writes the (supported — see luaValueSupported) value at idx.
void writeLuaValue(lua_State* L, int idx, JsonWriter& w, int depth) {
  idx = lua_absindex(L, idx);
  switch (lua_type(L, idx)) {
    case LUA_TSTRING:
      writeJsonString(w, lua_tostring(L, idx));
      break;
    case LUA_TNUMBER: {
      char num[40];
      if (lua_isinteger(L, idx)) {
        snprintf(num, sizeof(num), "%lld", (long long)lua_tointeger(L, idx));
      } else {
        snprintf(num, sizeof(num), "%g", lua_tonumber(L, idx));
      }
      w.puts_(num);
      break;
    }
    case LUA_TBOOLEAN:
      w.puts_(lua_toboolean(L, idx) ? "true" : "false");
      break;
    case LUA_TTABLE:
      if (lua_rawlen(L, idx) > 0) writeLuaArray(L, idx, w, depth + 1);
      else                        writeLuaObject(L, idx, w, depth + 1);
      break;
    default:
      break;  // unreachable — callers gate on luaValueSupported
  }
}

void writeLuaObject(lua_State* L, int idx, JsonWriter& w, int depth) {
  idx = lua_absindex(L, idx);
  w.putc_('{');
  bool first = true;
  lua_pushnil(L);
  while (lua_next(L, idx) != 0) {
    // String keys only (as ever: lua_tostring on a non-string key would
    // mutate it under lua_next). Traversal always runs to completion so the
    // Lua stack stays balanced even after overflow.
    if (lua_type(L, -2) == LUA_TSTRING && luaValueSupported(L, -1, depth)) {
      if (!first) w.putc_(',');
      writeJsonString(w, lua_tostring(L, -2));
      w.putc_(':');
      writeLuaValue(L, -1, w, depth);
      first = false;
    }
    lua_pop(L, 1);  // pop value, keep key for next iteration
  }
  w.putc_('}');
}

void writeLuaArray(lua_State* L, int idx, JsonWriter& w, int depth) {
  idx = lua_absindex(L, idx);
  lua_Integer n = (lua_Integer)lua_rawlen(L, idx);
  w.putc_('[');
  for (lua_Integer i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    if (i > 1) w.putc_(',');
    if (luaValueSupported(L, -1, depth)) writeLuaValue(L, -1, w, depth);
    else                                 w.puts_("null");  // hold the position
    lua_pop(L, 1);
  }
  w.putc_(']');
}

// Serialize the Lua table at idx into buf as a JSON object. Returns false on
// overflow — buf then holds a truncated (still NUL-terminated) string that
// MUST NOT be sent.
bool serializeLuaTableToJson(lua_State* L, int idx, char* buf, size_t cap) {
  JsonWriter w{buf, cap};
  writeLuaObject(L, idx, w, /*depth=*/1);
  w.terminate();
  return !w.overflow;
}

// ── JSON → Lua table (incoming app-channel event data) ───────────────────
//
// Mirror of the writer above, for processNextEvent's APP_EVENT delivery.
// ArduinoJson does the parsing (so string unescaping — \" \\ \n \r \t
// \u00XX — is correct); these helpers walk the parsed document onto the Lua
// stack with the same value rules as the outgoing side: strings, integers
// (lua_pushinteger), floats (lua_pushnumber), booleans, and nested
// objects/arrays to LUA_JSON_MAX_DEPTH container levels (counting the
// top-level data object). Deeper containers and JSON null have no
// representation: an object member is skipped with its key; an array
// element is left as a hole at its index — the incoming twin of the
// outgoing null placeholder, so later elements keep their positions.

void pushJsonObjectToLua(lua_State* L, JsonObjectConst obj, int depth);
void pushJsonArrayToLua(lua_State* L, JsonArrayConst arr, int depth);

// Pushes the JSON value as a Lua value and returns true, or pushes nothing
// and returns false. `depth` is the depth of the CONTAINING container.
bool pushJsonValueToLua(lua_State* L, JsonVariantConst v, int depth) {
  if (v.is<bool>()) { lua_pushboolean(L, v.as<bool>()); return true; }
  if (v.is<const char*>()) { lua_pushstring(L, v.as<const char*>()); return true; }
  if (v.is<int64_t>()) { lua_pushinteger(L, (lua_Integer)v.as<int64_t>()); return true; }
  if (v.is<double>()) { lua_pushnumber(L, v.as<double>()); return true; }
  if (v.is<JsonObjectConst>()) {
    if (depth >= LUA_JSON_MAX_DEPTH) return false;
    pushJsonObjectToLua(L, v.as<JsonObjectConst>(), depth + 1);
    return true;
  }
  if (v.is<JsonArrayConst>()) {
    if (depth >= LUA_JSON_MAX_DEPTH) return false;
    pushJsonArrayToLua(L, v.as<JsonArrayConst>(), depth + 1);
    return true;
  }
  return false;  // null / unrepresentable
}

void pushJsonObjectToLua(lua_State* L, JsonObjectConst obj, int depth) {
  lua_createtable(L, 0, (int)obj.size());
  for (JsonPairConst kv : obj) {
    if (pushJsonValueToLua(L, kv.value(), depth)) {
      lua_setfield(L, -2, kv.key().c_str());
    }
  }
}

void pushJsonArrayToLua(lua_State* L, JsonArrayConst arr, int depth) {
  lua_createtable(L, (int)arr.size(), 0);
  lua_Integer i = 1;
  for (JsonVariantConst v : arr) {
    if (pushJsonValueToLua(L, v, depth)) lua_rawseti(L, -2, i);
    i++;
  }
}

}  // namespace

// events.send(name [, data_table]) -> boolean. Defined here (not in
// ResidentEvents.h) because it needs Sandbox::publishEvent, i.e. Sandbox as
// a complete type — see the linkage note atop ResidentEvents.h.
int EventsModule::send(lua_State* L)
{
  // Arg 1: event name (string, required)
  const char* name = luaL_checkstring(L, 1);

  // Arg 2: data table (optional)
  char dataJson[RESIDENT_EVENT_JSON_MAX] = "{}";

  if (lua_gettop(L) >= 2 && lua_istable(L, 2)) {
    if (!serializeLuaTableToJson(L, 2, dataJson, sizeof(dataJson))) {
      // Overflow: never send a cut-off payload — drop the event instead.
      Serial.println("Resident::Sandbox: events.send data exceeds "
                     "RESIDENT_EVENT_JSON_MAX; event dropped");
      lua_pushboolean(L, false);
      return 1;
    }
  }

  bool sent = _sandbox && _sandbox->publishEvent(name, dataJson);
  lua_pushboolean(L, sent);
  return 1;
}

// Reserved-type routing + user callback. Entered from onCourierMessage (after
// the filter and deferral guards) and, for an applied stash, directly from
// deferAppLoads — a stashed load already passed the filter at receipt, so
// re-running it would let a dedup/self-echo filter drop the message.
void Sandbox::dispatchMessage(const char* transportName,
                              const char* type, JsonDocument& doc)
{
  // Reserved types — Resident handles internally; user callback never sees these.
  if (strcmp(type, "app") == 0) {
    const char* code = doc["code"];
    if (code) loadApp(code);
    return;
  }
  if (strcmp(type, "shader") == 0) {
    ShaderFields fields;
    for (JsonPair kv : doc.as<JsonObject>()) {
      if (strcmp(kv.key().c_str(), "type") == 0) continue;
      if (kv.value().is<const char*>()) {
        fields[String(kv.key().c_str())] = String(kv.value().as<const char*>());
      }
    }
    loadShader(fields);
    return;
  }
  if (strcmp(type, "app_event") == 0) {
    const char* name = doc["name"];
    char dataJson[RESIDENT_EVENT_JSON_MAX];
    if (doc["data"].is<JsonObject>()) {
      // Same drop-don't-truncate guard as handleAppMessage.
      if (measureJson(doc["data"]) + 1 > sizeof(dataJson)) {
        Serial.printf("Resident::Sandbox: incoming data exceeds RESIDENT_EVENT_JSON_MAX; "
                      "app event '%s' dropped\n", name ? name : "");
        return;
      }
      serializeJson(doc["data"], dataJson, sizeof(dataJson));
    } else {
      strcpy(dataJson, "{}");
    }
    // Legacy un-channelled wire path: still an app-plane frame, not a
    // host-firmware injection — tag "app", not the "driver" default.
    if (name) sendAppEvent(name, dataJson, "app");
    return;
  }
  if (strcmp(type, "forget") == 0) {
    clearPersistedApp();
    return;
  }

  // Anything else → user callback if registered.
  if (_onMessage) _onMessage(transportName, type, doc);
}

void Sandbox::onCourierConnectionChange(Courier::State state)
{
  using S = Courier::State;

  // Resident's internal status-text handling. Runs unconditionally if a
  // systemDisplay is configured. User's onConnectionChange callback runs
  // after, in addition (does not replace).
  if (_runState != RunState::Pending) {
    switch (state) {
      case S::WifiConnecting:        showStatusText("WiFi..."); break;
      case S::WifiConfiguring: {
        String s = _apName.isEmpty() ? "Configure WiFi" : (String("Configure WiFi\n\n") + _apName);
        showStatusText(s.c_str());
        break;
      }
      case S::WifiConnected:         showStatusText("WiFi connected"); break;
      case S::TransportsConnecting:  showStatusText("Connecting..."); break;
      case S::Connected:             enterIdleScreen(); break;
      case S::Reconnecting:          showStatusText("Reconnecting..."); break;
      case S::ConnectionFailed:      showStatusText("Connection failed"); break;
      default: break;
    }
  }

  if (systemLED()) {
    switch (state) {
      case S::WifiConnecting:
      case S::WifiConfiguring:       systemLED()->solidColor(0xFFFF00); break;
      case S::WifiConnected:
      case S::TransportsConnecting:  systemLED()->solidColor(0x00FFFF); break;
      case S::Connected:             systemLED()->solidColor(0x00FF00); break;
      case S::Reconnecting:          systemLED()->solidColor(0xFF8800); break;
      case S::ConnectionFailed:      systemLED()->solidColor(0xFF0000); break;
      default: break;
    }
  }

  if (_onConnectionChange) _onConnectionChange(state);
}

void Sandbox::onCourierConnected() {
  if (_onConnected) _onConnected();
}

void Sandbox::onCourierTransportsWillConnect() {
  // Resident's default: built-in WS gets /agents/<deviceType>-agent/<deviceId>.
  // User callback runs after and can override (e.g. set /devices/<id>).
  String wsPath = String("/agents/") + getDeviceType() + "-agent/" + _deviceId;
  ws().setEndpoint(_config.network->host ? _config.network->host : "localhost",
                   443, wsPath.c_str());
  Serial.printf("[resident] WS path: %s\n", wsPath.c_str());

  if (_onTransportsWillConnect) _onTransportsWillConnect();
}

void Sandbox::showStatusText(const char* text)
{
  if (!systemDisplay()) return;
  if (_lastStatusText == text) return;
  _lastStatusText = text;
  systemDisplay()->displayText(text);
}

void Sandbox::showIdleScreen(int countdownSecs)
{
  if (!systemDisplay()) return;
  String s;
  if (_idleScreenTitle.length() > 0) { s += _idleScreenTitle; s += '\n'; }
  s += "Device ID: "; s += _deviceId;
  s += "\nType: ";     s += getDeviceType();
  if (countdownSecs >= 0) { s += '\n'; s += String(countdownSecs); s += 's'; }
  showStatusText(s.c_str());
}

void Sandbox::enterIdleScreen()
{
  // Called when the device is ready to present its idle UI. With a persisted
  // app: show the identity screen + 20s countdown (then load), or restore
  // immediately when there's no display to count down on. With no persisted
  // app: rest on the identity screen. No-op once an app is loaded or counting
  // down (so a reconnect doesn't re-arm or repaint over a running app).
  if (_runState != RunState::Ready) return;

  if (_pendingPersistedSource.isEmpty()) {
    showReadyScreen();
    return;
  }
  if (systemDisplay()) {
    _runState = RunState::Pending;
    _countdownStartMs = millis();
    _lastCountdownSecondShown = -1;
  } else {
    finishBootCountdown();   // no display — nothing to count down on; restore now
  }
}

void Sandbox::showReadyScreen()
{
  // The Ready identity screen is the resting display when no app is loaded.
  // Show it once the device is reachable (connected, or standalone); while
  // connecting, the connection-status text shows instead, and while an app
  // runs it owns the screen.
  if (!_courier.has_value() || isConnected()) showIdleScreen();
}

void Sandbox::loop() {
  if (_courier.has_value()) {
    _courier->loop();
  }
  if (!_lua) return;

  // Driver heartbeat — single de-duped walk, connectivity-independent.
  // Peripherals (role-assigned) update every loop; other extensions only
  // while an app is loaded (Running or Suspended).
  for (uint8_t i = 0; i < _lifecycleCount; i++) {
    Extension* e = _lifecycle[i];
    if (isSystemExtension(e) || isAppRunning()) {
      e->update();
    }
  }

  updateSystemButtonHold();
  updateOverlays();
  updateMicStream();

  if (_runState == RunState::Pending) {
    updateBootCountdown();
    return;  // app not loaded yet; skip the tick path
  }

  if (_runState != RunState::Running) return;

  // Networked apps tick only once connected (unchanged); standalone always.
  if (_courier.has_value() && !isConnected()) return;

  unsigned long now = millis();
  unsigned long elapsed = now - _lastTickTime;
  if (elapsed >= TICK_INTERVAL) {
    callOnTick(elapsed);
    _lastTickTime = now;
  }

  processNextEvent();
}

void Sandbox::loadApp(const char* luaCode)
{
  loadAppInternal(luaCode, /*persistOnSuccess=*/true);
}

bool Sandbox::loadAppInternal(const char* luaCode, bool persistOnSuccess)
{
  // Reset the runtime hold detector: a reload (or a failed reload) must never
  // leave stale hold state that would fire a spurious gesture on the next app.
  _holdWasDown = false;
  _holdFired = false;

  // An explicit load supersedes a pending boot-countdown restore.
  if (_runState == RunState::Pending) {
    _runState = RunState::Ready;
    _pendingPersistedSource = "";
  }

  // Stop any currently-loaded app (Running or Suspended) before loading the
  // new one. A freshly loaded app starts Running, never Suspended — compileApp
  // sets that below. Any arbiter-held suspension died with the old app; the
  // post-load reconcile re-suspends the new app if a claim is still held.
  if (isAppRunning()) {
    _runState = RunState::Ready;
    _overlaySuspendedApp = false;
    notifyAppRunning(false);
  }

  // Reset extensions (declared extensions only, not slot-only peripherals).
  // onAppReset() is an app-facing hook; slot-only peripherals are begun/updated
  // via the lifecycle set but deliberately don't receive app lifecycle events.
  for (uint8_t i = 0; i < _config.extensions.count; i++) {
    _config.extensions.items[i]->onAppReset();
  }
  _eventsModule.onAppReset();

  // Generate new generation ID
  _generationId = String(millis(), HEX);
  emitTelemetry("app_received");

  bool compiled = compileApp(luaCode);
  if (compiled) {
    Serial.println("Resident::Sandbox: app compiled successfully");
    emitTelemetry("app_compiled");
  } else {
    Serial.println("Resident::Sandbox: app compilation failed");
  }

  bool loadedOk = compiled && _lastInitOk;

  // An app loaded while a dual-role overlay claim is held starts suspended
  // rather than ticking (and drawing) beneath the overlay.
  if (loadedOk) reconcileOverlaySuspension();

  // Persist only an app we know loaded cleanly, and never re-persist a restore.
  if (persistOnSuccess && loadedOk && _config.persistApps && _store) {
    if (!_store->save(luaCode, strlen(luaCode))) {
      emitTelemetry("persist_too_big");
    }
  }

  // A load that failed outright (compile error → no app running) returns the
  // display to the Ready identity screen.
  if (!loadedOk && _runState == RunState::Ready) {
    showReadyScreen();
  }

  return loadedOk;
}

void Sandbox::updateBootCountdown()
{
  // System button (if present): a tap loads the saved app now; a long press
  // forgets it. Either gesture ends the countdown.
  if (_config.systemButton && handleCountdownButton()) return;

  unsigned long elapsed = millis() - _countdownStartMs;
  if (elapsed >= BOOT_COUNTDOWN_MS) {
    finishBootCountdown();
    return;
  }

  // Ceil to whole seconds so the first frame reads "20s" and the last "1s".
  int remaining = (int)((BOOT_COUNTDOWN_MS - elapsed + 999) / 1000);
  if (remaining != _lastCountdownSecondShown) {
    _lastCountdownSecondShown = remaining;
    showIdleScreen(remaining);
  }
}

bool Sandbox::handleCountdownButton()
{
  bool down = _config.systemButton->pressed();

  if (down && !_buttonWasDown) {
    // Press edge — start timing.
    _buttonWasDown = true;
    _buttonDownSince = millis();
    _longPressFired = false;
    return false;
  }

  if (down && _buttonWasDown) {
    // Held — fire the long press once the threshold is crossed (no release
    // needed, so a long hold has tactile feedback as soon as it counts).
    if (!_longPressFired &&
        millis() - _buttonDownSince >= SYSTEM_BUTTON_LONG_PRESS_MS) {
      _longPressFired = true;
      _buttonWasDown = false;
      _pendingPersistedSource = "";
      if (_store) _store->clear();
      _runState = RunState::Ready;
      showReadyScreen();           // settle on the device-identity screen
      return true;
    }
    return false;
  }

  if (!down && _buttonWasDown) {
    // Release edge.
    _buttonWasDown = false;
    if (!_longPressFired) {
      finishBootCountdown();       // tap → load the saved app now
      return true;
    }
  }
  return false;
}

void Sandbox::updateSystemButtonHold()
{
  if (!_onHoldCb || !_config.systemButton || _runState == RunState::Pending) return;
  bool down = _config.systemButton->pressed();

  if (down && !_holdWasDown) {
    _holdWasDown = true;
    _holdDownSince = millis();
    _holdFired = false;
  } else if (down && _holdWasDown) {
    if (!_holdFired && millis() - _holdDownSince >= SYSTEM_BUTTON_HOLD_MS) {
      _holdFired = true;
      _onHoldCb(true);
    }
  } else if (!down && _holdWasDown) {
    _holdWasDown = false;
    if (_holdFired) {
      _holdFired = false;
      _onHoldCb(false);
    }
  }
}

bool Sandbox::startMicStream()
{
  if (_micStreaming) return true;
  if (!_config.systemMic) return false;
  if (!_config.systemMic->begin()) return false;
  _micStreaming = true;
  return true;
}

void Sandbox::stopMicStream()
{
  if (!_micStreaming) return;
  _micStreaming = false;
  _config.systemMic->end();
}

void Sandbox::updateMicStream()
{
  if (!_micStreaming || !_config.systemMic) return;
  int want = _config.systemMic->frameSamples();
  if (want > MIC_STREAM_MAX_SAMPLES) want = MIC_STREAM_MAX_SAMPLES;
  if (want <= 0) return;
  int got = _config.systemMic->read(_micBuf, want, 0);
  if (got <= 0) return;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(_micBuf);
  size_t len = (size_t)got * sizeof(int16_t);
  if (_micSink) _micSink(bytes, len);
  else if (_courier.has_value()) ws().sendBinary(bytes, len);
}

bool Sandbox::isAppExtension(Extension* e) const
{
  for (uint8_t i = 0; i < _config.extensions.count; i++) {
    if (_config.extensions.items[i] == e) return true;
  }
  return false;
}

bool Sandbox::appDrawsTo(SystemDisplay* surface) const
{
  Extension* e = static_cast<Extension*>(surface);
  return surface && isSystemExtension(e) && isAppExtension(e);
}

void Sandbox::addOverlay(Overlay* o, SystemDisplay* surface, int priority)
{
  if (!o || _overlayCount >= MAX_OVERLAYS) return;
  _overlays[_overlayCount++] = {o, surface, priority, false, false};
}

void Sandbox::removeOverlay(Overlay* o)
{
  for (uint8_t i = 0; i < _overlayCount; i++) {
    if (_overlays[i].o != o) continue;
    bool wasWinning = _overlays[i].winning;
    SystemDisplay* surface = _overlays[i].surface;
    if (wasWinning) o->onRelease();
    for (uint8_t j = i; j + 1 < _overlayCount; j++) _overlays[j] = _overlays[j + 1];
    _overlayCount--;
    if (wasWinning) {
      // Promote any successor on this surface and reconcile suspension in
      // one arbitration pass; restore the surface only if no successor
      // claimed it (updateOverlays can't see the removed slot's release).
      updateOverlays();
      if (surface) {
        bool stillClaimed = false;
        for (uint8_t j = 0; j < _overlayCount; j++) {
          if (_overlays[j].winning && _overlays[j].surface == surface) {
            stillClaimed = true;
            break;
          }
        }
        if (!stillClaimed) surface->restoreContent();
      }
    }
    return;
  }
}

void Sandbox::requestOverlay(Overlay* o, bool active)
{
  for (uint8_t i = 0; i < _overlayCount; i++) {
    if (_overlays[i].o == o) { _overlays[i].requested = active; return; }
  }
}

// Suspend the app iff any winning claim sits on a dual-role surface. Only a
// suspension the arbiter itself performed is ever resumed here, so a
// device-initiated suspendApp() survives overlay churn untouched.
void Sandbox::reconcileOverlaySuspension()
{
  bool wantSuspend = false;
  for (uint8_t i = 0; i < _overlayCount; i++) {
    if (_overlays[i].winning && appDrawsTo(_overlays[i].surface)) {
      wantSuspend = true;
      break;
    }
  }
  if (wantSuspend && !_overlaySuspendedApp && _runState == RunState::Running) {
    suspendApp();
    _overlaySuspendedApp = true;
  } else if (!wantSuspend && _overlaySuspendedApp) {
    _overlaySuspendedApp = false;
    resumeApp();
  }
}

void Sandbox::updateOverlays()
{
  // Phase 1: per-surface winners. A slot wins iff requested and no other
  // requested slot on the SAME (non-null) surface outranks it — higher
  // priority, or equal priority registered earlier. A null surface is
  // dedicated: the slot contends with nothing and wins whenever requested.
  bool newWinning[MAX_OVERLAYS];
  for (uint8_t i = 0; i < _overlayCount; i++) {
    OverlaySlot& s = _overlays[i];
    bool win = s.requested;
    if (win && s.surface) {
      for (uint8_t j = 0; j < _overlayCount; j++) {
        if (j == i) continue;
        OverlaySlot& t = _overlays[j];
        if (!t.requested || t.surface != s.surface) continue;
        if (t.priority > s.priority ||
            (t.priority == s.priority && j < i)) {
          win = false;
          break;
        }
      }
    }
    newWinning[i] = win;
  }

  // Phase 2: releases before acquires, remembering which non-null surfaces
  // lost their winner (at most one winner per surface, so no dedup needed).
  SystemDisplay* releasedSurfaces[MAX_OVERLAYS];
  uint8_t releasedCount = 0;
  for (uint8_t i = 0; i < _overlayCount; i++) {
    if (_overlays[i].winning && !newWinning[i]) {
      _overlays[i].winning = false;
      _overlays[i].o->onRelease();
      if (_overlays[i].surface) {
        releasedSurfaces[releasedCount++] = _overlays[i].surface;
      }
    }
  }

  // Phase 3: acquires.
  for (uint8_t i = 0; i < _overlayCount; i++) {
    if (!_overlays[i].winning && newWinning[i]) {
      _overlays[i].winning = true;
      _overlays[i].o->onAcquire();
    }
  }

  // Phase 4: app suspension tracks the winning set.
  reconcileOverlaySuspension();

  // Phase 5: a surface whose last claim just released repaints its
  // underlying content — after the resume above (so the app is live again),
  // and never on a handoff (the surface still has a winner then).
  for (uint8_t r = 0; r < releasedCount; r++) {
    bool stillClaimed = false;
    for (uint8_t i = 0; i < _overlayCount; i++) {
      if (_overlays[i].winning && _overlays[i].surface == releasedSurfaces[r]) {
        stillClaimed = true;
        break;
      }
    }
    if (!stillClaimed) releasedSurfaces[r]->restoreContent();
  }

  // Phase 6: winning overlays draw on the app-tick cadence — the overlay
  // takes over the (suspended) app's tick slot. onAcquire already painted
  // the first frame; event-driven repaints outside onDraw remain fine.
  unsigned long now = millis();
  unsigned long elapsed = now - _lastOverlayDrawTime;
  if (elapsed >= TICK_INTERVAL) {
    _lastOverlayDrawTime = now;
    for (uint8_t i = 0; i < _overlayCount; i++) {
      if (_overlays[i].winning) _overlays[i].o->onDraw(elapsed);
    }
  }
}

void Sandbox::finishBootCountdown()
{
  String src = _pendingPersistedSource;
  _pendingPersistedSource = "";
  _runState = RunState::Ready;
  if (src.isEmpty()) return;

  // Restore through the normal load path, but never re-persist a restore.
  bool ok = loadAppInternal(src.c_str(), /*persistOnSuccess=*/false);
  if (ok) {
    emitTelemetry("app_restored");
    // Repaint the resting idle screen (clears the countdown). On devices whose
    // status display IS the app screen this no-ops (the app owns it); on devices
    // with a separate status display (e.g. a dedicated status LCD) it brings the
    // idle screen back instead of leaving the last countdown frame stuck.
    showReadyScreen();
    return;
  }

  // "Just C": a saved app that no longer loads (e.g. the sandbox was reflashed)
  // is discarded; stop any partial app and fall back to the status screen.
  if (isAppRunning()) {
    _runState = RunState::Ready;
    notifyAppRunning(false);
  }
  if (_store) _store->clear();
  emitTelemetry("persist_load_failed");

  // Back to the Ready identity screen. (loadAppInternal already repaints it on
  // a compile failure; this also covers the init-failure case, where the app
  // briefly entered Running before we stopped it above.)
  showReadyScreen();
}

void Sandbox::clearPersistedApp()
{
  if (_store) _store->clear();
}

void Sandbox::loadShader(const ShaderFields& fields) {
  if (!_config.shaderTemplate) {
    Serial.println("[sandbox] No shader template set");
    return;
  }
  String luaCode = _config.shaderTemplate(fields);
  if (luaCode.isEmpty()) {
    Serial.println("[sandbox] Shader template returned empty code");
    return;
  }
  loadApp(luaCode.c_str());
}

// Events received while the app is suspended are still queued onto the ring
// here; loop() defers dispatch (processNextEvent) until resumeApp(), so they
// are deferred — not dropped — though a long suspend can overflow the 8-slot
// ring and lose the oldest. Gating on isAppRunning() (true while Suspended) is
// deliberate: a suspended app is still loaded and will see the events.
void Sandbox::sendAppEvent(const char* name, const char* dataJson,
                           const char* channel)
{
  if (!isAppRunning() || !_onEventFuncRef) return;
  pushAppEvent(name, dataJson ? dataJson : "{}", "", millis(), channel);
}

bool Sandbox::isAppRunning() const
{
  return _runState == RunState::Running || _runState == RunState::Suspended;
}

void Sandbox::suspendApp()
{
  if (_runState != RunState::Running) return;
  _runState = RunState::Suspended;
  notifyAppRunning(false);  // free the status display for overlay text
}

void Sandbox::resumeApp()
{
  if (_runState != RunState::Suspended) return;
  _runState = RunState::Running;
  notifyAppRunning(true);   // re-suppress status display; app owns the screen
}

bool Sandbox::isAppSuspended() const
{
  return _runState == RunState::Suspended;
}

// --- Lua compilation ---

bool Sandbox::compileApp(const char* code)
{
  if (!_lua) return false;

  // Free old function references
  if (_initFuncRef != LUA_NOREF) {
    luaL_unref(_lua, LUA_REGISTRYINDEX, _initFuncRef);
    _initFuncRef = LUA_NOREF;
  }
  if (_onTickFuncRef != LUA_NOREF) {
    luaL_unref(_lua, LUA_REGISTRYINDEX, _onTickFuncRef);
    _onTickFuncRef = LUA_NOREF;
  }
  if (_onEventFuncRef != LUA_NOREF) {
    luaL_unref(_lua, LUA_REGISTRYINDEX, _onEventFuncRef);
    _onEventFuncRef = LUA_NOREF;
  }

  // Reset runtime error rate limiter
  _runtimeErrorCount = 0;
  _lastRuntimeErrorMillis = 0;
  _lastInitOk = false;

  // Clear old global functions
  lua_pushnil(_lua);
  lua_setglobal(_lua, "init");
  lua_pushnil(_lua);
  lua_setglobal(_lua, "on_tick");
  lua_pushnil(_lua);
  lua_setglobal(_lua, "on_event");

  // Load and execute the chunk
  int loadResult = luaL_loadstring(_lua, code);

  if (loadResult == 0) {
    int execResult = lua_pcall(_lua, 0, 0, 0);
    if (execResult != 0) {
      const char* errMsg = lua_tostring(_lua, -1);
      Serial.printf("Resident::Sandbox: execution failed: %s\n", errMsg);
      emitTelemetry("compile_error", errMsg);
      lua_pop(_lua, 1);
    }
  } else {
    const char* errMsg = lua_tostring(_lua, -1);
    Serial.printf("Resident::Sandbox: compile failed: %s\n", errMsg);
    emitTelemetry("compile_error", errMsg);
    lua_pop(_lua, 1);
    return false;
  }

  // Check for callback functions
  lua_getglobal(_lua, "init");
  bool hasInit = lua_isfunction(_lua, -1);
  lua_pop(_lua, 1);

  lua_getglobal(_lua, "on_tick");
  bool hasOnTick = lua_isfunction(_lua, -1);
  lua_pop(_lua, 1);

  lua_getglobal(_lua, "on_event");
  bool hasOnEvent = lua_isfunction(_lua, -1);
  lua_pop(_lua, 1);

  if (!hasInit && !hasOnTick && !hasOnEvent) {
    Serial.println("Resident::Sandbox: no callbacks found (init, on_tick, or on_event required)");
    emitTelemetry("compile_error", "no callbacks found (init, on_tick, or on_event required)");
    return false;
  }

  // Extract function references
  lua_getglobal(_lua, "init");
  if (lua_isfunction(_lua, -1)) {
    _initFuncRef = luaL_ref(_lua, LUA_REGISTRYINDEX);
  } else {
    lua_pop(_lua, 1);
  }

  lua_getglobal(_lua, "on_tick");
  if (lua_isfunction(_lua, -1)) {
    _onTickFuncRef = luaL_ref(_lua, LUA_REGISTRYINDEX);
  } else {
    lua_pop(_lua, 1);
  }

  lua_getglobal(_lua, "on_event");
  if (lua_isfunction(_lua, -1)) {
    _onEventFuncRef = luaL_ref(_lua, LUA_REGISTRYINDEX);
  } else {
    lua_pop(_lua, 1);
  }

  // Reset timing
  _triggerResetTime = millis();
  _lastTickTime = millis();

  _runState = RunState::Running;
  notifyAppRunning(true);
  _lastInitOk = callInit();

  return true;   // loadAppInternal logs + emits the app_compiled telemetry
}

bool Sandbox::callInit()
{
  if (!_lua || _initFuncRef == LUA_NOREF) return true;

  lua_rawgeti(_lua, LUA_REGISTRYINDEX, _initFuncRef);

  // Push ctx table
  unsigned long initT = millis() - _triggerResetTime;
  lua_newtable(_lua);
  lua_pushinteger(_lua, initT);
  lua_setfield(_lua, -2, "time_ms");
  lua_pushinteger(_lua, _triggerCount);
  lua_setfield(_lua, -2, "trigger_count");

  // Time-of-day fields
  pushLocalTimeFields();

  int result = lua_pcall(_lua, 1, 0, 0);
  if (result != 0) {
    const char* errMsg = lua_tostring(_lua, -1);
    Serial.printf("Resident::Sandbox: init() error: %s\n", errMsg);
    emitTelemetry("runtime_error", errMsg);
    lua_pop(_lua, 1);
    return false;
  }
  return true;
}

void Sandbox::callOnTick(unsigned long dt_ms)
{
  if (!_lua || _onTickFuncRef == LUA_NOREF) return;

  lua_rawgeti(_lua, LUA_REGISTRYINDEX, _onTickFuncRef);

  // Push ctx table
  unsigned long t = millis() - _triggerResetTime;
  lua_newtable(_lua);
  lua_pushinteger(_lua, t);
  lua_setfield(_lua, -2, "time_ms");
  lua_pushinteger(_lua, _triggerCount);
  lua_setfield(_lua, -2, "trigger_count");

  // Time-of-day fields
  pushLocalTimeFields();

  lua_pushinteger(_lua, dt_ms);

  int result = lua_pcall(_lua, 2, 0, 0);
  if (result != 0) {
    const char* errMsg = lua_tostring(_lua, -1);
    Serial.printf("Resident::Sandbox: on_tick() error: %s\n", errMsg);

    // Rate-limit on_tick errors (fires 10x/sec)
    unsigned long now = millis();
    if (_runtimeErrorCount < RUNTIME_ERROR_MAX_BURST ||
        now - _lastRuntimeErrorMillis >= RUNTIME_ERROR_COOLDOWN) {
      emitTelemetry("runtime_error", errMsg);
      _lastRuntimeErrorMillis = now;
      _runtimeErrorCount++;
    }
    lua_pop(_lua, 1);
  }
}

void Sandbox::pushLocalTimeFields()
{
  int utcH = UTC.hour();
  int utcM = UTC.minute();
  lua_pushinteger(_lua, utcH);
  lua_setfield(_lua, -2, "utc_h");
  lua_pushinteger(_lua, utcM);
  lua_setfield(_lua, -2, "utc_m");

  int localH = _hasTimezone ? _tz.hour()   : utcH;
  int localM = _hasTimezone ? _tz.minute() : utcM;
  lua_pushinteger(_lua, localH);
  lua_setfield(_lua, -2, "localtime_h");
  lua_pushinteger(_lua, localM);
  lua_setfield(_lua, -2, "localtime_m");
}

void Sandbox::processNextEvent()
{
  if (_eventHead == _eventTail) return;
  if (!_lua || _onEventFuncRef == LUA_NOREF) return;

  Event& e = _events[_eventTail];
  _eventTail = (_eventTail + 1) % SANDBOX_MAX_EVENTS;

  lua_rawgeti(_lua, LUA_REGISTRYINDEX, _onEventFuncRef);

  // Push ctx table
  lua_newtable(_lua);
  lua_pushinteger(_lua, millis() - _triggerResetTime);
  lua_setfield(_lua, -2, "time_ms");
  lua_pushinteger(_lua, _triggerCount);
  lua_setfield(_lua, -2, "trigger_count");

  // Push event table
  lua_newtable(_lua);
  lua_pushstring(_lua, e.name);
  lua_setfield(_lua, -2, "name");
  lua_pushstring(_lua, e.from);
  lua_setfield(_lua, -2, "from");
  lua_pushinteger(_lua, e.ts_ms);
  lua_setfield(_lua, -2, "ts_ms");
  // Envelope: channel always present (driver emissions and BUTTON slots are
  // hardware-side → "driver"; wire-borne APP_EVENTs carry app/runtime, and
  // host injections default "driver" via pushAppEvent). src/seq only when
  // the frame stamped them.
  lua_pushstring(_lua, (e.type == Event::APP_EVENT && e.channel[0])
                           ? e.channel : "driver");
  lua_setfield(_lua, -2, "channel");
  if (e.src[0]) {
    lua_pushstring(_lua, e.src);
    lua_setfield(_lua, -2, "src");
  }
  if (e.hasSeq) {
    lua_pushinteger(_lua, e.seq);
    lua_setfield(_lua, -2, "seq");
  }

  if (e.type == Event::DRIVER) {
    // Flatten driver event fields directly onto the event table
    const char* json = e.data;
    size_t len = strlen(json);
    if (len >= 2 && json[0] == '{' && json[len - 1] == '}') {
      size_t pos = 1;
      while (pos < len - 1) {
        while (pos < len - 1 && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t'))
          pos++;
        if (pos >= len - 1) break;
        if (json[pos] != '"') break;
        pos++;
        char key[64] = {0};
        size_t keyLen = 0;
        while (pos < len - 1 && json[pos] != '"' && keyLen < sizeof(key) - 1)
          key[keyLen++] = json[pos++];
        key[keyLen] = '\0';
        if (pos >= len - 1) break;
        pos++;
        while (pos < len - 1 && (json[pos] == ':' || json[pos] == ' '))
          pos++;
        if (pos >= len - 1) break;
        if (json[pos] == '"') {
          pos++;
          char val[128] = {0};
          size_t valLen = 0;
          while (pos < len - 1 && json[pos] != '"' && valLen < sizeof(val) - 1)
            val[valLen++] = json[pos++];
          val[valLen] = '\0';
          if (pos < len - 1) pos++;
          lua_pushstring(_lua, val);
          lua_setfield(_lua, -2, key);
        } else if (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9')) {
          char numStr[32] = {0};
          size_t numLen = 0;
          while (pos < len - 1 && numLen < sizeof(numStr) - 1 &&
                 (json[pos] == '-' || json[pos] == '.' || (json[pos] >= '0' && json[pos] <= '9')))
            numStr[numLen++] = json[pos++];
          numStr[numLen] = '\0';
          lua_pushnumber(_lua, atof(numStr));
          lua_setfield(_lua, -2, key);
        } else {
          while (pos < len - 1 && json[pos] != ',' && json[pos] != '}')
            pos++;
        }
      }
    }
  } else {
    // APP_EVENT: parse data JSON into event.data subtable. Real JSON parsing
    // (ArduinoJson) + the pushJson* mirror of the outgoing serializer —
    // strings arrive unescaped, booleans as booleans, nested objects/arrays
    // as tables to LUA_JSON_MAX_DEPTH. Same drop-don't-truncate discipline
    // as the outgoing side: unparseable data (e.g. a payload that was cut
    // off upstream) is never delivered garbled — the whole event is dropped.
    JsonDocument dataDoc;
    // const char* input → ArduinoJson copy mode (not zero-copy destructive).
    if (deserializeJson(dataDoc, (const char*)e.data) ||
        !dataDoc.is<JsonObjectConst>()) {
      Serial.printf("Resident::Sandbox: unparseable data for app event '%s'; event dropped\n",
                    e.name);
      lua_pop(_lua, 3);  // on_event handler, ctx table, event table
      return;
    }
    pushJsonObjectToLua(_lua, dataDoc.as<JsonObjectConst>(), /*depth=*/1);
    lua_setfield(_lua, -2, "data");
  }

  int callResult = lua_pcall(_lua, 2, 0, 0);
  if (callResult != 0) {
    const char* errMsg = lua_tostring(_lua, -1);
    Serial.printf("Resident::Sandbox: on_event() error: %s\n", errMsg);
    emitTelemetry("runtime_error", errMsg);
    lua_pop(_lua, 1);
  }
}

void Sandbox::pushAppEvent(const char* name, const char* dataJson, const char* from, uint32_t ts_ms,
                           const char* channel, const char* src,
                           bool hasSeq, uint32_t seq)
{
  int nextHead = (_eventHead + 1) % SANDBOX_MAX_EVENTS;
  if (nextHead == _eventTail) {
    _eventTail = (_eventTail + 1) % SANDBOX_MAX_EVENTS;
  }

  Event& e = _events[_eventHead];
  memset(&e, 0, sizeof(Event));
  e.type = Event::APP_EVENT;
  e.ts_ms = ts_ms;
  strncpy(e.name, name, sizeof(e.name) - 1);
  strncpy(e.data, dataJson, sizeof(e.data) - 1);
  strncpy(e.from, from, sizeof(e.from) - 1);
  strncpy(e.channel, (channel && channel[0]) ? channel : "driver",
          sizeof(e.channel) - 1);
  strncpy(e.src, src ? src : "", sizeof(e.src) - 1);
  e.hasSeq = hasSeq;
  e.seq = seq;
  _eventHead = nextHead;
}

void Sandbox::driverEventHandler(void* ctx, const char* name,
                                  const EventField* fields, int fieldCount)
{
  Sandbox* self = (Sandbox*)ctx;

  // Accept events only while an app is loaded (Running or Suspended). In
  // Suspended they are queued and dispatched on resume (loop() gates dispatch
  // on Running); in Ready/Pending there is no app, so drop them.
  if (!self->isAppRunning()) return;

  // Count button events for ctx.trigger_count
  if (strcmp(name, "button") == 0) {
    self->_triggerCount++;
  }

  // Queue the event
  int nextHead = (self->_eventHead + 1) % SANDBOX_MAX_EVENTS;
  if (nextHead == self->_eventTail) {
    // Ring buffer full — drop oldest
    self->_eventTail = (self->_eventTail + 1) % SANDBOX_MAX_EVENTS;
  }

  Event& e = self->_events[self->_eventHead];
  memset(&e, 0, sizeof(Event));
  e.type = Event::DRIVER;
  e.ts_ms = millis();
  strncpy(e.name, name, sizeof(e.name) - 1);

  // Serialize EventField array into data as compact JSON
  char* p = e.data;
  char* end = e.data + sizeof(e.data) - 1;
  *p++ = '{';
  for (int i = 0; i < fieldCount && p < end - 20; i++) {
    if (i > 0 && p < end) *p++ = ',';
    p += snprintf(p, end - p, "\"%s\":", fields[i].key);
    if (fields[i].type == EventField::INT) {
      p += snprintf(p, end - p, "%d", fields[i].i);
    } else {
      p += snprintf(p, end - p, "\"%s\"", fields[i].s);
    }
  }
  if (p < end) *p++ = '}';
  *p = '\0';

  self->_eventHead = nextHead;
}

void Sandbox::notifyAppRunning(bool running) {
  // Declared extensions only — same intentional carve-out as onAppReset():
  // slot-only peripherals are begun/updated via the lifecycle set but don't
  // receive app-facing hooks (onAppRunning / onAppReset).
  for (uint8_t i = 0; i < _config.extensions.count; i++) {
    Driver* driver = _config.extensions.items[i]->asDriver();
    if (driver) driver->onAppRunning(running);
  }
}

void Sandbox::emitTelemetry(const char* name, const char* error)
{
  if (!_telemetryCb) return;

  char buf[768];
  char escapedError[256] = "";

  if (error) {
    // JSON-escape the error string (just escape quotes and backslashes)
    const char* src = error;
    char* dst = escapedError;
    char* end = escapedError + sizeof(escapedError) - 2;
    while (*src && dst < end) {
      if (*src == '"' || *src == '\\') {
        *dst++ = '\\';
      } else if (*src == '\n') {
        *dst++ = '\\';
        *dst++ = 'n';
        src++;
        continue;
      }
      *dst++ = *src++;
    }
    *dst = '\0';
  }

  // Format: { generationId?, name, data }
  // Matches server-side telemetry protocol
  char dataStr[300];
  if (error) {
    snprintf(dataStr, sizeof(dataStr), "{\"error\":\"%s\"}", escapedError);
  } else {
    strcpy(dataStr, "{}");
  }

  // JSON-escape the generationId (it's a JSON string like {"traceId":"...","spanId":"..."})
  char escapedGenId[256] = "";
  if (_generationId.length() > 0) {
    const char* src = _generationId.c_str();
    char* dst = escapedGenId;
    char* end = escapedGenId + sizeof(escapedGenId) - 2;
    while (*src && dst < end) {
      if (*src == '"' || *src == '\\') {
        *dst++ = '\\';
      }
      *dst++ = *src++;
    }
    *dst = '\0';
  }

  if (_generationId.length() > 0) {
    snprintf(buf, sizeof(buf),
      "{\"type\":\"telemetry\",\"generationId\":\"%s\",\"name\":\"%s\",\"data\":%s}",
      escapedGenId, name, dataStr);
  } else {
    snprintf(buf, sizeof(buf),
      "{\"type\":\"telemetry\",\"name\":\"%s\",\"data\":%s}",
      name, dataStr);
  }

  _telemetryCb(buf);
}

// --- Lua C functions ---

int Sandbox::lua_rgb(lua_State* L)
{
  double r = luaL_checknumber(L, 1);
  double g = luaL_checknumber(L, 2);
  double b = luaL_checknumber(L, 3);

  uint8_t r8 = (uint8_t)(fmax(0.0, fmin(1.0, r)) * 255.0);
  uint8_t g8 = (uint8_t)(fmax(0.0, fmin(1.0, g)) * 255.0);
  uint8_t b8 = (uint8_t)(fmax(0.0, fmin(1.0, b)) * 255.0);

  uint32_t packed = (r8 << 16) | (g8 << 8) | b8;
  lua_pushnumber(L, -(double)packed);
  return 1;
}

int Sandbox::lua_fract(lua_State* L)
{
  double x = luaL_checknumber(L, 1);
  lua_pushnumber(L, x - floor(x));
  return 1;
}

int Sandbox::lua_beat(lua_State* L)
{
  double bpm = luaL_checknumber(L, 1);
  double t = luaL_checknumber(L, 2);
  lua_pushnumber(L, t / (60000.0 / bpm));
  return 1;
}

// noise2d(x, y) — deterministic 2D value noise, returns -1 to +1
// Ported from SmolNoise.h (deleted in 38bb1fe)
static inline double smolHash(int x, int y) {
  uint32_t h = (uint32_t)x * 0x8da6b343 ^ (uint32_t)y * 0xd8163841;
  h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16;
  return (h & 0xFFFFFF) / double(0xFFFFFF);
}

int Sandbox::lua_noise2d(lua_State* L)
{
  double x = luaL_checknumber(L, 1);
  double y = luaL_checknumber(L, 2);
  int xi = (int)floor(x), yi = (int)floor(y);
  double xf = x - xi, yf = y - yi;
  double u = smolHash(xi,     yi    );
  double v = smolHash(xi + 1, yi    );
  double w = smolHash(xi,     yi + 1);
  double z = smolHash(xi + 1, yi + 1);
  double a = u + (v - u) * xf;
  double b = w + (z - w) * xf;
  lua_pushnumber(L, (a + (b - a) * yf) * 2.0 - 1.0);
  return 1;
}

int Sandbox::lua_log_info(lua_State* L)
{
  const char* msg = luaL_checkstring(L, 1);
  Serial.printf("[INFO] %s\n", msg);
  return 0;
}

int Sandbox::lua_log_warn(lua_State* L)
{
  const char* msg = luaL_checkstring(L, 1);
  Serial.printf("[WARN] %s\n", msg);
  return 0;
}

int Sandbox::lua_log_error(lua_State* L)
{
  const char* msg = luaL_checkstring(L, 1);
  Serial.printf("[ERROR] %s\n", msg);

  // Forward log.error() to telemetry
  lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
  Sandbox* self = (Sandbox*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (self) {
    self->emitTelemetry("log_error", msg);
  }
  return 0;
}

int Sandbox::lua_time_is_valid(lua_State* L)
{
  lua_pushboolean(L, timeStatus() == timeSet);
  return 1;
}

int Sandbox::lua_time_hour(lua_State* L)
{
  lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
  Sandbox* self = (Sandbox*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (self && self->_hasTimezone) {
    lua_pushinteger(L, self->_tz.hour());
  } else {
    lua_pushinteger(L, UTC.hour());
  }
  return 1;
}

int Sandbox::lua_time_minute(lua_State* L)
{
  lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
  Sandbox* self = (Sandbox*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (self && self->_hasTimezone) {
    lua_pushinteger(L, self->_tz.minute());
  } else {
    lua_pushinteger(L, UTC.minute());
  }
  return 1;
}

int Sandbox::lua_time_second(lua_State* L)
{
  lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
  Sandbox* self = (Sandbox*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (self && self->_hasTimezone) {
    lua_pushinteger(L, self->_tz.second());
  } else {
    lua_pushinteger(L, UTC.second());
  }
  return 1;
}

int Sandbox::lua_time_day_id(lua_State* L)
{
  lua_pushinteger(L, millis() / 86400000);
  return 1;
}

int Sandbox::lua_time_has_timezone(lua_State* L)
{
  lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
  Sandbox* self = (Sandbox*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  lua_pushboolean(L, self && self->hasTimezone());
  return 1;
}

// --- Math wrapper globals ---

int Sandbox::lua_math_floor(lua_State* L)
{
  lua_pushnumber(L, floor(luaL_checknumber(L, 1)));
  return 1;
}

int Sandbox::lua_math_ceil(lua_State* L)
{
  lua_pushnumber(L, ceil(luaL_checknumber(L, 1)));
  return 1;
}

int Sandbox::lua_math_abs(lua_State* L)
{
  lua_pushnumber(L, fabs(luaL_checknumber(L, 1)));
  return 1;
}

int Sandbox::lua_math_sin(lua_State* L)
{
  lua_pushnumber(L, ::sin(luaL_checknumber(L, 1)));
  return 1;
}

int Sandbox::lua_math_cos(lua_State* L)
{
  lua_pushnumber(L, ::cos(luaL_checknumber(L, 1)));
  return 1;
}

int Sandbox::lua_math_tan(lua_State* L)
{
  lua_pushnumber(L, ::tan(luaL_checknumber(L, 1)));
  return 1;
}

int Sandbox::lua_math_sqrt(lua_State* L)
{
  lua_pushnumber(L, ::sqrt(luaL_checknumber(L, 1)));
  return 1;
}

int Sandbox::lua_math_min(lua_State* L)
{
  lua_pushnumber(L, fmin(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
  return 1;
}

int Sandbox::lua_math_max(lua_State* L)
{
  lua_pushnumber(L, fmax(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
  return 1;
}

int Sandbox::lua_math_fmod(lua_State* L)
{
  lua_pushnumber(L, ::fmod(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
  return 1;
}

} // namespace Resident
