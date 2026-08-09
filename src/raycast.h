#pragma once
#include "world.h"

struct RaycastHit {
  int pos[3];
  int normal[3];
};

// Voxel DDA ray march (Amanatides & Woo). Returns true and fills `out` with
// the first solid block the ray hits within maxDistance; out.normal points
// back toward the ray origin (i.e. it's the face that was hit, and
// pos+normal is the adjacent empty cell to place a block into).
bool raycastVoxel(World& world, const Vec3& origin, const Vec3& direction, double maxDistance, RaycastHit& out);
