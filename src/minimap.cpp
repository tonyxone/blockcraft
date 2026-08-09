#include "minimap.h"
#include "textures.h"
#include "gfx.h"
#include "win_gl.h"

namespace {

const int N = MINIMAP_BLOCKS;
const int HALF = N / 2;

GLuint g_tex = 0;
std::vector<uint8_t> g_pixels;   // RGBA, row 0 = north edge
int g_centerX = INT_MIN, g_centerZ = INT_MIN; // block the map is built around
bool g_haveMap = false;

struct Rgb { int r, g, b; };
Rgb g_blockColor[BLOCK_TYPE_COUNT];

// Average colour of a block's top-face tile, so the map matches the world's
// own procedurally generated textures instead of a hand-picked palette.
void buildPalette() {
  const Atlas& atlas = buildTextureAtlas();
  for (int id = 0; id < BLOCK_TYPE_COUNT; id++) {
    int tile = faceTexture((uint8_t)id, 0);
    if (tile < 0) {
      g_blockColor[id] = { 0, 0, 0 };
      continue;
    }
    long sr = 0, sg = 0, sb = 0, n = 0;
    for (int y = 0; y < atlas.height; y++) {
      for (int x = 0; x < TILE_PX; x++) {
        const uint8_t* p = &atlas.pixels[(size_t)(y * atlas.width + tile * TILE_PX + x) * 4];
        if (p[3] == 0) continue; // skip the transparent parts of cutouts
        sr += p[0];
        sg += p[1];
        sb += p[2];
        n++;
      }
    }
    if (n == 0) n = 1;
    g_blockColor[id] = { (int)(sr / n), (int)(sg / n), (int)(sb / n) };
  }
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

void minimapInit() {
  if (g_tex) return;
  buildPalette();
  g_pixels.assign((size_t)N * N * 4, 0);
  glGenTextures(1, &g_tex);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               g_pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void minimapUpdate(World& world, double playerX, double playerZ) {
  if (!g_tex) return;
  int cx = (int)std::floor(playerX);
  int cz = (int)std::floor(playerZ);
  if (cx == g_centerX && cz == g_centerZ) return; // still the same block
  g_centerX = cx;
  g_centerZ = cz;

  // Pass 1: surface height and block for every column in the window.
  static std::vector<int> height;
  static std::vector<uint8_t> surface;
  static std::vector<int> waterDepth;
  height.assign((size_t)N * N, -1);
  surface.assign((size_t)N * N, BLOCK_AIR);
  waterDepth.assign((size_t)N * N, 0);

  // Resolved chunk-by-chunk rather than through world.getBlock: a full
  // rebuild touches ~400k cells, and a hash lookup per cell would hitch
  // every time the player steps to a new block.
  for (int j = 0; j < N; j++) {
    int wz = cz - HALF + j; // row 0 is the north edge
    Chunk* chunk = nullptr;
    int haveCx = INT_MIN, haveCz = INT_MIN;
    for (int i = 0; i < N; i++) {
      int wx = cx - HALF + i;
      if (!inWorldBorder(wx, wz)) continue;
      int ccx = floorDiv(wx, CHUNK_SIZE), ccz = floorDiv(wz, CHUNK_SIZE);
      if (ccx != haveCx || ccz != haveCz) { // 16 columns share a chunk
        haveCx = ccx;
        haveCz = ccz;
        chunk = world.getChunk(ccx, ccz);
      }
      if (!chunk) continue; // not generated yet
      int lx = wx - ccx * CHUNK_SIZE, lz = wz - ccz * CHUNK_SIZE;
      int water = 0;
      for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
        uint8_t id = chunk->getLocal(lx, y, lz);
        if (id == BLOCK_AIR || isPlant(id)) continue;
        if (id == BLOCK_WATER) {
          water++; // keep descending: the map shows the bed under the water
          continue;
        }
        height[(size_t)j * N + i] = y;
        surface[(size_t)j * N + i] = id;
        waterDepth[(size_t)j * N + i] = water;
        break;
      }
    }
  }

  // Pass 2: colour, with relief from the height step to the north.
  for (int j = 0; j < N; j++) {
    for (int i = 0; i < N; i++) {
      size_t k = (size_t)j * N + i;
      uint8_t* px = &g_pixels[k * 4];
      if (height[k] < 0) { // outside the border or not generated yet
        px[0] = px[1] = px[2] = 0;
        px[3] = 0;
        continue;
      }
      Rgb c = g_blockColor[surface[k] < BLOCK_TYPE_COUNT ? surface[k] : 0];

      // Relief: brighter when this column stands above the one to its
      // north, darker when it drops away.
      double shade = 1.0;
      if (j > 0 && height[k - N] >= 0) {
        int d = height[k] - height[k - N];
        shade += clampi(d, -3, 3) * 0.09;
      }

      // Water sits over the bed colour: deeper reads darker and bluer.
      if (waterDepth[k] > 0) {
        double t = std::min(1.0, waterDepth[k] / 6.0);
        Rgb w = g_blockColor[BLOCK_WATER];
        c.r = (int)(c.r * (1 - t) + w.r * t);
        c.g = (int)(c.g * (1 - t) + w.g * t);
        c.b = (int)(c.b * (1 - t) + w.b * t);
        shade *= 1.0 - 0.25 * t;
      }

      px[0] = (uint8_t)clampi((int)(c.r * shade), 0, 255);
      px[1] = (uint8_t)clampi((int)(c.g * shade), 0, 255);
      px[2] = (uint8_t)clampi((int)(c.b * shade), 0, 255);
      px[3] = 255;
    }
  }

  glBindTexture(GL_TEXTURE_2D, g_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, N, N, GL_RGBA, GL_UNSIGNED_BYTE,
                  g_pixels.data());
  g_haveMap = true;
}

void minimapArrowDir(double yaw, double& dx, double& dy) {
  // The view direction is (-sin yaw, -cos yaw) in world XZ. On a north-up
  // map east is +x and north (-Z) is up, and HUD y grows downward, so the
  // world z component maps straight onto screen y.
  dx = -std::sin(yaw);
  dy = -std::cos(yaw);
}

void drawMinimap(int winW, int winH, double playerYaw) {
  if (!g_tex || !g_haveMap) return;

  const double S = MINIMAP_SCREEN;
  double x = winW - MINIMAP_MARGIN - S;
  double y = MINIMAP_MARGIN;

  // backing + frame
  drawRect(x - 3, y - 3, S + 6, S + 6, 0, 0, 0, 0.55);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glColor4d(1, 1, 1, 0.95);
  glBegin(GL_QUADS);
  glTexCoord2d(0, 0); glVertex2d(x, y);          // v=0 is the north edge
  glTexCoord2d(1, 0); glVertex2d(x + S, y);
  glTexCoord2d(1, 1); glVertex2d(x + S, y + S);
  glTexCoord2d(0, 1); glVertex2d(x, y + S);
  glEnd();
  glDisable(GL_TEXTURE_2D);
  drawRectOutline(x - 3, y - 3, S + 6, S + 6, 2, 1, 1, 1, 0.75);

  // Player arrow at the centre. The map is north-up, so the arrow points
  // along the view direction projected onto the map: east is +x, north is
  // -y on screen.
  double cx = x + S / 2, cy = y + S / 2;
  double fx, fz;
  minimapArrowDir(playerYaw, fx, fz);
  double rx = -fz, rz = fx;         // perpendicular, for the arrow's base
  const double TIP = 9.0, BACK = 6.0, SIDE = 5.5;
  glDisable(GL_TEXTURE_2D);
  glBegin(GL_TRIANGLES);
  glColor4d(1, 1, 1, 1);
  glVertex2d(cx + fx * TIP, cy + fz * TIP);
  glVertex2d(cx - fx * BACK + rx * SIDE, cy - fz * BACK + rz * SIDE);
  glVertex2d(cx - fx * BACK - rx * SIDE, cy - fz * BACK - rz * SIDE);
  glColor4d(0.85, 0.1, 0.1, 1); // red core so it reads over any terrain
  glVertex2d(cx + fx * (TIP - 3), cy + fz * (TIP - 3));
  glVertex2d(cx - fx * (BACK - 2) + rx * (SIDE - 2), cy - fz * (BACK - 2) + rz * (SIDE - 2));
  glVertex2d(cx - fx * (BACK - 2) - rx * (SIDE - 2), cy - fz * (BACK - 2) - rz * (SIDE - 2));
  glEnd();

  // Compass letters: fixed, because the map never rotates. Each sits on its
  // own dark tab — terrain runs under them, and white-on-snow was
  // unreadable without a backdrop.
  struct Mark { const char* s; double dx, dy; };
  const Mark marks[4] = {
    { "N", 0, -1 }, { "S", 0, 1 }, { "W", -1, 0 }, { "E", 1, 0 },
  };
  const double TAB = 15;
  for (const Mark& m : marks) {
    double bx = cx + m.dx * (S / 2 - TAB / 2 - 2) - TAB / 2;
    double by = cy + m.dy * (S / 2 - TAB / 2 - 2) - TAB / 2;
    drawRect(bx, by, TAB, TAB, 0, 0, 0, 0.55);
    double tw = textWidth(g_fontCount, m.s);
    drawText(g_fontCount, bx + (TAB - tw) / 2,
             by + (TAB - g_fontCount.height) / 2, m.s,
             m.dx == 0 && m.dy < 0 ? 1.0 : 1.0,   // north gets a warm tint
             m.dx == 0 && m.dy < 0 ? 0.42 : 1.0,  // so it stands out from
             m.dx == 0 && m.dy < 0 ? 0.36 : 1.0,  // the other three
             1);
  }
}
