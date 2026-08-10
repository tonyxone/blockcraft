#pragma once
#include "world.h"
#include "boat.h"

// The whole-world map (M key), as opposed to minimap.h's small HUD corner
// map: where the corner map just live-samples whatever chunks happen to be
// loaded near the player right now, this one remembers every column the
// player has actually explored (see REVEAL_RADIUS in worldmap.cpp) and
// keeps that recording once made. Both maps hide anything not yet recorded
// under a black mist.
//
// Not persisted across a save reload, same as the boats and animals this
// session already doesn't carry forward either.

// Builds the texture and shares minimap.h's block-colour palette. Requires
// a current GL context.
void worldMapInit();

// Records the colour of every column within REVEAL_RADIUS of (playerX,
// playerZ) that belongs to a currently loaded chunk and hasn't been
// recorded yet (or has been edited since it last was). Call once per
// frame; already-explored, unedited columns are just an array read, so
// this is cheap once an area has been fully explored. Accumulates just
// from walking around — the map doesn't need to be open for this to run.
void worldMapUpdate(World& world, double playerX, double playerZ);

// Forgets every recorded column and marker, and resets the zoom, for
// starting a new session (a fresh or loaded world has nothing to do with
// whatever the previous one's map looked like).
void worldMapReset();

bool worldMapExplored(int wx, int wz);

// Player-placed waypoints, shown on both the full map and the corner
// minimap. Capped so a player can't grow it without bound; the oldest
// marker is dropped to make room once full.
void addMapMarker(double wx, double wz);
// Removes whichever marker is nearest (wx, wz), if any exist.
void removeNearestMapMarker(double wx, double wz);
const std::vector<Vec3>& mapMarkers();

// Mouse-wheel zoom for the full map: positive notches zoom in (show less
// of the world, more magnified, centred on and following the player),
// negative zoom back out, bottoming out at the whole world. Only has any
// effect while the full map is open — call unconditionally, it's a no-op
// otherwise being harmless either way.
void worldMapAdjustZoom(int notches);

// Draws the full-screen map: backdrop, the world texture (explored area in
// its real colours, everywhere else black mist), boats, markers, and the
// player's own arrow. Call inside the 2D HUD pass, while the map is open.
void drawFullMap(int winW, int winH, const Vec3& playerPos, double playerYaw,
                 const std::vector<Boat>& boats);

// Converts a screen-space click to a world column, using the same layout
// (including current zoom/pan) drawFullMap itself uses — so this can be
// called from the mouse handler without having to draw first. Returns
// false for a click outside the map square (e.g. on the dimmed backdrop).
bool fullMapScreenToWorld(double sx, double sy, int winW, int winH, const Vec3& playerPos,
                         double& wx, double& wz);
