#pragma once
// Shared includes + tiny math helpers used across the whole game.

#include <cstdint>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <limits>

struct Vec3 {
  double x = 0, y = 0, z = 0;
  Vec3() = default;
  Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
};

// Floor division matching JS Math.floor(n / d) for integer block coords.
inline int floorDiv(int n, int d) {
  int q = n / d;
  int r = n % d;
  return (r != 0 && ((r < 0) != (d < 0))) ? q - 1 : q;
}

inline double clampd(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
