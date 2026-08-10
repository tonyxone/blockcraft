#include "fish.h"
#include "blocks.h"
#include "constants.h"
#include "noise.h"
#include "win_gl.h"
#include "world.h"
#include "worldgen.h"
#include <algorithm>
#include <cmath>

namespace {

const double PI = 3.14159265358979323846;

double wrapAngle(double a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}

Mulberry32 g_fishRng(0x5F15C0Du);

// Same CCW-outward corners + baked directional shading as every other
// hand-placed box in this game (animal.cpp's own copy of this, mesher.cpp's
// FACES, ...) — top brightest, bottom dimmest, sides in between.
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

// Tropical fish: color varies per spawn, not per species (see fish.h) — a
// small hand-picked set of real reef-fish palettes rather than vanilla's
// full 22, researched against clownfish (orange/white), a blue tang
// (blue/yellow) and the ornate butterflyfish reference this project keeps
// at Desktop\animal\OrnateButterfly_TropicalFishB.png (gray/orange).
struct TropicalPalette {
  double bodyR, bodyG, bodyB;
  double bandR, bandG, bandB;
};
const TropicalPalette TROPICAL_PALETTES[TROPICAL_PATTERN_COUNT] = {
  { 0.90, 0.45, 0.10, 0.95, 0.95, 0.92 }, // clownfish: orange + white
  { 0.15, 0.35, 0.75, 0.95, 0.80, 0.20 }, // blue tang: blue + yellow
  { 0.55, 0.58, 0.62, 0.75, 0.40, 0.15 }, // ornate butterfly: gray + orange
  { 0.90, 0.80, 0.20, 0.12, 0.12, 0.14 }, // yellow + black
};

// Cod, salmon and both tropical-fish body shapes are all built the same
// way: an elongated body, a belly band, pectoral fins near the head, a
// dorsal fin, an eye, and a tail (single wedge, or forked into two prongs
// like the salmon's own reference render). Head faces -Z, matching every
// other animal in this game's own yaw convention.
struct ElongatedFishShape {
  double bodyLen, bodyWid, bodyHt;
  double dorsalH;   // 0 = no dorsal fin
  bool forkedTail;  // salmon: two prongs, traced from Salmon_JE1.gif
  double bodyR, bodyG, bodyB;
  double bellyR, bellyG, bellyB;
  double finR, finG, finB;
};

void drawElongatedFish(const ElongatedFishShape& s, double animPhase, bool striped, double bandR,
                       double bandG, double bandB) {
  double hl = s.bodyLen / 2, hw = s.bodyWid / 2, hh = s.bodyHt / 2;

  // body
  drawBox(-hw, -hh, -hl, s.bodyWid, s.bodyHt, s.bodyLen, s.bodyR, s.bodyG, s.bodyB);

  // pale belly band along the underside
  drawBox(-hw - 0.005, -hh - 0.005, -hl + 0.02, s.bodyWid + 0.01, s.bodyHt * 0.32,
          s.bodyLen - 0.04, s.bellyR, s.bellyG, s.bellyB);

  // tropical fish: one bright band around the body's middle third — the
  // clownfish/butterfly stripe, standing in for vanilla's pattern textures
  if (striped) {
    drawBox(-hw - 0.006, -hh - 0.006, -hl * 0.25, s.bodyWid + 0.012, s.bodyHt + 0.012,
            s.bodyLen * 0.34, bandR, bandG, bandB);
  }

  // eye: a small dark cube on the front corner of the head
  drawBox(hw - s.bodyWid * 0.18, hh * 0.25, -hl - 0.005, s.bodyWid * 0.16, s.bodyWid * 0.16, 0.02,
          0.05, 0.05, 0.06);

  // pectoral fins: small flat fins just behind the head, angled down and out
  for (int side = -1; side <= 1; side += 2) {
    glPushMatrix();
    glTranslated(side * hw, -hh * 0.2, -hl * 0.55);
    glRotated(side * 35, 0, 0, 1);
    drawBox(0, -s.bodyHt * 0.35, -s.bodyLen * 0.08, side * s.bodyWid * 0.5, s.bodyHt * 0.35,
            s.bodyLen * 0.16, s.finR, s.finG, s.finB);
    glPopMatrix();
  }

  // dorsal fin: a ridge along the top-middle of the back
  if (s.dorsalH > 0) {
    drawBox(-s.bodyWid * 0.12, hh, -s.bodyLen * 0.1, s.bodyWid * 0.24, s.dorsalH,
            s.bodyLen * 0.32, s.finR, s.finG, s.finB);
  }

  // tail: wags side to side while swimming — a single wedge, or two prongs
  // forked apart for the salmon's own silhouette
  glPushMatrix();
  glTranslated(0, 0, hl);
  glRotated(std::sin(animPhase) * 20.0, 0, 1, 0);
  if (s.forkedTail) {
    for (int side = -1; side <= 1; side += 2) {
      glPushMatrix();
      glRotated(side * 18, 0, 1, 0);
      drawBox(-s.bodyWid * 0.08, -hh * 0.7, 0, s.bodyWid * 0.16, s.bodyHt * 1.1, s.bodyLen * 0.22,
              s.finR, s.finG, s.finB);
      glPopMatrix();
    }
  } else {
    drawBox(-s.bodyWid * 0.1, -hh * 0.8, 0, s.bodyWid * 0.2, s.bodyHt * 1.3, s.bodyLen * 0.22,
            s.finR, s.finG, s.finB);
  }
  glPopMatrix();
}

const ElongatedFishShape COD_SHAPE = {
  0.50, 0.16, 0.18, 0.05, false, 0.42, 0.34, 0.24, 0.70, 0.62, 0.50, 0.28, 0.20, 0.14,
};
const ElongatedFishShape SALMON_SHAPE = {
  0.60, 0.18, 0.20, 0.06, true, 0.66, 0.28, 0.26, 0.80, 0.74, 0.68, 0.40, 0.20, 0.16,
};
// Vanilla's two tropical-fish body shapes: "A" (streamlined, e.g. the
// clownfish reference) and the taller "B" (e.g. the ornate butterflyfish
// reference) — same construction, different proportions and a taller
// dorsal fin on the B shape.
const ElongatedFishShape TROPICAL_A_SHAPE = {
  0.24, 0.09, 0.12, 0.04, true, 0, 0, 0, 0.85, 0.83, 0.78, 0.30, 0.30, 0.32,
};
const ElongatedFishShape TROPICAL_B_SHAPE = {
  0.20, 0.08, 0.17, 0.09, true, 0, 0, 0, 0.85, 0.83, 0.78, 0.30, 0.30, 0.32,
};

// Pufferfish: round/boxy rather than elongated, inflating from the small
// reference render into the large spiky one as a player approaches
// (Fish::puffAmount, 0 = Pufferfish_small_JE1.gif, 1 = _large_JE1.gif).
void drawPufferfish(double puffAmount) {
  double s = 1.0 + puffAmount * 0.7; // large render is visibly rounder/bigger than small
  double bw = 0.20 * s, bh = 0.16 * s, bl = 0.20 * s;
  double topR = 0.78, topG = 0.56, topB = 0.20;
  double bellyR = 0.86, bellyG = 0.80, bellyB = 0.62;
  double spikeR = 0.55, spikeG = 0.52, spikeB = 0.16;

  drawBox(-bw / 2, -bh / 2, -bl / 2, bw, bh, bl, topR, topG, topB);
  drawBox(-bw / 2 - 0.004, -bh / 2 - 0.004, -bl / 2 + 0.01, bw + 0.008, bh * 0.4, bl - 0.02,
          bellyR, bellyG, bellyB);

  // two dark eyes on the top-front
  for (int side = -1; side <= 1; side += 2) {
    drawBox(side * bw * 0.22, bh * 0.28, -bl / 2 - 0.005, bw * 0.14, bw * 0.14, 0.02, 0.05, 0.05,
            0.06);
  }
  // side fins
  for (int side = -1; side <= 1; side += 2) {
    drawBox(side * bw / 2, -bh * 0.1, -bl * 0.05, side * bw * 0.3, bh * 0.3, bl * 0.2, 0.45, 0.55,
            0.62);
  }

  // spikes: only visible once it's puffed up past halfway, same as the
  // large reference render's crown of spines — the small render has none
  if (puffAmount > 0.5) {
    double spike = (puffAmount - 0.5) * 2.0; // 0..1 over the second half of inflating
    const double offsets[6][3] = {
      { -0.6, 1.0, -0.4 }, { 0.0, 1.0, -0.55 }, { 0.6, 1.0, -0.4 },
      { -0.6, 1.0, 0.4 },  { 0.0, 1.0, 0.55 },  { 0.6, 1.0, 0.4 },
    };
    for (const auto& o : offsets) {
      double px = o[0] * bw / 2, py = o[1] * bh / 2, pz = o[2] * bl / 2;
      double len = 0.05 * s * spike;
      drawBox(px - 0.015, py, pz - 0.015, 0.03, len, 0.03, spikeR, spikeG, spikeB);
    }
  }
}

} // namespace

const FishSpeciesDef FISH_SPECIES[FISH_SPECIES_COUNT] = {
  //                    name        weight  minScale maxScale minSpeed maxSpeed
  /* COD        */ { "cod",           3.0,  0.55,    1.60,    0.7,     1.1 },
  /* SALMON     */ { "salmon",        2.0,  0.60,    1.80,    1.0,     1.6 },
  /* PUFFERFISH */ { "pufferfish",    1.5,  0.60,    1.50,    0.5,     0.8 },
  /* TROPICAL   */ { "tropical fish", 3.5,  0.45,    1.40,    0.9,     1.5 },
};

void updateFish(Fish& f, World& world, double dt, const Vec3& playerPos) {
  const double SWIM_SPEED = 0.6;
  const double TURN_SPEED = 1.4;

  f.wanderTimer -= dt;
  if (f.wanderTimer <= 0) {
    f.targetYaw = g_fishRng.next() * 2 * PI;
    f.targetPitch = (g_fishRng.next() - 0.5) * 0.9; // gentle up/down bias, radians
    f.wanderTimer = 2.5 + g_fishRng.next() * 3.5;
  }

  f.yaw += clampd(wrapAngle(f.targetYaw - f.yaw), -TURN_SPEED * dt, TURN_SPEED * dt);
  f.pitch += clampd(f.targetPitch - f.pitch, -TURN_SPEED * dt, TURN_SPEED * dt);

  double speed = SWIM_SPEED * f.speedMult;
  double cp = std::cos(f.pitch);
  f.velocity.x = -std::sin(f.yaw) * cp * speed;
  f.velocity.z = -std::cos(f.yaw) * cp * speed;
  f.velocity.y = std::sin(f.pitch) * speed;

  double nx = f.position.x + f.velocity.x * dt;
  double ny = f.position.y + f.velocity.y * dt;
  double nz = f.position.z + f.velocity.z * dt;
  bool stillWater = isWater(world.getBlock((int)std::floor(nx), (int)std::floor(ny), (int)std::floor(nz)));
  if (stillWater) {
    f.position = Vec3(nx, ny, nz);
  } else {
    // Swam into the shore, the floor or the surface: cut the bout short and
    // pick a fresh heading next tick instead of pushing into it.
    f.wanderTimer = 0;
  }

  // tail-wag — fish never fully stop swimming, unlike land animals idling;
  // faster fish visibly flick their tail faster, not just glide quicker
  f.animPhase += dt * 7.0 * f.speedMult;

  if (f.species == FISH_PUFFERFISH) {
    double dx = f.position.x - playerPos.x, dy = f.position.y - playerPos.y,
           dz = f.position.z - playerPos.z;
    bool close = dx * dx + dy * dy + dz * dz < 16.0; // within 4 blocks
    double target = close ? 1.0 : 0.0;
    f.puffAmount += clampd(target - f.puffAmount, -dt * 2.0, dt * 2.0);
  }
}

void drawFishes(const std::vector<Fish>& fishes) {
  if (fishes.empty()) return;
  glDisable(GL_TEXTURE_2D);
  for (const Fish& f : fishes) {
    glPushMatrix();
    glTranslated(f.position.x, f.position.y, f.position.z);
    glRotated(f.yaw * 180.0 / PI, 0, 1, 0);
    glRotated(-f.pitch * 180.0 / PI, 1, 0, 0);
    glScaled(f.scale, f.scale, f.scale);
    switch (f.species) {
      case FISH_COD:
        drawElongatedFish(COD_SHAPE, f.animPhase, false, 0, 0, 0);
        break;
      case FISH_SALMON:
        drawElongatedFish(SALMON_SHAPE, f.animPhase, false, 0, 0, 0);
        break;
      case FISH_PUFFERFISH:
        drawPufferfish(f.puffAmount);
        break;
      case FISH_TROPICAL: {
        const ElongatedFishShape& shape = f.tallShape ? TROPICAL_B_SHAPE : TROPICAL_A_SHAPE;
        const TropicalPalette& pal = TROPICAL_PALETTES[f.patternIndex % TROPICAL_PATTERN_COUNT];
        ElongatedFishShape colored = shape;
        colored.bodyR = pal.bodyR; colored.bodyG = pal.bodyG; colored.bodyB = pal.bodyB;
        drawElongatedFish(colored, f.animPhase, true, pal.bandR, pal.bandG, pal.bandB);
        break;
      }
      default:
        break;
    }
    glPopMatrix();
  }
  glEnable(GL_TEXTURE_2D);
}

void maintainFishSpawns(World& world, std::vector<Fish>& fishes, double px, double pz,
                        int renderDistance) {
  const int MAX_TOTAL = 40;
  const int ATTEMPTS = 8;
  double range = CHUNK_SIZE * renderDistance;

  double totalWeight = 0;
  for (const FishSpeciesDef& d : FISH_SPECIES) totalWeight += d.spawnWeight;

  for (int i = 0; i < ATTEMPTS && (int)fishes.size() < MAX_TOTAL; i++) {
    double ang = g_fishRng.next() * 2 * PI;
    double dist = 8 + g_fishRng.next() * std::max(1.0, range - 8);
    int wx = (int)std::floor(px + std::cos(ang) * dist);
    int wz = (int)std::floor(pz + std::sin(ang) * dist);

    int biome = 0, surfaceY = 0;
    columnInfoAt(wx, wz, biome, surfaceY);
    // Needs a real body of water: the column's floor sits below sea level
    // with at least a couple of blocks of water over it (worldgen.cpp fills
    // surfaceY+1..SEA_LEVEL with water) — a beach's ankle-deep edge isn't
    // enough to swim in.
    if (surfaceY > SEA_LEVEL - 2) continue;

    int wy = surfaceY + 1 + (int)(g_fishRng.next() * (SEA_LEVEL - surfaceY - 1));
    wy = std::min(wy, SEA_LEVEL);
    if (!isWater(world.getBlock(wx, wy, wz))) continue;

    double r = g_fishRng.next() * totalWeight;
    int species = 0;
    for (; species < FISH_SPECIES_COUNT - 1; species++) {
      r -= FISH_SPECIES[species].spawnWeight;
      if (r <= 0) break;
    }
    const FishSpeciesDef& def = FISH_SPECIES[species];

    Fish f;
    f.species = (FishSpecies)species;
    f.position = Vec3(wx + 0.5, wy + 0.5, wz + 0.5);
    f.yaw = g_fishRng.next() * 2 * PI;
    f.scale = def.minScale + g_fishRng.next() * (def.maxScale - def.minScale);
    f.speedMult = def.minSpeed + g_fishRng.next() * (def.maxSpeed - def.minSpeed);
    f.animPhase = g_fishRng.next() * 2 * PI;
    if (species == FISH_TROPICAL) {
      f.patternIndex = (int)(g_fishRng.next() * TROPICAL_PATTERN_COUNT);
      f.tallShape = g_fishRng.next() < 0.5;
    }
    fishes.push_back(f);
  }

  double pruneRange = range + CHUNK_SIZE * 2;
  for (size_t i = 0; i < fishes.size();) {
    double dx = fishes[i].position.x - px, dz = fishes[i].position.z - pz;
    if (dx * dx + dz * dz > pruneRange * pruneRange) {
      fishes[i] = fishes.back();
      fishes.pop_back();
    } else {
      i++;
    }
  }
}
