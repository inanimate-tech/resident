// vision_frame.h — pure data model for Grove Vision AI V2 results.
//
// Header-only and Arduino-free so it runs under native unit tests. The
// driver copies SSCMA's result vectors into these structs once per invoke;
// everything downstream (events, Lua module, logging) reads this frame.
//
// Units: coordinates are integer pixels in the model's input frame
// (typically 192×192) exactly as SSCMA reports them; scores are 0–100.
#pragma once
#include <cstdint>
#include <vector>

namespace vision {

struct Box {
  uint16_t x = 0, y = 0, w = 0, h = 0;
  uint8_t score = 0, target = 0;
};

struct Point {
  uint16_t x = 0, y = 0, z = 0;
  uint8_t score = 0, target = 0;
};

struct Classification {
  uint8_t target = 0, score = 0;
};

struct Person {
  Box box;
  std::vector<Point> points;  // COCO order; pose models emit 17
};

enum class Kind { None, Pose, Boxes, Points, Classes };

// One model emits one result type per invoke; the precedence only matters
// defensively (richer kinds win if a model ever emits several).
inline Kind classify(size_t nPeople, size_t nBoxes, size_t nPoints, size_t nClasses) {
  if (nPeople > 0)  return Kind::Pose;
  if (nBoxes > 0)   return Kind::Boxes;
  if (nPoints > 0)  return Kind::Points;
  if (nClasses > 0) return Kind::Classes;
  return Kind::None;
}

inline const char* kindName(Kind k) {
  switch (k) {
    case Kind::Pose:    return "pose";
    case Kind::Boxes:   return "boxes";
    case Kind::Points:  return "points";
    case Kind::Classes: return "classes";
    default:            return "none";
  }
}

struct Frame {
  Kind kind = Kind::None;
  std::vector<Box> boxes;
  std::vector<Point> points;
  std::vector<Classification> classes;
  std::vector<Person> people;

  int count() const {
    switch (kind) {
      case Kind::Pose:    return (int)people.size();
      case Kind::Boxes:   return (int)boxes.size();
      case Kind::Points:  return (int)points.size();
      case Kind::Classes: return (int)classes.size();
      default:            return 0;
    }
  }
};

// Index of the highest-score detection for the frame's kind; -1 if none.
// Pose people rank by their box score.
inline int bestIndex(const Frame& f) {
  int best = -1;
  int bestScore = -1;
  int n = f.count();
  for (int i = 0; i < n; i++) {
    int s = -1;
    switch (f.kind) {
      case Kind::Pose:    s = f.people[i].box.score; break;
      case Kind::Boxes:   s = f.boxes[i].score; break;
      case Kind::Points:  s = f.points[i].score; break;
      case Kind::Classes: s = f.classes[i].score; break;
      default: break;
    }
    if (s > bestScore) { bestScore = s; best = i; }
  }
  return best;
}

}  // namespace vision
