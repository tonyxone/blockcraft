#pragma once
#include "world.h"
#include "boat.h"

// Top-right minimap, following the conventions Minecraft's own map uses:
// north is always up, one pixel per block, each pixel takes the colour of
// the topmost solid block in that column, and relief comes from comparing a
// column's height with its northern neighbour (darker when it drops away,
// brighter when it rises). Water is tinted by depth, shallows lighter.
//
// The map is a fixed north-up window on the world; the player sits at the
// centre as an arrow that turns with the view.

const int MINIMAP_BLOCKS = 96;      // blocks across (also the texture size)
const double MINIMAP_SCREEN = 168;  // on-screen size in pixels
const double MINIMAP_MARGIN = 14;   // gap from the window corner

// Builds the texture. Requires a current GL context.
void minimapInit();

// The same average-top-face-tile colour the minimap paints a block with,
// shared with worldmap.cpp so the corner map and the full map agree with
// each other (and with the world) about what a stone block looks like.
// minimapInit() must have run first.
void minimapBlockColor(uint8_t blockId, int& r, int& g, int& b);

// Re-samples the world when the player has moved to a new block. Cheap to
// call every frame; the rebuild itself is throttled.
void minimapUpdate(World& world, double playerX, double playerZ);

// Draws the map, its frame, the N/E/S/W markers, the player arrow, any
// boats in the window, and any player-placed markers (see worldmap.h) that
// fall within it. Call inside the 2D HUD pass.
void drawMinimap(int winW, int winH, double playerYaw, const std::vector<Boat>& boats);

// Screen-space direction the player arrow points for a given yaw, in HUD
// coordinates (x right, y DOWN). Exposed so the north-up convention can be
// asserted in tests rather than eyeballed on a 168px map.
void minimapArrowDir(double yaw, double& dx, double& dy);
