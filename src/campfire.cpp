#include "campfire.h"
#include "furnace.h" // drawFlame
#include "recipes.h" // ITEM_CAMPFIRE
#include "world.h"

void drawCampfireFires(World& world, double time) {
  for (auto& kv : world.edits) {
    if (kv.second != ITEM_CAMPFIRE) continue;
    const EditKey& pos = kv.first;
    // Dead centre of the block, resting right on top of the low pile — no
    // facing/opening to dodge the way a furnace's recessed firebox needs.
    drawFlame(pos.x + 0.5, pos.y + CAMPFIRE_HEIGHT, pos.z + 0.5, time);
  }
}
