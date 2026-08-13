#pragma once
#include "common.h"

enum Block : uint8_t {
  BLOCK_AIR = 0,
  BLOCK_GRASS = 1,
  BLOCK_DIRT = 2,
  BLOCK_STONE = 3,
  BLOCK_SAND = 4,
  BLOCK_WOOD = 5,
  BLOCK_LEAVES = 6,
  BLOCK_WATER = 7,
  BLOCK_BEDROCK = 8,
  BLOCK_SNOW = 9,
  BLOCK_ICE = 10,
  BLOCK_REDROCK = 11, // layered canyon rock (terracotta-like)
  BLOCK_TALL_GRASS = 12, // decorative plant: a cross of billboards, walk-through
  BLOCK_COAL = 13,       // black seams down in the stone; mine it, don't spawn with it
  // Decorative flowers scattered on grass, same cross-of-billboards shape as
  // tall grass (see isPlant) — different kinds, different colors, otherwise
  // identical mechanics (walk-through, mined for nothing).
  BLOCK_FLOWER_POPPY = 14,
  BLOCK_FLOWER_DANDELION = 15,
  BLOCK_FLOWER_DAISY = 16,
  BLOCK_FLOWER_CORNFLOWER = 17,
  // Ordinary leaves that happen to be carrying one fruit. A full solid cube
  // like BLOCK_LEAVES (not a plant) — the fruit is baked into the face
  // texture, not a separate object — so mining/meshing need no new cases,
  // only the fruit-specific harvest paths in main.cpp (see isFruitLeaves).
  BLOCK_LEAVES_APPLE = 18,
  BLOCK_LEAVES_PEACH = 19,
  BLOCK_LEAVES_PEAR = 20,
  BLOCK_LEAVES_CHERRY = 21,
  BLOCK_LEAVES_ORANGE = 22,
  BLOCK_TYPE_COUNT = 23,
};

// Atlas tile ids; order matches the tile-drawer order in textures.cpp.
enum Tile : int {
  TILE_NONE = -1,
  TILE_GRASS_TOP = 0,
  TILE_GRASS_SIDE,
  TILE_DIRT,
  TILE_STONE,
  TILE_SAND,
  TILE_WOOD_TOP,
  TILE_WOOD_SIDE,
  TILE_LEAVES,
  TILE_WATER,
  TILE_BEDROCK,
  TILE_SNOW,
  TILE_ICE,
  TILE_REDROCK,
  TILE_TALL_GRASS,
  TILE_COAL,
  TILE_FLOWER_POPPY,
  TILE_FLOWER_DANDELION,
  TILE_FLOWER_DAISY,
  TILE_FLOWER_CORNFLOWER,
  TILE_LEAVES_APPLE,
  TILE_LEAVES_PEACH,
  TILE_LEAVES_PEAR,
  TILE_LEAVES_CHERRY,
  TILE_LEAVES_ORANGE,

  // Everything from here on is a flat ITEM sprite rather than a block face:
  // crafted goods have no cube in the world but still need an icon for the
  // inventory slots (see craftItemTile in recipes.h). Keeping them in one
  // contiguous run means "is this a sprite?" stays a comparison rather than
  // a list that has to be maintained in parallel.
  TILE_FIRST_ITEM,
  TILE_PLANKS = TILE_FIRST_ITEM,
  TILE_STICK,
  TILE_CRAFTING_TABLE,
  TILE_CHEST,
  TILE_FURNACE,
  TILE_WOOD_SLAB,
  TILE_STONE_SLAB,
  TILE_WOOD_STAIRS,
  TILE_STONE_STAIRS,
  TILE_STONE_BRICKS,
  TILE_SANDSTONE,
  TILE_SNOW_BLOCK,
  TILE_PACKED_ICE,
  TILE_FENCE,
  TILE_DOOR,
  TILE_TRAPDOOR,
  TILE_LADDER,
  TILE_WOOD_PICKAXE,
  TILE_STONE_PICKAXE,
  TILE_WOOD_AXE,
  TILE_STONE_AXE,
  TILE_WOOD_SHOVEL,
  TILE_STONE_SHOVEL,
  TILE_WOOD_SWORD,
  TILE_STONE_SWORD,
  TILE_WOOD_HOE,
  TILE_STONE_HOE,
  TILE_BED,
  TILE_CAMPFIRE,
  TILE_RAW_MEAT,
  TILE_COOKED_MEAT,
  TILE_BOAT,
  TILE_SWORD, // hand-drawn (art\sword.png), not procedural — see textures.cpp
  TILE_APPLE,
  TILE_PEACH,
  TILE_PEAR,
  TILE_CHERRY,
  TILE_ORANGE,
  // Health potions, brewed from poppies (the red flower) — see
  // ITEM_HEALTH_POTION_SMALL/BIG in recipes.h. Two tiles, not tiers of one
  // shape: the small bottle is tall and narrow, the big one a short wide
  // flask, so the two read apart even at slot size (item_art.cpp).
  TILE_POTION_SMALL,
  TILE_POTION_BIG,
  // One icon per fish species (item_art.cpp) — a killed fish (fish.h) drops
  // ITEM_RAW_COD/SALMON/PUFFERFISH/RAW_TROPICAL_FISH, and each needs to read
  // apart from the others at a glance the same way the fruits do.
  TILE_RAW_COD,
  TILE_RAW_SALMON,
  TILE_RAW_PUFFERFISH,
  TILE_RAW_TROPICAL_FISH,
  TILE_RAW_SHARK,
  TILE_COOKED_FISH,

  // World textures for the crafted goods that can be placed. Separate from
  // the slot icons above: an icon is a small shape on a transparent field,
  // a block face has to cover the whole tile. Drawn from the same art table
  // (item_art.cpp) simply because it is the same machinery.
  TILE_BLK_PLANKS,
  TILE_BLK_BRICKS,
  TILE_BLK_SANDSTONE,
  TILE_BLK_CHEST,
  TILE_BLK_CHEST_TOP,
  TILE_BLK_FURNACE,
  TILE_BLK_FURNACE_TOP,
  TILE_BLK_TABLE_TOP,
  TILE_BLK_TABLE_SIDE,
  TILE_BLK_LADDER,
  TILE_BLK_DOOR,
  TILE_BLK_TRAPDOOR,
  TILE_BLK_BED_TOP,
  TILE_BLK_BED_SIDE,
  TILE_BLK_CAMPFIRE_TOP,
  TILE_BLK_CAMPFIRE_SIDE,
  TILE_COUNT,
};

struct BlockDef {
  const char* name;
  bool solid;
  bool water;
  bool minable;
  int texTop;    // Tile id per face, TILE_NONE if untextured (air)
  int texBottom;
  int texSide;
};

extern const BlockDef BLOCKS[BLOCK_TYPE_COUNT];

bool isSolid(uint8_t id);
bool isWater(uint8_t id);
// Blocks whose faces should be drawn even when touching an identical
// neighbor block (used for the water surface mesh).
bool isEmptyForMeshing(uint8_t id);
// face: 0 = top, 1 = bottom, 2 = side
int faceTexture(uint8_t blockId, int face);

// Crafted goods (ids above BLOCK_TYPE_COUNT) can all be carried, but only
// some are building material: planks or a chest belong in the world, a stick
// or a pickaxe does not. Placing something that is not placeable would leave
// an untextured, unmineable block behind, so the check happens before the
// item is spent.
bool isPlaceable(uint8_t id);

// Can this be dug up and collected? True for minable blocks AND for any
// placed crafted good — otherwise you could put one down and never get it
// back.
bool isMinable(uint8_t id);

// Blocks that are a thin PANEL fixed to a wall rather than a full cube — the
// ladder is the first. They do not fill their cell: you can walk through
// them, they never hide the block behind them, and the mesher builds them
// their own geometry instead of six cube faces. Which wall they hang on is
// worked out from the neighbours at mesh time, since a cell stores only an
// id and has nowhere to record a facing.
bool isPanel(uint8_t id);

// Stairs fill their cell only halfway: a full-width bottom slab plus a top
// slab on the half they rise toward. The mesher builds them that stepped
// shape and the physics gives them matching sub-cell collision boxes, so the
// player walks up and down them instead of treating them as a cube. Which
// half they rise toward comes from the neighbours (see World::stairFacing),
// since a cell stores only an id and has nowhere to record a facing.
bool isStairs(uint8_t id);

// Plants are drawn as crossed billboards rather than cubes, and never block
// a neighbouring block's face. Covers both tall grass and every flower kind.
bool isPlant(uint8_t id);

// The 4 flower kinds worldgen scatters on grass, for it to pick from.
const int FLOWER_KIND_COUNT = 4;
extern const uint8_t FLOWER_BLOCKS[FLOWER_KIND_COUNT];

// True for any of the 4 flower kinds specifically — unlike tall grass (also
// isPlant, but mined for nothing per Minecraft convention), a flower is a
// real collectible: mining one gives the flower itself back (see tryMine,
// main.cpp).
bool isFlower(uint8_t id);

// The 5 fruit-bearing leaves variants, in the same order as the CraftItem
// fruits they yield (see fruitItemForLeaves) — for worldgen to pick a kind
// when a tree is rolled to bear fruit.
const int FRUIT_KIND_COUNT = 5;
extern const uint8_t FRUIT_LEAF_BLOCKS[FRUIT_KIND_COUNT];

// True for a leaves block currently carrying a fruit (BLOCK_LEAVES_APPLE..
// BLOCK_LEAVES_ORANGE).
bool isFruitLeaves(uint8_t id);

// The CraftItem (recipes.h) a fruiting-leaves block yields when its fruit is
// picked, or -1 if `id` isn't one. Returned as int (not uint8_t) so -1 is a
// real out-of-band value rather than wrapping to 255.
int fruitItemForLeaves(uint8_t id);

// A chest: a squat box with a lid that opens (see ChestState in chest.h). The
// mesher bakes only the static body; the lid is drawn separately each frame
// so it can animate. Unlike a panel it still fills its cell for collision —
// you can't walk through a chest.
bool isChest(uint8_t id);

// A slab: the bottom half of a cell, full width. Sits low the way a real
// slab does — you can step onto one from beside it without jumping.
bool isSlab(uint8_t id);

// Both fence materials are the same shape now: a directional wall panel 2
// cells wide x 2 cells tall x a sub-cell THICK deep, standing wherever the
// player was facing when it was placed (see fencePanelFootprint below) —
// the same anchor+facing "furniture" idea as the table/bed, just spanning a
// vertical pair of cells instead of a horizontal one. Wood fills that box
// with 3 vertical pickets + 2 horizontal rails; the stone fence (a "wall")
// fills it solid.
const int FENCE_PANEL_WIDE = 2;
const int FENCE_PANEL_TALL = 2;
const double FENCE_PANEL_THICK = 1.0 / 3.0;

// A wood fence: pickets + rails within its panel box.
bool isFence(uint8_t id);

// A stone fence (a wall, in Minecraft terms): a solid slab filling its panel
// box instead of pickets and rails.
bool isStoneFence(uint8_t id);

// Either fence material — everywhere the two are treated identically
// (meshing exemptions, collision skip-lists, placement) this is the one to
// use; the mesher reaches for isFence/isStoneFence individually only to pick
// which shape (pickets vs. solid slab) to draw.
bool isAnyFence(uint8_t id);

// A door: a thin panel standing flush against whichever wall is beside it,
// same "read the facing off the neighbours" trick as a panel/ladder (see
// World::panelFacing, reused directly since the geometry problem is
// identical — a thin object flush against a solid neighbour).
bool isDoor(uint8_t id);

// A trapdoor: a thin panel lying flush with the floor, like a hatch.
bool isTrapdoor(uint8_t id);

// A furnace: a squat hollow box the same footprint as a chest, open on the
// face it was placed to front, with a fire lit inside toggled by pressing E
// (see FurnaceState in furnace.h). Unlike a panel it still fills its cell
// for collision, the same simplification a chest gets.
bool isFurnace(uint8_t id);

// A campfire: a low flat block, always lit, with a permanent flame on top —
// a second cook-station type alongside a lit furnace. Sub-cell height, like
// a slab (see boxCollidesCampfire in physics.cpp, which the mesher's shape
// and the collision box both read this same height from).
bool isCampfire(uint8_t id);
const double CAMPFIRE_HEIGHT = 0.4375; // 7/16 — a low pile, shorter than a slab's half-block

// The crafting table: a thin top on four legs rather than a cube, two cells
// long. A bed is the same idea at a bigger footprint: a flat mattress with a
// raised pillow, 3 cells long x 2 wide. Neither fits in one cell, so placing
// either stamps a whole footprint of cells at once (see tableFootprint,
// bedFootprint) rather than one block at a time — the first blocks in this
// game to claim more than one cell horizontally (a door already claims two,
// but stacked vertically). Every cell of the footprint stores the same block
// id and points back at one shared World::FurnitureState (world.h) keyed by
// an "anchor" cell, so any of them can be mined to remove the whole object.
// A fence panel (see isAnyFence) uses this same anchor+facing mechanism, just
// with a footprint that spans two cells vertically instead of horizontally.
bool isTable(uint8_t id);
bool isBed(uint8_t id);

const int TABLE_FOOTPRINT_CELLS = 2;
const int BED_FOOTPRINT_CELLS = 6;
const int FENCE_PANEL_FOOTPRINT_CELLS = 4;

// (dx,dy,dz) cell offsets from the anchor cell for a table/bed/fence-panel
// placed facing `facing` (0 -Z, 1 +Z, 2 -X, 3 +X —
// playerCardinalFacing()'s convention). The anchor is always the cell the
// player targeted: for the table it's one end of the 2-long strip that runs
// PERPENDICULAR to the player (so the long side faces them); for the bed
// it's the foot (nearest the player), 3 cells extending away with the
// 2-wide axis picked by rotating the facing 90 degrees; for a fence panel
// it's one bottom corner, 2 cells across (same perpendicular rotation) x 2
// cells up, with no offset along the facing axis at all — the panel is only
// a sub-cell THICK deep, not a second cell.
void tableFootprint(int facing, int out[TABLE_FOOTPRINT_CELLS][3]);
void bedFootprint(int facing, int out[BED_FOOTPRINT_CELLS][3]);
void fencePanelFootprint(int facing, int out[FENCE_PANEL_FOOTPRINT_CELLS][3]);

// Starting hotbar, in slot order: the collectible block types (grass blocks
// drop dirt, and tall grass drops nothing — neither has an item form).
const int HOTBAR_ORDER_LEN = 8;
extern const uint8_t HOTBAR_ORDER[HOTBAR_ORDER_LEN];
