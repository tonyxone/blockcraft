#include "raycast.h"

static const double INF = std::numeric_limits<double>::infinity();

static double intbound(double s, double ds) {
  if (ds > 0) return (std::ceil(s) - s) / ds;
  if (ds < 0) return (s - std::floor(s)) / -ds;
  return INF;
}

static int sign(double v) { return (v > 0) - (v < 0); }

// What the march does when it enters a cell holding this block id.
enum RayCell { RAY_PASS, RAY_HIT, RAY_BLOCK };

static RayCell classifySolid(uint8_t id) {
  // Panels are deliberately not solid — you walk through a ladder — but they
  // still have to be aim-able, or one could be placed and never mined back.
  return (isSolid(id) || isPanel(id)) ? RAY_HIT : RAY_PASS;
}

static bool raycastClassify(World& world, const Vec3& origin, const Vec3& direction,
                            double maxDistance, RayCell (*classify)(uint8_t), RaycastHit& out) {
  int x = (int)std::floor(origin.x);
  int y = (int)std::floor(origin.y);
  int z = (int)std::floor(origin.z);

  int stepX = sign(direction.x);
  int stepY = sign(direction.y);
  int stepZ = sign(direction.z);

  double tMaxX = intbound(origin.x, direction.x);
  double tMaxY = intbound(origin.y, direction.y);
  double tMaxZ = intbound(origin.z, direction.z);

  double tDeltaX = direction.x != 0 ? std::abs(1.0 / direction.x) : INF;
  double tDeltaY = direction.y != 0 ? std::abs(1.0 / direction.y) : INF;
  double tDeltaZ = direction.z != 0 ? std::abs(1.0 / direction.z) : INF;

  int normal[3] = { 0, 0, 0 };
  double t = 0;

  while (t <= maxDistance) {
    RayCell cell = classify(world.getBlock(x, y, z));
    if (cell == RAY_HIT) {
      out.pos[0] = x; out.pos[1] = y; out.pos[2] = z;
      out.normal[0] = normal[0]; out.normal[1] = normal[1]; out.normal[2] = normal[2];
      return true;
    }
    if (cell == RAY_BLOCK) return false;
    if (tMaxX < tMaxY) {
      if (tMaxX < tMaxZ) {
        x += stepX;
        t = tMaxX;
        tMaxX += tDeltaX;
        normal[0] = -stepX; normal[1] = 0; normal[2] = 0;
      } else {
        z += stepZ;
        t = tMaxZ;
        tMaxZ += tDeltaZ;
        normal[0] = 0; normal[1] = 0; normal[2] = -stepZ;
      }
    } else if (tMaxY < tMaxZ) {
      y += stepY;
      t = tMaxY;
      tMaxY += tDeltaY;
      normal[0] = 0; normal[1] = -stepY; normal[2] = 0;
    } else {
      z += stepZ;
      t = tMaxZ;
      tMaxZ += tDeltaZ;
      normal[0] = 0; normal[1] = 0; normal[2] = -stepZ;
    }
  }
  return false;
}

bool raycastVoxel(World& world, const Vec3& origin, const Vec3& direction, double maxDistance, RaycastHit& out) {
  return raycastClassify(world, origin, direction, maxDistance, classifySolid, out);
}

static RayCell classifyWater(uint8_t id) {
  if (isWater(id)) return RAY_HIT;
  // A solid wall (or a panel, same as classifySolid) stops the march rather
  // than passing through — you cannot aim a boat placement through a wall
  // at water on the far side of it.
  return (isSolid(id) || isPanel(id)) ? RAY_BLOCK : RAY_PASS;
}

bool raycastWater(World& world, const Vec3& origin, const Vec3& direction, double maxDistance, RaycastHit& out) {
  return raycastClassify(world, origin, direction, maxDistance, classifyWater, out);
}
