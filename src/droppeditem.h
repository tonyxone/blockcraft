#pragma once
#include "common.h"

class World;

// An item tossed into the world — from the inventory's right-click "Drop"
// button, or from dragging a stack out past the inventory panel's edge
// (main.cpp is what actually spawns one; Inventory only stages the request,
// same pattern as pendingEatAmount). Falls under gravity using the same
// boxCollides family every other physics object here uses, so it settles on
// whatever solid surface is beneath it — floor or table alike, no
// special-casing either, since a table already has its own solid top for
// things to land on. Not persisted across a save (like animals): dropped
// items don't survive a reload.
struct DroppedItem {
  int itemId = -1;
  int count = 1;
  Vec3 position;
  Vec3 velocity;
  bool onGround = false;
  double bobPhase = 0;      // per-item offset so a pile doesn't bob in lockstep
  double pickupDelay = 0.4; // seconds before it's eligible for pickup (main.cpp's
                            // nearestDroppedItem/tryPickUpItem, E key) — long enough
                            // that the item you just tossed doesn't immediately
                            // show "Press E to pick up" while still leaving your hand
};

// Advances one item's toss/fall physics one frame.
void updateDroppedItem(DroppedItem& item, World& world, double dt);

// Draws every dropped item as a small stationary 3D object — the item's own
// inventory icon extruded into a thin voxel relief (one shaded cube per
// opaque pixel), the same "chunky pixel art" look Minecraft's own item
// entities have. Built once per item type and cached as a display list, so
// steady state is just a translate + glCallList per item. Does not spin or
// billboard: it is real geometry, so it reads correctly from any angle at
// rest, the way an object actually sitting on the ground should.
void drawDroppedItems(const std::vector<DroppedItem>& items, double time);
