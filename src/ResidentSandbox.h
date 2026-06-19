// src/ResidentSandbox.h
#ifndef RESIDENT_SANDBOX_H
#define RESIDENT_SANDBOX_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ezTime.h>
#include <map>
#include <functional>
#include <optional>
#include <Courier.h>
#include "ResidentDriver.h"
#include "ResidentLuaModule.h"
#include "ResidentSandboxConfig.h"

namespace Resident {

class Sandbox {
public:
    Sandbox();
    explicit Sandbox(const SandboxConfig& config);
    ~Sandbox();

    // Full replacement of the stored config — not additive. Device::setup()
    // calls this automatically with the DeviceConfig forward; downstream
    // subclasses normally don't need to call it directly.
    void configure(const SandboxConfig& config);

    void setTelemetryCallback(TelemetryCallback cb) { _telemetryCb = cb; }

    // Current generation ID (from last app/shader message)
    const String& generationId() const { return _generationId; }

    // Lifecycle
    void initialize();
    void loop();  // runs on_tick

    // Public lifecycle — replaces the old Device::setup()/loop().
    // Standalone mode (no cfg.network): just initialises the Lua state.
    // Networked mode: also fires onConfigureNetwork, kicks off Courier.
    void setup();
    // loop() already exists for the standalone tick — extended to drive Courier.

    // Load an app from Lua source code
    void loadApp(const char* luaCode);

    // Load a shader from fields (uses shader template)
    void loadShader(const ShaderFields& fields);

    // Send an app event to the running app
    void sendAppEvent(const char* name, const char* dataJson);

    // Forget any persisted app (so the next boot has nothing to restore).
    void clearPersistedApp();

    // State queries
    bool isAppRunning() const;

    // App suspend/resume. Pauses the Lua tick (on_tick + event dispatch)
    // without unloading the app — Courier and extension update() keep running.
    // While suspended the status display is freed (notifyAppRunning(false)) so
    // displayText() can show e.g. a "Listening" overlay. Both are no-ops when
    // no app is loaded. isAppRunning() stays true while suspended; suspension
    // is a separate axis queried via isAppSuspended().
    void suspendApp();
    void resumeApp();
    bool isAppSuspended() const;

    // Timezone — no-op on nullptr/empty. Success means ezTime resolved the
    // zone (either from its own cache or via one UDP lookup to
    // timezoned.rop.nl). Failure logs and leaves hasTimezone() == false.
    void setTimezone(const char* ianaZone);
    bool hasTimezone() const { return _hasTimezone; }

    // Network accessors. Both assert if cfg.network was not set.
    Courier::Client& courier();
    Courier::WebSocketTransport& ws();

    // True iff cfg.network was set at construction time.
    bool hasNetwork() const { return _courier.has_value(); }

    // ── Setup-phase callback (register before setup()) ──
    using ConfigureNetworkCallback = std::function<void(Courier::Client&)>;
    void onConfigureNetwork(ConfigureNetworkCallback cb) {
      _onConfigureNetwork = std::move(cb);
    }

    // ── Reactive callbacks (single-slot, last registration wins) ──
    using TransportsWillConnectCallback = std::function<void()>;
    using MessageCallback = std::function<void(const char* transportName,
                                                const char* type,
                                                JsonDocument& doc)>;
    using ConnectionChangeCallback = std::function<void(Courier::State)>;
    using ConnectedCallback = std::function<void()>;

    void onTransportsWillConnect(TransportsWillConnectCallback cb) {
      _onTransportsWillConnect = std::move(cb);
    }
    void onMessage(MessageCallback cb) {
      _onMessage = std::move(cb);
    }
    void onConnectionChange(ConnectionChangeCallback cb) {
      _onConnectionChange = std::move(cb);
    }
    void onConnected(ConnectedCallback cb) {
      _onConnected = std::move(cb);
    }

    // ── Identity / status accessors ──
    const String& getDeviceId() const { return _deviceId; }
    const String& getAPName() const { return _apName; }
    const char* getDeviceType() const {
      return _config.deviceType ? _config.deviceType : "device";
    }
    bool isConnected() const;
    bool isTimeSynced() const;

    // Test hooks — only used by native tests. Exposed here because the mock
    // Timezone carries its configuration per-instance.
    Timezone& timezoneForTest() { return _tz; }
    bool luaGlobalBoolForTest(const char* name);
    int luaGlobalIntForTest(const char* name);
    void callOnTickForTest(unsigned long dt_ms) { callOnTick(dt_ms); }

private:
    struct lua_State* _lua = nullptr;

    // Unified execution state — the sandbox's single source of truth for what
    // the Lua VM is doing. Ready: no app loaded; the status display rests on
    // the device-identity screen (device type + ID) so the device is "ready"
    // to receive an app. Pending: a persisted app is waiting behind the boot
    // countdown (no app loaded yet). Running: app loaded and ticking.
    // Suspended: app loaded but tick + event dispatch are paused and the
    // status display is freed for overlay text. isAppRunning() is true for
    // both Running and Suspended; the Lua tick runs only in Running.
    // Transitions: Ready/Pending → Running (loadApp); Running ⇄ Suspended
    // (suspendApp/resumeApp); any → Ready (failed/replaced/cleared load).
    enum class RunState { Ready, Pending, Running, Suspended };
    RunState _runState = RunState::Ready;

    // Timezone selected via registration's detectedTimezone. When
    // _hasTimezone is true, ctx.localtime_* and time.hour/minute/second read
    // from _tz; otherwise they fall back to UTC.
    Timezone _tz;
    bool _hasTimezone = false;

    // Configuration
    SandboxConfig _config;

    // Optional Courier client — constructed iff cfg.network was set at
    // construction time. WS transport reference is cached for ws() accessor.
    std::optional<Courier::Client> _courier;
    Courier::WebSocketTransport* _ws = nullptr;
    String _deviceId;
    String _apName;

    // User-registered callbacks (single-slot, last registration wins).
    ConfigureNetworkCallback      _onConfigureNetwork;
    TransportsWillConnectCallback _onTransportsWillConnect;
    MessageCallback               _onMessage;
    ConnectionChangeCallback      _onConnectionChange;
    ConnectedCallback             _onConnected;

    // Internal Courier hook handlers (drive status indicators + reserved-type
    // routing, then delegate to user callbacks).
    void wireInternalCourierHooks();
    void onCourierMessage(const char* transportName, const char* type,
                          JsonDocument& doc);
    void onCourierConnectionChange(Courier::State state);
    void onCourierConnected();
    void onCourierTransportsWillConnect();

    // Status display helpers.
    void showStatusText(const char* text);
    // Paint the device-identity screen: "Device ID: <id>\nType: <t>", plus a
    // "\n\n<secs>s" line when countdownSecs >= 0 (the boot countdown).
    void showIdentityScreen(int countdownSecs = -1);
    // Paint the Ready identity screen when the device is idle and reachable
    // (connected, or standalone). No-op while connecting or app-owned.
    void showReadyScreen();
    String _lastStatusText;

    // Track whether the Lua state has been initialised, so setup() is idempotent.
    bool _initialized = false;

    // Unified lifecycle set: extensions[] plus any role-slot object not
    // already present, de-duped by pointer. Driven for begin() and update().
    Extension* _lifecycle[Extensions::MAX + 3] = {};
    uint8_t _lifecycleCount = 0;
    void buildLifecycleSet();
    void addLifecycle(Extension* e);
    // True iff e is one of the assigned role-slot objects (a peripheral).
    bool isPeripheral(Extension* e) const;

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

    // App persistence
    PersistentStore* _store = nullptr;
    bool _lastInitOk = false;   // set by compileApp via callInit()
    bool loadAppInternal(const char* luaCode, bool persistOnSuccess);

    // Boot countdown data (active while _runState == RunState::Pending): show
    // the device ID for BOOT_COUNTDOWN_MS before auto-loading the persisted
    // app. Hard-coded duration.
    String _pendingPersistedSource;
    unsigned long _countdownStartMs = 0;
    int _lastCountdownSecondShown = -1;
    static constexpr unsigned long BOOT_COUNTDOWN_MS = 20000;
    void updateBootCountdown();
    void finishBootCountdown();

    // SystemButton gesture tracking during the countdown (Pending): a tap
    // loads the saved app, a long press forgets it. pressed() is a level read,
    // so the runtime times the hold itself.
    bool _buttonWasDown = false;
    unsigned long _buttonDownSince = 0;
    bool _longPressFired = false;
    static constexpr unsigned long SYSTEM_BUTTON_LONG_PRESS_MS = 1000;
    // Returns true when a gesture ended the countdown (loaded or forgot).
    bool handleCountdownButton();

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
    bool callInit();  // true if init ran without error (or no init function)
    void callOnTick(unsigned long dt_ms);
    void processNextEvent();
    void pushLocalTimeFields();  // pushes utc_h/utc_m/localtime_h/localtime_m onto the Lua table at stack top
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

} // namespace Resident

#endif // RESIDENT_SANDBOX_H
