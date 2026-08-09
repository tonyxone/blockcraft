#include "furnace.h"
#include "chest.h" // CHEST_HINGE_X/Z, CHEST_W/D — a furnace shares a chest's footprint
#include "recipes.h" // ITEM_FURNACE
#include "win_gl.h"
#include "world.h"
#include <cmath>

// A small blaze of colour rather than a textured sprite — two crossed
// double-sided planes (same trick as the plant billboards in mesher.cpp),
// each built as a stack of shrinking horizontal segments so the silhouette
// tapers to a point instead of standing as a flat rectangle, swaying more
// near the tip than the base the way a real flame licks. Three colour
// stops (deep red base, hot orange middle, pale yellow tip) instead of a
// straight 2-stop gradient. A gentle per-position flicker on the height and
// sway keeps a room of lit furnaces from pulsing in lockstep. Exposed via
// furnace.h so campfire.cpp can reuse it rather than duplicating it.
void drawFlame(double cx, double cy, double cz, double time) {
  double flicker = 0.85 + 0.15 * std::sin(time * 11.0 + cx * 37.0 + cz * 53.0);
  double height = 0.34 * flicker;
  double halfBase = 0.15;
  const int SEGMENTS = 5;

  const uint8_t colBase[3] = { 210, 40, 10 };
  const uint8_t colMid[3] = { 255, 140, 30 };
  const uint8_t colTip[3] = { 255, 220, 90 };
  auto lerp8 = [](uint8_t a, uint8_t b, double t) { return (uint8_t)(a + (b - a) * t); };
  auto colorAt = [&](double t, uint8_t out[3]) {
    const uint8_t* a = t < 0.5 ? colBase : colMid;
    const uint8_t* b = t < 0.5 ? colMid : colTip;
    double u = t < 0.5 ? t / 0.5 : (t - 0.5) / 0.5;
    for (int k = 0; k < 3; k++) out[k] = lerp8(a[k], b[k], u);
  };

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE); // a billboard has no back
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  for (int q = 0; q < 2; q++) {
    glBegin(GL_QUAD_STRIP);
    for (int s = 0; s <= SEGMENTS; s++) {
      double t = (double)s / SEGMENTS;
      double y = height * t;
      double w = halfBase * (1.0 - 0.85 * t) + 0.01; // tapers, never quite to a point
      double swayAmt = 0.06 * t * t; // barely moves at the base, licks at the tip
      double sway = swayAmt * std::sin(time * 6.0 + t * 4.0 + cx * 37.0 + cz * 53.0 + q * 1.7);
      uint8_t col[3];
      colorAt(t, col);
      uint8_t alpha = (uint8_t)(230 * (1.0 - 0.5 * t));
      glColor4ub(col[0], col[1], col[2], alpha);
      if (q == 0) {
        glVertex3d(cx - w + sway, cy + y, cz);
        glVertex3d(cx + w + sway, cy + y, cz);
      } else {
        glVertex3d(cx, cy + y, cz - w + sway);
        glVertex3d(cx, cy + y, cz + w + sway);
      }
    }
    glEnd();
  }
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_TEXTURE_2D); // left off would leave every draw after this flame untextured
}

void drawFurnaceFires(World& world, double time) {
  for (auto& kv : world.edits) {
    if (kv.second != ITEM_FURNACE) continue;
    auto it = world.furnaces.find(kv.first);
    if (it == world.furnaces.end() || !it->second.lit) continue;
    const EditKey& pos = kv.first;
    // Partway between the opening and the back wall (addFurnaceBody now
    // leaves that whole middle genuinely empty) — comfortably inside the
    // visible hollow rather than at the footprint's literal centre, which
    // used to land behind the old solid "firebox" fill and lose the depth
    // test entirely.
    const double DEPTH_FRACTION = 0.4; // 0 = at the opening, 1 = at the back wall
    double cx = pos.x + CHEST_HINGE_X + CHEST_W / 2;
    double cz = pos.z + CHEST_HINGE_Z + CHEST_D / 2;
    switch (it->second.facing) {
      case 0: cz = pos.z + CHEST_HINGE_Z + CHEST_D * DEPTH_FRACTION; break;
      case 1: cz = pos.z + CHEST_HINGE_Z + CHEST_D * (1 - DEPTH_FRACTION); break;
      case 2: cx = pos.x + CHEST_HINGE_X + CHEST_W * DEPTH_FRACTION; break;
      default: cx = pos.x + CHEST_HINGE_X + CHEST_W * (1 - DEPTH_FRACTION); break;
    }
    drawFlame(cx, pos.y + 0.04, cz, time);
  }
}
