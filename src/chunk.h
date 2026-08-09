#pragma once
#include "common.h"
#include "constants.h"
#include "blocks.h"

class Chunk {
public:
  int cx, cz;
  std::array<uint8_t, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE> blocks;
  bool dirty = true;
  // OpenGL display lists holding the chunk's compiled geometry (0 = none).
  unsigned int listOpaque = 0;
  unsigned int listPlants = 0; // cutout billboards (grass), alpha-tested
  unsigned int listWater = 0;

  Chunk(int cx_, int cz_) : cx(cx_), cz(cz_) { blocks.fill(BLOCK_AIR); }

  static bool inBounds(int x, int y, int z) {
    return x >= 0 && x < CHUNK_SIZE && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_SIZE;
  }

  static int index(int x, int y, int z) {
    return y * (CHUNK_SIZE * CHUNK_SIZE) + z * CHUNK_SIZE + x;
  }

  uint8_t getLocal(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return BLOCK_AIR;
    return blocks[index(x, y, z)];
  }

  void setLocal(int x, int y, int z, uint8_t id) {
    if (!inBounds(x, y, z)) return;
    blocks[index(x, y, z)] = id;
    dirty = true;
  }

  int worldOriginX() const { return cx * CHUNK_SIZE; }
  int worldOriginZ() const { return cz * CHUNK_SIZE; }
};
