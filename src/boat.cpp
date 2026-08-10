#include "boat.h"
#include "blocks.h"
#include "constants.h"
#include "win_gl.h"
#include "world.h"
#include <cmath>

namespace {

const double PI = 3.14159265358979323846;

// Same per-face directional shading every hand-rolled box in this game uses
// (animal.cpp, tools.cpp, fish.cpp).
struct FaceDef {
  int corners[4][3];
  uint8_t shade;
};
const FaceDef FACES[6] = {
  { { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } }, 255 }, // top
  { { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } }, 154 }, // bottom
  { { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, 226 }, // +x
  { { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } }, 177 }, // -z
  { { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } }, 188 }, // -x
  { { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } }, 202 }, // +z
};

void drawBox(double x0, double y0, double z0, double w, double h, double d, double r, double g,
            double b) {
  for (const FaceDef& face : FACES) {
    double shade = face.shade / 255.0;
    glColor3d(r * shade, g * shade, b * shade);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
      glVertex3d(x0 + face.corners[i][0] * w, y0 + face.corners[i][1] * h,
                z0 + face.corners[i][2] * d);
    }
    glEnd();
  }
}

// Traced from the vanilla render this project keeps at
// Desktop\animal\Oak_Boat_JE4_BE2.png: a shallow open hull — flat bottom,
// raised planked side walls, both ends tapering to a point (bow and stern
// alike, unlike a rowboat which is only pointed at one end) — with a plank
// seat running across the middle. Built along Z (bow toward -Z, matching
// every other entity's own yaw convention in this game), centered on the
// point the player stands at while riding.
// Sized to fill a full 2x1 footprint (player request), scaled up from the
// original 1.4x0.62 hull rather than just stretched: every inset/thickness
// below is the old value re-proportioned to the new length/width so the
// taper and wall/seat thicknesses still read correctly at the bigger size.
const double HULL_LEN = 2.0, HULL_WID = 1.0, WALL_H = 0.3, FLOOR_H = 0.1;
const double WOOD_R = 0.55, WOOD_G = 0.38, WOOD_B = 0.22;
const double SEAT_R = 0.42, SEAT_G = 0.28, SEAT_B = 0.16;

void drawHull() {
  double hl = HULL_LEN / 2, hw = HULL_WID / 2;

  // flat bottom
  drawBox(-hw, 0, -hl + 0.2, HULL_WID, FLOOR_H, HULL_LEN - 0.4, WOOD_R, WOOD_G, WOOD_B);

  // side walls
  drawBox(-hw, FLOOR_H, -hl + 0.3, 0.13, WALL_H, HULL_LEN - 0.6, WOOD_R, WOOD_G, WOOD_B);
  drawBox(hw - 0.13, FLOOR_H, -hl + 0.3, 0.13, WALL_H, HULL_LEN - 0.6, WOOD_R, WOOD_G, WOOD_B);

  // bow and stern: two stacked, narrowing boxes at each end so the hull
  // reads as tapering to a point rather than being cut off square
  for (int end = -1; end <= 1; end += 2) {
    double tipZ = end * hl;
    drawBox(-hw + 0.05, 0, tipZ - end * 0.32, HULL_WID - 0.10, FLOOR_H + WALL_H * 0.7,
            end * 0.32, WOOD_R, WOOD_G, WOOD_B);
    drawBox(-hw * 0.55, 0, tipZ - end * 0.49, HULL_WID * 0.55, FLOOR_H + WALL_H * 0.4,
            end * 0.17, WOOD_R, WOOD_G, WOOD_B);
  }

  // plank seat across the middle, like the reference render's floor detail
  drawBox(-hw + 0.16, FLOOR_H, -0.11, HULL_WID - 0.32, 0.05, 0.22, SEAT_R, SEAT_G, SEAT_B);
}

} // namespace

bool canPlaceBoatAt(World& world, int x, int z, const std::vector<Boat>& boats, double& outY) {
  int y = -1;
  for (int cy = CHUNK_HEIGHT - 1; cy >= 0; cy--) {
    if (isWater(world.getBlock(x, cy, z))) { y = cy; break; }
    if (isSolid(world.getBlock(x, cy, z))) break; // solid ground under nothing but air: no water here
  }
  if (y < 0) return false;
  for (const Boat& b : boats) {
    double dx = b.position.x - (x + 0.5), dz = b.position.z - (z + 0.5);
    if (dx * dx + dz * dz < 1.0) return false; // already a boat here
  }
  outY = y + 1.0;
  return true;
}

void updateBoat(Boat& boat, World& world, double dt, double desiredYaw, int moveInput) {
  int bx = (int)std::floor(boat.position.x);
  int bz = (int)std::floor(boat.position.z);

  int waterY = -1;
  for (int y = (int)std::floor(boat.position.y); y >= 0; y--) {
    if (isWater(world.getBlock(bx, y, bz))) { waterY = y; break; }
    if (isSolid(world.getBlock(bx, y, bz))) break;
  }
  bool onWater = waterY >= 0;
  if (onWater) boat.position.y = waterY + 1.0;

  if (!boat.occupied || !onWater) return;

  boat.yaw = desiredYaw;
  if (moveInput != 0) {
    const double BOAT_SPEED = 4.0;
    double dirX = -std::sin(boat.yaw), dirZ = -std::cos(boat.yaw);
    double nx = boat.position.x + dirX * moveInput * BOAT_SPEED * dt;
    double nz = boat.position.z + dirZ * moveInput * BOAT_SPEED * dt;
    // Stay on water: refuse the step rather than drive up onto the shore.
    int nbx = (int)std::floor(nx), nbz = (int)std::floor(nz);
    if (isWater(world.getBlock(nbx, waterY, nbz))) {
      boat.position.x = nx;
      boat.position.z = nz;
    }
  }
}

int nearestBoat(const std::vector<Boat>& boats, const Vec3& pos) {
  const double REACH = 2.5;
  int best = -1;
  double bestDist2 = REACH * REACH;
  for (size_t i = 0; i < boats.size(); i++) {
    if (boats[i].occupied) continue;
    double dx = boats[i].position.x - pos.x, dy = boats[i].position.y - pos.y,
           dz = boats[i].position.z - pos.z;
    double d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < bestDist2) { bestDist2 = d2; best = (int)i; }
  }
  return best;
}

void drawBoats(const std::vector<Boat>& boats) {
  if (boats.empty()) return;
  glDisable(GL_TEXTURE_2D);
  for (const Boat& b : boats) {
    glPushMatrix();
    glTranslated(b.position.x, b.position.y, b.position.z);
    glRotated(b.yaw * 180.0 / PI, 0, 1, 0);
    drawHull();
    glPopMatrix();
  }
  glEnable(GL_TEXTURE_2D);
}
