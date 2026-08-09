#include "trapdoor.h"
#include "recipes.h" // ITEM_TRAPDOOR
#include "textures.h"
#include "win_gl.h"
#include "world.h"
#include <algorithm>

namespace {

// Same CCW-outward corner sets and baked directional shading as a regular
// cube face (see mesher.cpp's FACES / door.cpp's DOOR_FACES).
struct TrapdoorFace {
  int corners[4][3];
  uint8_t shade;
};
const TrapdoorFace TRAPDOOR_FACES[6] = {
  { { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } }, 255 }, // top
  { { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } }, 154 }, // bottom
  { { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, 226 }, // +x
  { { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } }, 188 }, // -x
  { { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } }, 202 }, // +z
  { { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } }, 177 }, // -z
};
const double TRAPDOOR_US[4] = { 0, 0, 1, 1 };
const double TRAPDOOR_VT[4] = { 0, 1, 1, 0 };

// A sx*sy*sz box with its min corner at (ox,oy,oz) — same technique as
// door.cpp's drawDoorBox.
void drawTrapdoorBox(double ox, double oy, double oz, double sx, double sy, double sz,
                     const UVRect& uvSide) {
  for (int f = 0; f < 6; f++) {
    const TrapdoorFace& face = TRAPDOOR_FACES[f];
    glColor3ub(face.shade, face.shade, face.shade);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
      double u = uvSide.u0 + TRAPDOOR_US[i] * (uvSide.u1 - uvSide.u0);
      glTexCoord2d(u, TRAPDOOR_VT[i]);
      glVertex3d(ox + face.corners[i][0] * sx, oy + face.corners[i][1] * sy,
                oz + face.corners[i][2] * sz);
    }
    glEnd();
  }
}

const double SWING_OPEN_DEG = 90.0;
const double T = 0.1875; // thickness, matches the old static mesh + boxCollidesTrapdoor

// Draws one trapdoor, hinged on a FLOOR edge (picked from `facing`) so it
// swings DOWN into the cell below rather than sideways like a door — the
// same translate-to-hinge/rotate/translate-back trick, but the rotation
// axis is horizontal (lying in the floor plane) instead of vertical.
//
// Rotation signs below are derived, not guessed: rotating the free edge's
// position (relative to the hinge) by the per-axis rotation formula must
// land it at negative Y (down) at swingAngle=1, not positive Y (up through
// whatever's above). Worth a quick in-game check since it can't be verified
// visually from here — flipping a wrong sign is a one-line fix.
void drawTrapdoorPanel(double px, double py, double pz, int facing, double swingAngle,
                       const UVRect& uvSide) {
  double angle = swingAngle * SWING_OPEN_DEG;
  double lo[3] = { 0, 0, 0 }, hi[3] = { 1, T, 1 };
  double hingeX = 0, hingeZ = 0;
  int axisX = 0, axisZ = 0;
  double rot = angle;
  switch (facing) {
    case 0: hingeZ = 0; axisX = 1; rot = angle; break;  // hinge along X at z=0
    case 1: hingeZ = 1; axisX = 1; rot = -angle; break; // hinge along X at z=1
    case 2: hingeX = 0; axisZ = 1; rot = -angle; break; // hinge along Z at x=0
    default: hingeX = 1; axisZ = 1; rot = angle; break; // hinge along Z at x=1
  }
  glPushMatrix();
  glTranslated(px + hingeX, py, pz + hingeZ);
  glRotated(rot, axisX, 0, axisZ);
  glTranslated(-hingeX, 0, -hingeZ);
  drawTrapdoorBox(lo[0], lo[1], lo[2], hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], uvSide);
  glPopMatrix();
}

} // namespace

void updateTrapdoorAnimations(World& world, double dt) {
  const double SWING_SPEED = 4.0; // full swing in ~0.25s, matches a door
  for (auto& kv : world.trapdoors) {
    TrapdoorState& t = kv.second;
    double target = t.open ? 1.0 : 0.0;
    if (t.swingAngle < target) t.swingAngle = std::min(target, t.swingAngle + SWING_SPEED * dt);
    else if (t.swingAngle > target) t.swingAngle = std::max(target, t.swingAngle - SWING_SPEED * dt);
  }
}

void drawTrapdoors(World& world) {
  UVRect uvSide;
  if (!getBlockFaceUV(ITEM_TRAPDOOR, 0, uvSide)) return; // its top/panel texture

  for (auto& kv : world.edits) {
    if (kv.second != ITEM_TRAPDOOR) continue;
    const EditKey& pos = kv.first;
    double swingAngle = 0;
    int facing = 0;
    auto it = world.trapdoors.find(pos);
    if (it != world.trapdoors.end()) {
      swingAngle = it->second.swingAngle;
      facing = it->second.facing;
    }
    drawTrapdoorPanel(pos.x, pos.y, pos.z, facing, swingAngle, uvSide);
  }
}
