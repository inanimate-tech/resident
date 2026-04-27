idf_component_register(
    SRCS "src/ResidentSandbox.cpp" "src/ResidentDevice.cpp"
    INCLUDE_DIRS "src"
    REQUIRES espressif__arduino-esp32 inanimate__courier bblanchon__arduinojson Esp32Lua
)
