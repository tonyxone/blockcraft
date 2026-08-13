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

// Modelled on the modern vanilla boat (minecraft.wiki's 1.9+ render): an
// upswept pointed BOW — stacked steps that narrow AND rise toward the tip —
// a flat transom STERN (unlike the old tub, which was pointed at both
// ends), planked floor, side walls with a darker gunwale rail, and two
// bench seats. No oars: the rider never holds any, so loose shafts stuck
// to the hull read as clutter (player request). Built along Z (bow toward
// -Z, matching every other entity's own yaw convention in this game),
// centered on the point the player stands at while riding.
// Still fills the full 2x1 footprint (player request).
const double HULL_LEN = 2.0, HULL_WID = 1.0, WALL_H = 0.3, FLOOR_H = 0.1;
const double WOOD_R = 0.55, WOOD_G = 0.38, WOOD_B = 0.22;
const double TRIM_R = 0.34, TRIM_G = 0.22, TRIM_B = 0.12;
const double SEAT_R = 0.46, SEAT_G = 0.31, SEAT_B = 0.18;

void drawHull() {
  double hl = HULL_LEN / 2, hw = HULL_WID / 2;
  double floorW = HULL_WID - 0.26;

  // planked floor: strips running across the hull, alternating shades
  for (int i = 0; i < 4; i++) {
    double z0 = -hl + 0.24 + i * (HULL_LEN - 0.48) / 4;
    double s = (i % 2 == 0) ? 1.0 : 0.88;
    drawBox(-floorW / 2, 0, z0, floorW, FLOOR_H, (HULL_LEN - 0.48) / 4 - 0.02,
            0.50 * s, 0.34 * s, 0.20 * s);
  }

  // side walls with a darker gunwale rail on top
  for (int side = -1; side <= 1; side += 2) {
    double x0 = side < 0 ? -hw : hw - 0.12;
    drawBox(x0, FLOOR_H, -hl + 0.42, 0.12, WALL_H, HULL_LEN - 0.84, WOOD_R, WOOD_G, WOOD_B);
    drawBox(x0, FLOOR_H + WALL_H, -hl + 0.42, 0.12, 0.05, HULL_LEN - 0.84, TRIM_R, TRIM_G, TRIM_B);
  }

  // bow (-Z): three stacked steps that narrow and RISE toward the tip, so
  // the front sweeps up out of the water instead of ending cut off square
  drawBox(-hw + 0.02, 0, -hl + 0.08, HULL_WID - 0.04, WALL_H + FLOOR_H, 0.36,
          WOOD_R, WOOD_G, WOOD_B);
  drawBox(-hw * 0.62, 0, -hl + 0.02, HULL_WID * 0.62, WALL_H + FLOOR_H + 0.10, 0.24,
          WOOD_R, WOOD_G, WOOD_B);
  drawBox(-hw * 0.30, 0, -hl, HULL_WID * 0.30, WALL_H + FLOOR_H + 0.20, 0.14,
          WOOD_R, WOOD_G, WOOD_B);
  drawBox(-hw * 0.62, WALL_H + FLOOR_H + 0.10, -hl + 0.02, HULL_WID * 0.62, 0.05, 0.24,
          TRIM_R, TRIM_G, TRIM_B);
  drawBox(-hw * 0.30, WALL_H + FLOOR_H + 0.20, -hl, HULL_WID * 0.30, 0.05, 0.14,
          TRIM_R, TRIM_G, TRIM_B);

  // stern (+Z): flat transom board with its own rail
  drawBox(-hw + 0.02, 0, hl - 0.20, HULL_WID - 0.04, WALL_H + FLOOR_H + 0.06, 0.12,
          WOOD_R, WOOD_G, WOOD_B);
  drawBox(-hw + 0.02, WALL_H + FLOOR_H + 0.06, hl - 0.20, HULL_WID - 0.04, 0.05, 0.12,
          TRIM_R, TRIM_G, TRIM_B);

  // two bench seats across the hull — vanilla's boat seats two
  for (int i = 0; i < 2; i++) {
    double z0 = i == 0 ? -0.52 : 0.30;
    drawBox(-floorW / 2 - 0.03, FLOOR_H + 0.14, z0, floorW + 0.06, 0.06, 0.22,
            SEAT_R, SEAT_G, SEAT_B);
  }
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
