// src/OutrunSandbox.h
#ifndef OUTRUN_SANDBOX_H
#define OUTRUN_SANDBOX_H

#include <Arduino.h>
#include <ezTime.h>
#include <map>
#include <functional>
#include "OutrunDriver.h"
#include "OutrunModule.h"

namespace Outrun {

using ShaderFields = std::map<String, String>;
using ShaderTemplateFn = String (*)(const ShaderFields& fields);
using TelemetryCallback = std::function<void(const char* json)>;

class Sandbox {
public:
    Sandbox();
    ~Sandbox();

    // Configuration (call before initialize)
    void addDriver(Driver* driver);
    void addModule(Module* module);
    void setShaderTemplate(ShaderTemplateFn fn);
    void setTelemetryCallback(TelemetryCallback cb) { _telemetryCb = cb; }

    // Current generation ID (from last app/shader message)
    const String& generationId() const { return _generationId; }

    // Lifecycle
    void initialize();
    void loop();  // runs on_tick

    // Load an app from Lua source code
    void loadApp(const char* luaCode);

    // Load a shader from fields (uses shader template)
    void loadShader(const ShaderFields& fields);

    // Send an app event to the running app
    void sendAppEvent(const char* name, const char* dataJson);

    // State queries
    bool isAppRunning() const;

    // Timezone — no-op on nullptr/empty. Success means ezTime resolved the
    // zone (either from its own cache or via one UDP lookup to
    // timezoned.rop.nl). Failure logs and leaves hasTimezone() == false.
    void setTimezone(const char* ianaZone);
    bool hasTimezone() const { return _hasTimezone; }

    // Test hooks — only used by native tests. Exposed here because the mock
    // Timezone carries its configuration per-instance.
    Timezone& timezoneForTest() { return _tz; }
    bool luaGlobalBoolForTest(const char* name);

private:
    struct lua_State* _lua = nullptr;
    bool _appRunning = false;

    // Timezone selected via registration's detectedTimezone. When
    // _hasTimezone is true, ctx.localtime_* and time.hour/minute/second read
    // from _tz; otherwise they fall back to UTC.
    Timezone _tz;
    bool _hasTimezone = false;

    // Drivers and modules
    static constexpr int MAX_DRIVERS = 8;
    Driver* _drivers[MAX_DRIVERS] = {};
    int _driverCount = 0;

    static constexpr int MAX_MODULES = 8;
    Module* _modules[MAX_MODULES] = {};
    int _moduleCount = 0;

    // Shader template
    ShaderTemplateFn _shaderTemplate = nullptr;

    // Telemetry
    TelemetryCallback _telemetryCb;
    String _generationId;
    void emitTelemetry(const char* name, const char* error = nullptr);

    // Runtime error rate limiting (suppress repeated on_tick errors)
    unsigned long _lastRuntimeErrorMillis = 0;
    int _runtimeErrorCount = 0;
    static constexpr unsigned long RUNTIME_ERROR_COOLDOWN = 5000;  // 5s between reports
    static constexpr int RUNTIME_ERROR_MAX_BURST = 3;

    // Lua function references
    int _initFuncRef = 0;
    int _onTickFuncRef = 0;
    int _onEventFuncRef = 0;

    // Frame timing
    unsigned long _lastTickTime = 0;
    static constexpr unsigned long TICK_INTERVAL = 100; // 10 FPS

    // Event queue
    struct Event {
        enum Type { BUTTON, APP_EVENT, DRIVER } type;
        char name[32];
        char data[256];
        char from[64];
        uint32_t ts_ms;
    };
    static constexpr int SANDBOX_MAX_EVENTS = 8;
    Event _events[SANDBOX_MAX_EVENTS];
    int _eventHead = 0;
    int _eventTail = 0;

    // Trigger state
    unsigned long _triggerResetTime = 0;
    int _triggerCount = 0;

    // Lua setup
    void setupLuaEnvironment();
    bool compileApp(const char* code);
    void callInit();
    void callOnTick(unsigned long dt_ms);
    void processNextEvent();
    void pushAppEvent(const char* name, const char* dataJson, const char* from, uint32_t ts_ms);
    void notifyAppRunning(bool running);
    static void driverEventHandler(void* ctx, const char* name,
                                   const EventField* fields, int fieldCount);

    // Lua C functions (static)
    static int lua_rgb(lua_State* L);
    static int lua_fract(lua_State* L);
    static int lua_beat(lua_State* L);
    static int lua_noise2d(lua_State* L);
    static int lua_log_info(lua_State* L);
    static int lua_log_warn(lua_State* L);
    static int lua_log_error(lua_State* L);
    static int lua_time_is_valid(lua_State* L);
    static int lua_time_hour(lua_State* L);
    static int lua_time_minute(lua_State* L);
    static int lua_time_second(lua_State* L);
    static int lua_time_day_id(lua_State* L);
    static int lua_time_has_timezone(lua_State* L);

    // Math wrapper functions
    static int lua_math_floor(lua_State* L);
    static int lua_math_ceil(lua_State* L);
    static int lua_math_abs(lua_State* L);
    static int lua_math_sin(lua_State* L);
    static int lua_math_cos(lua_State* L);
    static int lua_math_tan(lua_State* L);
    static int lua_math_sqrt(lua_State* L);
    static int lua_math_min(lua_State* L);
    static int lua_math_max(lua_State* L);
    static int lua_math_fmod(lua_State* L);
};

} // namespace Outrun

#endif // OUTRUN_SANDBOX_H
