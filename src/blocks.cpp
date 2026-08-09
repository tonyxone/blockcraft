#include "blocks.h"
#include "recipes.h" // the crafted goods that double as building material

const BlockDef BLOCKS[BLOCK_TYPE_COUNT] = {
  /* AIR     */ { "air",     false, false, false, TILE_NONE,      TILE_NONE,      TILE_NONE },
  /* GRASS   */ { "grass",   true,  false, true,  TILE_GRASS_TOP, TILE_DIRT,      TILE_GRASS_SIDE },
  /* DIRT    */ { "dirt",    true,  false, true,  TILE_DIRT,      TILE_DIRT,      TILE_DIRT },
  /* STONE   */ { "stone",   true,  false, true,  TILE_STONE,     TILE_STONE,     TILE_STONE },
  /* SAND    */ { "sand",    true,  false, true,  TILE_SAND,      TILE_SAND,      TILE_SAND },
  /* WOOD    */ { "wood",    true,  false, true,  TILE_WOOD_TOP,  TILE_WOOD_TOP,  TILE_WOOD_SIDE },
  /* LEAVES  */ { "leaves",  true,  false, true,  TILE_LEAVES,    TILE_LEAVES,    TILE_LEAVES },
  /* WATER   */ { "water",   false, true,  false, TILE_WATER,     TILE_WATER,     TILE_WATER },
  /* BEDROCK */ { "bedrock", true,  false, false, TILE_BEDROCK,   TILE_BEDROCK,   TILE_BEDROCK },
  /* SNOW    */ { "snow",    true,  false, true,  TILE_SNOW,      TILE_SNOW,      TILE_SNOW },
  /* ICE     */ { "ice",     true,  false, true,  TILE_ICE,       TILE_ICE,       TILE_ICE },
  /* REDROCK */ { "redrock", true,  false, true,  TILE_REDROCK,   TILE_REDROCK,   TILE_REDROCK },
  /* GRASS PLANT */ { "tall grass", false, false, true, TILE_TALL_GRASS, TILE_TALL_GRASS, TILE_TALL_GRASS },
  /* COAL    */ { "coal",    true,  false, true,  TILE_COAL,      TILE_COAL,      TILE_COAL },
};

// Crafted goods that can be placed in the world. A few (sandstone, packed
// ice...) are still a full cube wearing the texture of the material they
// were cut from; most of the rest have their own sub-cell geometry (see
// mesher.cpp: isPanel, isStairs, isSlab, isAnyFence, isDoor, isTrapdoor,
// isFurnace, isTable) but all of them still take their face textures from
// this table.
struct CraftBlockDef {
  uint8_t item;
  int texTop, texBottom, texSide;
};
static const CraftBlockDef CRAFT_BLOCKS[] = {
  { ITEM_PLANKS, TILE_BLK_PLANKS, TILE_BLK_PLANKS, TILE_BLK_PLANKS },
  { ITEM_CRAFTING_TABLE, TILE_BLK_TABLE_TOP, TILE_BLK_PLANKS, TILE_BLK_TABLE_SIDE },
  { ITEM_CHEST, TILE_BLK_CHEST_TOP, TILE_BLK_CHEST_TOP, TILE_BLK_CHEST },
  { ITEM_FURNACE, TILE_BLK_FURNACE_TOP, TILE_BLK_FURNACE_TOP, TILE_BLK_FURNACE },
  // A slab is a SHAPE this engine cannot cut, so it keeps its material's own
  // look as a full cube rather than pretending with a texture. Stairs once
  // shared that limitation; they now mesh as real steps (see mesher.cpp).
  { ITEM_WOOD_SLAB, TILE_BLK_PLANKS, TILE_BLK_PLANKS, TILE_BLK_PLANKS },
  { ITEM_STONE_SLAB, TILE_STONE, TILE_STONE, TILE_STONE },
  { ITEM_WOOD_STAIRS, TILE_BLK_PLANKS, TILE_BLK_PLANKS, TILE_BLK_PLANKS },
  { ITEM_STONE_STAIRS, TILE_STONE, TILE_STONE, TILE_STONE },
  { ITEM_STONE_BRICKS, TILE_BLK_BRICKS, TILE_BLK_BRICKS, TILE_BLK_BRICKS },
  { ITEM_SANDSTONE, TILE_BLK_SANDSTONE, TILE_BLK_SANDSTONE, TILE_BLK_SANDSTONE },
  { ITEM_SNOW_BLOCK, TILE_SNOW, TILE_SNOW, TILE_SNOW },
  { ITEM_PACKED_ICE, TILE_ICE, TILE_ICE, TILE_ICE },
  // The panel is real 3D pickets/rails now (mesher.cpp addFencePanel), not a
  // flat texture painting fake gaps, so it just wears plain plank color.
  { ITEM_FENCE, TILE_BLK_PLANKS, TILE_BLK_PLANKS, TILE_BLK_PLANKS },
  { ITEM_DOOR, TILE_BLK_PLANKS, TILE_BLK_PLANKS, TILE_BLK_DOOR },
  { ITEM_TRAPDOOR, TILE_BLK_TRAPDOOR, TILE_BLK_TRAPDOOR, TILE_BLK_PLANKS },
  { ITEM_LADDER, TILE_BLK_PLANKS, TILE_BLK_PLANKS, TILE_BLK_LADDER },
  { ITEM_BED, TILE_BLK_BED_TOP, TILE_BLK_PLANKS, TILE_BLK_BED_SIDE },
  { ITEM_CAMPFIRE, TILE_BLK_CAMPFIRE_TOP, TILE_BLK_CAMPFIRE_SIDE, TILE_BLK_CAMPFIRE_SIDE },
};

static const CraftBlockDef* craftBlockFor(uint8_t id) {
  for (const CraftBlockDef& c : CRAFT_BLOCKS) {
    if (c.item == id) return &c;
  }
  return nullptr;
}

bool isPlaceable(uint8_t id) {
  if (id == BLOCK_AIR) return false;
  if (id < BLOCK_TYPE_COUNT) return BLOCKS[id].solid;
  return craftBlockFor(id) != nullptr;
}

bool isMinable(uint8_t id) {
  if (id < BLOCK_TYPE_COUNT) return id != BLOCK_AIR && BLOCKS[id].minable;
  return craftBlockFor(id) != nullptr;
}

bool isPanel(uint8_t id) { return id == ITEM_LADDER; }

bool isStairs(uint8_t id) { return id == ITEM_WOOD_STAIRS || id == ITEM_STONE_STAIRS; }

bool isSolid(uint8_t id) {
  if (isPanel(id)) return false; // a thin panel does not fill its cell
  if (id >= BLOCK_TYPE_COUNT) return craftBlockFor(id) != nullptr;
  return id != BLOCK_AIR && BLOCKS[id].solid;
}

bool isWater(uint8_t id) {
  return id < BLOCK_TYPE_COUNT && BLOCKS[id].water;
}

bool isPlant(uint8_t id) {
  return id == BLOCK_TALL_GRASS;
}

bool isChest(uint8_t id) { return id == ITEM_CHEST; }

bool isSlab(uint8_t id) { return id == ITEM_WOOD_SLAB || id == ITEM_STONE_SLAB; }

bool isFence(uint8_t id) { return id == ITEM_FENCE; }

// The "Stone Bricks" recipe now builds a connecting stone fence (a wall)
// rather than a plain block — same id (ITEM_STONE_BRICKS) so old saves still
// load, just reshaped and renamed (see recipes.cpp craftItemName).
bool isStoneFence(uint8_t id) { return id == ITEM_STONE_BRICKS; }

bool isAnyFence(uint8_t id) { return isFence(id) || isStoneFence(id); }

bool isDoor(uint8_t id) { return id == ITEM_DOOR; }

bool isTrapdoor(uint8_t id) { return id == ITEM_TRAPDOOR; }

bool isFurnace(uint8_t id) { return id == ITEM_FURNACE; }

// A campfire: unlike a furnace it's always "lit" once placed (no E-toggle
// state), a simple low block with a permanent flame centred on top — a
// second valid heat source for cooking (main.cpp's R-key cook action)
// alongside a lit furnace.
bool isCampfire(uint8_t id) { return id == ITEM_CAMPFIRE; }

bool isTable(uint8_t id) { return id == ITEM_CRAFTING_TABLE; }

bool isBed(uint8_t id) { return id == ITEM_BED; }

void tableFootprint(int facing, int out[TABLE_FOOTPRINT_CELLS][3]) {
  // The anchor is where the player targeted; the strip runs along the
  // PERPENDICULAR axis (a 90-degree rotation of facing, same trick
  // bedFootprint uses for its width) so the table's long edge runs
  // left-right in front of the player instead of away from them — the long
  // side faces whoever placed it.
  const int DX[4] = { 0, 0, -1, 1 };
  const int DZ[4] = { -1, 1, 0, 0 };
  int lx = DX[facing], lz = DZ[facing];
  int wx = -lz, wz = lx; // perpendicular to facing
  out[0][0] = 0;
  out[0][1] = 0;
  out[0][2] = 0;
  out[1][0] = wx;
  out[1][1] = 0;
  out[1][2] = wz;
}

void bedFootprint(int facing, int out[BED_FOOTPRINT_CELLS][3]) {
  // Anchor = foot, nearest the player. 3 cells extend away along `facing`;
  // the 2-wide axis picks a fixed side via a 90-degree rotation of facing so
  // the choice is deterministic rather than arbitrary. Vertical offset is
  // always 0 — a bed is one storey.
  const int DX[4] = { 0, 0, -1, 1 };
  const int DZ[4] = { -1, 1, 0, 0 };
  int lx = DX[facing], lz = DZ[facing];    // long axis: away from the player
  int wx = -lz, wz = lx;                   // wide axis: 90 degrees from it
  int i = 0;
  for (int w = 0; w < 2; w++) {
    for (int l = 0; l < 3; l++) {
      out[i][0] = lx * l + wx * w;
      out[i][1] = 0;
      out[i][2] = lz * l + wz * w;
      i++;
    }
  }
}

void fencePanelFootprint(int facing, int out[FENCE_PANEL_FOOTPRINT_CELLS][3]) {
  // Anchor = the cell the player targeted (bottom of one side). The panel is
  // 1 cell deep along `facing` (its thin axis, sub-cell — no offset needed),
  // 2 cells across the perpendicular axis, 2 cells tall.
  const int DX[4] = { 0, 0, -1, 1 };
  const int DZ[4] = { -1, 1, 0, 0 };
  int lx = DX[facing], lz = DZ[facing];
  int wx = -lz, wz = lx; // perpendicular to facing
  int i = 0;
  for (int wc = 0; wc < 2; wc++) {
    for (int yc = 0; yc < 2; yc++) {
      out[i][0] = wx * wc;
      out[i][1] = yc;
      out[i][2] = wz * wc;
      i++;
    }
  }
}

bool isEmptyForMeshing(uint8_t id) {
  // Plants, panels, stairs, chests and every sub-cell furniture shape count
  // as empty: a block beside grass, behind a ladder, sharing a side with a
  // stair, or next to any of these must still draw that face — none of them
  // covers the whole cell, so culling it would leave a hole (or, for a squat
  // shape like a chest or furnace, a sliver gap under a block placed on top
  // of it) in the terrain behind.
  return id == BLOCK_AIR || id == BLOCK_WATER || isPlant(id) || isPanel(id) || isStairs(id) ||
         isChest(id) || isSlab(id) || isAnyFence(id) || isDoor(id) || isTrapdoor(id) ||
         isFurnace(id) || isTable(id) || isBed(id) || isCampfire(id);
}

int faceTexture(uint8_t blockId, int face) {
  if (blockId >= BLOCK_TYPE_COUNT) {
    // A placed crafted good wears its material's texture. Without this the
    // mesher got no UVs and drew the faces with whatever was bound last,
    // which is where the stray green blocks came from.
    const CraftBlockDef* c = craftBlockFor(blockId);
    if (!c) return TILE_NONE;
    switch (face) {
      case 0: return c->texTop;
      case 1: return c->texBottom;
      default: return c->texSide;
    }
  }
  const BlockDef& def = BLOCKS[blockId];
  switch (face) {
    case 0: return def.texTop;
    case 1: return def.texBottom;
    default: return def.texSide;
  }
}

const uint8_t HOTBAR_ORDER[HOTBAR_ORDER_LEN] = {
  BLOCK_DIRT, BLOCK_STONE, BLOCK_SAND, BLOCK_WOOD, BLOCK_LEAVES,
  BLOCK_ICE, BLOCK_REDROCK, BLOCK_SNOW,
};
