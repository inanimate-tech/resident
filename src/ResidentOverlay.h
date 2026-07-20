// src/ResidentOverlay.h
#ifndef RESIDENT_OVERLAY_H
#define RESIDENT_OVERLAY_H

namespace Resident {

// A transient takeover of a display. The arbiter draws the highest-priority
// requested overlay; whether it suspends the app is derived from the surface
// it is bound to (see Sandbox::addOverlay / appDrawsTo), not declared here.
class Overlay {
public:
  virtual int  priority() const = 0;   // higher wins
  virtual void onActivate()   {}       // became the winner
  virtual void onDraw()       {}       // each loop while winning
  virtual void onDeactivate() {}       // stopped winning
  virtual void restore()      {}       // repaint the app's last frame
  virtual ~Overlay() = default;
};

} // namespace Resident

#endif // RESIDENT_OVERLAY_H
