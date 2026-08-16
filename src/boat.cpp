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

// Modelled on the classic clinker-built wooden rowboat in the reference
// photo (Desktop\blockcraft\boat.jpg): light pine tones, BOTH ends upswept
// (the bow sweeps higher than the stern), hull sides visibly planked —
// three horizontal strakes in alternating shades — capped by a darker
// gunwale rail that overhangs the walls slightly inboard, a
// lengthwise-planked floor, and two bench thwarts. No oars: the rider
// never holds any, so a loose shaft across the hull reads as clutter
// (player request). Built along Z (bow toward -Z, matching every other
// entity's own yaw convention in this game), centered on the point the
// player stands at while riding. Still fills the full 2x1 footprint
// (player request).
const double HULL_LEN = 2.0, HULL_WID = 1.0, FLOOR_H = 0.10;
const double WOOD_R = 0.70, WOOD_G = 0.55, WOOD_B = 0.36;
const double STRAKE_S = 0.85; // alternating plank shade
const double TRIM_R = 0.50, TRIM_G = 0.37, TRIM_B = 0.23;
const double SEAT_R = 0.76, SEAT_G = 0.60, SEAT_B = 0.40;

// One side wall as three stacked plank strakes, alternating shades.
void drawStrakeWall(double x0, double z0, double len) {
  for (int i = 0; i < 3; i++) {
    double s = (i % 2 == 0) ? 1.0 : STRAKE_S;
    drawBox(x0, FLOOR_H + i * 0.10, z0, 0.10, 0.10, len,
            WOOD_R * s, WOOD_G * s, WOOD_B * s);
  }
}

void drawHull() {
  double hl = HULL_LEN / 2, hw = HULL_WID / 2;
  double floorW = HULL_WID - 0.24;
  double wallZ0 = -hl + 0.44, wallLen = HULL_LEN - 0.78; // between the end steps

  // planked floor: strips running lengthwise, alternating shades
  for (int i = 0; i < 3; i++) {
    double s = (i % 2 == 0) ? 1.0 : STRAKE_S;
    drawBox(-floorW / 2 + i * floorW / 3, 0, wallZ0, floorW / 3 - 0.02, FLOOR_H,
            wallLen, (WOOD_R - 0.10) * s, (WOOD_G - 0.10) * s, (WOOD_B - 0.08) * s);
  }

  // planked side walls with a darker gunwale rail on top, overhanging
  // slightly inboard like the real boat's capped rail
  for (int side = -1; side <= 1; side += 2) {
    double x0 = side < 0 ? -hw : hw - 0.10;
    drawStrakeWall(x0, wallZ0, wallLen);
    double railX0 = side < 0 ? -hw : hw - 0.13;
    drawBox(railX0, FLOOR_H + 0.30, wallZ0 - 0.04, 0.13, 0.06, wallLen + 0.08,
            TRIM_R, TRIM_G, TRIM_B);
  }

  // bow (-Z): three stacked steps that narrow and RISE toward the tip, so
  // the front sweeps up out of the water like the photo's high prow
  drawBox(-hw + 0.02, 0, -hl + 0.10, HULL_WID - 0.04, FLOOR_H + 0.20, 0.34,
          WOOD_R, WOOD_G, WOOD_B);
  drawBox(-hw * 0.66, 0, -hl + 0.03, HULL_WID * 0.66, FLOOR_H + 0.36, 0.21,
          WOOD_R, WOOD_G, WOOD_B);
  drawBox(-hw * 0.34, 0, -hl, HULL_WID * 0.34, FLOOR_H + 0.52, 0.12,
          WOOD_R, WOOD_G, WOOD_B);
  drawBox(-hw * 0.66, FLOOR_H + 0.36, -hl + 0.03, HULL_WID * 0.66, 0.06, 0.21,
          TRIM_R, TRIM_G, TRIM_B);
  drawBox(-hw * 0.34, FLOOR_H + 0.52, -hl, HULL_WID * 0.34, 0.06, 0.12,
          TRIM_R, TRIM_G, TRIM_B);

  // stern (+Z): same upswept treatment, but only two steps — the stern in
  // the photo rises gently where the bow rises steeply
  drawBox(-hw + 0.02, 0, hl - 0.36, HULL_WID - 0.04, FLOOR_H + 0.18, 0.26,
          WOOD_R, WOOD_G, WOOD_B);
  drawBox(-hw * 0.68, 0, hl - 0.12, HULL_WID * 0.68, FLOOR_H + 0.34, 0.12,
          WOOD_R, WOOD_G, WOOD_B);
  drawBox(-hw * 0.68, FLOOR_H + 0.34, hl - 0.12, HULL_WID * 0.68, 0.06, 0.12,
          TRIM_R, TRIM_G, TRIM_B);

  // two bench thwarts across the hull, up near rail height as in the photo
  for (int i = 0; i < 2; i++) {
    double z0 = i == 0 ? -0.42 : 0.26;
    drawBox(-floorW / 2 - 0.03, FLOOR_H + 0.20, z0, floorW + 0.06, 0.06, 0.24,
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
