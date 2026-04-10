// src/OutrunDevice.h
#ifndef OUTRUN_DEVICE_H
#define OUTRUN_DEVICE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Courier.h>
#include "OutrunSandbox.h"
#include "OutrunDeviceConfig.h"
#include "OutrunMessageHandler.h"
#include "chipstring.h"

namespace Outrun {

class Device {
public:
  explicit Device(const DeviceConfig& config);
  virtual ~Device();

  void setup();
  void loop();

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

  // Message handling (for non-sandbox messages)
  void addMessageHandler(MessageHandler* handler);
  virtual bool onMessage(const char* type, JsonDocument& doc) { return false; }

protected:
  virtual void deviceSetup() {}
  virtual void deviceLoop() {}
  virtual String buildWebSocketPath();

  // Status helpers for subclasses
  StatusLED* statusLED() const { return _config.statusLED; }
  StatusDisplay* statusDisplay() const { return _config.statusDisplay; }

  // Device identity
  String _deviceId;

  // Courier accessor for subclasses
  CourierWSTransport& _ws;

  // Constants — accessible to subclasses
  static constexpr uint16_t DEFAULT_PORT = 443;

private:
  DeviceConfig _config;
  Courier _courier;
  Sandbox _sandbox;

  // Message handlers
  static constexpr int MAX_MESSAGE_HANDLERS = 4;
  MessageHandler* _messageHandlers[MAX_MESSAGE_HANDLERS] = {};
  int _messageHandlerCount = 0;

  // Internal message routing
  void routeSandboxMessage(const char* type, JsonDocument& doc);

  // Courier lifecycle hooks
  void wireHooks();

  // Status display
  void showStatusText(const char* text);
  String _lastStatusText;

  // Singleton for static callbacks
  static Device* _instance;
};

} // namespace Outrun

#endif // OUTRUN_DEVICE_H
