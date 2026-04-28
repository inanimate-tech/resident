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
  Courier& courier() { return _courier; }
  Sandbox& sandbox() { return _sandbox; }

  // State accessors
  bool isConnected() const;
  String getDeviceId() const { return _deviceId; }
  bool isTimeSynced() const;
  virtual const char* getDeviceType();

  // Sending
  bool send(const char* payload);
  bool sendTo(const char* transportName, const char* payload);
  bool sendBinaryTo(const char* transportName, const uint8_t* data, size_t len);

  // Message handling — override in subclasses, call super to keep sandbox routing
  virtual void onMessage(const char* type, JsonDocument& doc);

  // Connection lifecycle — override in subclasses, call super to keep base behavior
  virtual void onConnectionChange(CourierState state);
  virtual void onTransportsWillConnect();
  virtual void onConnected();

protected:
  virtual void deviceSetup() {}
  virtual void deviceLoop() {}
  virtual String buildWebSocketPath();

  // Status helpers for subclasses
  StatusLED* statusLED() const { return _config.statusLED; }
  StatusDisplay* statusDisplay() const { return _config.statusDisplay; }

  // Constants — accessible to subclasses
  static constexpr uint16_t DEFAULT_PORT = 443;

private:
  DeviceConfig _config;
  Courier _courier;

protected:
  // Courier accessor for subclasses — declared after _courier
  CourierWSTransport& _ws;

  // Device identity — declared after _courier so init order matches constructor
  String _deviceId;

private:
  Sandbox _sandbox;

  // Courier lifecycle hooks
  void wireHooks();

  // Status display
  void showStatusText(const char* text);
  String _lastStatusText;

  // Persisted path string — CourierEndpoint stores const char* (no copy)
  String _wsPath;

  // Singleton for static callbacks
  static Device* _instance;
};

} // namespace Resident

#endif // RESIDENT_DEVICE_H
