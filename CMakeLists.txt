idf_component_register(
    SRCS "src/OutrunSandbox.cpp"
    INCLUDE_DIRS "src"
    REQUIRES espressif__arduino-esp32 Esp32Lua
)
