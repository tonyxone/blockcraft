#include "recipes.h"

// Grid shapes below are Minecraft's own (verified against the wiki's
// crafting pages). Where a canonical recipe needs a resource this game has
// no source for — torches (need coal or charcoal AND a stick, and this
// game's furnace can't char wood into charcoal), iron, wool — it is left
// out rather than invented; see the notes at the bottom of the file.
//
// Keys used in the patterns:
//   P planks    S stick     W wood/log   C stone
//   A sand      N snow      I ice        L wood slab
//   K coal

const Recipe CRAFT_RECIPES[] = {
  // --- materials -------------------------------------------------------
  { "Planks", ITEM_PLANKS, 4, true,
    { "W..", "...", "..." }, { { 'W', BLOCK_WOOD } } },

  { "Sticks", ITEM_STICK, 4, false,
    { "P..", "P..", "..." }, { { 'P', ITEM_PLANKS } } },

  // --- utility blocks --------------------------------------------------
  { "Crafting Table", ITEM_CRAFTING_TABLE, 1, false,
    { "PP.", "PP.", ".S." }, { { 'P', ITEM_PLANKS }, { 'S', ITEM_STICK } } },

  { "Chest", ITEM_CHEST, 1, false,
    { "PP.", "PP.", "..." }, { { 'P', ITEM_PLANKS } } },

  { "Furnace", ITEM_FURNACE, 1, false,
    { "CCC", "CKC", "CCC" }, { { 'C', BLOCK_STONE }, { 'K', BLOCK_COAL } } },

  // A second cook station: logs for the pile, coal for the fuel — "woods
  // and coals," both plural.
  { "Campfire", ITEM_CAMPFIRE, 1, false,
    { "WWW", "KK.", "..." }, { { 'W', BLOCK_WOOD }, { 'K', BLOCK_COAL } } },

  // --- building --------------------------------------------------------
  // Every recipe below makes exactly one of the thing it names — no
  // multi-output batches to keep track of — so its cost is the price of
  // ONE, not a batch's worth split three ways. Door and Trapdoor no longer
  // share an identical total (6 planks each, the game's own old sore
  // point) — each shape below asks for whatever a single one of it
  // plausibly costs. A couple still share a total with something else
  // (Wood Stairs/Door both 2 planks, Stone Stairs/Stone Fence both 2
  // stone) — same as Pickaxe/Axe already did, that's fine: the shape you
  // actually placed breaks the tie (see findRecipeByCount), so each just
  // needs its OWN distinct shape, not a unique total.
  { "Wood Slab", ITEM_WOOD_SLAB, 1, false,
    { "P..", "...", "..." }, { { 'P', ITEM_PLANKS } } },

  { "Stone Slab", ITEM_STONE_SLAB, 1, false,
    { "C..", "...", "..." }, { { 'C', BLOCK_STONE } } },

  // Diagonal, not the vertical pair Sticks already claims.
  { "Wood Stairs", ITEM_WOOD_STAIRS, 1, false,
    { "P..", ".P.", "..." }, { { 'P', ITEM_PLANKS } } },

  { "Stone Stairs", ITEM_STONE_STAIRS, 1, false,
    { "C..", ".C.", "..." }, { { 'C', BLOCK_STONE } } },

  // A connecting stone fence (a "wall", in Minecraft terms) rather than a
  // plain block — see isStoneFence in blocks.h.
  { "Stone Fence", ITEM_STONE_BRICKS, 1, false,
    { "CC.", "...", "..." }, { { 'C', BLOCK_STONE } } },

  { "Sandstone", ITEM_SANDSTONE, 1, false,
    { "AA.", "AA.", "..." }, { { 'A', BLOCK_SAND } } },

  { "Snow Block", ITEM_SNOW_BLOCK, 1, false,
    { "NN.", "NN.", "..." }, { { 'N', BLOCK_SNOW } } },

  { "Packed Ice", ITEM_PACKED_ICE, 1, false,
    { "III", "III", "III" }, { { 'I', BLOCK_ICE } } },

  { "Fence", ITEM_FENCE, 1, false,
    { "PSP", "...", "..." }, { { 'P', ITEM_PLANKS }, { 'S', ITEM_STICK } } },

  { "Door", ITEM_DOOR, 1, false,
    { "PP.", "...", "..." }, { { 'P', ITEM_PLANKS } } },

  { "Trapdoor", ITEM_TRAPDOOR, 1, false,
    { "PPP", "...", "..." }, { { 'P', ITEM_PLANKS } } },

  { "Ladder", ITEM_LADDER, 1, false,
    { "S.S", "SSS", "S.S" }, { { 'S', ITEM_STICK } } },

  // Vanilla's own bed shape (3 top + 3 bottom) with wood slabs standing in
  // for wool, since this game has no sheep/wool source (see the notes at the
  // bottom of this file).
  { "Bed", ITEM_BED, 1, false,
    { "LLL", "PPP", "..." }, { { 'L', ITEM_WOOD_SLAB }, { 'P', ITEM_PLANKS } } },

  // --- tools -----------------------------------------------------------
  // Every tool is the tier's material over a two-stick handle.
  { "Wooden Pickaxe", ITEM_WOOD_PICKAXE, 1, false,
    { "PPP", ".S.", ".S." }, { { 'P', ITEM_PLANKS }, { 'S', ITEM_STICK } } },
  { "Stone Pickaxe", ITEM_STONE_PICKAXE, 1, false,
    { "CCC", ".S.", ".S." }, { { 'C', BLOCK_STONE }, { 'S', ITEM_STICK } } },

  { "Wooden Axe", ITEM_WOOD_AXE, 1, false,
    { "PP.", "PS.", ".S." }, { { 'P', ITEM_PLANKS }, { 'S', ITEM_STICK } } },
  { "Stone Axe", ITEM_STONE_AXE, 1, false,
    { "CC.", "CS.", ".S." }, { { 'C', BLOCK_STONE }, { 'S', ITEM_STICK } } },

  { "Wooden Shovel", ITEM_WOOD_SHOVEL, 1, false,
    { "P..", "S..", "S.." }, { { 'P', ITEM_PLANKS }, { 'S', ITEM_STICK } } },
  { "Stone Shovel", ITEM_STONE_SHOVEL, 1, false,
    { "C..", "S..", "S.." }, { { 'C', BLOCK_STONE }, { 'S', ITEM_STICK } } },

  { "Wooden Sword", ITEM_WOOD_SWORD, 1, false,
    { "P..", "P..", "S.." }, { { 'P', ITEM_PLANKS }, { 'S', ITEM_STICK } } },
  { "Stone Sword", ITEM_STONE_SWORD, 1, false,
    { "C..", "C..", "S.." }, { { 'C', BLOCK_STONE }, { 'S', ITEM_STICK } } },

  { "Wooden Hoe", ITEM_WOOD_HOE, 1, false,
    { "PP.", ".S.", ".S." }, { { 'P', ITEM_PLANKS }, { 'S', ITEM_STICK } } },
  { "Stone Hoe", ITEM_STONE_HOE, 1, false,
    { "CC.", ".S.", ".S." }, { { 'C', BLOCK_STONE }, { 'S', ITEM_STICK } } },

  // --- vehicles ----------------------------------------------------------
  // A full ring of planks framing the hull — more material than vanilla's
  // own 5-plank shape, by request.
  { "Boat", ITEM_BOAT, 1, false,
    { "PPP", "P.P", "PPP" }, { { 'P', ITEM_PLANKS } } },

  // --- weapons -------------------------------------------------------------
  // A heavier stone blade than the standard Stone Sword (3 stone rather
  // than 2, for +1 attack power) — by request, with its own hand-drawn icon.
  { "The First Sword", ITEM_SWORD, 1, false,
    { "C..", "C..", "CS." }, { { 'C', BLOCK_STONE }, { 'S', ITEM_STICK } } },

  // --- potions -----------------------------------------------------------
  // Shapeless: any 3 (or 6) poppies anywhere in the grid, no crafting table
  // step beyond the grid itself needed.
  { "Small Health Potion", ITEM_HEALTH_POTION_SMALL, 1, true,
    { "FFF", "...", "..." }, { { 'F', BLOCK_FLOWER_POPPY } } },

  { "Big Health Potion", ITEM_HEALTH_POTION_BIG, 1, true,
    { "FFF", "FFF", "..." }, { { 'F', BLOCK_FLOWER_POPPY } } },
};

const int CRAFT_RECIPE_COUNT = (int)(sizeof(CRAFT_RECIPES) / sizeof(CRAFT_RECIPES[0]));

namespace {

// Ingredient id a pattern character stands for; BLOCK_AIR for '.'.
uint8_t keyItem(const Recipe& r, char c) {
  if (c == '.') return BLOCK_AIR;
  for (const Recipe::KeyEntry& k : r.keys) {
    if (k.key == c) return k.item;
  }
  return BLOCK_AIR;
}

// The recipe pattern expanded into a 3x3 id grid.
void patternGrid(const Recipe& r, uint8_t out[9]) {
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      out[row * 3 + col] = keyItem(r, r.rows[row][col]);
    }
  }
}

// Bounding box of the non-empty cells; returns false for an empty grid.
bool bounds(const uint8_t g[9], int& r0, int& c0, int& r1, int& c1) {
  r0 = c0 = 3;
  r1 = c1 = -1;
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      if (g[r * 3 + c] == BLOCK_AIR) continue;
      r0 = std::min(r0, r);
      c0 = std::min(c0, c);
      r1 = std::max(r1, r);
      c1 = std::max(c1, c);
    }
  }
  return r1 >= 0;
}

// Shaped match: compare the trimmed shapes, so a recipe drawn anywhere in
// the grid still works. `mirror` flips it left-to-right, which Minecraft
// also accepts.
bool shapeMatches(const uint8_t want[9], const uint8_t got[9], bool mirror) {
  int wr0, wc0, wr1, wc1, gr0, gc0, gr1, gc1;
  if (!bounds(want, wr0, wc0, wr1, wc1)) return false;
  if (!bounds(got, gr0, gc0, gr1, gc1)) return false;
  if (wr1 - wr0 != gr1 - gr0 || wc1 - wc0 != gc1 - gc0) return false;
  int h = wr1 - wr0, w = wc1 - wc0;
  for (int r = 0; r <= h; r++) {
    for (int c = 0; c <= w; c++) {
      uint8_t a = want[(wr0 + r) * 3 + (wc0 + (mirror ? w - c : c))];
      uint8_t b = got[(gr0 + r) * 3 + (gc0 + c)];
      if (a != b) return false;
    }
  }
  return true;
}

// Shapeless match: same multiset of ingredients, layout ignored.
bool shapelessMatches(const uint8_t want[9], const uint8_t got[9]) {
  int counts[CRAFT_ITEM_COUNT] = {};
  int total = 0;
  for (int i = 0; i < 9; i++) {
    if (want[i] == BLOCK_AIR) continue;
    counts[want[i]]++;
    total++;
  }
  for (int i = 0; i < 9; i++) {
    if (got[i] == BLOCK_AIR) continue;
    if (got[i] >= CRAFT_ITEM_COUNT || counts[got[i]] == 0) return false;
    counts[got[i]]--;
    total--;
  }
  return total == 0;
}

} // namespace

const Recipe* findRecipe(const uint8_t grid[9]) {
  for (int i = 0; i < CRAFT_RECIPE_COUNT; i++) {
    const Recipe& r = CRAFT_RECIPES[i];
    uint8_t want[9];
    patternGrid(r, want);
    if (r.shapeless) {
      if (shapelessMatches(want, grid)) return &r;
    } else if (shapeMatches(want, grid, false) || shapeMatches(want, grid, true)) {
      return &r;
    }
  }
  return nullptr;
}

const Recipe* findRecipeByCount(const uint8_t items[9], const int counts[9],
                                bool* ambiguous) {
  if (ambiguous) *ambiguous = false;

  // What the grid actually holds, summed across all nine cells.
  int have[CRAFT_ITEM_COUNT] = {};
  for (int i = 0; i < 9; i++) {
    if (items[i] == BLOCK_AIR || counts[i] <= 0) continue;
    if (items[i] >= CRAFT_ITEM_COUNT) return nullptr;
    have[items[i]] += counts[i];
  }

  // Layout of the occupied cells, used only to break ties below.
  uint8_t occupancy[9];
  for (int i = 0; i < 9; i++) {
    occupancy[i] = counts[i] > 0 ? items[i] : (uint8_t)BLOCK_AIR;
  }

  const Recipe* firstMatch = nullptr;
  const Recipe* shapedMatch = nullptr;
  int matches = 0;

  for (int i = 0; i < CRAFT_RECIPE_COUNT; i++) {
    const Recipe& r = CRAFT_RECIPES[i];
    int want[CRAFT_ITEM_COUNT] = {};
    uint8_t ing[RECIPE_MAX_INGREDIENTS];
    int qty[RECIPE_MAX_INGREDIENTS];
    int n = recipeIngredients(r, ing, qty);
    for (int k = 0; k < n; k++) want[ing[k]] += qty[k];
    // Exact: the grid must hold these amounts and nothing besides.
    if (std::memcmp(have, want, sizeof(have)) != 0) continue;

    matches++;
    if (!firstMatch) firstMatch = &r;
    if (!shapedMatch && !r.shapeless) {
      uint8_t pattern[9];
      patternGrid(r, pattern);
      if (shapeMatches(pattern, occupancy, false) ||
          shapeMatches(pattern, occupancy, true)) {
        shapedMatch = &r;
      }
    }
  }

  if (ambiguous) *ambiguous = matches > 1;
  return shapedMatch ? shapedMatch : firstMatch;
}

int recipeIngredients(const Recipe& r, uint8_t items[RECIPE_MAX_INGREDIENTS],
                      int counts[RECIPE_MAX_INGREDIENTS]) {
  uint8_t grid[9];
  patternGrid(r, grid);
  int n = 0;
  for (int i = 0; i < 9; i++) {
    if (grid[i] == BLOCK_AIR) continue;
    bool merged = false;
    for (int k = 0; k < n && !merged; k++) {
      if (items[k] == grid[i]) {
        counts[k]++;
        merged = true;
      }
    }
    if (!merged && n < RECIPE_MAX_INGREDIENTS) {
      items[n] = grid[i];
      counts[n] = 1;
      n++;
    }
  }
  return n;
}

const char* craftItemName(uint8_t id) {
  if (id < BLOCK_TYPE_COUNT) return BLOCKS[id].name;
  switch (id) {
    case ITEM_PLANKS: return "planks";
    case ITEM_STICK: return "stick";
    case ITEM_CRAFTING_TABLE: return "crafting table";
    case ITEM_CHEST: return "chest";
    case ITEM_FURNACE: return "furnace";
    case ITEM_WOOD_SLAB: return "wood slab";
    case ITEM_STONE_SLAB: return "stone slab";
    case ITEM_WOOD_STAIRS: return "wood stairs";
    case ITEM_STONE_STAIRS: return "stone stairs";
    case ITEM_STONE_BRICKS: return "stone fence";
    case ITEM_SANDSTONE: return "sandstone";
    case ITEM_SNOW_BLOCK: return "snow block";
    case ITEM_PACKED_ICE: return "packed ice";
    case ITEM_FENCE: return "fence";
    case ITEM_DOOR: return "door";
    case ITEM_TRAPDOOR: return "trapdoor";
    case ITEM_LADDER: return "ladder";
    case ITEM_BED: return "bed";
    case ITEM_CAMPFIRE: return "campfire";
    case ITEM_RAW_MEAT: return "raw meat";
    case ITEM_COOKED_MEAT: return "cooked meat";
    case ITEM_WOOD_PICKAXE: return "wooden pickaxe";
    case ITEM_STONE_PICKAXE: return "stone pickaxe";
    case ITEM_WOOD_AXE: return "wooden axe";
    case ITEM_STONE_AXE: return "stone axe";
    case ITEM_WOOD_SHOVEL: return "wooden shovel";
    case ITEM_STONE_SHOVEL: return "stone shovel";
    case ITEM_WOOD_SWORD: return "wooden sword";
    case ITEM_STONE_SWORD: return "stone sword";
    case ITEM_WOOD_HOE: return "wooden hoe";
    case ITEM_STONE_HOE: return "stone hoe";
    case ITEM_BOAT: return "boat";
    case ITEM_SWORD: return "the first sword";
    case ITEM_APPLE: return "apple";
    case ITEM_PEACH: return "peach";
    case ITEM_PEAR: return "pear";
    case ITEM_CHERRY: return "cherry";
    case ITEM_ORANGE: return "orange";
    case ITEM_HEALTH_POTION_SMALL: return "small health potion";
    case ITEM_HEALTH_POTION_BIG: return "big health potion";
    case ITEM_RAW_COD: return "raw cod";
    case ITEM_RAW_SALMON: return "raw salmon";
    case ITEM_RAW_PUFFERFISH: return "raw pufferfish";
    case ITEM_RAW_TROPICAL_FISH: return "raw tropical fish";
    case ITEM_RAW_SHARK: return "raw shark";
    default: return "?";
  }
}

bool isEatableFood(uint8_t id) {
  switch (id) {
    case ITEM_COOKED_MEAT:
    case ITEM_APPLE:
    case ITEM_PEACH:
    case ITEM_PEAR:
    case ITEM_CHERRY:
    case ITEM_ORANGE:
    case ITEM_RAW_COD:
    case ITEM_RAW_SALMON:
    case ITEM_RAW_PUFFERFISH:
    case ITEM_RAW_TROPICAL_FISH:
    case ITEM_RAW_SHARK:
      return true;
    default:
      return false;
  }
}

int healthPotionHeal(uint8_t id) {
  switch (id) {
    case ITEM_HEALTH_POTION_SMALL: return 4; // 2 hearts
    case ITEM_HEALTH_POTION_BIG: return 8;   // 4 hearts
    default: return 0;
  }
}

int craftItemTile(uint8_t id) {
  if (id < BLOCK_TYPE_COUNT) return faceTexture(id, 0); // block: top face
  switch (id) {
    // Every crafted good has its own drawn sprite (item_art.cpp).
    case ITEM_PLANKS: return TILE_PLANKS;
    case ITEM_STICK: return TILE_STICK;
    case ITEM_CRAFTING_TABLE: return TILE_CRAFTING_TABLE;
    case ITEM_CHEST: return TILE_CHEST;
    case ITEM_FURNACE: return TILE_FURNACE;
    case ITEM_WOOD_SLAB: return TILE_WOOD_SLAB;
    case ITEM_STONE_SLAB: return TILE_STONE_SLAB;
    case ITEM_WOOD_STAIRS: return TILE_WOOD_STAIRS;
    case ITEM_STONE_STAIRS: return TILE_STONE_STAIRS;
    case ITEM_STONE_BRICKS: return TILE_STONE_BRICKS;
    case ITEM_SANDSTONE: return TILE_SANDSTONE;
    case ITEM_SNOW_BLOCK: return TILE_SNOW_BLOCK;
    case ITEM_PACKED_ICE: return TILE_PACKED_ICE;
    case ITEM_FENCE: return TILE_FENCE;
    case ITEM_DOOR: return TILE_DOOR;
    case ITEM_TRAPDOOR: return TILE_TRAPDOOR;
    case ITEM_LADDER: return TILE_LADDER;
    case ITEM_BED: return TILE_BED;
    case ITEM_CAMPFIRE: return TILE_CAMPFIRE;
    case ITEM_RAW_MEAT: return TILE_RAW_MEAT;
    case ITEM_COOKED_MEAT: return TILE_COOKED_MEAT;
    case ITEM_WOOD_PICKAXE: return TILE_WOOD_PICKAXE;
    case ITEM_STONE_PICKAXE: return TILE_STONE_PICKAXE;
    case ITEM_WOOD_AXE: return TILE_WOOD_AXE;
    case ITEM_STONE_AXE: return TILE_STONE_AXE;
    case ITEM_WOOD_SHOVEL: return TILE_WOOD_SHOVEL;
    case ITEM_STONE_SHOVEL: return TILE_STONE_SHOVEL;
    case ITEM_WOOD_SWORD: return TILE_WOOD_SWORD;
    case ITEM_STONE_SWORD: return TILE_STONE_SWORD;
    case ITEM_WOOD_HOE: return TILE_WOOD_HOE;
    case ITEM_STONE_HOE: return TILE_STONE_HOE;
    case ITEM_BOAT: return TILE_BOAT;
    case ITEM_SWORD: return TILE_SWORD;
    case ITEM_APPLE: return TILE_APPLE;
    case ITEM_PEACH: return TILE_PEACH;
    case ITEM_PEAR: return TILE_PEAR;
    case ITEM_CHERRY: return TILE_CHERRY;
    case ITEM_ORANGE: return TILE_ORANGE;
    case ITEM_HEALTH_POTION_SMALL: return TILE_POTION_SMALL;
    case ITEM_HEALTH_POTION_BIG: return TILE_POTION_BIG;
    case ITEM_RAW_COD: return TILE_RAW_COD;
    case ITEM_RAW_SALMON: return TILE_RAW_SALMON;
    case ITEM_RAW_PUFFERFISH: return TILE_RAW_PUFFERFISH;
    case ITEM_RAW_TROPICAL_FISH: return TILE_RAW_TROPICAL_FISH;
    case ITEM_RAW_SHARK: return TILE_RAW_SHARK;
    default: return TILE_NONE;
  }
}

// Deliberately not included, because this game has no source for the
// ingredients and inventing one would misrepresent the recipe:
//   Torch      = coal over a stick            (coal is now obtainable, but
//                                              there's no light-emission
//                                              system for a torch to plug
//                                              into, so it would just be an
//                                              inert item)
//   Glass      = sand, SMELTED not crafted    (needs a working furnace)
//   Bucket     = 3 iron in a V                (needs iron ore)
//   Bed        = 3 wool over 3 planks         (needs sheep/wool)
