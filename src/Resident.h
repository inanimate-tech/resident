// src/Resident.h
#ifndef RESIDENT_H
#define RESIDENT_H

#include "ResidentSandbox.h"
#include "ResidentSandboxConfig.h"
#include "ResidentDriver.h"
#include "ResidentExtension.h"
#include "ResidentExtensions.h"
#include "ResidentLuaModule.h"
#include "ResidentStatusLED.h"
#include "ResidentStatusDisplay.h"
// ResidentDevice.h intentionally NOT in the umbrella — it's deprecated and
// gets deleted once the examples migrate to Resident::Sandbox. Until then,
// examples that still use it include it directly.

#endif // RESIDENT_H
