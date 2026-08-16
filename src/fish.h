#pragma once
#include "common.h"

class World;

// Ambient wildlife for open water — cod, salmon, pufferfish and tropical
// fish, researched against the vanilla renders in Desktop\animal\*.png
// (minecraft.wiki's own JE renders: Cod_JE1.gif, Salmon_JE1.gif,
// Pufferfish_small/large_JE1.gif, and the two tropical-fish body shapes,
// Clownfish_TropicalFishA.png / OrnateButterfly_TropicalFishB.png). Also a
// resource now: killable for a species-specific raw fish item (see
// fishItemFor) that eats like a fruit, same "swing to damage, corpse lies
// for a beat, then grants the drop" convention animal.h's Animal uses.
enum FishSpecies {
  FISH_COD,
  FISH_SALMON,
  FISH_PUFFERFISH,
  FISH_TROPICAL,
  // The one real predator in open water: rare, only spawns in a genuinely
  // big body of water (see maintainFishSpawns), much bigger and tougher
  // than every other species, and — once provoked by a hit — chases and
  // bites the player back (see Fish::provoked, resolved in main.cpp the
  // same way a provoked land predator is in animal.h/main.cpp).
  FISH_SHARK,
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
  // Double, not int, same reason AnimalSpeciesDef::maxHealth is — tools.h's
  // attackPower comes in 0.5 steps. Tiny fish, so this stays low: even the
  // bare-handed swing (attackPower's floor) kills in a hit or two.
  double maxHealth;
  // Half-extents at scale=1.0 (fish.position is body-center, unlike
  // Animal's feet-anchored AABB) — raycastFish scales these by the fish's
  // own Fish::scale. Roughly matches each species' drawElongatedFish/
  // drawPufferfish body box in fish.cpp, not pixel-exact: it only needs to
  // be close enough that a shot aimed at the fish's body registers.
  double halfLength, halfWidth, halfHeight;
  // A bite's damage, 0 for every ambient species — only the shark ever
  // attacks back, so unlike AnimalSpeciesDef::predator this is a flat
  // per-species number rather than a formula off size.
  double attackPower;
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

  // Alternating slow-drift / fast-dart swim phase: speedPhaseMult multiplies
  // on top of speedMult, speedPhaseTimer counts down to the next phase
  // switch. Starts at 0 so the very first update() tick immediately rolls
  // an initial phase instead of swimming at 1x until the first switch.
  bool fastPhase = false;
  double speedPhaseMult = 1.0;
  double speedPhaseTimer = 0;

  // Tropical fish only: which color pattern (0..TROPICAL_PATTERN_COUNT-1)
  // and body shape (vanilla's "A" streamlined vs "B" tall/disc-bodied).
  int patternIndex = 0;
  bool tallShape = false;

  // Pufferfish only: inflates defensively when a player gets close, the way
  // the real mob does — smoothed 0..1 rather than a hard cut, so the swap
  // between the small and large reference renders reads as an animation.
  double puffAmount = 0;

  // Combat/death, same convention as Animal::health/dying/deathTimer: takes
  // damage from tryMine's swing, and once health reaches 0 stops swimming
  // (updateFish returns immediately) and lies dead in place for deathTimer
  // seconds before main.cpp grants the drop and removes it — a kill reads
  // as an event instead of an instant pop.
  double health = 1;
  bool dying = false;
  double deathTimer = 0;

  // Set to the session clock (main.cpp's g_elapsedTime) every time the
  // player lands a hit — main.cpp shows a floating health bar over the fish
  // while it's within 5 seconds of this timestamp, same convention
  // animal.h's Animal::lastHitTime uses. Starts far in the past so a fresh
  // spawn shows no bar.
  double lastHitTime = -1e9;

  // Shark only: set the instant the player lands a hit, cleared once
  // provokedTimer runs out without a fresh one. While provoked, updateFish
  // chases the player instead of ambient wandering; main.cpp resolves the
  // actual bite once close enough, gated by attackCooldown — same pattern
  // animal.h's Animal uses for a provoked land predator.
  bool provoked = false;
  double provokedTimer = 0;
  double attackCooldown = 0;

  // Set by main.cpp the instant a bite actually lands: a brief speed burst
  // in updateFish so the shark visibly charges at the player at the moment
  // of the attack, same convention animal.h's Animal::attackLungeTimer uses.
  double attackLungeTimer = 0;
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

// Ray-vs-AABB test against every non-dying fish in the list (each fish's box
// built from its species' half-extents scaled by its own Fish::scale), same
// reach convention and "closest hit within reach, or -1" contract as
// animal.h's raycastAnimal.
int raycastFish(const std::vector<Fish>& fishes, const Vec3& origin, const Vec3& dir,
                double reach);

// The CraftItem (recipes.h) a killed fish of this species drops — one raw
// fish per kill, unlike meatDropFor's size-scaled count, since each species
// is its own distinct item/icon rather than a shared "raw meat" stack.
// Returned as int (not the CraftItem enum's uint8_t) so fish.h doesn't need
// to include recipes.h just for this one declaration.
int fishItemFor(FishSpecies species);
