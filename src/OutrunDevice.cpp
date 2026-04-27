// src/OutrunDevice.cpp
#include "OutrunDevice.h"

namespace Outrun {

Device* Device::_instance = nullptr;

static CourierConfig buildCourierConfig(const DeviceConfig& config) {
  CourierConfig cfg;
  cfg.host = config.host ? config.host : "localhost";
  cfg.port = 443;
  cfg.path = "/";
  cfg.apName = nullptr;
  return cfg;
}

Device::Device(const DeviceConfig& config)
    : _config(config),
      _courier(buildCourierConfig(config)),
      _ws(_courier.builtinWS()),
      _deviceId(::getDeviceId())
{
  _instance = this;
  wireHooks();
}

Device::~Device() {}

void Device::wireHooks()
{
  // Courier delivers to Device virtuals — subclasses override and call super
  _courier.onMessage([this](const char* type, JsonDocument& doc) {
    onMessage(type, doc);
  });
  _courier.onConnectionChange([this](CourierState state) {
    onConnectionChange(state);
  });
  _courier.onTransportsWillConnect([this]() {
    onTransportsWillConnect();
  });
  _courier.onConnected([this]() {
    onConnected();
  });
}

void Device::onConnectionChange(CourierState state)
{
  switch (state) {
    case COURIER_WIFI_CONNECTING:
      showStatusText("WiFi...");
      break;
    case COURIER_WIFI_CONFIGURING:
      showStatusText("Configure WiFi");
      break;
    case COURIER_WIFI_CONNECTED:
      showStatusText("WiFi connected");
      break;
    case COURIER_TRANSPORTS_CONNECTING:
      showStatusText("Connecting...");
      break;
    case COURIER_CONNECTED:
      showStatusText("Connected");
      break;
    case COURIER_RECONNECTING:
      showStatusText("Reconnecting...");
      break;
    case COURIER_CONNECTION_FAILED:
      showStatusText("Connection failed");
      break;
    default:
      break;
  }

  if (_config.statusLED) {
    switch (state) {
      case COURIER_WIFI_CONNECTING:
      case COURIER_WIFI_CONFIGURING:
        _config.statusLED->solidColor(0xFFFF00); // yellow
        break;
      case COURIER_WIFI_CONNECTED:
      case COURIER_TRANSPORTS_CONNECTING:
        _config.statusLED->solidColor(0x00FFFF); // cyan
        break;
      case COURIER_CONNECTED:
        _config.statusLED->solidColor(0x00FF00); // green
        break;
      case COURIER_RECONNECTING:
        _config.statusLED->solidColor(0xFF8800); // orange
        break;
      case COURIER_CONNECTION_FAILED:
        _config.statusLED->solidColor(0xFF0000); // red
        break;
      default:
        break;
    }
  }
}

void Device::onTransportsWillConnect()
{
  _wsPath = buildWebSocketPath();
  CourierEndpoint wsEp;
  wsEp.path = _wsPath.c_str();
  _courier.setEndpoint("ws", wsEp);
  Serial.printf("[outrun] WS path: %s\n", _wsPath.c_str());
}

void Device::onConnected()
{
  // Base: no-op. Subclasses override for post-connection work.
}

// Base implementation: routes sandbox messages (app, shader, app_event).
// Subclasses override and call Device::onMessage() to keep sandbox routing.
void Device::onMessage(const char* type, JsonDocument& doc)
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
  _courier.setAPName((String("Outrun ") + apName + " " + idSuffix).c_str());

  Serial.printf("[outrun] Device: %s (%s)\n", getDeviceType(), _deviceId.c_str());

  // Forward extension/shader config from DeviceConfig into the internal sandbox.
  Outrun::SandboxConfig sandboxCfg;
  sandboxCfg.extensions     = _config.extensions;
  sandboxCfg.shaderTemplate = _config.shaderTemplate;
  _sandbox.configure(sandboxCfg);

  // Status display gets its own lifecycle from Device.
  if (_config.statusDisplay) _config.statusDisplay->begin();

  deviceSetup();
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
  return _courier.getState() == COURIER_CONNECTED;
}

bool Device::isTimeSynced() const
{
  return _courier.isTimeSynced();
}

bool Device::send(const char* payload)
{
  return _courier.send(payload);
}

bool Device::sendTo(const char* transportName, const char* payload)
{
  return _courier.sendTo(transportName, payload);
}

bool Device::sendBinaryTo(const char* transportName, const uint8_t* data, size_t len)
{
  return _courier.sendBinaryTo(transportName, data, len);
}

String Device::buildWebSocketPath()
{
  return String("/agents/") + getDeviceType() + "-agent/" + _deviceId;
}

void Device::showStatusText(const char* text)
{
  if (!_config.statusDisplay) return;
  if (_lastStatusText == text) return;
  _lastStatusText = text;
  _config.statusDisplay->displayText(text);
}

} // namespace Outrun
