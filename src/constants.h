#pragma once

const int CHUNK_SIZE = 16;   // blocks along X and Z
const int CHUNK_HEIGHT = 48; // blocks along Y (whole world height, single vertical chunk)
const int SEA_LEVEL = 18;
const int RENDER_DISTANCE = 4; // chunk radius kept loaded/meshed around the player

// The world is finite: a square from -WORLD_RADIUS to WORLD_RADIUS-1 on X/Z
// (Minecraft-style world border — four vertical faces nothing can cross).
const int WORLD_RADIUS = 256; // 512x512 blocks, chunks -16..15

inline bool inWorldBorder(int wx, int wz) {
  return wx >= -WORLD_RADIUS && wx < WORLD_RADIUS && wz >= -WORLD_RADIUS && wz < WORLD_RADIUS;
}
