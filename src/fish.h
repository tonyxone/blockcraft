#pragma once
#include "common.h"

class World;

// Ambient wildlife for open water — cod, salmon, pufferfish and tropical
// fish, researched against the vanilla renders in Desktop\animal\*.png
// (minecraft.wiki's own JE renders: Cod_JE1.gif, Salmon_JE1.gif,
// Pufferfish_small/large_JE1.gif, and the two tropical-fish body shapes,
// Clownfish_TropicalFishA.png / OrnateButterfly_TropicalFishB.png). Purely
// ambient — no health, no combat, nothing to collect — swimming wildlife to
// make the water feel alive, not a resource.
enum FishSpecies {
  FISH_COD,
  FISH_SALMON,
  FISH_PUFFERFISH,
  FISH_TROPICAL,
  FISH_SPECIES_COUNT,
};

struct FishSpeciesDef {
  const char* name;
  double spawnWeight; // relative population density, same convention as animal.h
  // Random per-spawn scale range (uniform glScaled) — wide enough that the
  // smallest and largest of a kind are unmistakably different sizes at a
  // glance, not just a subtle few percent. This is where "each kind has a
  // variety of sizes" comes from: no two fish of the same species are quite
  // the same size.
  double minScale, maxScale;
  // Random per-spawn swim speed multiplier (applied to the shared
  // SWIM_SPEED base in updateFish) — cod amble, salmon and tropical fish
  // dart, pufferfish drift; within a species there's still a real spread,
  // not every fish of a kind moving in lockstep.
  double minSpeed, maxSpeed;
};
extern const FishSpeciesDef FISH_SPECIES[FISH_SPECIES_COUNT];

// Tropical fish colors are randomized per spawn rather than fixed per
// species (vanilla has 22 named combinations) — a small hand-picked palette
// of body/band color pairs, researched against real reef fish (clownfish
// orange-and-white, a blue tang, the gray-and-orange ornate butterflyfish).
// This is where "variety of kind" comes from for the one species that
// covers all of "tropical fish": the shape is picked per spawn too (see
// Fish::tallShape below), independent of the color.
const int TROPICAL_PATTERN_COUNT = 4;

struct Fish {
  FishSpecies species = FISH_COD;
  Vec3 position; // roughly center-of-body, unlike Animal's feet convention — fish have no feet
  Vec3 velocity;
  double yaw = 0;
  double pitch = 0; // swimming up/down tilt
  double scale = 1.0;
  double speedMult = 1.0; // this fish's own share of its species' speed range
  double wanderTimer = 0;
  double targetYaw = 0;
  double targetPitch = 0;
  double animPhase = 0; // tail-wag phase, always advancing — fish never fully stop swimming

  // Tropical fish only: which color pattern (0..TROPICAL_PATTERN_COUNT-1)
  // and body shape (vanilla's "A" streamlined vs "B" tall/disc-bodied).
  int patternIndex = 0;
  bool tallShape = false;

  // Pufferfish only: inflates defensively when a player gets close, the way
  // the real mob does — smoothed 0..1 rather than a hard cut, so the swap
  // between the small and large reference renders reads as an animation.
  double puffAmount = 0;
};

// Advances one fish's swim AI and physics one frame — wanders in 3D, turning
// gently toward a new random heading/pitch every few seconds, and never
// leaves the water it's in (checks isWater on the next step; hitting a wall,
// the floor or the surface just ends the current bout early, the same
// "wandered into something solid, pick a new heading" idea animal.cpp's
// land AI uses).
void updateFish(Fish& fish, World& world, double dt, const Vec3& playerPos);

// Draws every fish, dispatching to the elongated-body renderer (cod, salmon,
// tropical) or the round/spiky pufferfish renderer. Handles its own
// GL_TEXTURE_2D disable/re-enable, same convention animal.h's drawAnimals
// documents.
void drawFishes(const std::vector<Fish>& fishes);

// Tops up the fish population toward each species' target (from
// spawnWeight) within `renderDistance` chunks of (px,pz), checking for
// actual open water (SEA_LEVEL and the column's generated surface height —
// see constants.h) rather than just "not solid ground" the way land animals
// do, and prunes anything that has drifted well outside that range.
void maintainFishSpawns(World& world, std::vector<Fish>& fishes, double px, double pz,
                        int renderDistance);
