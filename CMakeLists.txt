idf_component_register(
    SRCS "src/OutrunSandbox.cpp" "src/OutrunDevice.cpp"
    INCLUDE_DIRS "src"
    REQUIRES espressif__arduino-esp32 inanimate__courier bblanchon__arduinojson Esp32Lua
)
