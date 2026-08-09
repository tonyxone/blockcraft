#include "droppeditem.h"
#include "gfx.h"
#include "physics.h"
#include "recipes.h"
#include "textures.h"
#include "win_gl.h"
#include "world.h"

namespace {
const double GRAVITY = 28;
const double TERMINAL_FALL_SPEED = -50;
// A dropped item's collision box is small and short — big enough to catch
// on the ground and walls without behaving like a player-sized object.
const double HALF_WIDTH = 0.15;
const double HEIGHT = 0.2;
} // namespace

void updateDroppedItem(DroppedItem& item, World& world, double dt) {
  if (item.pickupDelay > 0) item.pickupDelay -= dt;

  item.velocity.y = std::max(item.velocity.y - GRAVITY * dt, TERMINAL_FALL_SPEED);

  auto blocked = [&](double x, double y, double z) {
    return boxCollides(world, x, y, z, HALF_WIDTH, HEIGHT) ||
          boxCollidesStairs(world, x, y, z, HALF_WIDTH, HEIGHT) ||
          boxCollidesSubCell(world, x, y, z, HALF_WIDTH, HEIGHT);
  };

  double nx = item.position.x + item.velocity.x * dt;
  if (!blocked(nx, item.position.y, item.position.z)) item.position.x = nx;
  else item.velocity.x = 0;

  double nz = item.position.z + item.velocity.z * dt;
  if (!blocked(item.position.x, item.position.y, nz)) item.position.z = nz;
  else item.velocity.z = 0;

  bool wasFalling = item.velocity.y <= 0;
  double ny = item.position.y + item.velocity.y * dt;
  item.onGround = false;
  if (!blocked(item.position.x, ny, item.position.z)) {
    item.position.y = ny;
  } else {
    item.velocity.y = 0;
    if (wasFalling) item.onGround = true;
  }

  // Friction once grounded: a tossed item settles near where it lands
  // instead of sliding forever.
  if (item.onGround) {
    double drag = std::max(0.0, 1.0 - dt * 8.0);
    item.velocity.x *= drag;
    item.velocity.z *= drag;
  }
}

namespace {

// Same per-face directional shading every hand-rolled box in this game uses
// (animal.cpp, tools.cpp, mesher.cpp) — scaled against each pixel's own
// color here instead of a fixed material color.
struct VoxelFace {
  int dir[3];
  uint8_t shade;
  int corners[4][3];
};
const VoxelFace VOXEL_FACES[6] = {
  { { 0, 1, 0 }, 255, { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } } },
  { { 0, -1, 0 }, 154, { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } } },
  { { 1, 0, 0 }, 226, { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } } },
  { { -1, 0, 0 }, 188, { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } } },
  { { 0, 0, 1 }, 202, { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } } },
  { { 0, 0, -1 }, 177, { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } } },
};

void drawVoxel(double x0, double y0, double z0, double s, double depth,
              uint8_t r, uint8_t g, uint8_t b) {
  for (const VoxelFace& face : VOXEL_FACES) {
    glColor3ub((uint8_t)(r * face.shade / 255), (uint8_t)(g * face.shade / 255),
              (uint8_t)(b * face.shade / 255));
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
      glVertex3d(x0 + face.corners[i][0] * s, y0 + face.corners[i][1] * s,
                z0 + face.corners[i][2] * depth);
    }
    glEnd();
  }
}

std::unordered_map<int, GLuint> g_itemVoxelLists;

// Builds (once) and caches a display list voxelizing the item's own
// inventory icon: one small shaded cube per opaque pixel, sampled at the
// atlas's native 16x16 resolution (every ATLAS_SCALEth pixel — the 32x32
// atlas is a lossless 2x upscale for procedural art, see textures.h) so
// duplicated pixels don't turn into redundant stacked cubes. Thin in Z, like
// Minecraft's own item-entity model — a relief, not a solid block of cubes.
GLuint itemVoxelList(int itemId) {
  auto found = g_itemVoxelLists.find(itemId);
  if (found != g_itemVoxelLists.end()) return found->second;

  GLuint list = glGenLists(1);
  glNewList(list, GL_COMPILE);

  int tile = craftItemTile((uint8_t)itemId);
  if (tile >= 0) {
    const Atlas& atlas = buildTextureAtlas();
    const double SIZE = 0.5;          // overall footprint, world units
    const double CELL = SIZE / TILE_PX;
    const double DEPTH = CELL * 2.0;  // thin relief, not a cube per pixel
    for (int gy = 0; gy < TILE_PX; gy++) {
      for (int gx = 0; gx < TILE_PX; gx++) {
        int ax = tile * ATLAS_TILE_PX + gx * ATLAS_SCALE;
        int ay = gy * ATLAS_SCALE; // atlas row 0 = bottom, so gy=0 -> the object's base
        size_t idx = (size_t)(ay * atlas.width + ax) * 4;
        if (idx + 3 >= atlas.pixels.size() || atlas.pixels[idx + 3] < 128) continue;
        double x0 = (gx - TILE_PX / 2.0) * CELL;
        double y0 = gy * CELL;
        double z0 = -DEPTH / 2.0;
        drawVoxel(x0, y0, z0, CELL, DEPTH, atlas.pixels[idx], atlas.pixels[idx + 1],
                 atlas.pixels[idx + 2]);
      }
    }
  }
  glEndList();
  g_itemVoxelLists[itemId] = list;
  return list;
}

} // namespace

void drawDroppedItems(const std::vector<DroppedItem>& items, double time) {
  if (items.empty()) return;
  glDisable(GL_TEXTURE_2D); // flat-shaded voxel cubes, not textured
  for (const DroppedItem& it : items) {
    GLuint list = itemVoxelList(it.itemId);
    double bob = std::sin(time * 2.5 + it.bobPhase) * 0.05;
    glPushMatrix();
    glTranslated(it.position.x, it.position.y + 0.25 + bob, it.position.z);
    glCallList(list);
    glPopMatrix();
  }
  glEnable(GL_TEXTURE_2D);
}
