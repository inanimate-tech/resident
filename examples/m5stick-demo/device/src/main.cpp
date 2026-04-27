#include <M5Unified.h>
#include <OutrunDevice.h>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"

// M5StickC Plus2: Button A = GPIO 37, Button B = GPIO 39
static constexpr uint8_t BUTTON_PINS[] = {37, 39};
static constexpr PushButtonsConfig buttonConfig = {.numButtons = 2, .pins = BUTTON_PINS};

DisplayDriver displayDriver;
IMUDriver imuDriver;
BuzzerDriver buzzerDriver{255};
PushButtonsDriver buttonDriver{buttonConfig};

String shaderTemplate(const Outrun::ShaderFields& fields) {
    auto it = fields.find("expr");
    if (it == fields.end()) return "";

    String lua = "function on_tick(ctx, dt_ms)\n";
    lua += "  local time_ms = ctx.time_ms\n";
    lua += "  local c = (" + it->second + ")\n";
    lua += "  local r, g, b\n";
    lua += "  if c < -1 then\n";
    lua += "    c = -c\n";
    lua += "    b = math.floor(c) % 256\n";
    lua += "    g = math.floor(c / 256) % 256\n";
    lua += "    r = math.floor(c / 65536) % 256\n";
    lua += "  elseif c > 0.5 then\n";
    lua += "    r, g, b = 255, 255, 255\n";
    lua += "  else\n";
    lua += "    r, g, b = 0, 0, 0\n";
    lua += "  end\n";
    lua += "  screen.clear(r, g, b)\n";
    lua += "  screen.flip()\n";
    lua += "end\n";
    return lua;
}

static const char* DEFAULT_SHADER =
    "rgb(sin(time_ms/1000)*0.5+0.5, sin(time_ms/1000+2.094)*0.5+0.5, sin(time_ms/1000+4.189)*0.5+0.5)";

Outrun::DeviceConfig makeConfig() {
    Outrun::DeviceConfig cfg;
    cfg.deviceType     = "stick";
    cfg.host           = "outrun-m5stick-demo.genmon.workers.dev";
    cfg.statusDisplay  = &displayDriver;
    cfg.shaderTemplate = shaderTemplate;
    cfg.extensions     = {&displayDriver, &imuDriver, &buzzerDriver, &buttonDriver};
    return cfg;
}

class DemoDevice : public Outrun::Device {
public:
    DemoDevice() : Outrun::Device(makeConfig()) {}

    String buildWebSocketPath() override {
        return "/agents/device-agent/m5stick-demo";
    }

    void deviceLoop() override {
        M5.update();

        static bool loaded = false;
        if (!loaded && isConnected()) {
            loaded = true;
            Outrun::ShaderFields fields;
            fields["expr"] = DEFAULT_SHADER;
            sandbox().loadShader(fields);
        }
    }
};

DemoDevice device;

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for USB CDC on M5StickS3; harmless on M5Stick
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    device.setup();
}

void loop() {
    device.loop();
}
