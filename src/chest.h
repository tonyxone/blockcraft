#pragma once
#include "hotbar.h"

class World; // world.h includes this header for ChestState; avoid the cycle

const int CHEST_SLOT_COUNT = 9;

// A chest is smaller than a full cell, centred in its footprint: the body
// (baked into the chunk mesh by mesher.cpp) spans this X/Z square from
// y=0 up to CHEST_HINGE_Y, and the lid (drawn separately by chest.cpp, so it
// can animate) is a CHEST_LID_H-thick slab sitting flush on top of it,
// hinged along its -Z edge. Shared by both so the two pieces always line up.
const double CHEST_HINGE_X = 0.125, CHEST_HINGE_Z = 0.125;
const double CHEST_W = 0.75, CHEST_D = 0.75;   // footprint, both smaller than a full block
const double CHEST_HINGE_Y = 0.5;              // body height / lid's resting Y
const double CHEST_LID_H = 0.25;               // lid thickness

// One placed chest's contents, facing and lid animation. A chest nobody has
// interacted with (or loaded from an old save) simply has no entry in
// World::chests, and reads as empty/closed/facing +Z (see drawChestLids).
struct ChestState {
  Hotbar::Slot slots[CHEST_SLOT_COUNT];
  bool open = false;   // target the lid eases toward
  double lidAngle = 0; // 0 closed .. 1 open
  // Which way the lid opens — the front (the edge that lifts) faces this
  // direction, set from the player's facing at placement: 0 -Z, 1 +Z, 2 -X,
  // 3 +X. The hinge sits on the opposite edge.
  int facing = 1;
};

// True if none of the chest's slots hold anything.
bool chestIsEmpty(const ChestState& c);

// Eases every chest's lid toward its target (open/closed). Call once a frame
// with the real frame dt.
void updateChestAnimations(World& world, double dt);

// Draws the animated lid of every chest in world.chests. The mesher already
// baked each chest's static body into its chunk's opaque list; call this
// after that list is drawn, with the block atlas already bound (it uses the
// same texture) and depth test/cull face in their normal 3D-pass state.
void drawChestLids(World& world);
