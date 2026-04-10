idf_component_register(
    SRCS "src/OutrunSandbox.cpp" "src/OutrunDevice.cpp"
    INCLUDE_DIRS "src"
    REQUIRES espressif__arduino-esp32 courier ArduinoJson Esp32Lua
)
