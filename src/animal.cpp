#include "animal.h"
#include "blocks.h"
#include "chunk.h"
#include "constants.h"
#include "noise.h"
#include "physics.h"
#include "win_gl.h"
#include "world.h"
#include "worldgen.h"
#include <algorithm>
#include <cmath>

namespace {

const double PI = 3.14159265358979323846;

// --- species data ------------------------------------------------------
// Redrawn against the vanilla renders in this project's reference set
// (Desktop\animal\*.png, themselves minecraft.wiki's JE1/BE1 renders): the
// generic "box body + cube head" build stays, but every species now gets
// the parts that actually identify it — a pig's snout, a cow's horns and
// pale muzzle, a sheep's dark face under a wool cap, a wolf's tan muzzle
// with a black nose and a bushy raised tail, a rabbit's long upright ears,
// the cats' long curled-up tails, a panda's black legs, ears, eye patches
// and shoulder band, a polar bear's gray muzzle.
struct QuadrupedShape {
  double bodyLen, bodyWid, bodyHt;
  double headSize;
  double legHt, legThick;
  double earW, earH;                    // 0 = no ears; depth is earW * 0.5
  double hornSize;                      // 0 = none; light cubes at the head's top corners
  double snoutW, snoutH, snoutL;        // 0 = no muzzle; sits low on the face
  bool blackNose;                       // dark tip across the muzzle's front end
  bool eyePatches;                      // panda: dark patches on the upper face
  bool shoulderBand;                    // panda: dark band over the front of the body
  bool woolCap;                         // sheep: body-colored slab over the dark face
  double tailLen, tailThick, tailAngle; // tailLen 0 = no tail; angle <0 tips up
  double bodyR, bodyG, bodyB;
  double headR, headG, headB;           // == body except the dark-faced sheep
  double legR, legG, legB;
  double accR, accG, accB;              // ears, horns, eye patches, shoulder band
  double snoutR, snoutG, snoutB;
};

// clang-format off
const QuadrupedShape QUADRUPED_SHAPES[ANIMAL_SPECIES_COUNT] = {
  /* PIG        */ { 0.60,0.40,0.35, 0.30, 0.35,0.10, 0.08,0.06, 0,    0.16,0.12,0.07, false,false,false,false, 0.08,0.04,-60,
                     0.94,0.64,0.60, 0.94,0.64,0.60, 0.94,0.64,0.60, 0.88,0.55,0.52, 0.88,0.55,0.52 },
  /* COW        */ { 0.75,0.45,0.50, 0.32, 0.50,0.12, 0.10,0.05, 0.07, 0.24,0.14,0.08, false,false,false,false, 0.30,0.03, 40,
                     0.24,0.18,0.14, 0.24,0.18,0.14, 0.24,0.18,0.14, 0.82,0.80,0.74, 0.78,0.72,0.66 },
  /* CHICKEN    */ { 0,0,0, 0, 0,0, 0,0, 0, 0,0,0, false,false,false,false, 0,0,0,
                     0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0 }, // unused — chicken has its own renderer
  /* SHEEP      */ { 0.60,0.45,0.45, 0.28, 0.35,0.10, 0.07,0.05, 0,    0.08,0.06,0.03, false,false,false,true, 0.08,0.04,-20,
                     0.88,0.87,0.82, 0.66,0.55,0.46, 0.66,0.55,0.46, 0.66,0.55,0.46, 0.85,0.55,0.55 },
  /* WOLF       */ { 0.55,0.28,0.35, 0.26, 0.40,0.09, 0.09,0.10, 0,    0.12,0.10,0.12, true, false,false,false, 0.25,0.09,-45,
                     0.75,0.75,0.77, 0.75,0.75,0.77, 0.75,0.75,0.77, 0.42,0.42,0.44, 0.83,0.75,0.65 },
  /* RABBIT     */ { 0.28,0.20,0.20, 0.18, 0.16,0.06, 0.05,0.22, 0,    0.06,0.05,0.03, false,false,false,false, 0.06,0.06,  0,
                     0.47,0.39,0.30, 0.47,0.39,0.30, 0.47,0.39,0.30, 0.47,0.39,0.30, 0.85,0.62,0.68 },
  /* OCELOT     */ { 0.45,0.22,0.25, 0.20, 0.28,0.07, 0.06,0.06, 0,    0.10,0.07,0.06, false,false,false,false, 0.35,0.05,-60,
                     0.86,0.73,0.42, 0.86,0.73,0.42, 0.86,0.73,0.42, 0.72,0.58,0.30, 0.90,0.82,0.60 },
  /* CAT        */ { 0.42,0.20,0.22, 0.19, 0.25,0.06, 0.06,0.06, 0,    0.10,0.07,0.05, false,false,false,false, 0.32,0.05,-60,
                     0.89,0.62,0.26, 0.89,0.62,0.26, 0.89,0.62,0.26, 0.78,0.50,0.20, 0.93,0.88,0.76 },
  /* PANDA      */ { 0.65,0.45,0.40, 0.33, 0.30,0.13, 0.08,0.06, 0,    0.18,0.12,0.08, true, true, true, false, 0,0,0,
                     0.92,0.92,0.90, 0.92,0.92,0.90, 0.08,0.08,0.08, 0.08,0.08,0.08, 0.92,0.92,0.90 },
  /* POLAR_BEAR */ { 1.05,0.58,0.55, 0.36, 0.48,0.17, 0.08,0.06, 0,    0.20,0.14,0.14, true, false,false,false, 0.06,0.05,  0,
                     0.94,0.95,0.91, 0.94,0.95,0.91, 0.94,0.95,0.91, 0.94,0.95,0.91, 0.72,0.73,0.68 },
};
// clang-format on

} // namespace

// Health tiers now strictly increase with size (spawnWeight, descending):
// rabbit(5) < chicken(4) < cat/ocelot(3) < sheep/pig(2.5) < wolf(2) <
// cow(1.5) < panda(1) < polar bear(0.7) — see the maxHealth comment above.
const AnimalSpeciesDef ANIMAL_SPECIES[ANIMAL_SPECIES_COUNT] = {
  /* PIG        */ { "pig",         8, 2.5, { true,  false, false, false }, 0.28, 0.55 },
  /* COW        */ { "cow",        14, 1.5, { true,  false, false, false }, 0.32, 0.85 },
  /* CHICKEN    */ { "chicken",     4, 4.0, { true,  false, false, false }, 0.18, 0.55 },
  /* SHEEP      */ { "sheep",       8, 2.5, { true,  false, false, false }, 0.30, 0.75 },
  /* WOLF       */ { "wolf",       10, 2.0, { true,  false, true,  true  }, 0.24, 0.65 },
  /* RABBIT     */ { "rabbit",      3, 5.0, { true,  true,  false, true  }, 0.16, 0.32 },
  /* OCELOT     */ { "ocelot",      6, 3.0, { true,  false, false, false }, 0.22, 0.50 },
  /* CAT        */ { "cat",         6, 3.0, { true,  false, false, false }, 0.20, 0.45 },
  /* PANDA      */ { "panda",      20, 1.0, { true,  false, false, false }, 0.32, 0.70 },
  /* POLAR_BEAR */ { "polar bear", 30, 0.7, { false, false, false, true  }, 0.38, 1.05 },
};

int meatDropFor(AnimalSpecies species) {
  switch (species) {
    case ANIMAL_RABBIT:
    case ANIMAL_CHICKEN:
    case ANIMAL_CAT:
    case ANIMAL_OCELOT:
      return 1;
    case ANIMAL_SHEEP:
    case ANIMAL_PIG:
    case ANIMAL_WOLF:
      return 2;
    case ANIMAL_COW:
    case ANIMAL_PANDA:
      return 3;
    case ANIMAL_POLAR_BEAR:
      return 4;
    default:
      return 1;
  }
}

namespace {

// Same CCW-outward corner sets and baked directional shading as every other
// hand-placed box in this game (mesher.cpp's FACES, door.cpp's DOOR_FACES,
// playermodel.cpp's FACES) — top brightest, bottom dimmest, sides in
// between, so a flat-colored animal still reads with correct-looking form
// under the world's own lighting convention.
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

// Draws a w*h*d box, min corner at (x0,y0,z0), in the CURRENT transformed
// space — callers push/translate/rotate their own matrix first for
// positioning or joint pivots (legs), rather than this taking a pivot
// parameter itself, so it works correctly no matter where in the model the
// box sits (unlike a Y-only pivot shortcut, which only looks right for
// parts already centered on the rotation axis).
void drawBox(double x0, double y0, double z0, double w, double h, double d,
            double r, double g, double b) {
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

// One leg: hinged at (pivotX, pivotY, pivotZ), hanging straight down,
// swinging fore/aft by `angleDeg` about that point.
void drawLeg(double pivotX, double pivotY, double pivotZ, double thick, double legHt,
            double angleDeg, double r, double g, double b) {
  glPushMatrix();
  glTranslated(pivotX, pivotY, pivotZ);
  glRotated(angleDeg, 1, 0, 0);
  drawBox(-thick / 2, -legHt, -thick / 2, thick, legHt, thick, r, g, b);
  glPopMatrix();
}

// Tips the whole model onto its side once dying, pivoting around roughly
// mid-body height so it settles near the ground instead of floating —
// species-agnostic (uses the same collision height every renderer already
// has via ANIMAL_SPECIES, rather than each shape's own proportions) so both
// drawQuadruped and drawChicken can call it identically.
void applyDeathTip(const Animal& a) {
  if (!a.dying) return;
  double h = ANIMAL_SPECIES[a.species].height;
  glTranslated(0, h * 0.4, 0);
  glRotated(85, 0, 0, 1);
  glTranslated(0, -h * 0.4, 0);
}

// A flat, dark red, alpha-blended pool on the ground under/around a dying
// animal — stays for the whole lie-down (main.cpp's deathTimer), same
// blended-quad technique the furnace flame uses, just horizontal and static
// instead of a swaying billboard.
void drawBloodPool(const Animal& a) {
  double r = 0.5;
  double y = a.position.y + 0.01; // a hair above the ground, avoids z-fighting
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4d(0.35, 0.02, 0.02, 0.55);
  glBegin(GL_QUADS);
  glVertex3d(a.position.x - r, y, a.position.z - r);
  glVertex3d(a.position.x - r, y, a.position.z + r);
  glVertex3d(a.position.x + r, y, a.position.z + r);
  glVertex3d(a.position.x + r, y, a.position.z - r);
  glEnd();
  glDisable(GL_BLEND);
}

void drawQuadruped(const Animal& a, const QuadrupedShape& s) {
  glPushMatrix();
  glTranslated(a.position.x, a.position.y, a.position.z);
  glRotated(a.yaw * 180.0 / PI, 0, 1, 0);
  applyDeathTip(a);

  double legY = s.legHt;

  // body
  drawBox(-s.bodyWid / 2, legY, -s.bodyLen / 2, s.bodyWid, s.bodyHt, s.bodyLen, s.bodyR, s.bodyG,
          s.bodyB);

  // panda's black shoulders: a slightly oversized band over the front half
  // of the body, so the white body still shows past it toward the rear
  if (s.shoulderBand) {
    drawBox(-s.bodyWid / 2 - 0.012, legY - 0.012, -s.bodyLen / 2 - 0.012, s.bodyWid + 0.024,
            s.bodyHt + 0.024, s.bodyLen * 0.42, s.accR, s.accG, s.accB);
  }

  // head: centered in X, poking forward (-Z) of the body, near the top
  double headY0 = legY + s.bodyHt - s.headSize * 0.75;
  double headZ0 = -s.bodyLen / 2 - s.headSize * 0.55;
  drawBox(-s.headSize / 2, headY0, headZ0, s.headSize, s.headSize, s.headSize, s.headR, s.headG,
          s.headB);

  // muzzle: a box low on the face, protruding forward — the pig's snout,
  // the cow's pale mouth, the wolf/bear muzzle, the sheep/rabbit's pink nose
  if (s.snoutL > 0) {
    double snoutY = headY0 + s.headSize * 0.08;
    drawBox(-s.snoutW / 2, snoutY, headZ0 - s.snoutL, s.snoutW, s.snoutH, s.snoutL, s.snoutR,
            s.snoutG, s.snoutB);
    if (s.blackNose) {
      // dark tip across the top of the muzzle's front end (wolf, bears)
      drawBox(-s.snoutW / 2, snoutY + s.snoutH * 0.5, headZ0 - s.snoutL - 0.02, s.snoutW,
              s.snoutH * 0.5, 0.04, 0.08, 0.07, 0.07);
    }
  }

  // panda's eye patches: dark plates on the upper corners of the face
  if (s.eyePatches) {
    double p = s.headSize * 0.2;
    drawBox(-s.headSize * 0.42, headY0 + s.headSize * 0.48, headZ0 - 0.012, p, p * 1.4, 0.02,
            s.accR, s.accG, s.accB);
    drawBox(s.headSize * 0.42 - p, headY0 + s.headSize * 0.48, headZ0 - 0.012, p, p * 1.4, 0.02,
            s.accR, s.accG, s.accB);
  }

  // sheep's wool cap: a body-colored slab over the dark face, leaving the
  // face itself showing only below it
  if (s.woolCap) {
    drawBox(-s.headSize / 2 - 0.01, headY0 + s.headSize * 0.68, headZ0 - 0.01, s.headSize + 0.02,
            s.headSize * 0.34, s.headSize + 0.02, s.bodyR, s.bodyG, s.bodyB);
  }

  // ears: two boxes on top of the head's back corners — tiny on most
  // species, long and upright on the rabbit
  if (s.earW > 0) {
    double earY = headY0 + s.headSize;
    double earZ = headZ0 + s.headSize * 0.15;
    drawBox(-s.headSize / 2 + s.earW * 0.1, earY, earZ, s.earW, s.earH, s.earW * 0.5, s.accR,
            s.accG, s.accB);
    drawBox(s.headSize / 2 - s.earW * 1.1, earY, earZ, s.earW, s.earH, s.earW * 0.5, s.accR,
            s.accG, s.accB);
  }

  // horns: light cubes at the head's top corners, just outside the ears
  if (s.hornSize > 0) {
    double hY = headY0 + s.headSize * 0.9;
    drawBox(-s.headSize / 2 - s.hornSize * 0.4, hY, headZ0 + s.headSize * 0.25, s.hornSize,
            s.hornSize, s.hornSize, s.accR, s.accG, s.accB);
    drawBox(s.headSize / 2 - s.hornSize * 0.6, hY, headZ0 + s.headSize * 0.25, s.hornSize,
            s.hornSize, s.hornSize, s.accR, s.accG, s.accB);
  }

  // tail: a box angled back from the rear of the body — short and raised on
  // a pig, long and curled up on the cats, thick on the wolf, drooping on
  // the cow (positive angle), a puff on the rabbit
  if (s.tailLen > 0) {
    glPushMatrix();
    glTranslated(0, legY + s.bodyHt * 0.75, s.bodyLen / 2);
    glRotated(s.tailAngle, 1, 0, 0);
    drawBox(-s.tailThick / 2, -s.tailThick / 2, 0, s.tailThick, s.tailThick, s.tailLen, s.bodyR,
            s.bodyG, s.bodyB);
    glPopMatrix();
  }

  // 4 legs at the body's corners, diagonal trot gait (front-left + back-right
  // swing together, opposite the front-right + back-left pair).
  double insetX = s.bodyWid / 2 - s.legThick * 0.6;
  double insetZ = s.bodyLen / 2 - s.legThick * 0.6;

  double swing = a.moving ? std::sin(a.animPhase) * 25.0 : 0.0;
  drawLeg(-insetX, legY, -insetZ, s.legThick, s.legHt, swing, s.legR, s.legG, s.legB);
  drawLeg(insetX, legY, -insetZ, s.legThick, s.legHt, -swing, s.legR, s.legG, s.legB);
  drawLeg(-insetX, legY, insetZ, s.legThick, s.legHt, -swing, s.legR, s.legG, s.legB);
  drawLeg(insetX, legY, insetZ, s.legThick, s.legHt, swing, s.legR, s.legG, s.legB);

  glPopMatrix();
}

// Chicken is the one biped: body, head, beak, wattle, two flat wings, a
// tail, two thin centered legs with flat feet — matched to the vanilla
// render (wide flat orange beak spanning the face, red wattle under it,
// pale yellow legs).
void drawChicken(const Animal& a) {
  glPushMatrix();
  glTranslated(a.position.x, a.position.y, a.position.z);
  glRotated(a.yaw * 180.0 / PI, 0, 1, 0);
  applyDeathTip(a);

  const double legHt = 0.20, legThick = 0.04;
  const double bodyW = 0.30, bodyH = 0.28, bodyL = 0.38;
  const double headSize = 0.16;
  const double WHITE_R = 0.93, WHITE_G = 0.93, WHITE_B = 0.90;
  const double LEG_R = 0.85, LEG_G = 0.72, LEG_B = 0.30; // pale yellow

  double bodyY0 = legHt;
  drawBox(-bodyW / 2, bodyY0, -bodyL / 2, bodyW, bodyH, bodyL, WHITE_R, WHITE_G, WHITE_B);

  double headY0 = bodyY0 + bodyH - headSize * 0.6;
  double headZ0 = -bodyL / 2 - headSize * 0.5;
  drawBox(-headSize / 2, headY0, headZ0, headSize, headSize, headSize, WHITE_R, WHITE_G, WHITE_B);

  // beak: a wide flat orange box spanning most of the face
  double beakW = headSize * 0.6, beakL = headSize * 0.45;
  drawBox(-beakW / 2, headY0 + headSize * 0.3, headZ0 - beakL, beakW, headSize * 0.3, beakL,
          LEG_R, LEG_G, LEG_B);

  // wattle: tiny red box under the beak
  double wattleSize = headSize * 0.3;
  drawBox(-wattleSize / 2, headY0, headZ0 - wattleSize * 0.5, wattleSize, wattleSize * 0.6,
          wattleSize, 0.75, 0.12, 0.12);

  // wings: thin flat plates on the sides of the body
  double wingT = 0.04;
  drawBox(-bodyW / 2 - wingT, bodyY0 + bodyH * 0.25, -bodyL * 0.3, wingT, bodyH * 0.6, bodyL * 0.5,
          0.85, 0.85, 0.82);
  drawBox(bodyW / 2, bodyY0 + bodyH * 0.25, -bodyL * 0.3, wingT, bodyH * 0.6, bodyL * 0.5, 0.85,
          0.85, 0.82);

  // tail: a small box angled up from the rear of the body
  glPushMatrix();
  glTranslated(0, bodyY0 + bodyH * 0.7, bodyL / 2);
  glRotated(-45, 1, 0, 0);
  drawBox(-0.05, -0.05, 0, 0.10, 0.10, 0.16, WHITE_R, WHITE_G, WHITE_B);
  glPopMatrix();

  // 2 legs, centered under the body (not at the corners), each on a flat
  // foot pointing forward
  double swing = a.moving ? std::sin(a.animPhase) * 25.0 : 0.0;
  drawLeg(-legThick * 1.5, legHt, 0, legThick, legHt, swing, LEG_R, LEG_G, LEG_B);
  drawLeg(legThick * 1.5, legHt, 0, legThick, legHt, -swing, LEG_R, LEG_G, LEG_B);
  drawBox(-legThick * 1.5 - 0.035, 0, -0.07, 0.07, 0.02, 0.11, LEG_R, LEG_G, LEG_B);
  drawBox(legThick * 1.5 - 0.035, 0, -0.07, 0.07, 0.02, 0.11, LEG_R, LEG_G, LEG_B);

  glPopMatrix();
}

double wrapAngle(double a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}

Mulberry32 g_animalRng(0x4E1A7B2u);

} // namespace

void updateAnimal(Animal& a, World& world, double dt) {
  // A dying animal just lies where it fell — main.cpp counts its
  // deathTimer down and removes it once the lie-down is over; no AI, no
  // physics, so it doesn't slide or wander mid-death-animation.
  if (a.dying) return;

  const AnimalSpeciesDef& def = ANIMAL_SPECIES[a.species];
  const double WALK_SPEED = 1.3;
  const double GRAVITY = 28;
  // v^2/2g = 1.29 blocks of rise — clears a single ledge with real margin
  // (the old 7.5 worked out to 1.004, a knife's edge the discrete timestep
  // regularly shaved below 1 block, stranding small animals at every ledge)
  // while staying well under two.
  const double JUMP_SPEED = 8.5;
  const double TERMINAL_FALL_SPEED = -50;
  const double TURN_SPEED = 2.5; // radians/sec

  a.wanderTimer -= dt;
  if (a.wanderTimer <= 0) {
    if (g_animalRng.next() < 0.35) {
      a.moving = false;
      a.wanderTimer = 1.0 + g_animalRng.next() * 2.0;
    } else {
      a.moving = true;
      a.targetYaw = g_animalRng.next() * 2 * PI;
      a.wanderTimer = 2.0 + g_animalRng.next() * 3.0;
    }
  }

  if (a.moving) {
    double diff = wrapAngle(a.targetYaw - a.yaw);
    double step = clampd(diff, -TURN_SPEED * dt, TURN_SPEED * dt);
    a.yaw += step;
  }

  double speed = a.moving ? WALK_SPEED : 0.0;
  a.velocity.x = -std::sin(a.yaw) * speed;
  a.velocity.z = -std::cos(a.yaw) * speed;
  a.velocity.y = std::max(a.velocity.y - GRAVITY * dt, TERMINAL_FALL_SPEED);

  double hw = def.halfWidth, h = def.height;
  auto blocked = [&](double x, double y, double z) {
    return boxCollides(world, x, y, z, hw, h) || boxCollidesStairs(world, x, y, z, hw, h) ||
          boxCollidesSubCell(world, x, y, z, hw, h);
  };

  // Water never blocks movement (isSolid(BLOCK_WATER) is false), so nothing
  // stops an animal from walking straight into it — the reason one visibly
  // hovers at a shoreline is the wander AI itself: a fresh heading is picked
  // every 2-5 seconds, so an animal that reaches the water's edge mid-bout
  // has good odds of the timer expiring (or turning back inland) right at
  // the lip, before it's actually crossed in. Detecting water immediately
  // ahead and guaranteeing a bit more committed forward time is what turns
  // "occasionally wanders in eventually" into "reliably drops in" once it's
  // headed that way.
  if (a.moving && a.onGround) {
    int aheadX = (int)std::floor(a.position.x - std::sin(a.yaw) * 0.6);
    int aheadZ = (int)std::floor(a.position.z - std::cos(a.yaw) * 0.6);
    int footY = (int)std::floor(a.position.y);
    if (isWater(world.getBlock(aheadX, footY, aheadZ)) ||
        isWater(world.getBlock(aheadX, footY - 1, aheadZ))) {
      a.wanderTimer = std::max(a.wanderTimer, 1.5);
    }
  }

  // Jump over a knee-to-head-high ledge straight ahead instead of stopping
  // at it — this is what lets animals wander onto uneven terrain rather
  // than needing a perfectly flat spawn area.
  if (a.moving && a.onGround) {
    double lookX = a.position.x + a.velocity.x * 0.3;
    double lookZ = a.position.z + a.velocity.z * 0.3;
    if (blocked(lookX, a.position.y, lookZ) && !blocked(lookX, a.position.y + 1.05, lookZ)) {
      a.velocity.y = JUMP_SPEED;
    }
  }

  double nx = a.position.x + a.velocity.x * dt;
  if (!blocked(nx, a.position.y, a.position.z)) {
    a.position.x = nx;
  } else {
    a.velocity.x = 0;
    a.wanderTimer = 0; // pick a new direction next tick instead of pushing into the wall
  }

  double nz = a.position.z + a.velocity.z * dt;
  if (!blocked(a.position.x, a.position.y, nz)) {
    a.position.z = nz;
  } else {
    a.velocity.z = 0;
    a.wanderTimer = 0;
  }

  bool wasFalling = a.velocity.y <= 0;
  double ny = a.position.y + a.velocity.y * dt;
  a.onGround = false;
  if (!blocked(a.position.x, ny, a.position.z)) {
    a.position.y = ny;
  } else {
    a.velocity.y = 0;
    if (wasFalling) a.onGround = true;
  }

  if (a.moving && a.onGround) a.animPhase += dt * 6.0;
}

void drawAnimals(const std::vector<Animal>& animals) {
  if (animals.empty()) return;
  // Flat-shaded geometry, no texture — disabling texturing here (and always
  // restoring it before returning) is the same lesson the furnace flame
  // bug taught: leaving it off would untexture everything drawn afterward.
  glDisable(GL_TEXTURE_2D);
  for (const Animal& a : animals) {
    if (a.dying) drawBloodPool(a);
    if (a.species == ANIMAL_CHICKEN) drawChicken(a);
    else drawQuadruped(a, QUADRUPED_SHAPES[a.species]);
  }
  glEnable(GL_TEXTURE_2D);
}

void maintainAnimalSpawns(World& world, std::vector<Animal>& animals, double px, double pz,
                          int renderDistance) {
  const int MAX_TOTAL = 50;
  const int ATTEMPTS = 8;
  double range = CHUNK_SIZE * renderDistance;

  double totalWeight = 0;
  for (const AnimalSpeciesDef& d : ANIMAL_SPECIES) totalWeight += d.spawnWeight;

  for (int i = 0; i < ATTEMPTS && (int)animals.size() < MAX_TOTAL; i++) {
    double r = g_animalRng.next() * totalWeight;
    int species = 0;
    for (; species < ANIMAL_SPECIES_COUNT - 1; species++) {
      r -= ANIMAL_SPECIES[species].spawnWeight;
      if (r <= 0) break;
    }
    const AnimalSpeciesDef& def = ANIMAL_SPECIES[species];

    double ang = g_animalRng.next() * 2 * PI;
    double dist = 8 + g_animalRng.next() * std::max(1.0, range - 8);
    int wx = (int)std::floor(px + std::cos(ang) * dist);
    int wz = (int)std::floor(pz + std::sin(ang) * dist);

    int biome = 0, surfaceY = 0;
    columnInfoAt(wx, wz, biome, surfaceY);
    if (biome < 0 || biome > 3 || !def.biomes[biome]) continue;

    uint8_t ground = world.getBlock(wx, surfaceY, wz);
    if (!isSolid(ground) || isWater(ground)) continue;
    if (world.getBlock(wx, surfaceY + 1, wz) != BLOCK_AIR) continue;
    if (world.getBlock(wx, surfaceY + 2, wz) != BLOCK_AIR) continue;

    Animal a;
    a.species = (AnimalSpecies)species;
    a.position = Vec3(wx + 0.5, surfaceY + 1, wz + 0.5);
    a.yaw = g_animalRng.next() * 2 * PI;
    a.health = def.maxHealth;
    animals.push_back(a);
  }

  // Prune anything that's drifted well outside the loaded area.
  double pruneRange = range + CHUNK_SIZE * 2;
  for (size_t i = 0; i < animals.size();) {
    double dx = animals[i].position.x - px, dz = animals[i].position.z - pz;
    if (dx * dx + dz * dz > pruneRange * pruneRange) {
      animals[i] = animals.back();
      animals.pop_back();
    } else {
      i++;
    }
  }
}

int raycastAnimal(const std::vector<Animal>& animals, const Vec3& origin, const Vec3& dir,
                  double reach) {
  int best = -1;
  double bestT = reach;
  for (size_t i = 0; i < animals.size(); i++) {
    const Animal& a = animals[i];
    if (a.dying) continue; // a corpse mid-lie-down isn't a valid target
    const AnimalSpeciesDef& def = ANIMAL_SPECIES[a.species];
    double lo[3] = { a.position.x - def.halfWidth, a.position.y, a.position.z - def.halfWidth };
    double hi[3] = { a.position.x + def.halfWidth, a.position.y + def.height,
                     a.position.z + def.halfWidth };
    double o[3] = { origin.x, origin.y, origin.z };
    double d[3] = { dir.x, dir.y, dir.z };
    double t0 = 0, t1 = bestT;
    bool hit = true;
    for (int ax = 0; ax < 3 && hit; ax++) {
      if (std::fabs(d[ax]) < 1e-9) {
        if (o[ax] < lo[ax] || o[ax] > hi[ax]) hit = false;
      } else {
        double ta = (lo[ax] - o[ax]) / d[ax];
        double tb = (hi[ax] - o[ax]) / d[ax];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) hit = false;
      }
    }
    if (hit && t0 >= 0 && t0 < bestT) {
      bestT = t0;
      best = (int)i;
    }
  }
  return best;
}
