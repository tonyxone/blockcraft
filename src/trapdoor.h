#pragma once
#include "common.h"

class World;

// A trapdoor's swing state and which edge it's hinged on. Unlike a door this
// is a TRAP, not hand-toggled: something standing on the cell above springs
// it open (see main.cpp's per-frame check), and it swings shut again on its
// own a couple of seconds after nothing is passing through it any more —
// long enough to guarantee whoever sprung it actually has time to drop.
// Keyed by its own cell (a trapdoor is one cell, unlike a door's two) in
// World::trapdoors. A trapdoor nobody has stood on yet has no entry, and
// reads as closed/facing 0.
struct TrapdoorState {
  bool open = false;
  int facing = 0;        // which floor edge it's hinged on: 0 -Z,1 +Z,2 -X,3 +X
  double swingAngle = 0; // 0 closed (flat) .. 1 open (hanging straight down)
  double closeTimer = 0; // seconds left before an unused open trapdoor swings shut
};

// Eases every known trapdoor's swing toward its target (open/closed). Call
// once a frame with the real frame dt.
void updateTrapdoorAnimations(World& world, double dt);

// Draws every trapdoor as a hinged panel. No static geometry is baked for a
// trapdoor (see mesher.cpp: isTrapdoor is a no-op there), so this is the
// only place one is drawn; call with the block atlas already bound and
// depth test/cull face in their normal 3D-pass state.
void drawTrapdoors(World& world);
