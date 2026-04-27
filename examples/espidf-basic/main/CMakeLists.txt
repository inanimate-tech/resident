idf_component_register(
    SRCS "main.cpp" "StubLEDDriver.cpp"
    INCLUDE_DIRS "."
    REQUIRES outrun espressif__arduino-esp32
)
