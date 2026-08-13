#pragma once
#include "world.h"

const double PLAYER_HALF_WIDTH = 0.3;
const double PLAYER_HEIGHT = 1.8;
const double EYE_HEIGHT = 1.62;

// True if the axis-aligned box [x-hw,x+hw] x [y,y+height] x [z-hw,z+hw]
// overlaps any solid block.
bool boxCollides(World& world, double x, double y, double z, double halfWidth, double height);

// True if that same box overlaps a stair's stepped body: a full-cell bottom
// slab half a block tall, plus a top slab on the half the stair rises toward
// (the same two boxes the mesher draws). boxCollides skips stair cells, so
// both checks are needed; together with the auto-step in Player::update this
// is what makes a staircase walkable instead of a wall.
bool boxCollidesStairs(World& world, double x, double y, double z, double halfWidth, double height);

// True if that same box overlaps a climbable panel (a ladder), so the player
// can grab on instead of falling.
bool touchingLadder(World& world, double x, double y, double z, double halfWidth, double height);

// True if any part of the box overlaps a water block — Player::update reads
// this to switch into swim physics (see Player::swimming).
bool touchingWater(World& world, double x, double y, double z, double halfWidth, double height);

// True if the box overlaps a ladder's own thin body (it stands ~0.14 blocks
// proud of the wall it hangs on). Checked on the X/Z axes only: it stops the
// player from walking straight through the rails from the open side, while
// leaving the Y axis alone so climbing through the same body still works.
bool boxCollidesLadder(World& world, double x, double y, double z, double halfWidth, double height);

// True if the box overlaps any of the sub-cell "furniture" shapes — a slab,
// fence panel, door panel or trapdoor — none of which boxCollides treats as
// a full cube (it skips them, the same way it skips stairs). One combined
// call so every movement check only has to add a single extra condition;
// see Player::update.
bool boxCollidesSubCell(World& world, double x, double y, double z, double halfWidth, double height);
