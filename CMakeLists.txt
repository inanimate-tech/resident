# Vendoring consumers (e.g. projects that vendor courier/ArduinoJson/ezTime/
# Esp32Lua under bare directory names rather than via the ESP Component
# Registry) can override these to match their component graph by setting
# the cache vars in their project's root CMakeLists.txt:
#
#   set(RESIDENT_COURIER_DEP     "courier"     CACHE STRING "" FORCE)
#   set(RESIDENT_ARDUINOJSON_DEP "ArduinoJson" CACHE STRING "" FORCE)
#   set(RESIDENT_EZTIME_DEP      "ezTime"      CACHE STRING "" FORCE)  # already bare today
#   set(RESIDENT_ESP32LUA_DEP    "Esp32Lua"    CACHE STRING "" FORCE)  # already bare today
#
# Defaults match the ESP Component Registry namespacing convention so registry
# consumers work out of the box. The two "already bare today" deps are exposed
# as cache vars for symmetry — if either is ever published to the registry,
# registry consumers can override without a Resident change.
#
# arduino-esp32 is NOT cache-var-overridable: vendoring consumers don't
# typically vendor it themselves; it comes from the registry on both paths.
set(RESIDENT_COURIER_DEP     "inanimate__courier"     CACHE STRING "REQUIRES name for courier")
set(RESIDENT_ARDUINOJSON_DEP "bblanchon__arduinojson" CACHE STRING "REQUIRES name for ArduinoJson")
set(RESIDENT_EZTIME_DEP      "ezTime"                 CACHE STRING "REQUIRES name for ezTime")
set(RESIDENT_ESP32LUA_DEP    "Esp32Lua"               CACHE STRING "REQUIRES name for Esp32Lua")

idf_component_register(
    SRCS "src/ResidentSandbox.cpp" "src/ResidentDevice.cpp"
    INCLUDE_DIRS "src"
    REQUIRES espressif__arduino-esp32
             ${RESIDENT_COURIER_DEP}
             ${RESIDENT_ARDUINOJSON_DEP}
             ${RESIDENT_EZTIME_DEP}
             ${RESIDENT_ESP32LUA_DEP}
)
