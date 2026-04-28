// src/ResidentDevice.cpp
#include "ResidentDevice.h"

namespace Resident {

Device* Device::_instance = nullptr;

static Courier::Config buildCourierConfig(const DeviceConfig& config) {
  Courier::Config cfg;
  cfg.host = config.host ? config.host : "localhost";
  cfg.port = 443;
  cfg.path = "/";                  // overridden in onTransportsWillConnect()
  cfg.apName = nullptr;
  cfg.defaultTransport = "ws";     // built-in WS is the default for Client::send
  return cfg;
}

Device::Device(const DeviceConfig& config)
    : _config(config),
      _courier(buildCourierConfig(config)),
      _ws(_courier.transport<Courier::WebSocketTransport>("ws")),
      _deviceId(::getDeviceId())
{
  _instance = this;
  wireHooks();
}

Device::~Device() {}

void Device::wireHooks()
{
  // Courier delivers to Device virtuals — subclasses override and call super
  _courier.onMessage([this](const char* transportName, const char* type, JsonDocument& doc) {
    onMessage(transportName, type, doc);
  });
  _courier.onConnectionChange([this](Courier::State state) {
    onConnectionChange(state);
  });
  _courier.onTransportsWillConnect([this]() {
    onTransportsWillConnect();
  });
  _courier.onConnected([this]() {
    onConnected();
  });
}

void Device::onConnectionChange(Courier::State state)
{
  using S = Courier::State;
  switch (state) {
    case S::WifiConnecting:
      showStatusText("WiFi...");
      break;
    case S::WifiConfiguring:
      showStatusText("Configure WiFi");
      break;
    case S::WifiConnected:
      showStatusText("WiFi connected");
      break;
    case S::TransportsConnecting:
      showStatusText("Connecting...");
      break;
    case S::Connected:
      showStatusText("Connected");
      break;
    case S::Reconnecting:
      showStatusText("Reconnecting...");
      break;
    case S::ConnectionFailed:
      showStatusText("Connection failed");
      break;
    default:
      break;
  }

  if (_config.statusLED) {
    switch (state) {
      case S::WifiConnecting:
      case S::WifiConfiguring:
        _config.statusLED->solidColor(0xFFFF00); // yellow
        break;
      case S::WifiConnected:
      case S::TransportsConnecting:
        _config.statusLED->solidColor(0x00FFFF); // cyan
        break;
      case S::Connected:
        _config.statusLED->solidColor(0x00FF00); // green
        break;
      case S::Reconnecting:
        _config.statusLED->solidColor(0xFF8800); // orange
        break;
      case S::ConnectionFailed:
        _config.statusLED->solidColor(0xFF0000); // red
        break;
      default:
        break;
    }
  }
}

void Device::onTransportsWillConnect()
{
  // Default: built-in WS gets /agents/<deviceType>-agent/<deviceId>. Re-set
  // every cycle so reconnects pick up identity changes (deviceType, deviceId).
  // Subclasses override to register additional transports' endpoints, or to
  // perform pre-connect HTTP work (e.g. registration that yields a roomId).
  String wsPath = String("/agents/") + getDeviceType() + "-agent/" + _deviceId;
  _ws.setEndpoint(_config.host ? _config.host : "localhost",
                  DEFAULT_PORT,
                  wsPath.c_str());
  Serial.printf("[resident] WS path: %s\n", wsPath.c_str());
}

void Device::onConnected()
{
  // Base: no-op. Subclasses override for post-connection work.
}

// Base implementation: routes sandbox messages (app, shader, app_event).
// Subclasses override and call Device::onMessage() to keep sandbox routing.
void Device::onMessage(const char* /*transportName*/, const char* type, JsonDocument& doc)
{
  if (strcmp(type, "app") == 0) {
    const char* code = doc["code"];
    if (code) _sandbox.loadApp(code);
  } else if (strcmp(type, "shader") == 0) {
    ShaderFields fields;
    for (JsonPair kv : doc.as<JsonObject>()) {
      if (strcmp(kv.key().c_str(), "type") == 0) continue;
      if (kv.value().is<const char*>()) {
        fields[String(kv.key().c_str())] = String(kv.value().as<const char*>());
      }
    }
    _sandbox.loadShader(fields);
  } else if (strcmp(type, "app_event") == 0) {
    const char* name = doc["name"];
    char dataJson[256];
    if (doc["data"].is<JsonObject>()) {
      serializeJson(doc["data"], dataJson, sizeof(dataJson));
    } else {
      strcpy(dataJson, "{}");
    }
    if (name) _sandbox.sendAppEvent(name, dataJson);
  }
}

void Device::setup()
{
  _deviceId = ::getDeviceId();

  // Build AP name for WiFi config portal
  String apName = String(getDeviceType());
  apName[0] = toupper(apName[0]);
  String idSuffix = _deviceId.substring(0, 4);
  _courier.setAPName((String("Resident ") + apName + " " + idSuffix).c_str());

  Serial.printf("[resident] Device: %s (%s)\n", getDeviceType(), _deviceId.c_str());

  // Forward extension/shader config from DeviceConfig into the internal sandbox.
  Resident::SandboxConfig sandboxCfg;
  sandboxCfg.extensions     = _config.extensions;
  sandboxCfg.shaderTemplate = _config.shaderTemplate;
  _sandbox.configure(sandboxCfg);

  // Status display gets its own lifecycle from Device.
  if (_config.statusDisplay) _config.statusDisplay->begin();

  // Subclass hook — last chance to set telemetry/timezone or add custom setup
  // before the sandbox initializes its Lua state and walks extensions.
  deviceSetup();

  // Initialize the sandbox now that all configuration is in place. This is
  // what was previously done manually inside subclass deviceSetup(); after
  // the driver-DX rework, Device handles it.
  _sandbox.initialize();

  _courier.setup();
}

void Device::loop()
{
  _courier.loop();

  if (_config.statusDisplay) _config.statusDisplay->update();

  if (isConnected()) {
    _sandbox.loop();
    deviceLoop();
  }
}

const char* Device::getDeviceType()
{
  return _config.deviceType ? _config.deviceType : "device";
}

bool Device::isConnected() const
{
  return _courier.getState() == Courier::State::Connected;
}

bool Device::isTimeSynced() const
{
  return _courier.isTimeSynced();
}

void Device::showStatusText(const char* text)
{
  if (!_config.statusDisplay) return;
  if (_lastStatusText == text) return;
  _lastStatusText = text;
  _config.statusDisplay->displayText(text);
}

} // namespace Resident
