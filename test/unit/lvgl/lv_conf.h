// LVGL configuration for the native LVGL suites (env:native-lvgl): the
// library's defaults (lv_conf_internal.h), on the host C library, with no OS
// and the RGB565 colour depth ResidentLvglModule requires.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_USE_NATIVE_HELIUM_ASM 0
// luavgl's property API, as the m5stick-lvgl example configures it.
#define LV_USE_OBJ_ID 1
#define LV_USE_OBJ_ID_BUILTIN 0
#define LV_USE_OBJ_PROPERTY 1

#endif  // LV_CONF_H
