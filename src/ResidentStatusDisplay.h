// src/ResidentStatusDisplay.h — deprecated forwarder. Use ResidentSystemDisplay.h.
#ifndef RESIDENT_STATUS_DISPLAY_H
#define RESIDENT_STATUS_DISPLAY_H

#include "ResidentSystemDisplay.h"

namespace Resident {
// Deprecated: renamed to SystemDisplay. Kept as a plain alias so existing
// driver subclasses compile unchanged.
using StatusDisplay = SystemDisplay;
}

#endif // RESIDENT_STATUS_DISPLAY_H
