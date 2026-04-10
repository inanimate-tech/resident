// src/OutrunDevice.cpp
#include "OutrunDevice.h"

namespace Outrun {

Device* Device::_instance = nullptr;

static CourierConfig buildCourierConfig(const DeviceConfig& config) {
  CourierConfig cfg;
  cfg.host = config.host ? config.host : "localhost";
  cfg.port = Device::DEFAULT_PORT;
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
  // Route incoming messages from Courier to sandbox + handlers
  _courier.onMessage([this](const char* type, JsonDocument& doc) {
    // Route sandbox messages (app, shader, app_event)
    if (strcmp(type, "app") == 0 || strcmp(type, "shader") == 0 ||
        strcmp(type, "app_event") == 0) {
      routeSandboxMessage(type, doc);
      return;
    }

    // Try registered message handlers
    for (int i = 0; i < _messageHandlerCount; i++) {
      if (_messageHandlers[i]->handleMessage(type, doc)) return;
    }

    // Finally try device subclass
    onMessage(type, doc);
  });

  // Connection state changes → status display
  _courier.onConnectionChange([this](CourierState state) {
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

    // Status LED
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
  });

  // Set up WS transport endpoint after WiFi connects
  _courier.onTransportsWillConnect([this]() {
    String wsPath = buildWebSocketPath();
    CourierEndpoint wsEp;
    wsEp.path = wsPath;
    _courier.setEndpoint("ws", wsEp);
    Serial.printf("[outrun] WS path: %s\n", wsPath.c_str());
  });
}

void Device::routeSandboxMessage(const char* type, JsonDocument& doc)
{
  if (strcmp(type, "app") == 0) {
    const char* code = doc["code"];
    if (code) _sandbox.loadApp(code);
  } else if (strcmp(type, "shader") == 0) {
    // Extract all fields from the JSON into ShaderFields map
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
    // Serialize the data field back to JSON string for sandbox
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
  apName.toUpperCase();
  String idSuffix = _deviceId.substring(0, 4);
  _courier.setAPName((String("Outrun ") + apName + " " + idSuffix).c_str());

  Serial.printf("[outrun] Device: %s (%s)\n", getDeviceType(), _deviceId.c_str());

  deviceSetup();
  _courier.setup();
}

void Device::loop()
{
  _courier.loop();

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

void Device::addMessageHandler(MessageHandler* handler)
{
  if (_messageHandlerCount < MAX_MESSAGE_HANDLERS) {
    _messageHandlers[_messageHandlerCount++] = handler;
  }
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
