#pragma once
#include "common.h"

class World;

// Draws the permanent flame on top of every placed campfire. Unlike a
// furnace a campfire has no lit/unlit state (see isCampfire, blocks.h) — if
// it exists, it's burning. `time` drives the same flicker drawFlame
// (furnace.h) always has. Call with the block atlas not bound and depth
// test/cull face in their normal 3D-pass state.
void drawCampfireFires(World& world, double time);
