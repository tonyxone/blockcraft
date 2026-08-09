#pragma once
#include "common.h"

class World;

// A door's swing state and which wall it's flush against when closed. A
// door is two cells tall; state is stored once, keyed by its BOTTOM cell —
// the top cell has no entry of its own, and readers that land on the top
// half check the cell below for it (see drawDoors, boxCollidesDoor). A door
// nobody has toggled (or loaded from an old save) has no entry in
// World::doors, and reads as closed/facing +Z.
struct DoorState {
  int facing = 1;        // flush against which wall when closed: 0 -Z,1 +Z,2 -X,3 +X
  bool open = false;     // target the swing eases toward
  double swingAngle = 0; // 0 closed .. 1 open (90 degrees)
};

// Eases every door's swing toward its target (open/closed). Call once a
// frame with the real frame dt.
void updateDoorAnimations(World& world, double dt);

// Draws every door — both cells of it, as one swinging panel. No static
// geometry is baked for a door (see mesher.cpp: isDoor is a no-op there), so
// this is the only place a door is drawn; call with the block atlas already
// bound and depth test/cull face in their normal 3D-pass state.
void drawDoors(World& world);
