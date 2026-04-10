// src/OutrunMessageHandler.h
#ifndef OUTRUN_MESSAGE_HANDLER_H
#define OUTRUN_MESSAGE_HANDLER_H

#include <ArduinoJson.h>

namespace Outrun {

class MessageHandler {
public:
  virtual ~MessageHandler() = default;
  virtual bool handleMessage(const char* type, JsonDocument& doc) = 0;
};

} // namespace Outrun

#endif // OUTRUN_MESSAGE_HANDLER_H
