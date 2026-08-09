#include "door.h"
#include "recipes.h" // ITEM_DOOR
#include "textures.h"
#include "win_gl.h"
#include "world.h"
#include <algorithm>

namespace {

// Same CCW-outward corner sets and baked directional shading as a regular
// cube face (see mesher.cpp's FACES / chest.cpp's LID_FACES).
struct DoorFace {
  int corners[4][3];
  uint8_t shade;
};
const DoorFace DOOR_FACES[6] = {
  { { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } }, 255 }, // top
  { { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } }, 154 }, // bottom
  { { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, 226 }, // +x
  { { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } }, 188 }, // -x
  { { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } }, 202 }, // +z
  { { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } }, 177 }, // -z
};
const double DOOR_US[4] = { 0, 0, 1, 1 };
const double DOOR_VT[4] = { 0, 1, 1, 0 };

// A sx*sy*sz box with its min corner at (ox,oy,oz) — same technique as
// chest.cpp's drawLidBox, just with an explicit origin since a door's box
// doesn't always start at its own local (0,0,0).
void drawDoorBox(double ox, double oy, double oz, double sx, double sy, double sz,
                 const UVRect& uvSide) {
  for (int f = 0; f < 6; f++) {
    const DoorFace& face = DOOR_FACES[f];
    glColor3ub(face.shade, face.shade, face.shade);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
      double u = uvSide.u0 + DOOR_US[i] * (uvSide.u1 - uvSide.u0);
      glTexCoord2d(u, DOOR_VT[i]);
      glVertex3d(ox + face.corners[i][0] * sx, oy + face.corners[i][1] * sy,
                oz + face.corners[i][2] * sz);
    }
    glEnd();
  }
}

const double SWING_OPEN_DEG = 90.0;
const double T = 0.1875; // matches boxCollidesDoor in physics.cpp

// Draws one door (both cells, as a single 2-tall panel) hinged on whichever
// vertical edge lets it swing INTO the cell it fronts — the standard
// translate/rotate/translate-back trick (see chest.cpp's drawChestLid,
// which hinges a lid the very same way).
void drawDoorPanel(double px, double py, double pz, int facing, double swingAngle,
                   const UVRect& uvSide) {
  double angle = swingAngle * SWING_OPEN_DEG;
  double lo[3] = { 0, 0, 0 }, hi[3] = { 1, 2, 1 };
  double hingeX = 0, hingeZ = 0, rot = angle;
  switch (facing) {
    case 0: hi[2] = T; hingeX = 0; hingeZ = 0; rot = -angle; break;
    case 1: lo[2] = 1 - T; hingeX = 0; hingeZ = 1; rot = angle; break;
    case 2: hi[0] = T; hingeX = 0; hingeZ = 0; rot = angle; break;
    default: lo[0] = 1 - T; hingeX = 1; hingeZ = 0; rot = -angle; break;
  }
  glPushMatrix();
  glTranslated(px + hingeX, py, pz + hingeZ);
  glRotated(rot, 0, 1, 0);
  glTranslated(-hingeX, 0, -hingeZ);
  drawDoorBox(lo[0], lo[1], lo[2], hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], uvSide);
  glPopMatrix();
}

} // namespace

void updateDoorAnimations(World& world, double dt) {
  const double SWING_SPEED = 4.0; // full swing in ~0.25s, matches a chest lid
  for (auto& kv : world.doors) {
    DoorState& d = kv.second;
    double target = d.open ? 1.0 : 0.0;
    if (d.swingAngle < target) d.swingAngle = std::min(target, d.swingAngle + SWING_SPEED * dt);
    else if (d.swingAngle > target) d.swingAngle = std::max(target, d.swingAngle - SWING_SPEED * dt);
  }
}

void drawDoors(World& world) {
  UVRect uvSide;
  if (!getBlockFaceUV(ITEM_DOOR, 2, uvSide)) return;

  for (auto& kv : world.edits) {
    if (kv.second != ITEM_DOOR) continue;
    const EditKey& pos = kv.first;
    // Only the bottom cell draws — the top cell is the same panel, not a
    // second one (see World::doors, keyed by the bottom cell only).
    auto below = world.edits.find({ pos.x, pos.y - 1, pos.z });
    if (below != world.edits.end() && below->second == ITEM_DOOR) continue;

    double swingAngle = 0;
    int facing = 1;
    auto it = world.doors.find(pos);
    if (it != world.doors.end()) {
      swingAngle = it->second.swingAngle;
      facing = it->second.facing;
    }
    drawDoorPanel(pos.x, pos.y, pos.z, facing, swingAngle, uvSide);
  }
}
