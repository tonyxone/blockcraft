#pragma once

// Hand-drawn item art, converted to source by tools\gen_sprite.ps1 and
// compiled into the exe. Keeping the pixels in the binary preserves the
// property that makes this build what it is: no asset files to ship, lose or
// fail to load — the game still runs from the exe alone.
//
// To use your own art:
//   1. draw a TILE_PX x TILE_PX (16x16) image, transparent where you want
//      the slot to show through, and save it as art\<name>.png
//   2. run  powershell -File tools\gen_sprite.ps1
//   3. rebuild
// `name` matches a tile through spriteNameForTile() in textures.cpp; a name
// with no matching tile is ignored, and a tile with no art keeps its
// procedural drawing, so the two can coexist.
struct GeneratedSprite {
  const char* name;
  const unsigned char* rgba; // TILE_PX*TILE_PX*4, row 0 = TOP, straight alpha
};

extern const GeneratedSprite GENERATED_SPRITES[];
extern const int GENERATED_SPRITE_COUNT;

// The generated sprite for `name`, or nullptr when none was supplied.
const GeneratedSprite* generatedSpriteNamed(const char* name);
