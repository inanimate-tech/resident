// src/ResidentSandbox.h
#ifndef RESIDENT_SANDBOX_H
#define RESIDENT_SANDBOX_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ezTime.h>
#include <map>
#include <functional>
#include <optional>
#include <vector>
#include <Courier.h>
#include "ResidentDriver.h"
#include "ResidentLuaModule.h"
#include "ResidentSandboxConfig.h"
#include "ResidentOverlay.h"
#include "ResidentEvents.h"
#include "ResidentStoreModule.h"

// Maximum size (bytes, including the NUL terminator) of a serialized event
// `data` JSON payload — used by the outgoing events.send serializer, the
// incoming app-channel data buffer, and the event ring's per-slot storage.
// Override with a build flag, e.g. -DRESIDENT_EVENT_JSON_MAX=2048.
// RAM note: the event ring holds SANDBOX_MAX_EVENTS (8) slots, so raising
// this grows the Sandbox by 8x the increase.
#ifndef RESIDENT_EVENT_JSON_MAX
#define RESIDENT_EVENT_JSON_MAX 1024
#endif

// The wire-protocol version announced in the device hello (data.protocol).
// Bumped only on incompatible envelope changes — this is the whole
// compatibility story (there is no feature list; a genuinely optional
// capability introduces its own hello field when it exists).
#define RESIDENT_PROTOCOL_VERSION 1

// Event ring depth (slots; one is kept free, so usable depth is one less).
// Override with a build flag, e.g. -DRESIDENT_EVENT_RING_SIZE=16.
// RAM note: each slot holds RESIDENT_EVENT_JSON_MAX bytes of data.
#ifndef RESIDENT_EVENT_RING_SIZE
#define RESIDENT_EVENT_RING_SIZE 8
#endif

// Outbound event queue depth (rate-limited / offline sends wait here and
// drain in order on capacity or reconnect).
#ifndef RESIDENT_EVENT_QUEUE_SIZE
#define RESIDENT_EVENT_QUEUE_SIZE 16
#endif

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

    // Optional line shown at the TOP of the idle screen (Ready + Pending),
    // above Device ID / Type. Empty by default. Device-supplied (e.g. a
    // wake-word hint); resident never generates it.
    void setIdleScreenTitle(const char* title) { _idleScreenTitle = title ? title : ""; }

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

    // Run a Lua chunk in the RUNNING app's lua_State: an in-place patch, where
    // loadApp is a restart. The app's globals, timers and event flow survive
    // and init() is NOT re-called, so a chunk that reassigns a function or a
    // table entry swaps it without losing app state. Never persisted (chunks
    // are ephemeral; the server re-sends them after a reboot). Returns false —
    // with the app left running and untouched — on compile error, runtime
    // error, no app loaded, or during a deferAppLoads window (chunks are
    // DROPPED with a log, not stashed).
    // Wire entry: system-channel {type:"chunk", code:"..."}.
    bool loadChunk(const char* code);

    // Send an app event to the running app. Host-firmware injections are
    // tagged channel "driver" by default (delivered as e.channel in Lua);
    // pass "app"/"runtime" to impersonate a wire channel deliberately.
    void sendAppEvent(const char* name, const char* dataJson,
                      const char* channel = "driver");

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

    // Runtime hold gesture on the SystemButton role slot. cb(true) fires once
    // when a hold crosses the threshold; cb(false) on release. Fires whenever
    // an app is loaded OR the device is idle — inert during the boot countdown
    // (RunState::Pending), where handleCountdownButton owns the button, so the
    // two never process the same press within a single tick. Note: on a device
    // that BOTH persists apps AND wires this to the same button, holding
    // through the countdown's forget-gesture and continuing past the hold
    // threshold can fire a hold enter without an intervening release; no
    // shipped example combines these. Optional — unset means no hold handling.
    using SystemButtonHoldCallback = std::function<void(bool held)>;
    void onSystemButtonHold(SystemButtonHoldCallback cb) { _onHoldCb = std::move(cb); }

    // Mic streaming pump. startMicStream() begins the mic (capture runs only
    // while streaming; false = no mic, or it failed to start); each loop()
    // then drains systemMic and ships int16 frames to the sink (default:
    // ws().sendBinary). No framing — control frames are a device concern.
    // stopMicStream() ends the mic, releasing shared capture hardware.
    using MicStreamSink = std::function<bool(const uint8_t* data, size_t len)>;
    void setMicStreamSink(MicStreamSink sink) { _micSink = std::move(sink); }
    bool startMicStream();
    void stopMicStream();
    bool isMicStreaming() const { return _micStreaming; }

    // Capture brackets (0.8) — the one dialect for media capture: sends
    // {channel:"system", type:"capture", data:{state:"start", stream,
    // format}} BEFORE the first media frame (payloads stay raw; the bracket
    // carries the metadata), starts the mic pump, and reverses on end.
    // Format 1 = PCM16 mono 16 kHz. Call from the main loop only (a send
    // from the receive context is silently dropped by the transport).
    bool startCapture(uint16_t stream = 1, uint16_t format = 1);
    void endCapture();

    // Overlay support. Register an overlay as a claim on the display surface
    // it draws to (nullptr = a dedicated surface: never contends, never
    // suspends the app); toggle its desired state with requestOverlay. The
    // arbiter resolves claims PER SURFACE — the highest-priority requested
    // overlay on each surface wins (ties: earlier registration) and receives
    // onAcquire / tick-paced onDraw / onRelease. While any winning overlay's
    // surface is dual-role (appDrawsTo), the app is suspended. When a
    // surface's last claim releases, the arbiter calls its restoreContent().
    void addOverlay(Overlay* o, SystemDisplay* surface, int priority);
    void removeOverlay(Overlay* o);
    void requestOverlay(Overlay* o, bool active);

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
    // Legacy un-channelled path ONLY: fires for messages with no "channel"
    // field (and, among those, only non-reserved types — app/shader/
    // app_event/forget are still routed internally). New code should use
    // channel routing instead: handleAppMessage / handleSystemMessage /
    // onMessageWithChannel. This slot exists so senders that predate the
    // channel field keep working, and is not where new message types land.
    void onMessage(MessageCallback cb) {
      _onMessage = std::move(cb);
    }
    void onConnectionChange(ConnectionChangeCallback cb) {
      _onConnectionChange = std::move(cb);
    }
    void onConnected(ConnectedCallback cb) {
      _onConnected = std::move(cb);
    }

    // ── Message interposition ──
    // Legacy un-channelled path ONLY: runs before app-load deferral,
    // reserved-type routing (app/shader/app_event/forget), and the user
    // onMessage callback — but only for messages with no "channel" field.
    // Channelled messages (app/system/custom) bypass this filter entirely;
    // it does not run in front of handleAppMessage / handleSystemMessage /
    // onMessageWithChannel. Return true to continue normal routing; return
    // false to consume the message (nothing further runs). Kept for
    // dedup/self-echo filtering and platform-only message types on senders
    // that predate the channel field.
    using MessageFilter = std::function<bool(const char* transportName,
                                              const char* type,
                                              JsonDocument& doc)>;
    void onMessageFilter(MessageFilter cb) { _messageFilter = std::move(cb); }

    // Defer app/shader loads (e.g. during a voice recording, when a Lua
    // compile would stall the audio path). While set, incoming app/shader
    // messages are stashed — last one wins — instead of loaded; clearing
    // applies the stashed load immediately, routing it straight to loading
    // without re-running the filter (it already passed at receipt, so a dedup
    // filter won't drop it a second time). Other types flow normally.
    void deferAppLoads(bool defer);
    bool hasDeferredAppLoad() const { return _deferredLoadJson != nullptr; }

    // Route a message through the sandbox exactly as if it arrived from a
    // transport (filter → deferral → reserved-type routing → user callback).
    // For wrappers with their own receive path, and for native tests.
    void injectMessage(const char* transportName, const char* type,
                       JsonDocument& doc);

    // ── Channel routing (see docs: channels) ──
    // Data-plane entry: every message on channel "app". Self-echo drop and
    // nonce dedup (Task 4), then delivery to on_event with event.name = type.
    // Public because wrappers call it directly from per-transport hooks
    // (e.g. MQTT topic mapping) — no loopback through Courier.
    void handleAppMessage(const char* transportName, const char* type,
                          JsonDocument& doc);

    // Control-plane entry: every message on channel "system". Reserved types
    // app/shader/forget are handled internally (deferral included); all other
    // types fall through to the "system" channel slot.
    void handleSystemMessage(const char* transportName, const char* type,
                             JsonDocument& doc);

    // Single slot per channel, last registration wins. "app" is not
    // registrable (the data plane belongs to the Lua app); a "system" slot
    // receives only non-reserved control types.
    void onMessageWithChannel(const char* channel, MessageCallback cb);

    // ── Control-plane emit ──
    // Stamps channel:"system" and sends via the default transport. For
    // device control messages (voice start/end etc.). Returns false when no
    // network is configured or the send fails; the doc is stamped either way.
    bool sendSystem(JsonDocument& doc);

    // App/shader "description" → systemDisplay on load receipt (default on).
    // Disable on devices whose system display IS the main app screen.
    void setShowDescriptions(bool show) { _showDescriptions = show; }

    // ── Data-plane emit ──
    // Builds {channel:"app", type:name, data, from, nonce, ts_ms} and
    // delivers it through the outbound event queue: sent immediately when
    // under the rate limit (5/s, burst 10) and a sink/transport is up;
    // otherwise queued (bounded; oldest non-keeper evicted on overflow)
    // and drained in order from loop(). Shared by the Lua path
    // (events.send) and the C++ path (wrapper aliases).
    enum class SendResult { Sent, Queued, Dropped };
    SendResult publishEventEx(const char* name, const char* dataJson,
                              bool keep = false,
                              const char* channel = "app");
    // Legacy boolean shape: true = the event will go (sent or queued).
    bool publishEvent(const char* name, const char* dataJson) {
      return publishEventEx(name, dataJson) != SendResult::Dropped;
    }

    using EventSink = std::function<bool(JsonDocument&)>;
    void setEventSink(EventSink sink) { _eventSink = std::move(sink); }

    // ── Control-plane emit seam ──
    // When set, sendSystem() hands the stamped doc here instead of the
    // transport — the system-plane mirror of setEventSink (native tests, or
    // a platform wrapper that owns its own delivery).
    using SystemSink = std::function<bool(JsonDocument&)>;
    void setSystemSink(SystemSink sink) { _systemSink = std::move(sink); }

    // ── Hello (see docs/api.md "Hello") ──
    // Queue the device hello for the next loop() drain. Called internally on
    // every transport connect; public so boards and tests can re-announce.
    void requestHello() { _helloPending = true; }
    // True once a host hello has been received this boot. The legacy
    // un-channelled path and the app_event wrapper close when it is true
    // (the reverse-hello rule from the receiving side); future defaults
    // key on it the same way.
    bool hostHelloSeen() const { return _hostHelloSeen; }

    // Count a silently-dropped item (ring overflow, oversize payload,
    // rate limit, closed legacy path). Public so internal modules
    // (EventsModule) can report through the same counter; reported
    // upstream as throttled `dropped` telemetry.
    void countDrop() { _dropCount++; }

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
    MessageFilter                 _messageFilter;

    // Deferred app/shader load: a heap-owned serialized copy of the last
    // stashed message (nullptr = none). Raw malloc, not String, so the stash
    // costs one allocation during the memory-sensitive window that deferral
    // exists for. Freed on apply, overwrite, and destruction.
    bool  _deferLoads = false;
    char* _deferredLoadJson = nullptr;

    // Channel slot registry (single slot per channel, exact-string match).
    static constexpr int MAX_CHANNEL_SLOTS = 8;
    struct ChannelSlot { String name; MessageCallback cb; };
    ChannelSlot _channelSlots[MAX_CHANNEL_SLOTS];
    int _channelSlotCount = 0;
    MessageCallback* lookupChannelSlot(const char* channel);

    // Data-plane emit: internal Extension registering the `events` Lua
    // module (events.send) + the shared token-bucket rate limiter. Named
    // _eventsModule (not _events) — that name is already taken by the
    // Event ring buffer below.
    EventsModule _eventsModule;
    // The Lua `store` KV slot — survives loadApp; namespaced by the app-load
    // message's storeNs; debounced write-through to _store.
    StoreModule _storeModule;
    EventSink _eventSink;
    SystemSink _systemSink;
    unsigned long _eventNonceCounter = 0;

    // ── Outbound control-plane queue (hello + wire telemetry) ──
    // Sends must never happen from the receive context (a reentrant WS send
    // is silently dropped), and telemetry fires from paths that RUN in the
    // receive context (loadApp → emitTelemetry). So control-plane emissions
    // queue here and drain from loop(). Bounded; overflow drops the oldest.
    bool _helloPending = false;
    bool _hostHelloSeen = false;
    String _bootId;
    struct PendingTelemetry { String name; String generationId; String error; long count = -1; };
    static constexpr int TELEMETRY_QUEUE_SIZE = 8;
    PendingTelemetry _pendingTelemetry[TELEMETRY_QUEUE_SIZE];
    int _telemetryHead = 0;
    int _telemetryTail = 0;
    void queueTelemetryWire(const char* name, const char* error, long count = -1);
    void drainOutboundSystem();
    bool sendHello();

    // ── Drop accounting (loss is reported, never silent) ──
    // One counter for every silent-loss site: ring overflow, oversize
    // payloads (both directions), rate-limited publishes, closed legacy
    // paths. Reported as `dropped` telemetry (count = cumulative since
    // boot), throttled to one report per interval and only when changed.
    static constexpr unsigned long DROP_REPORT_INTERVAL_MS = 60000;
    uint32_t _dropCount = 0;
    uint32_t _lastReportedDrops = 0;
    unsigned long _lastDropReportMs = 0;

    // ── Outbound event queue (R9): rate-limited/offline sends wait here ──
    // Envelopes are stamped (seq/nonce) at enqueue so retries are dedup-safe
    // and ordering holds; the drain takes rate-limit tokens as it sends.
    // keep=true entries (asks) are never evicted by overflow.
    struct QueuedEvent { String json; bool keep = false; uint32_t seq = 0; };
    std::vector<QueuedEvent> _eventQueue;
    void drainOutboundEvents();

    // Active capture bracket (startCapture/endCapture).
    uint16_t _captureStream = 0;

    // Fresh app environment (R5): baseline snapshot + per-load reset.
    void snapshotBaselineGlobals();
    void resetAppGlobals();

    // ── Execution budget (R8): per-dispatch instruction cap ──
    void armExecutionBudget();
    void disarmExecutionBudget();
    static void executionBudgetHook(struct lua_State* L, struct lua_Debug* ar);

    // Shared ctx builder for event/framework dispatch.
    void pushCtxTable();

    // ── Framework module hosting (R16) ──
    // The framework runs in its own environment table (__index → _G): its
    // globals are unreachable from app code, while it reads the shared
    // baseline. C++ holds refs to its env and hook functions (set to
    // LUA_NOREF in initialize(), like the lifecycle refs); the runtime-
    // channel sender is a C closure handed into that env — no app path to
    // it exists. Hook contract (all optional, looked up in the framework
    // env after its chunk runs):
    //   framework_install()        — before each app chunk: (re)install the
    //                                framework's app-facing API into _G
    //   framework_tick(ctx, dt)    — before the app's on_tick
    //   framework_event(ctx, e)    — before the app's on_event; return
    //                                true to consume (app never sees it)
    //   framework_app_loaded()     — after an app loads successfully
    int _fwEnvRef = 0;
    int _fwInstallRef = 0;
    int _fwTickRef = 0;
    int _fwEventRef = 0;
    int _fwAppLoadedRef = 0;
    bool _fwActive = false;
    String _fwName;
    int _fwVersion = 0;
    const char* _fwSlotSource = "builtin";   // "builtin" | "slot"
    bool frameworkActive() const { return _fwActive; }
    bool loadFramework(const char* code, const char* name, int version,
                       bool fromSlot);
    void unloadFramework();
    void setupFramework();       // boot: slot blob if valid, else built-in
    void handleFrameworkMessage(JsonDocument& doc);
    void refreshFrameworkHooks();
    bool callFrameworkNoArg(int ref, const char* what);
    void callFrameworkTick(unsigned long dt_ms);
    static int luaRuntimeSend(struct lua_State* L);

    // Description-on-load display (setShowDescriptions).
    bool _showDescriptions = true;
    void maybeShowDescription(JsonDocument& doc);

    // Shared by the channelled and legacy load paths.
    void stashDeferredLoad(JsonDocument& doc);

    // Internal Courier hook handlers (drive status indicators + reserved-type
    // routing, then delegate to user callbacks).
    void wireInternalCourierHooks();
    void onCourierMessage(const char* transportName, const char* type,
                          JsonDocument& doc);
    // Reserved-type routing + user callback, below the filter/deferral guards.
    // Called directly when an applied stash must bypass a re-filter.
    void dispatchMessage(const char* transportName, const char* type,
                         JsonDocument& doc);
    void onCourierConnectionChange(Courier::State state);
    void onCourierConnected();
    void onCourierTransportsWillConnect();

    // Status display helpers.
    void showStatusText(const char* text);
    // Paint the idle screen: an optional title line (setIdleScreenTitle) on top,
    // then "Device ID: <id>\nType: <t>", plus a "\n<secs>s" line when
    // countdownSecs >= 0 (the boot countdown).
    void showIdleScreen(int countdownSecs = -1);
    // Paint the Ready identity screen when the device is idle and reachable
    // (connected, or standalone). No-op while connecting or app-owned.
    void showReadyScreen();
    String _lastStatusText;

    // Track whether the Lua state has been initialised, so setup() is idempotent.
    bool _initialized = false;

    // Unified lifecycle set: extensions[] plus any role-slot object not
    // already present, de-duped by pointer. Driven for begin() and update().
    // Sized for extensions[] (MAX) plus the Driver role slots (display, LED,
    // button) that buildLifecycleSet() may append when each is a distinct
    // object not already in extensions[]. (systemMic is not a Driver and not
    // in this set — the mic pump owns its begin()/end(). One slot of
    // headroom remains from when it was.)
    Extension* _lifecycle[Extensions::MAX + 4] = {};
    uint8_t _lifecycleCount = 0;
    void buildLifecycleSet();
    void addLifecycle(Extension* e);
    // True iff e is one of the assigned system role-slot objects.
    bool isSystemExtension(Extension* e) const;
    // Deprecated-field reconciliation: new field wins, old is the fallback.
    SystemDisplay* systemDisplay() const;
    SystemLED* systemLED() const;

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

    String _idleScreenTitle;   // optional top line on the idle screen (see setIdleScreenTitle)

    // Boot countdown data (active while _runState == RunState::Pending): show
    // the device ID for BOOT_COUNTDOWN_MS before auto-loading the persisted
    // app. Hard-coded duration.
    String _pendingPersistedSource;
    unsigned long _countdownStartMs = 0;
    int _lastCountdownSecondShown = -1;
    static constexpr unsigned long BOOT_COUNTDOWN_MS = 20000;
    void updateBootCountdown();
    void finishBootCountdown();
    // Present the idle UI once the device is reachable (first connection, or
    // standalone setup): identity screen + countdown if an app is persisted,
    // else just the identity screen. No-op once an app is loaded/counting down.
    void enterIdleScreen();

    // SystemButton gesture tracking during the countdown (Pending): a tap
    // loads the saved app, a long press forgets it. pressed() is a level read,
    // so the runtime times the hold itself.
    bool _buttonWasDown = false;
    unsigned long _buttonDownSince = 0;
    bool _longPressFired = false;
    static constexpr unsigned long SYSTEM_BUTTON_LONG_PRESS_MS = 1000;
    // Returns true when a gesture ended the countdown (loaded or forgot).
    bool handleCountdownButton();

    // Runtime hold detector (distinct state from the countdown gesture).
    SystemButtonHoldCallback _onHoldCb;
    bool _holdWasDown = false;
    unsigned long _holdDownSince = 0;
    bool _holdFired = false;
    static constexpr unsigned long SYSTEM_BUTTON_HOLD_MS = 500;
    void updateSystemButtonHold();

    // Mic streaming pump state.
    bool _micStreaming = false;
    MicStreamSink _micSink;
    static constexpr int MIC_STREAM_MAX_SAMPLES = 512;
    int16_t _micBuf[MIC_STREAM_MAX_SAMPLES] = {};
    void updateMicStream();

    // Overlay arbiter state. Winners are tracked per slot (per-surface
    // arbitration — see updateOverlays); _overlaySuspendedApp marks a
    // suspension the ARBITER performed, so a device-initiated suspendApp()
    // is never resumed by an overlay releasing.
    static constexpr int MAX_OVERLAYS = 4;
    struct OverlaySlot {
        Overlay* o;
        SystemDisplay* surface;   // nullptr = dedicated surface
        int priority;
        bool requested;
        bool winning;
    };
    OverlaySlot _overlays[MAX_OVERLAYS] = {};
    uint8_t _overlayCount = 0;
    bool _overlaySuspendedApp = false;
    unsigned long _lastOverlayDrawTime = 0;
    void updateOverlays();
    // Suspend/resume the app to match the current winning claims. Called
    // from the arbiter and from loadAppInternal (an app loaded under a held
    // dual-role overlay starts suspended rather than ticking beneath it).
    void reconcileOverlaySuspension();
    // True iff e is a declared (app-facing) extension.
    bool isAppExtension(Extension* e) const;
    // True iff the app draws to surface (dual-role: system slot AND app ext).
    bool appDrawsTo(SystemDisplay* surface) const;

    // Frame timing
    unsigned long _lastTickTime = 0;
    static constexpr unsigned long TICK_INTERVAL = 100; // 10 FPS

    // Event queue
    struct Event {
        enum Type { BUTTON, APP_EVENT, DRIVER } type;
        char name[32];
        char data[RESIDENT_EVENT_JSON_MAX];
        char from[64];
        uint32_t ts_ms;
        // Envelope fields (APP_EVENT): channel discriminates source
        // (app/runtime for wire-borne frames, driver for host-firmware
        // injections); src/seq are surfaced only when the frame carried them.
        char channel[16];
        char src[16];
        uint32_t seq;
        bool hasSeq;
    };
    static constexpr int SANDBOX_MAX_EVENTS = RESIDENT_EVENT_RING_SIZE;
    Event _events[SANDBOX_MAX_EVENTS];
    int _eventHead = 0;
    int _eventTail = 0;

    // Data-plane event dedup — the same event may arrive via multiple
    // transports (e.g. LAN multicast fast path + broker durable path).
    static constexpr int DEDUP_RING_SIZE = 16;
    char _recentNonces[DEDUP_RING_SIZE][48] = {};
    int _nonceRingPos = 0;
    bool isDuplicateNonce(const char* nonce);

    // Trigger state
    unsigned long _triggerResetTime = 0;
    int _triggerCount = 0;

    // Lua setup
    void setupLuaEnvironment();
    bool compileApp(const char* code);
    // Re-take the registry ref for a lifecycle global (init/on_tick/on_event)
    // iff it currently holds a function — used by loadChunk so a chunk's
    // redefinition reaches the dispatchers, which call cached refs.
    void refreshLifecycleRef(const char* global, int& ref);
    bool callInit();  // true if init ran without error (or no init function)
    void callOnTick(unsigned long dt_ms);
    void processNextEvent();
    // Push the Lua event table for `e` (envelope + data + driver shadow);
    // pushes exactly one value on success, nothing when the payload is
    // unparseable (the event drops).
    bool pushEventTable(const Event& e);
    void pushLocalTimeFields();  // pushes utc_h/utc_m/localtime_h/localtime_m onto the Lua table at stack top
    // Sets ctx.generation_id on the table at stack top — only when the app
    // load carried a server-stamped generationId (Lua sees nil otherwise).
    void pushCtxGenerationId();
    String _nextGenerationId;             // wire-provided id for the NEXT load
    bool _generationIdFromWire = false;   // current _generationId came off the wire
    void pushAppEvent(const char* name, const char* dataJson, const char* from, uint32_t ts_ms,
                      const char* channel = "driver", const char* src = "",
                      bool hasSeq = false, uint32_t seq = 0);
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
