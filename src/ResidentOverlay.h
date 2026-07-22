// src/ResidentOverlay.h
#ifndef RESIDENT_OVERLAY_H
#define RESIDENT_OVERLAY_H

namespace Resident {

// An overlay is a transient CLAIM on a display surface — a voice-recording
// ring, an agent-status line — registered with Sandbox::addOverlay(overlay,
// surface, priority) and toggled with Sandbox::requestOverlay.
//
// The arbiter resolves claims PER SURFACE: overlays bound to the same
// surface contend by priority (highest wins; ties go to the earlier
// registration), while overlays on different surfaces are independent. A
// null surface means a dedicated surface — the overlay contends with
// nothing, never suspends the app, and no restore is issued for it.
//
// While a claim is held on a surface the app also draws to (dual-role:
// present in both extensions[] and a system* role slot), the app is
// suspended; it resumes when the claim ends. When a surface's last claim
// releases, the arbiter calls SystemDisplay::restoreContent() on the
// surface — overlays never repaint what's underneath themselves.
class Overlay {
public:
  // You now own the surface. Paint the first frame here for immediate
  // response; subsequent frames arrive via onDraw.
  virtual void onAcquire()   {}
  // Animation heartbeat while owning, paced on the sandbox app-tick cadence
  // (the overlay effectively takes over the suspended app's tick slot).
  // Event-driven repaints outside this callback are fine too.
  virtual void onDraw(unsigned long dtMs) { (void)dtMs; }
  // You no longer own the surface (a higher-priority claim took over, or
  // the request was withdrawn). Wind down internal state only — the arbiter
  // handles app resume and surface restore.
  virtual void onRelease()   {}
  virtual ~Overlay() = default;
};

} // namespace Resident

#endif // RESIDENT_OVERLAY_H
