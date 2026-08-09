#pragma once
#include "chunk.h"

std::unique_ptr<Chunk> generateChunk(int cx, int cz);

// World seed: each new game rolls a fresh one; saves persist theirs so a
// loaded game regenerates the same terrain. Setting the seed resets all
// generator noise. Chunks must not outlive a seed change (sessions rebuild
// the World whenever they set it).
void setWorldSeed(uint32_t seed);
uint32_t currentWorldSeed();

// Noise-only column query for tests/tooling (no chunk generation).
// biomeOut: 0 plains, 1 desert, 2 canyon, 3 snow.
void columnInfoAt(int wx, int wz, int& biomeOut, int& surfaceYOut);

// True if this column sits inside a carved canyon channel (gorge floor).
bool canyonCutAt(int wx, int wz);

// Iceberg noise sample (bergs form where this exceeds 0.5 in frozen ocean).
double bergValueAt(int wx, int wz);

// Center of this world's guaranteed tall snow-capped landmark mountain.
void landmarkPosition(double& x, double& z);

// The block exposed on top of a column (grass/stone/snow/sand/redrock).
uint8_t surfaceBlockAt(int wx, int wz);

// Tree size bounds (trunk blocks above ground).
extern const int TREE_MIN_HEIGHT;
extern const int TREE_MAX_HEIGHT;

// Coal seams: how far below a column's surface the first coal can appear, so
// you have to dig down to reach it rather than stumbling on it at ground
// level. Coal only ever replaces stone.
extern const int COAL_MIN_DEPTH;

// Picks a starting column on substantial dry land — the world is mostly
// ocean, so the origin is usually seabed and bare sandbars make a poor
// spawn. Searches outward from the origin; always returns something.
void findSpawnColumn(int& outX, int& outZ);
