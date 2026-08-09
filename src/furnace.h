#pragma once
#include "common.h"

class World;

// A furnace's lit state and which side its opening fronts. A furnace nobody
// has toggled (or loaded from an old save) has no entry in World::furnaces,
// and reads as unlit/facing +Z — same defaults a fresh chest gets.
struct FurnaceState {
  bool lit = false;
  int facing = 1; // 0 -Z, 1 +Z, 2 -X, 3 +X — same convention as ChestState::facing
};

// Draws the fire inside every lit furnace. The mesher bakes the static
// hollow body (mesher.cpp's addFurnaceBody); this is the only part redrawn
// every frame, so a furnace can be toggled on/off (E key) without a remesh.
// `time` drives a small flicker — pass any steadily increasing clock. Call
// with the block atlas not bound and depth test/cull face in their normal
// 3D-pass state (the flame is untextured and alpha-blended).
void drawFurnaceFires(World& world, double time);

// A single flame billboard centred at (cx,cy,cz) — the same tapered,
// swaying, three-colour blaze a lit furnace shows, exposed so campfire.cpp
// can reuse it directly instead of duplicating it (a campfire's flame is
// visually identical, just centred over open ground instead of a hollow
// firebox mouth). Same call-site contract as drawFurnaceFires.
void drawFlame(double cx, double cy, double cz, double time);
