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

// Same march, but for water instead of solid ground — used to aim a boat
// placement at the water's surface. water is not solid (isSolid(BLOCK_WATER)
// is false), so the ordinary raycastVoxel above never stops on it; this one
// stops on the first water cell instead, and gives up (false) if it hits a
// solid wall first, so you cannot place a boat through a wall into water on
// the far side of it.
bool raycastWater(World& world, const Vec3& origin, const Vec3& direction, double maxDistance, RaycastHit& out);
