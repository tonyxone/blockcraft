#pragma once
#include "blocks.h"

// Crafting recipe list, following Minecraft's real grid shapes.
//
// Ingredients are ordinary block ids where the game already has the block
// (wood, stone, sand, snow, ice...). Recipes also produce goods the world
// generator never places — planks, sticks, tools and so on — which get ids
// above BLOCK_TYPE_COUNT. Those are inventory items only: they can be
// crafted, stacked and carried today, but a CraftItem above the block range
// has no cube in the world, so placing one must stay disabled until it is
// given a block entry and a texture tile.

// Crafted goods are numbered from a FIXED base well clear of the block range,
// not from BLOCK_TYPE_COUNT. Save files store raw ids, so basing them on the
// block count meant adding one new block silently renumbered every crafted
// item — a saved pickaxe would load back as something else. The gap between
// the last block and this base is deliberate headroom for new blocks.
const uint8_t CRAFT_ITEM_BASE = 64;

enum CraftItem : uint8_t {
  // --- materials -------------------------------------------------------
  ITEM_PLANKS = CRAFT_ITEM_BASE, // 4 per log, the base of most wood recipes
  ITEM_STICK,
  // --- utility blocks --------------------------------------------------
  ITEM_CRAFTING_TABLE,
  ITEM_CHEST,
  ITEM_FURNACE,
  ITEM_CAMPFIRE,
  // --- building --------------------------------------------------------
  ITEM_WOOD_SLAB,
  ITEM_STONE_SLAB,
  ITEM_WOOD_STAIRS,
  ITEM_STONE_STAIRS,
  ITEM_STONE_BRICKS,
  ITEM_SANDSTONE,
  ITEM_SNOW_BLOCK,
  ITEM_PACKED_ICE,
  ITEM_FENCE,
  ITEM_DOOR,
  ITEM_TRAPDOOR,
  ITEM_LADDER,
  ITEM_BED,
  // --- food --------------------------------------------------------------
  // Neither is a CRAFT_RECIPES entry — raw meat drops from a killed animal
  // (main.cpp), cooked meat comes from raw meat + a heat source (see the
  // R-key cook action, also main.cpp), not the crafting grid.
  ITEM_RAW_MEAT,
  ITEM_COOKED_MEAT,
  // --- tools -----------------------------------------------------------
  ITEM_WOOD_PICKAXE,
  ITEM_STONE_PICKAXE,
  ITEM_WOOD_AXE,
  ITEM_STONE_AXE,
  ITEM_WOOD_SHOVEL,
  ITEM_STONE_SHOVEL,
  ITEM_WOOD_SWORD,
  ITEM_STONE_SWORD,
  ITEM_WOOD_HOE,
  ITEM_STONE_HOE,
  // --- vehicles ----------------------------------------------------------
  // Not a CRAFT_BLOCKS entry: a boat has no cell in the world grid at all —
  // placing one spawns a free-floating Boat (boat.h) directly on the water
  // it's aimed at, rather than filling a block. See main.cpp's tryPlaceBoat.
  ITEM_BOAT,
  // A one-off weapon with its own hand-drawn icon (art\sword.png) rather
  // than a wood/stone tier of the pickaxe-style progression above — see
  // TOOL_VISUALS in tools.cpp for the 3D geometry it's held with.
  ITEM_SWORD,
  CRAFT_ITEM_COUNT,
};

// One entry of the craft list. `rows` is the 3x3 grid read top-to-bottom,
// three characters per row: '.' is an empty slot, any other character is a
// key looked up in `keys`. Shaped recipes may sit anywhere in the grid and
// match mirrored, exactly as Minecraft does; shapeless ones ignore layout.
struct Recipe {
  const char* name;
  uint8_t output;
  int outputCount;
  bool shapeless;
  const char* rows[3];
  struct KeyEntry {
    char key;
    uint8_t item;
  } keys[4];
};

extern const Recipe CRAFT_RECIPES[];
extern const int CRAFT_RECIPE_COUNT;

// Matches a 3x3 grid of ingredient ids (row-major, 9 entries; use
// BLOCK_AIR for an empty slot) against the list. Returns the recipe, or
// nullptr when the grid makes nothing.
const Recipe* findRecipe(const uint8_t grid[9]);

// Matches by TOTAL ingredient quantity instead of layout: what matters is
// that the nine cells hold exactly the amounts some recipe asks for, no
// matter which cells they sit in or how they are stacked.
//
// A few recipes differ ONLY in layout and so share a quantity signature
// (wood stairs / door / trapdoor are all 6 planks; pickaxe and axe of a tier
// are both 3 material + 2 sticks). When several match, one whose layout also
// fits the grid wins, so those items stay reachable by placing them properly;
// otherwise the first is taken. `ambiguous` reports that case for callers
// that want to explain it.
const Recipe* findRecipeByCount(const uint8_t items[9], const int counts[9],
                                bool* ambiguous = nullptr);

// Display name for any ingredient or crafted good.
const char* craftItemName(uint8_t id);

// The distinct ingredients a recipe needs, with how many grid cells each
// fills — "1 wood + 2 stone" rather than the raw 3x3 pattern. Written into
// `items`/`counts` in first-appearance order; returns how many were written
// (never more than RECIPE_MAX_INGREDIENTS). For the recipe list UI.
const int RECIPE_MAX_INGREDIENTS = 4;
int recipeIngredients(const Recipe& r, uint8_t items[RECIPE_MAX_INGREDIENTS],
                      int counts[RECIPE_MAX_INGREDIENTS]);

// Atlas tile to draw in an inventory/hotbar slot for any ingredient or
// crafted good. Blocks use their top face, exactly as the slots always did;
// crafted goods (which have no cube in the world, so faceTexture gives them
// nothing) get a sprite instead — a dedicated one where it exists, otherwise
// the tile of the material they are made from, so no item is ever invisible.
// Giving a new item dedicated art is a one-line change here plus its tile.
int craftItemTile(uint8_t id);
