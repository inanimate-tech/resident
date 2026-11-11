// A SystemButton for native tests: a queue of edges the test fills and Sandbox
// drains through takeEdge(). press()/release() with no argument stamp the edge
// at the fake clock's current time; with one, at the time given, so a test can
// queue edges that happened while the loop was away.
#pragma once
#include <deque>
#include "Arduino.h"
#include "ResidentSystemButton.h"

class FakeSystemButton : public Resident::SystemButton {
public:
  std::deque<Resident::ButtonEdge> edges;

  const char* name() const override { return "fake-button"; }

  void press(uint32_t ms) { edges.push_back({true, ms}); }
  void release(uint32_t ms) { edges.push_back({false, ms}); }
  void press() { press(static_cast<uint32_t>(millis())); }
  void release() { release(static_cast<uint32_t>(millis())); }

  bool takeEdge(Resident::ButtonEdge& edge) override {
    if (edges.empty()) return false;
    edge = edges.front();
    edges.pop_front();
    return true;
  }
};
