#pragma once
#include "common.h"

// A drifting layer of blocky clouds above the world, in the style of the
// terrain itself: a coarse grid of cells, each either cloud or open sky.

const double CLOUD_HEIGHT = 58.0;   // well clear of the tallest peak (y42)
const double CLOUD_CELL = 12.0;     // blocks per cloud cell (as in Minecraft)
const double CLOUD_THICKNESS = 4.0; // how deep the slab looks from below
const int CLOUD_GRID = 48;          // cells per side; the pattern wraps here
const double CLOUD_DRIFT_SPEED = 0.6; // blocks per second, drifting west
// Only a small share of the sky carries cloud, so they read as a handful of
// drifting puffs rather than overcast. Set exactly (by percentile) instead
// of guessed at with a noise threshold.
const double CLOUD_COVERAGE = 0.10;
const double CLOUD_ALPHA = 0.45; // translucent, like Minecraft's fancy clouds

// Cloud/no-cloud for a cell, wrapping at CLOUD_GRID so the layer can be
// tiled seamlessly as it drifts.
bool cloudAt(int i, int j);

// Fraction of cells carrying cloud (for tests/tuning).
double cloudCoverage();

// Builds the cloud geometry. Requires a current GL context.
void skyInit();

// Draws the layer, drifted by elapsed time. Call inside the 3D pass.
void drawClouds(double timeSeconds, double camX, double camZ);
