#pragma once
#include "common.h"

class World;

// Ten passive species modeled after the classic Minecraft box-mob look
// (body + head + legs as separate flat-shaded cuboids — see animal.cpp's
// drawColorBox, the same per-face directional shading every hand-placed box
// in this game already uses). Chicken is the only biped; everything else
// shares one quadruped renderer, parameterized per species.
enum AnimalSpecies {
  ANIMAL_PIG,
  ANIMAL_COW,
  ANIMAL_CHICKEN,
  ANIMAL_SHEEP,
  ANIMAL_WOLF,
  ANIMAL_RABBIT,
  ANIMAL_OCELOT,
  ANIMAL_CAT,
  ANIMAL_PANDA,
  ANIMAL_POLAR_BEAR,
  ANIMAL_SPECIES_COUNT,
};

// Per-species stats, exposed so the spawner/combat code in main.cpp can read
// health and collision size without duplicating them.
struct AnimalSpeciesDef {
  const char* name;
  // Double, not int: attack power (tools.h) comes in 0.5 steps, so health
  // needs to too. Strictly increasing with size (spawnWeight below is
  // already correctly size-ordered — bigger animal, smaller weight — so
  // it's the axis health is keyed off, rather than re-deriving from the
  // wiki again: the original researched-per-species values didn't stay
  // monotonic against this game's OWN rendered model sizes, e.g. cat/ocelot
  // ended up with more health than the visibly-bigger wolf).
  double maxHealth;
  double spawnWeight; // relative population density — bigger animals, smaller weight
  // Which of this game's 4 biomes (worldgen.h's columnInfoAt order: 0
  // plains, 1 desert, 2 canyon, 3 snow) this species can spawn in. This
  // game has no jungle/forest/taiga, so vanilla's jungle-only mobs
  // (ocelot/cat/panda) map to plains as the closest analog; polar bear
  // (snow-only), desert/snow-tolerant rabbit, and canyon-roaming wolf are
  // where real differentiation shows up.
  bool biomes[4];
  double halfWidth, height; // collision AABB, same convention as PLAYER_HALF_WIDTH/HEIGHT
};
extern const AnimalSpeciesDef ANIMAL_SPECIES[ANIMAL_SPECIES_COUNT];

// Raw meat dropped on death, sized the same way health is (§4's tiers):
// rabbit/chicken/cat/ocelot 1, sheep/pig/wolf 2, cow/panda 3, polar bear 4.
int meatDropFor(AnimalSpecies species);

struct Animal {
  AnimalSpecies species = ANIMAL_PIG;
  Vec3 position; // feet position, like Player::position
  Vec3 velocity;
  double yaw = 0;
  double health = 1;
  bool onGround = false;
  // Simple wander AI state: `moving` while `wanderTimer` counts down, then a
  // new bout (walk in a new direction, or stand still) is picked.
  bool moving = false;
  double wanderTimer = 0;
  double targetYaw = 0;
  double animPhase = 0; // leg-swing phase, advances while moving on the ground

  // Death sequence: once health reaches 0 the animal isn't removed right
  // away — it lies down for a few seconds (see drawAnimals) before actually
  // leaving the world, so a kill reads as an event instead of a pop.
  bool dying = false;
  double deathTimer = 0;
};

// Advances one animal's simple wander/jump AI and physics one frame. Uses
// the same boxCollides family (physics.h) the player does, against a
// per-species AABB, so animals collide with terrain identically.
void updateAnimal(Animal& animal, World& world, double dt);

// Draws every animal in the list, flat-shaded (no texture atlas — see
// animal.cpp). Handles its own GL_TEXTURE_2D disable/re-enable so callers
// can't forget to restore it (exactly the bug the furnace flame had).
void drawAnimals(const std::vector<Animal>& animals);

// Tops up each species toward a target population (from its spawnWeight)
// within `renderDistance` chunks of (px,pz) — a handful of attempts per
// call, biome- and ground-checked via World — and prunes any animal that's
// drifted well outside that range. Call every few seconds, not every frame;
// total active animals is capped to bound cost (rendering here is
// immediate-mode with no culling/instancing).
void maintainAnimalSpawns(World& world, std::vector<Animal>& animals, double px, double pz,
                          int renderDistance);

// Ray-vs-AABB test against every animal in the list, same reach convention
// as the block raycast. Returns the index of the closest hit within reach,
// or -1.
int raycastAnimal(const std::vector<Animal>& animals, const Vec3& origin, const Vec3& dir,
                  double reach);
