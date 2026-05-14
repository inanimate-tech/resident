# Vendoring consumers (e.g. projects that vendor courier/ArduinoJson/Esp32Lua
# under bare directory names rather than via the ESP Component Registry) can
# override these to match their component graph by setting the cache vars in
# their project's root CMakeLists.txt:
#
#   set(RESIDENT_COURIER_DEP     "courier"     CACHE STRING "" FORCE)
#   set(RESIDENT_ARDUINOJSON_DEP "ArduinoJson" CACHE STRING "" FORCE)
#   set(RESIDENT_EZTIME_DEP      "ezTime"      CACHE STRING "" FORCE)
#   set(RESIDENT_ESP32LUA_DEP    "Esp32Lua"    CACHE STRING "" FORCE)  # already bare
#
# Defaults for courier/ArduinoJson match the ESP Component Registry namespacing
# convention so registry consumers work out of the box. Esp32Lua defaults bare
# (it's not on the registry; the example fetches it via tools/fetch-deps.sh).
#
# RESIDENT_EZTIME_DEP defaults EMPTY: on the registry path,
# inanimate__courier already bundles ezTime via CMake FetchContent, so there
# is no separate ezTime component to require. Vendoring consumers who do
# have a separate ezTime component (e.g. via lib_deps in PIO-style or git-
# submodule vendoring) should set the var to "ezTime" so ResidentSandbox.h's
# include of <ezTime.h> passes ESP-IDF's strict header-required-component
# check.
#
# arduino-esp32 is NOT cache-var-overridable: vendoring consumers don't
# typically vendor it themselves; it comes from the registry on both paths.
set(RESIDENT_COURIER_DEP     "inanimate__courier"     CACHE STRING "REQUIRES name for courier")
set(RESIDENT_ARDUINOJSON_DEP "bblanchon__arduinojson" CACHE STRING "REQUIRES name for ArduinoJson")
set(RESIDENT_EZTIME_DEP      ""                       CACHE STRING "REQUIRES name for ezTime (empty = none; courier provides ezTime transitively on the registry path)")
set(RESIDENT_ESP32LUA_DEP    "Esp32Lua"               CACHE STRING "REQUIRES name for Esp32Lua")

set(RESIDENT_REQUIRES
    espressif__arduino-esp32
    ${RESIDENT_COURIER_DEP}
    ${RESIDENT_ARDUINOJSON_DEP}
    ${RESIDENT_ESP32LUA_DEP}
)
if(RESIDENT_EZTIME_DEP)
    list(APPEND RESIDENT_REQUIRES ${RESIDENT_EZTIME_DEP})
endif()

idf_component_register(
    SRCS "src/ResidentSandbox.cpp"
    INCLUDE_DIRS "src"
    REQUIRES ${RESIDENT_REQUIRES}
)
