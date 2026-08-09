#pragma once
#include "world.h"

// Rebuilds a chunk's opaque + water display lists from its block data and
// clears the dirty flag. Requires a current OpenGL context.
void remeshChunk(World& world, Chunk& chunk);

// Frees the chunk's display lists (call before dropping an evicted chunk).
void disposeChunk(Chunk& chunk);

// Opacity of the water surface over a column this many blocks deep. Shallow
// water shows the bottom; past WATER_OPAQUE_DEPTH it hides it completely.
const int WATER_OPAQUE_DEPTH = 5;
uint8_t waterSurfaceAlpha(int depth);
