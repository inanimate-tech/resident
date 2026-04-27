idf_component_register(
    SRCS "main.cpp" "StubLEDDriver.cpp"
    INCLUDE_DIRS "."
    REQUIRES resident espressif__arduino-esp32
)
