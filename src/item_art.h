#pragma once
#include "textures.h" // TILE_PX

// Hand-authored artwork for the crafted items, kept apart from the block
// textures so the sprites you actually redraw by hand are all in one place
// (item_art.cpp) instead of buried among the procedural terrain drawers.
//
// A sprite is TILE_PX rows of TILE_PX characters. '.' is transparent; every
// other character is looked up in the sprite's own palette, so the meaning of
// a letter is per-sprite rather than global. A dark outline is generated
// automatically around whatever is drawn, so the art must NOT include one or
// it doubles up.
//
// Shapes are shared where two items differ only in material: a wooden and a
// stone pickaxe are the same rows with a different palette, and a slab is a
// slab whatever it is cut from.

struct ItemArtColor {
  char key;             // the character this paints, 0 ends the palette
  uint32_t base, dark;  // 0xRRGGBB; `dark` is the jitter the fill leans toward
};

const int ITEM_ART_MAX_COLORS = 5;

struct ItemArt {
  const char* const* rows;                  // TILE_PX strings of TILE_PX chars
  ItemArtColor palette[ITEM_ART_MAX_COLORS];
};

// The sprite for an atlas tile, or nullptr if that tile is not an item.
const ItemArt* itemArtForTile(int tile);
