#include "physics.h"

bool boxCollides(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  // The world border acts as a solid wall (nothing can move past it).
  if (!inWorldBorder(minX, minZ) || !inWorldBorder(maxX, maxZ)) return true;

  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        // An unloaded chunk has no real geometry yet — World::getBlock
        // silently reports it as air — so treating it as passable let
        // anything using gravity (animals, dropped items) wander to the
        // edge of the loaded area and fall straight through into an
        // undefined void below, sinking forever and vanishing for good with
        // no death sequence (no blood, no corpse). Same "acts as a wall"
        // treatment the world border above already gets.
        if (!world.isChunkLoadedAt(bx, bz)) return true;
        uint8_t id = world.getBlock(bx, by, bz);
        // Stairs, slabs, fences, doors, trapdoors and campfires are all
        // solid but do not fill their cell: each collides as its own
        // sub-cell box, checked separately (boxCollidesStairs /
        // boxCollidesSubCell).
        if (isStairs(id) || isSlab(id) || isAnyFence(id) || isDoor(id) || isTrapdoor(id) ||
            isCampfire(id)) {
          continue;
        }
        if (isSolid(id)) return true;
      }
    }
  }
  return false;
}

bool boxCollidesStairs(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        if (!isStairs(world.getBlock(bx, by, bz))) continue;
        // Same three stacked treads the mesher draws (addStairs), without
        // its z-fighting inset: each a third of a block tall, each covering
        // a third less of the rise axis than the one below it.
        int facing = world.stairFacing(bx, by, bz);
        const double H = 1.0 / 3.0;
        for (int t = 0; t < 3; t++) {
          double cut = t * H;
          double x0 = bx, x1 = bx + 1, z0 = bz, z1 = bz + 1;
          switch (facing) {
            case 0: z1 = bz + 1 - cut; break;
            case 1: z0 = bz + cut; break;
            case 2: x1 = bx + 1 - cut; break;
            case 3: x0 = bx + cut; break;
          }
          if (x + halfWidth > x0 && x - halfWidth < x1 &&
              z + halfWidth > z0 && z - halfWidth < z1 &&
              y < by + (t + 1) * H && y + height > by + t * H) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool touchingLadder(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        if (isPanel(world.getBlock(bx, by, bz))) return true;
      }
    }
  }
  return false;
}

bool boxCollidesLadder(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  const double T = 0.14; // matches the rail thickness the mesher draws
  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        if (!isPanel(world.getBlock(bx, by, bz))) continue;
        int facing = world.panelFacing(bx, by, bz);
        double lx0 = bx, lx1 = bx + 1, lz0 = bz, lz1 = bz + 1;
        switch (facing) {
          case 0: lz1 = bz + T; break;
          case 1: lz0 = bz + 1 - T; break;
          case 2: lx1 = bx + T; break;
          case 3: lx0 = bx + 1 - T; break;
          default: lz0 = bz + 0.5 - T / 2; lz1 = bz + 0.5 + T / 2; break;
        }
        if (x + halfWidth > lx0 && x - halfWidth < lx1 &&
            z + halfWidth > lz0 && z - halfWidth < lz1) {
          return true;
        }
      }
    }
  }
  return false;
}

bool touchingWater(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        if (isWater(world.getBlock(bx, by, bz))) return true;
      }
    }
  }
  return false;
}

bool boxCollidesSlab(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        if (!isSlab(world.getBlock(bx, by, bz))) continue;
        double top = by + 0.5; // matches the mesher's slab box
        if (x + halfWidth > bx && x - halfWidth < bx + 1 &&
            z + halfWidth > bz && z - halfWidth < bz + 1 &&
            y < top && y + height > by) {
          return true;
        }
      }
    }
  }
  return false;
}

bool boxCollidesCampfire(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        if (!isCampfire(world.getBlock(bx, by, bz))) continue;
        double top = by + CAMPFIRE_HEIGHT; // matches the mesher's campfire box
        if (x + halfWidth > bx && x - halfWidth < bx + 1 &&
            z + halfWidth > bz && z - halfWidth < bz + 1 &&
            y < top && y + height > by) {
          return true;
        }
      }
    }
  }
  return false;
}

bool boxCollidesFencePanel(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  const double THICK = FENCE_PANEL_THICK;
  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        uint8_t id = world.getBlock(bx, by, bz);
        if (!isAnyFence(id)) continue;
        // Every cell of the panel independently blocks its own thin slice —
        // no anchor lookup needed, unlike the mesher (which only draws from
        // one cell): full width/height, THICK deep centred on the facing
        // axis. Facing defaults to 0 for a cell with no furniture entry (a
        // graceful fallback for a save made before this feature existed).
        auto it = world.furniture.find({ bx, by, bz });
        int facing = it != world.furniture.end() ? it->second.facing : 0;
        double x0 = bx, x1 = bx + 1, z0 = bz, z1 = bz + 1;
        if (facing == 0 || facing == 1) {
          z0 = bz + 0.5 - THICK / 2;
          z1 = bz + 0.5 + THICK / 2;
        } else {
          x0 = bx + 0.5 - THICK / 2;
          x1 = bx + 0.5 + THICK / 2;
        }
        if (x + halfWidth > x0 && x - halfWidth < x1 &&
            z + halfWidth > z0 && z - halfWidth < z1 &&
            y < by + 1 && y + height > by) {
          return true;
        }
      }
    }
  }
  return false;
}

bool boxCollidesTrapdoor(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        if (!isTrapdoor(world.getBlock(bx, by, bz))) continue;
        // A sprung trapdoor (see main.cpp's per-frame check) has no
        // collision at all — that's what lets you fall through. Gated on
        // the `open` target directly, not the eased swingAngle: the visual
        // swing lags behind for looks, same simplification a door's own
        // collision already makes.
        auto it = world.trapdoors.find({ bx, by, bz });
        if (it != world.trapdoors.end() && it->second.open) continue;
        double top = by + 0.1875; // matches the closed panel's thickness (trapdoor.cpp's T)
        if (x + halfWidth > bx && x - halfWidth < bx + 1 &&
            z + halfWidth > bz && z - halfWidth < bz + 1 &&
            y < top && y + height > by) {
          return true;
        }
      }
    }
  }
  return false;
}

bool boxCollidesDoor(World& world, double x, double y, double z, double halfWidth, double height) {
  int minX = (int)std::floor(x - halfWidth);
  int maxX = (int)std::floor(x + halfWidth);
  int minY = (int)std::floor(y);
  int maxY = (int)std::floor(y + height - 1e-4);
  int minZ = (int)std::floor(z - halfWidth);
  int maxZ = (int)std::floor(z + halfWidth);

  const double T = 0.1875; // matches door.cpp's drawDoorPanel panel thickness
  for (int by = minY; by <= maxY; by++) {
    for (int bz = minZ; bz <= maxZ; bz++) {
      for (int bx = minX; bx <= maxX; bx++) {
        if (!isDoor(world.getBlock(bx, by, bz))) continue;
        // A door's state is keyed by its BOTTOM cell (door.h); this cell
        // might be the top half, so check one below too.
        auto it = world.doors.find({ bx, by, bz });
        if (it == world.doors.end()) it = world.doors.find({ bx, by - 1, bz });
        if (it != world.doors.end() && it->second.open) continue; // swung clear
        int facing = it != world.doors.end() ? it->second.facing : world.panelFacing(bx, by, bz);
        double lx0 = bx, lx1 = bx + 1, lz0 = bz, lz1 = bz + 1;
        switch (facing) {
          case 0: lz1 = bz + T; break;
          case 1: lz0 = bz + 1 - T; break;
          case 2: lx1 = bx + T; break;
          case 3: lx0 = bx + 1 - T; break;
          default: lz0 = bz + 0.5 - T / 2; lz1 = bz + 0.5 + T / 2; break;
        }
        if (x + halfWidth > lx0 && x - halfWidth < lx1 &&
            z + halfWidth > lz0 && z - halfWidth < lz1) {
          return true;
        }
      }
    }
  }
  return false;
}

bool boxCollidesSubCell(World& world, double x, double y, double z, double halfWidth, double height) {
  return boxCollidesSlab(world, x, y, z, halfWidth, height) ||
         boxCollidesFencePanel(world, x, y, z, halfWidth, height) ||
         boxCollidesDoor(world, x, y, z, halfWidth, height) ||
         boxCollidesTrapdoor(world, x, y, z, halfWidth, height) ||
         boxCollidesCampfire(world, x, y, z, halfWidth, height);
}
