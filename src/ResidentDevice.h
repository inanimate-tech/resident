// src/ResidentDevice.h
#ifndef RESIDENT_DEVICE_H
#define RESIDENT_DEVICE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Courier.h>
#include "ResidentSandbox.h"
#include "ResidentDeviceConfig.h"
#include "chipstring.h"

namespace Resident {

class Device {
public:
  explicit Device(const DeviceConfig& config);
  virtual ~Device();

  void setup();
  virtual void loop();

  // Direct access to composed objects
  Courier::Client& courier() { return _courier; }
  Sandbox& sandbox() { return _sandbox; }

  // State accessors
  bool isConnected() const;
  String getDeviceId() const { return _deviceId; }
  bool isTimeSynced() const;
  virtual const char* getDeviceType();

  // Message handling — override in subclasses, call super to keep sandbox routing.
  // transportName identifies which transport delivered the message ("ws", "mqtt", ...).
  virtual void onMessage(const char* transportName, const char* type, JsonDocument& doc);

  // Connection lifecycle — override in subclasses, call super to keep base behavior
  virtual void onConnectionChange(Courier::State state);

  // Fired after WiFi is up, before transports begin connecting. The canonical
  // place to (re-)resolve and set per-transport endpoints, including any
  // dynamic state (e.g. roomId from a registration HTTP call). The default
  // sets the built-in WS endpoint to /agents/<deviceType>-agent/<deviceId>.
  // Subclasses override and either replace or call Device::onTransportsWillConnect().
  virtual void onTransportsWillConnect();

  virtual void onConnected();

protected:
  virtual void deviceSetup() {}
  virtual void deviceLoop() {}

  // Status helpers for subclasses
  StatusLED* statusLED() const { return _config.statusLED; }
  StatusDisplay* statusDisplay() const { return _config.statusDisplay; }

  // Constants — accessible to subclasses
  static constexpr uint16_t DEFAULT_PORT = 443;

private:
  DeviceConfig _config;
  Courier::Client _courier;

protected:
  // Built-in WS transport accessor for subclasses — declared after _courier
  Courier::WebSocketTransport& _ws;

  // Device identity — declared after _courier so init order matches constructor
  String _deviceId;

private:
  Sandbox _sandbox;

  // Courier lifecycle hooks
  void wireHooks();

  // Status display
  void showStatusText(const char* text);
  String _lastStatusText;

  // Singleton for static callbacks
  static Device* _instance;
};

} // namespace Resident

#endif // RESIDENT_DEVICE_H
