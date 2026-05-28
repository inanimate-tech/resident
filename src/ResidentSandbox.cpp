#include "ResidentSandbox.h"
#include <Arduino.h>
#include <ezTime.h>
#include <math.h>
#include <cassert>
#include "chipstring.h"

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"

// Custom Lua allocator that routes all allocations to PSRAM.
static void* psramLuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  (void)ud;
  (void)osize;
  if (nsize == 0) {
    heap_caps_free(ptr);
    return NULL;
  }
  if (ptr == NULL) {
    return heap_caps_malloc(nsize, MALLOC_CAP_SPIRAM);
  }
  return heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM);
}
#endif

namespace Resident {

// Registry key for accessing the sandbox instance from Lua C functions
static const char* REGISTRY_KEY = "ResidentSandbox_instance";

Sandbox::Sandbox() {
  memset(_events, 0, sizeof(_events));
}

Sandbox::Sandbox(const SandboxConfig& config) : Sandbox() {
  configure(config);
  _deviceId = ::getDeviceId();

  if (_config.network.has_value()) {
    _courier.emplace(*_config.network);
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

  // Walk extensions in registration order: begin (idempotent), wire
  // event sink if Driver, build Lua module table.
  for (uint8_t i = 0; i < _config.extensions.count; i++) {
    Extension* ext = _config.extensions.items[i];
    Serial.printf("  Initializing extension: %s\n", ext->name());

    // Wire event sink first so a Driver's begin() can safely sendEvent().
    Driver* driver = ext->asDriver();
    if (driver) {
      driver->setEventSink(driverEventHandler, this);
    }

    Extension::beginExtension(*ext);

    // Register Lua module: push fresh table, let extension populate it,
    // setglobal under the extension's name.
    lua_newtable(_lua);
    LuaModule m(_lua, ext);
    ext->registerModule(m);
    lua_setglobal(_lua, ext->name());
  }

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

  if (_courier.has_value()) {
    // Re-derive deviceId in case it wasn't ready at construction.
    _deviceId = ::getDeviceId();

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

  // 4. Status display starts up regardless of network.
  if (_config.statusDisplay) _config.statusDisplay->begin();

  // 5. Sandbox internals (Lua state, extensions). Always.
  initialize();

  // 6. Kick off Courier (WiFi + transports).
  if (_courier.has_value()) {
    _courier->setup();
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

void Sandbox::onCourierMessage(const char* transportName,
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
    char dataJson[256];
    if (doc["data"].is<JsonObject>()) {
      serializeJson(doc["data"], dataJson, sizeof(dataJson));
    } else {
      strcpy(dataJson, "{}");
    }
    if (name) sendAppEvent(name, dataJson);
    return;
  }

  // Anything else → user callback if registered.
  if (_onMessage) _onMessage(transportName, type, doc);
}

void Sandbox::onCourierConnectionChange(Courier::State state)
{
  using S = Courier::State;

  // Resident's internal status-text handling. Runs unconditionally if a
  // statusDisplay is configured. User's onConnectionChange callback runs
  // after, in addition (does not replace).
  switch (state) {
    case S::WifiConnecting:        showStatusText("WiFi..."); break;
    case S::WifiConfiguring: {
      // Build status text, appending AP name when available
      String s = _apName.isEmpty() ? "Configure WiFi" : (String("Configure WiFi\n") + _apName);
      showStatusText(s.c_str());
      break;
    }
    case S::WifiConnected:         showStatusText("WiFi connected"); break;
    case S::TransportsConnecting:  showStatusText("Connecting..."); break;
    case S::Connected:             showStatusText("Connected"); break;
    case S::Reconnecting:          showStatusText("Reconnecting..."); break;
    case S::ConnectionFailed:      showStatusText("Connection failed"); break;
    default: break;
  }

  if (_config.statusLED) {
    switch (state) {
      case S::WifiConnecting:
      case S::WifiConfiguring:       _config.statusLED->solidColor(0xFFFF00); break;
      case S::WifiConnected:
      case S::TransportsConnecting:  _config.statusLED->solidColor(0x00FFFF); break;
      case S::Connected:             _config.statusLED->solidColor(0x00FF00); break;
      case S::Reconnecting:          _config.statusLED->solidColor(0xFF8800); break;
      case S::ConnectionFailed:      _config.statusLED->solidColor(0xFF0000); break;
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
  if (!_config.statusDisplay) return;
  if (_lastStatusText == text) return;
  _lastStatusText = text;
  _config.statusDisplay->displayText(text);
}

void Sandbox::loop() {
  if (_courier.has_value()) {
    _courier->loop();
  }
  if (_config.statusDisplay) _config.statusDisplay->update();

  if (!_lua) return;

  // Standalone path always ticks; networked path gates on isConnected
  // (matches today's Device::loop behavior).
  bool shouldTick = !_courier.has_value() || isConnected();
  if (!shouldTick) return;

  // Drive every registered extension's update() at full main-loop rate
  // — independent of whether an app is loaded. Drivers like button
  // pollers depend on continuous polling for debounce and latency.
  for (uint8_t i = 0; i < _config.extensions.count; i++) {
    _config.extensions.items[i]->update();
  }

  // Lua tick + event dispatch only when an app is running.
  if (!_appRunning) return;

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
  // Stop current app before loading new one
  if (_appRunning) {
    _appRunning = false;
    notifyAppRunning(false);
  }

  // Reset extensions
  for (uint8_t i = 0; i < _config.extensions.count; i++) {
    _config.extensions.items[i]->onAppReset();
  }

  // Generate new generation ID
  _generationId = String(millis(), HEX);
  emitTelemetry("app_received");

  if (compileApp(luaCode)) {
    Serial.println("Resident::Sandbox: app compiled successfully");
    emitTelemetry("app_compiled");
  } else {
    Serial.println("Resident::Sandbox: app compilation failed");
  }
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

void Sandbox::sendAppEvent(const char* name, const char* dataJson)
{
  if (!_appRunning || !_onEventFuncRef) return;
  pushAppEvent(name, dataJson ? dataJson : "{}", "", millis());
}

bool Sandbox::isAppRunning() const
{
  return _appRunning;
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

  _appRunning = true;
  notifyAppRunning(true);
  callInit();

  Serial.println("Resident::Sandbox: app compiled successfully");
  return true;
}

void Sandbox::callInit()
{
  if (!_lua || _initFuncRef == LUA_NOREF) return;

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
  }
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
    // APP_EVENT: parse data into event.data subtable (existing behavior)
    lua_newtable(_lua);
    {
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
    }
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

void Sandbox::pushAppEvent(const char* name, const char* dataJson, const char* from, uint32_t ts_ms)
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
  _eventHead = nextHead;
}

void Sandbox::driverEventHandler(void* ctx, const char* name,
                                  const EventField* fields, int fieldCount)
{
  Sandbox* self = (Sandbox*)ctx;

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
