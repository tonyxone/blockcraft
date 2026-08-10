#include "worldmap.h"
#include "minimap.h"
#include "gfx.h"
#include "win_gl.h"

namespace {

const int DIM = WORLD_RADIUS * 2; // 512, one texel per block, the whole world at once

// How close the player actually has to walk to a column to clear its mist
// — deliberately much tighter than the ~64-block render distance that
// keeps a chunk loaded at all, so having a distant chunk loaded (and thus
// visible in the 3D world) doesn't by itself reveal it on the map. Roughly
// a chunk and a half: close enough that "exploring" means something.
const double REVEAL_RADIUS = 20.0;

const double PANEL_MAX = 2160.0; // on-screen size cap, 3x the original 720
const double ZOOM_MIN = 1.0;     // whole world visible
const double ZOOM_MAX = 16.0;    // a 32-block span across the panel
const double ZOOM_STEP = 1.25;   // per wheel notch

std::vector<uint8_t> g_r, g_g, g_b; // per-column recorded colour, DIM*DIM
std::vector<int16_t> g_height;      // -1 = never explored
std::vector<Vec3> g_markers;
const size_t MAX_MARKERS = 32;
double g_zoom = ZOOM_MIN;

GLuint g_tex = 0;
std::vector<uint8_t> g_texPixels; // RGBA scratch, rebuilt from g_r/g_g/g_b/g_height when dirty
bool g_dirty = true;

int idx(int wx, int wz) { return (wz + WORLD_RADIUS) * DIM + (wx + WORLD_RADIUS); }
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct MapRect { double x, y, size; };

// A pure function of the window size so mouse clicks can invert it without
// having to have drawn a frame first.
MapRect computeRect(int winW, int winH) {
  double size = std::min({ (double)winW, (double)winH, PANEL_MAX }) * 0.82;
  return { (winW - size) / 2, (winH - size) / 2, size };
}

// The world-space window currently shown in the panel: `span` blocks
// across, centred on (centerX, centerZ). At minimum zoom this is the whole
// world centred on the origin (matching the map's original whole-world
// view exactly); zooming in shrinks the span and re-centres on the player,
// clamped so the window never has to show past the world border.
void computeView(const Vec3& playerPos, double& centerX, double& centerZ, double& span) {
  span = DIM / g_zoom;
  double half = span / 2;
  if (half >= WORLD_RADIUS) {
    centerX = 0;
    centerZ = 0;
  } else {
    centerX = clampd(playerPos.x, -WORLD_RADIUS + half, WORLD_RADIUS - half);
    centerZ = clampd(playerPos.z, -WORLD_RADIUS + half, WORLD_RADIUS - half);
  }
}

void rebuildTextureIfDirty() {
  if (!g_dirty) return;
  g_dirty = false;
  for (int j = 0; j < DIM; j++) {
    for (int i = 0; i < DIM; i++) {
      size_t k = (size_t)j * DIM + i;
      uint8_t* px = &g_texPixels[k * 4];
      if (g_height[k] < 0) {
        px[0] = 8; px[1] = 8; px[2] = 12; px[3] = 255; // unexplored: black mist
        continue;
      }
      // Relief against the column to the north, same idea minimap.cpp uses.
      double shade = 1.0;
      if (j > 0 && g_height[k - DIM] >= 0) {
        int d = (int)g_height[k] - (int)g_height[k - DIM];
        shade += clampi(d, -3, 3) * 0.09;
      }
      px[0] = (uint8_t)clampi((int)(g_r[k] * shade), 0, 255);
      px[1] = (uint8_t)clampi((int)(g_g[k] * shade), 0, 255);
      px[2] = (uint8_t)clampi((int)(g_b[k] * shade), 0, 255);
      px[3] = 255;
    }
  }
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DIM, DIM, GL_RGBA, GL_UNSIGNED_BYTE, g_texPixels.data());
}

// The recorded-colour buffers are pure CPU state and need no GL context —
// split out so worldMapUpdate/worldMapExplored work headlessly (the
// selftest runs before any window exists), while the GL texture itself is
// only ever touched from worldMapInit()/drawFullMap(), which do require one.
void ensureBuffers() {
  if (!g_r.empty()) return;
  g_r.assign((size_t)DIM * DIM, 0);
  g_g.assign((size_t)DIM * DIM, 0);
  g_b.assign((size_t)DIM * DIM, 0);
  g_height.assign((size_t)DIM * DIM, -1);
}

} // namespace

void worldMapInit() {
  ensureBuffers();
  minimapInit(); // shares its block-colour palette
  if (g_tex) return;
  g_texPixels.assign((size_t)DIM * DIM * 4, 0);
  glGenTextures(1, &g_tex);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DIM, DIM, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               g_texPixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void worldMapUpdate(World& world, double playerX, double playerZ) {
  ensureBuffers();
  double r2 = REVEAL_RADIUS * REVEAL_RADIUS;
  for (auto& kv : world.chunks) {
    Chunk& chunk = *kv.second;
    int ox = chunk.worldOriginX(), oz = chunk.worldOriginZ();
    // Cheap reject: skip the whole chunk if even its nearest corner is
    // farther than REVEAL_RADIUS from the player.
    double nx = clampd(playerX, ox, ox + CHUNK_SIZE);
    double nz = clampd(playerZ, oz, oz + CHUNK_SIZE);
    bool chunkDirty = chunk.dirty;
    if (!chunkDirty) {
      double dx = nx - playerX, dz = nz - playerZ;
      if (dx * dx + dz * dz > r2) continue;
    }

    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
      int wz = oz + lz;
      if (wz < -WORLD_RADIUS || wz >= WORLD_RADIUS) continue;
      for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        int wx = ox + lx;
        if (wx < -WORLD_RADIUS || wx >= WORLD_RADIUS) continue;

        size_t k = (size_t)idx(wx, wz);
        bool alreadyExplored = g_height[k] >= 0;
        if (alreadyExplored && !chunkDirty) continue; // recorded, unchanged since

        double dx = (wx + 0.5) - playerX, dz = (wz + 0.5) - playerZ;
        if (dx * dx + dz * dz > r2) continue; // not explored (yet)

        int water = 0;
        uint8_t surface = BLOCK_AIR;
        int h = -1;
        for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
          uint8_t id = chunk.getLocal(lx, y, lz);
          if (id == BLOCK_AIR || isPlant(id)) continue;
          if (id == BLOCK_WATER) { water++; continue; } // keep descending to the bed
          h = y;
          surface = id;
          break;
        }
        if (h < 0) continue; // nothing solid in this column (shouldn't happen, but be safe)

        int r, g, b;
        minimapBlockColor(surface, r, g, b);
        if (water > 0) {
          int wr, wg, wb;
          minimapBlockColor(BLOCK_WATER, wr, wg, wb);
          double t = std::min(1.0, water / 6.0);
          r = (int)(r * (1 - t) + wr * t);
          g = (int)(g * (1 - t) + wg * t);
          b = (int)(b * (1 - t) + wb * t);
        }
        g_r[k] = (uint8_t)clampi(r, 0, 255);
        g_g[k] = (uint8_t)clampi(g, 0, 255);
        g_b[k] = (uint8_t)clampi(b, 0, 255);
        g_height[k] = (int16_t)h;
        g_dirty = true;
      }
    }
  }
}

void worldMapReset() {
  ensureBuffers();
  std::fill(g_height.begin(), g_height.end(), (int16_t)-1);
  g_markers.clear();
  g_zoom = ZOOM_MIN;
  g_dirty = true;
}

bool worldMapExplored(int wx, int wz) {
  if (!inWorldBorder(wx, wz)) return false;
  ensureBuffers();
  return g_height[(size_t)idx(wx, wz)] >= 0;
}

void addMapMarker(double wx, double wz) {
  if (g_markers.size() >= MAX_MARKERS) g_markers.erase(g_markers.begin());
  g_markers.push_back(Vec3(wx, 0, wz));
}

void removeNearestMapMarker(double wx, double wz) {
  if (g_markers.empty()) return;
  size_t best = 0;
  double bestD = 1e18;
  for (size_t i = 0; i < g_markers.size(); i++) {
    double dx = g_markers[i].x - wx, dz = g_markers[i].z - wz;
    double d = dx * dx + dz * dz;
    if (d < bestD) { bestD = d; best = i; }
  }
  g_markers.erase(g_markers.begin() + (long)best);
}

const std::vector<Vec3>& mapMarkers() { return g_markers; }

void worldMapAdjustZoom(int notches) {
  if (notches == 0) return;
  g_zoom = clampd(g_zoom * std::pow(ZOOM_STEP, notches), ZOOM_MIN, ZOOM_MAX);
}

void drawFullMap(int winW, int winH, const Vec3& playerPos, double playerYaw,
                 const std::vector<Boat>& boats) {
  if (!g_tex) return;
  rebuildTextureIfDirty();
  MapRect r = computeRect(winW, winH);
  double centerX, centerZ, span;
  computeView(playerPos, centerX, centerZ, span);
  double half = span / 2;

  drawRect(0, 0, winW, winH, 0, 0, 0, 0.72); // dims the world out from under the map
  drawRect(r.x - 4, r.y - 4, r.size + 8, r.size + 8, 0, 0, 0, 0.6);

  // Texture coords select the zoomed-in sub-square of the whole-world
  // texture; GL_NEAREST filtering makes it go pleasantly blocky rather
  // than blurry when zoomed past 1:1, matching this game's pixel-art look
  // everywhere else.
  double u0 = (centerX - half + WORLD_RADIUS) / DIM, u1 = (centerX + half + WORLD_RADIUS) / DIM;
  double v0 = (centerZ - half + WORLD_RADIUS) / DIM, v1 = (centerZ + half + WORLD_RADIUS) / DIM;
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glColor4d(1, 1, 1, 1);
  glBegin(GL_QUADS);
  glTexCoord2d(u0, v0); glVertex2d(r.x, r.y);
  glTexCoord2d(u1, v0); glVertex2d(r.x + r.size, r.y);
  glTexCoord2d(u1, v1); glVertex2d(r.x + r.size, r.y + r.size);
  glTexCoord2d(u0, v1); glVertex2d(r.x, r.y + r.size);
  glEnd();
  glDisable(GL_TEXTURE_2D);
  drawRectOutline(r.x - 4, r.y - 4, r.size + 8, r.size + 8, 2, 1, 1, 1, 0.8);

  auto worldToScreen = [&](double wx, double wz, double& sx, double& sy) -> bool {
    sx = r.x + (wx - (centerX - half)) / span * r.size;
    sy = r.y + (wz - (centerZ - half)) / span * r.size;
    return sx >= r.x - 8 && sx <= r.x + r.size + 8 && sy >= r.y - 8 && sy <= r.y + r.size + 8;
  };

  for (const Boat& boat : boats) {
    double sx, sy;
    if (!worldToScreen(boat.position.x, boat.position.z, sx, sy)) continue;
    drawRect(sx - 3, sy - 3, 6, 6, 0.55, 0.38, 0.22, 1);
    drawRectOutline(sx - 3.5, sy - 3.5, 7, 7, 1, 0, 0, 0, 0.85);
  }

  glDisable(GL_TEXTURE_2D);
  for (const Vec3& m : g_markers) {
    double sx, sy;
    if (!worldToScreen(m.x, m.z, sx, sy)) continue;
    // A small pin: a diamond over a dark outline, easy to pick out against
    // any terrain colour underneath it.
    glBegin(GL_TRIANGLE_FAN);
    glColor4d(1, 0.82, 0.15, 1);
    glVertex2d(sx, sy - 7);
    glVertex2d(sx + 5, sy);
    glVertex2d(sx, sy + 7);
    glVertex2d(sx - 5, sy);
    glEnd();
    glBegin(GL_LINE_LOOP);
    glColor4d(0, 0, 0, 0.85);
    glVertex2d(sx, sy - 7);
    glVertex2d(sx + 5, sy);
    glVertex2d(sx, sy + 7);
    glVertex2d(sx - 5, sy);
    glEnd();
  }

  // Player arrow, same shape/colours as the corner minimap's.
  {
    double sx, sy;
    if (worldToScreen(playerPos.x, playerPos.z, sx, sy)) {
      double fx, fz;
      minimapArrowDir(playerYaw, fx, fz);
      double rx = -fz, rz = fx;
      const double TIP = 11.0, BACK = 7.0, SIDE = 6.5;
      glBegin(GL_TRIANGLES);
      glColor4d(1, 1, 1, 1);
      glVertex2d(sx + fx * TIP, sy + fz * TIP);
      glVertex2d(sx - fx * BACK + rx * SIDE, sy - fz * BACK + rz * SIDE);
      glVertex2d(sx - fx * BACK - rx * SIDE, sy - fz * BACK - rz * SIDE);
      glColor4d(0.85, 0.1, 0.1, 1);
      glVertex2d(sx + fx * (TIP - 3), sy + fz * (TIP - 3));
      glVertex2d(sx - fx * (BACK - 2) + rx * (SIDE - 2), sy - fz * (BACK - 2) + rz * (SIDE - 2));
      glVertex2d(sx - fx * (BACK - 2) - rx * (SIDE - 2), sy - fz * (BACK - 2) - rz * (SIDE - 2));
      glEnd();
    }
  }

  const char* title = "World Map";
  double tw = textWidth(g_fontButton, title);
  drawText(g_fontButton, r.x + r.size / 2 - tw / 2, r.y - 36, title, 1, 1, 1, 1);
  const char* hint =
      "Left click: place marker    Right click: remove nearest    Scroll: zoom    M: close";
  double hw = textWidth(g_fontHint, hint);
  drawText(g_fontHint, r.x + r.size / 2 - hw / 2, r.y + r.size + 10, hint, 0.85, 0.85, 0.85, 1);
}

bool fullMapScreenToWorld(double sx, double sy, int winW, int winH, const Vec3& playerPos,
                         double& wx, double& wz) {
  MapRect r = computeRect(winW, winH);
  if (sx < r.x || sx > r.x + r.size || sy < r.y || sy > r.y + r.size) return false;
  double centerX, centerZ, span;
  computeView(playerPos, centerX, centerZ, span);
  double half = span / 2;
  wx = (sx - r.x) / r.size * span + (centerX - half);
  wz = (sy - r.y) / r.size * span + (centerZ - half);
  return true;
}
