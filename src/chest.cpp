#include "chest.h"
#include "recipes.h"
#include "textures.h"
#include "win_gl.h"
#include "world.h"

namespace {

// Same CCW-outward corner sets and baked directional shading as a regular
// cube face (see mesher.cpp's FACES / playermodel.cpp's FACES), scaled to
// the lid's own box instead of a unit cube.
struct LidFace {
  int corners[4][3];
  uint8_t shade;
};
const LidFace LID_FACES[6] = {
  { { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } }, 255 }, // top
  { { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } }, 154 }, // bottom
  { { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, 226 }, // +x
  { { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } }, 188 }, // -x
  { { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } }, 202 }, // +z
  { { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } }, 177 }, // -z
};
const double LID_US[4] = { 0, 0, 1, 1 };
const double LID_VT[4] = { 0, 1, 1, 0 };

// Draws a sx*sy*sz box with its min corner at the current origin (caller has
// already translated/rotated). Top+bottom sample uvTop, the four sides
// sample uvSide — the same two textures the chest's static body wears.
void drawLidBox(double sx, double sy, double sz, const UVRect& uvTop, const UVRect& uvSide) {
  for (int f = 0; f < 6; f++) {
    const LidFace& face = LID_FACES[f];
    const UVRect& uv = (f < 2) ? uvTop : uvSide;
    glColor3ub(face.shade, face.shade, face.shade);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
      double u = uv.u0 + LID_US[i] * (uv.u1 - uv.u0);
      glTexCoord2d(u, LID_VT[i]);
      glVertex3d(face.corners[i][0] * sx, face.corners[i][1] * sy, face.corners[i][2] * sz);
    }
    glEnd();
  }
}

const double LID_OPEN_DEG = 100.0; // past vertical, like a real chest lid resting open

// Draws one chest's lid, hinged so its front (the edge that lifts) faces
// `facing`'s direction: 0 -Z, 1 +Z, 2 -X, 3 +X — matching World::panelFacing's
// convention. The box is always drawn with the same positive extents (so its
// winding/culling never changes); what varies per facing is which edge the
// rotation pivots around, done with the standard translate/rotate/translate-
// back trick (as playermodel.cpp's drawBox does for limb joints) rather than
// mirroring the geometry itself.
void drawChestLid(double px, double py, double pz, int facing, double lidAngle,
                   const UVRect& uvTop, const UVRect& uvSide) {
  double angle = lidAngle * LID_OPEN_DEG;
  glPushMatrix();
  glTranslated(px + CHEST_HINGE_X, py + CHEST_HINGE_Y, pz + CHEST_HINGE_Z);
  switch (facing) {
    case 0: // front faces -Z: hinge on the +Z edge
      glTranslated(0, 0, CHEST_D);
      glRotated(angle, 1, 0, 0);
      glTranslated(0, 0, -CHEST_D);
      break;
    case 2: // front faces -X: hinge on the +X edge
      glTranslated(CHEST_W, 0, 0);
      glRotated(-angle, 0, 0, 1);
      glTranslated(-CHEST_W, 0, 0);
      break;
    case 3: // front faces +X: hinge on the -X edge (no wrap: it's already there)
      glRotated(angle, 0, 0, 1);
      break;
    default: // 1: front faces +Z: hinge on the -Z edge (no wrap: it's already there)
      glRotated(-angle, 1, 0, 0);
      break;
  }
  drawLidBox(CHEST_W, CHEST_LID_H, CHEST_D, uvTop, uvSide);
  glPopMatrix();
}

} // namespace

bool chestIsEmpty(const ChestState& c) {
  for (const Hotbar::Slot& s : c.slots) {
    if (s.blockId >= 0 && s.count > 0) return false;
  }
  return true;
}

void updateChestAnimations(World& world, double dt) {
  const double LID_SPEED = 4.0; // full swing in ~0.25s
  for (auto& kv : world.chests) {
    ChestState& c = kv.second;
    double target = c.open ? 1.0 : 0.0;
    if (c.lidAngle < target) c.lidAngle = std::min(target, c.lidAngle + LID_SPEED * dt);
    else if (c.lidAngle > target) c.lidAngle = std::max(target, c.lidAngle - LID_SPEED * dt);
  }
}

void drawChestLids(World& world) {
  UVRect uvTop, uvSide;
  if (!getBlockFaceUV(ITEM_CHEST, 0, uvTop) || !getBlockFaceUV(ITEM_CHEST, 2, uvSide)) return;

  // Every chest that exists needs a (possibly closed) lid drawn, not just
  // ones that have been opened at least once — world.chests only holds the
  // latter (see chest.h), so a chest fresh off placement had no entry there
  // and drew no lid at all until the first time someone opened it. edits
  // records every block the player has ever placed or mined, with whatever
  // id is CURRENTLY there, so filtering it for chests finds all of them.
  for (auto& kv : world.edits) {
    if (kv.second != ITEM_CHEST) continue;
    const EditKey& pos = kv.first;

    double lidAngle = 0;
    int facing = 1; // default: chests placed before facing existed open toward +Z
    auto it = world.chests.find(pos);
    if (it != world.chests.end()) {
      lidAngle = it->second.lidAngle;
      facing = it->second.facing;
    }

    drawChestLid(pos.x, pos.y, pos.z, facing, lidAngle, uvTop, uvSide);
  }
}
