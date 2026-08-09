#pragma once
#include "common.h"

// The grid the procedural tile drawers paint on. Blocks are authored at this
// chunky resolution and that is what gives them their look, so it does not
// change.
const int TILE_PX = 16;

// ...but the atlas STORES each tile at a multiple of that, so hand-drawn item
// art can be finer than the block grid without blurring it down to 16x16.
// Procedural art is written as scaled-up blocks and comes out pixel-identical
// to before; only supplied art actually uses the extra detail.
const int ATLAS_SCALE = 2;
const int ATLAS_TILE_PX = TILE_PX * ATLAS_SCALE;

// Procedurally generated pixel-art texture atlas (TILE_COUNT tiles laid out
// horizontally, each ATLAS_TILE_PX square, RGBA). Deterministic PRNG so it
// looks the same every run — same algorithm as the JS version.
struct Atlas {
  int width = 0;   // ATLAS_TILE_PX * TILE_COUNT
  int height = 0;  // ATLAS_TILE_PX
  std::vector<uint8_t> pixels; // RGBA rows, row 0 = BOTTOM (OpenGL order);
                               // the canvas-space image is flipped at build
                               // time so tile tops land at v = 1.
};

const Atlas& buildTextureAtlas();

// UV rect of a tile within the atlas: u in [u0,u1], v in [0,1], v=1 = tile top.
struct UVRect { double u0, v0, u1, v1; };
UVRect tileUV(int tileIndex);

// UV for a given block id + face (0 top / 1 bottom / 2 side); returns false
// if the block has no texture.
bool getBlockFaceUV(uint8_t blockId, int face, UVRect& out);

// The art\<name>.png that may replace this tile's procedural drawing, or
// nullptr if the tile cannot be overridden. See sprites_generated.h.
const char* spriteNameForTile(int tile);
