#include "mesher.h"
#include "textures.h"
#include "win_gl.h"

// Each face: corner offsets (CCW, outward normal), the face slot used to look
// up a texture (0 top / 1 bottom / 2 side), and a baked brightness combining
// the JS version's flat AO tint with its ambient+directional lighting
// (sun from (60,120,40)), gamma-adjusted to match the sRGB output there.
namespace {

struct Face {
  int dir[3];
  int faceSlot;
  uint8_t shade; // 0..255 multiplier baked into vertex color
  int corners[4][3];
};

// tint * min(1, 0.6 + 0.7*max(0, dot(n, sunDir))), sunDir = (3,6,2)/7,
// then ^(1/2.2):  top 1.0, bottom .604, +x .885, -x .736, +z .793, -z .695
const Face FACES[6] = {
  { { 0, 1, 0 },  0, 255, { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } } },
  { { 0, -1, 0 }, 1, 154, { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } } },
  { { 1, 0, 0 },  2, 226, { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } } },
  { { -1, 0, 0 }, 2, 188, { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } } },
  { { 0, 0, 1 },  2, 202, { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } } },
  { { 0, 0, -1 }, 2, 177, { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } } },
};

const double UV_S[4] = { 0, 0, 1, 1 };
const double UV_T[4] = { 0, 1, 1, 0 };

struct GeometryBuilder {
  std::vector<float> positions;
  std::vector<float> uvs;
  std::vector<uint8_t> colors; // RGBA
  std::vector<uint32_t> indices;

  void addFace(const Face& face, int x, int y, int z, uint8_t blockId, uint8_t alpha,
               double shadeScale = 1.0) {
    UVRect uv;
    bool hasUV = getBlockFaceUV(blockId, face.faceSlot, uv);
    uint8_t shade = (uint8_t)clampd(face.shade * shadeScale, 0, 255);
    uint32_t start = (uint32_t)(positions.size() / 3);
    for (int i = 0; i < 4; i++) {
      positions.push_back((float)(x + face.corners[i][0]));
      positions.push_back((float)(y + face.corners[i][1]));
      positions.push_back((float)(z + face.corners[i][2]));
      double s = UV_S[i];
      double t = UV_T[i];
      if (hasUV) {
        uvs.push_back((float)(uv.u0 + s * (uv.u1 - uv.u0)));
        uvs.push_back((float)t);
      } else {
        uvs.push_back(0);
        uvs.push_back(0);
      }
      colors.push_back(shade);
      colors.push_back(shade);
      colors.push_back(shade);
      colors.push_back(alpha);
    }
    indices.push_back(start);
    indices.push_back(start + 1);
    indices.push_back(start + 2);
    indices.push_back(start);
    indices.push_back(start + 2);
    indices.push_back(start + 3);
  }

  // An axis-aligned sub-cell box: the six cube faces shrunk into [lo,hi]
  // (block-space fractions). Corners are lerped from the unit FACES table, so
  // winding and shading match a full cube exactly; UVs sample the patch of
  // the tile each face corresponds to (u along the face's horizontal axis,
  // v along the vertical one), the same convention as the ladder bars.
  void addBox(int x, int y, int z, uint8_t blockId, const double lo[3], const double hi[3]) {
    for (const Face& face : FACES) {
      UVRect uv;
      bool hasUV = getBlockFaceUV(blockId, face.faceSlot, uv);
      uint32_t start = (uint32_t)(positions.size() / 3);
      for (int i = 0; i < 4; i++) {
        double px = lo[0] + face.corners[i][0] * (hi[0] - lo[0]);
        double py = lo[1] + face.corners[i][1] * (hi[1] - lo[1]);
        double pz = lo[2] + face.corners[i][2] * (hi[2] - lo[2]);
        positions.push_back((float)(x + px));
        positions.push_back((float)(y + py));
        positions.push_back((float)(z + pz));
        double s, t;
        if (face.dir[1] != 0) { s = px; t = pz; }      // top / bottom
        else if (face.dir[0] != 0) { s = pz; t = py; } // +/-X
        else { s = px; t = py; }                       // +/-Z
        if (hasUV) {
          uvs.push_back((float)(uv.u0 + s * (uv.u1 - uv.u0)));
          uvs.push_back((float)t);
        } else {
          uvs.push_back(0);
          uvs.push_back(0);
        }
        colors.push_back(face.shade);
        colors.push_back(face.shade);
        colors.push_back(face.shade);
        colors.push_back(255);
      }
      indices.push_back(start);
      indices.push_back(start + 1);
      indices.push_back(start + 2);
      indices.push_back(start);
      indices.push_back(start + 2);
      indices.push_back(start + 3);
    }
  }

  // Same as addBox, but with an explicit atlas tile on every face instead of
  // one resolved from a block id — for a part that shouldn't wear its
  // parent block's own face texture (e.g. a table leg, which used to spread
  // the tabletop's whole-width shadow apron across its own tiny face).
  void addBoxTile(int x, int y, int z, int tile, const double lo[3], const double hi[3]) {
    UVRect uv = tileUV(tile);
    for (const Face& face : FACES) {
      uint32_t start = (uint32_t)(positions.size() / 3);
      for (int i = 0; i < 4; i++) {
        double px = lo[0] + face.corners[i][0] * (hi[0] - lo[0]);
        double py = lo[1] + face.corners[i][1] * (hi[1] - lo[1]);
        double pz = lo[2] + face.corners[i][2] * (hi[2] - lo[2]);
        positions.push_back((float)(x + px));
        positions.push_back((float)(y + py));
        positions.push_back((float)(z + pz));
        double s, t;
        if (face.dir[1] != 0) { s = px; t = pz; }
        else if (face.dir[0] != 0) { s = pz; t = py; }
        else { s = px; t = py; }
        uvs.push_back((float)(uv.u0 + s * (uv.u1 - uv.u0)));
        uvs.push_back((float)t);
        colors.push_back(face.shade);
        colors.push_back(face.shade);
        colors.push_back(face.shade);
        colors.push_back(255);
      }
      indices.push_back(start);
      indices.push_back(start + 1);
      indices.push_back(start + 2);
      indices.push_back(start);
      indices.push_back(start + 2);
      indices.push_back(start + 3);
    }
  }

  // A stair: three stacked treads, each a third of a block tall, each
  // covering a third less of the rise axis than the one below it — a real
  // 3-step staircase silhouette (`facing`: 0 -Z, 1 +Z, 2 -X, 3 +X, from
  // World::stairFacing, the direction it climbs toward). Every edge is
  // pulled in by a hair so a face that shares a plane with a neighbour never
  // z-fights; the tiers' own shared planes get the same treatment against
  // each other. Physics walks the same three boxes (boxCollidesStairs),
  // without the inset.
  void addStairs(int x, int y, int z, uint8_t blockId, int facing) {
    const double EPS = 0.001;
    const double H = 1.0 / 3.0;
    for (int t = 0; t < 3; t++) {
      double cut = t * H;
      double lo[3] = { EPS, t == 0 ? EPS : cut + EPS, EPS };
      double hi[3] = { 1 - EPS, t == 2 ? 1 - EPS : cut + H, 1 - EPS };
      switch (facing) {
        case 0: hi[2] = 1 - cut - EPS; break; // rises toward -Z
        case 1: lo[2] = cut + EPS; break;     // rises toward +Z
        case 2: hi[0] = 1 - cut - EPS; break; // rises toward -X
        case 3: lo[0] = cut + EPS; break;     // rises toward +X
      }
      addBox(x, y, z, blockId, lo, hi);
    }
  }

  // A chest's static body: smaller than a full cell (CHEST_LO/HI below), and
  // open-topped — the lid (chest.cpp) is drawn separately every frame so it
  // can animate, and caps this opening when closed. A dark recessed floor
  // and four inward-facing walls line the opening, so looking down through
  // it while the lid is open reads as an empty box rather than a solid one.
  void addChestBody(int x, int y, int z, uint8_t blockId, const double lo[3], const double hi[3]) {
    // Outer walls + bottom: every face but the top.
    for (const Face& face : FACES) {
      if (face.dir[1] == 1) continue;
      UVRect uv;
      bool hasUV = getBlockFaceUV(blockId, face.faceSlot, uv);
      uint32_t start = (uint32_t)(positions.size() / 3);
      for (int i = 0; i < 4; i++) {
        double px = lo[0] + face.corners[i][0] * (hi[0] - lo[0]);
        double py = lo[1] + face.corners[i][1] * (hi[1] - lo[1]);
        double pz = lo[2] + face.corners[i][2] * (hi[2] - lo[2]);
        positions.push_back((float)(x + px));
        positions.push_back((float)(y + py));
        positions.push_back((float)(z + pz));
        double s = face.dir[0] != 0 ? pz : px, t = py;
        if (hasUV) {
          uvs.push_back((float)(uv.u0 + s * (uv.u1 - uv.u0)));
          uvs.push_back((float)t);
        } else {
          uvs.push_back(0);
          uvs.push_back(0);
        }
        colors.push_back(face.shade);
        colors.push_back(face.shade);
        colors.push_back(face.shade);
        colors.push_back(255);
      }
      indices.push_back(start);
      indices.push_back(start + 1);
      indices.push_back(start + 2);
      indices.push_back(start);
      indices.push_back(start + 2);
      indices.push_back(start + 3);
    }

    // Interior: inset from the outer walls (real wall thickness) and capped
    // by a floor short of the bottom, all flat-shaded — nobody sees the
    // inside of a chest well enough to care about its texture, only that
    // it's dark and hollow.
    const double WALL = 0.0625;
    const double in[3][2] = {
      { lo[0] + WALL, hi[0] - WALL }, { lo[1] + WALL, hi[1] }, { lo[2] + WALL, hi[2] - WALL },
    };
    const uint8_t DARK = 55;
    auto quad = [&](double p0[3], double p1[3], double p2[3], double p3[3]) {
      uint32_t start = (uint32_t)(positions.size() / 3);
      double* pts[4] = { p0, p1, p2, p3 };
      for (int i = 0; i < 4; i++) {
        positions.push_back((float)(x + pts[i][0]));
        positions.push_back((float)(y + pts[i][1]));
        positions.push_back((float)(z + pts[i][2]));
        uvs.push_back(0);
        uvs.push_back(0);
        colors.push_back(DARK);
        colors.push_back(DARK);
        colors.push_back(DARK);
        colors.push_back(255);
      }
      indices.push_back(start);
      indices.push_back(start + 1);
      indices.push_back(start + 2);
      indices.push_back(start);
      indices.push_back(start + 2);
      indices.push_back(start + 3);
    };
    // floor (normal +Y, so it's visible looking down through the opening)
    {
      double p0[3] = { in[0][0], in[1][0], in[2][0] }, p1[3] = { in[0][0], in[1][0], in[2][1] };
      double p2[3] = { in[0][1], in[1][0], in[2][1] }, p3[3] = { in[0][1], in[1][0], in[2][0] };
      quad(p0, p1, p2, p3);
    }
    // +x inner wall (inward normal -x, at the outer +x side)
    {
      double p0[3] = { in[0][1], in[1][0], in[2][0] }, p1[3] = { in[0][1], in[1][0], in[2][1] };
      double p2[3] = { in[0][1], in[1][1], in[2][1] }, p3[3] = { in[0][1], in[1][1], in[2][0] };
      quad(p0, p1, p2, p3);
    }
    // -x inner wall (inward normal +x, at the outer -x side)
    {
      double p0[3] = { in[0][0], in[1][0], in[2][0] }, p1[3] = { in[0][0], in[1][1], in[2][0] };
      double p2[3] = { in[0][0], in[1][1], in[2][1] }, p3[3] = { in[0][0], in[1][0], in[2][1] };
      quad(p0, p1, p2, p3);
    }
    // +z inner wall (inward normal -z, at the outer +z side)
    {
      double p0[3] = { in[0][0], in[1][0], in[2][1] }, p1[3] = { in[0][0], in[1][1], in[2][1] };
      double p2[3] = { in[0][1], in[1][1], in[2][1] }, p3[3] = { in[0][1], in[1][0], in[2][1] };
      quad(p0, p1, p2, p3);
    }
    // -z inner wall (inward normal +z, at the outer -z side)
    {
      double p0[3] = { in[0][1], in[1][0], in[2][0] }, p1[3] = { in[0][1], in[1][1], in[2][0] };
      double p2[3] = { in[0][0], in[1][1], in[2][0] }, p3[3] = { in[0][0], in[1][0], in[2][0] };
      quad(p0, p1, p2, p3);
    }
  }

  // A plant: two quads crossing through the cell, drawn double-sided. Size
  // varies per position so a patch of grass isn't a field of clones.
  // One rail or rung of a ladder: a thin box standing off the wall.
  // `facing` says which wall it hangs on: 0 -Z, 1 +Z, 2 -X, 3 +X, anything
  // else stands it in the middle. `wide0..wide1` is the box's extent along
  // the wall (x for a Z-facing wall, z for an X-facing one); `y0..y1` is its
  // vertical extent. Both are block-space fractions, so they double as the
  // UV coordinate on that axis — each bar samples exactly the patch of the
  // ladder texture it corresponds to, rather than the whole tile squeezed
  // onto every bar.
  void addLadderBar(int x, int y, int z, uint8_t blockId, int facing,
                     double wide0, double wide1, double y0, double y1) {
    UVRect uv;
    if (!getBlockFaceUV(blockId, 2, uv)) return; // its side texture
    const double T = 0.14; // thickness off the wall
    double x0, x1, z0, z1;
    bool wideIsZ = facing == 2 || facing == 3;
    switch (facing) {
      case 0: x0 = wide0; x1 = wide1; z0 = 0; z1 = T; break;
      case 1: x0 = wide0; x1 = wide1; z0 = 1 - T; z1 = 1; break;
      case 2: z0 = wide0; z1 = wide1; x0 = 0; x1 = T; break;
      case 3: z0 = wide0; z1 = wide1; x0 = 1 - T; x1 = 1; break;
      default: x0 = wide0; x1 = wide1; z0 = 0.5 - T / 2; z1 = 0.5 + T / 2; break;
    }

    const double faces[6][4][3] = {
      { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 } }, // -Z
      { { x1, y0, z1 }, { x0, y0, z1 }, { x0, y1, z1 }, { x1, y1, z1 } }, // +Z
      { { x0, y0, z1 }, { x0, y0, z0 }, { x0, y1, z0 }, { x0, y1, z1 } }, // -X
      { { x1, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x1, y1, z0 } }, // +X
      { { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } }, // top
      { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y0, z0 }, { x0, y0, z0 } }, // bottom
    };
    // Same baked directional shades as a regular cube face, so a ladder
    // lights the same as the wall it hangs on.
    const uint8_t SHADES[6] = { 177, 202, 188, 226, 255, 154 };
    for (int fq = 0; fq < 6; fq++) {
      uint32_t start = (uint32_t)(positions.size() / 3);
      for (int i = 0; i < 4; i++) {
        double px = faces[fq][i][0], py = faces[fq][i][1], pz = faces[fq][i][2];
        positions.push_back((float)(x + px));
        positions.push_back((float)(y + py));
        positions.push_back((float)(z + pz));
        double wideCoord = wideIsZ ? pz : px;
        uvs.push_back((float)(uv.u0 + wideCoord * (uv.u1 - uv.u0)));
        uvs.push_back((float)py);
        uint8_t shade = SHADES[fq];
        colors.push_back(shade);
        colors.push_back(shade);
        colors.push_back(shade);
        colors.push_back(255);
      }
      indices.push_back(start);
      indices.push_back(start + 1);
      indices.push_back(start + 2);
      indices.push_back(start);
      indices.push_back(start + 2);
      indices.push_back(start + 3);
    }
  }

  // A ladder: two vertical rails and four rungs, each its own bar with real
  // gaps between them instead of a picture of gaps painted on a slab. The
  // layout is lifted straight from BLK_LADDER_ROWS (item_art.cpp) so the 3D
  // shape matches the icon.
  void addLadder(int x, int y, int z, uint8_t blockId, int facing) {
    static const double BARS[6][4] = {
      // wide0,  wide1,  y0,     y1
      { 0.125,  0.25,   0,      1     }, // left rail
      { 0.75,   0.875,  0,      1     }, // right rail
      { 0.25,   0.75,   0.0625, 0.125 }, // rung
      { 0.25,   0.75,   0.3125, 0.375 }, // rung
      { 0.25,   0.75,   0.5625, 0.625 }, // rung
      { 0.25,   0.75,   0.8125, 0.875 }, // rung
    };
    for (const auto& b : BARS) addLadderBar(x, y, z, blockId, facing, b[0], b[1], b[2], b[3]);
  }

  // One bar of a fence panel, in FOOTPRINT-LOCAL coordinates: `wideLo..
  // wideHi` spans 0..FENCE_PANEL_WIDE across the perpendicular-to-facing
  // axis, `yLo..yHi` spans 0..FENCE_PANEL_TALL vertically, `depthLo..depthHi`
  // is a sub-cell range (0..1) centred on the facing axis. A bar that
  // straddles a cell boundary (most of them do — the panel is 2 cells wide
  // and/or tall) is split into up to 4 per-cell addBox calls, each clamped
  // to that cell's own 0..1 window, so its UVs still tile per-cell instead
  // of stretching across the seam (the same trick addTable/addBed use for
  // their merged shapes).
  void addFencePanelBar(int ax, int ay, int az, uint8_t blockId, int facing,
                        double wideLo, double wideHi, double yLo, double yHi,
                        double depthLo, double depthHi) {
    const int DX[4] = { 0, 0, -1, 1 };
    const int DZ[4] = { -1, 1, 0, 0 };
    int lx = DX[facing], lz = DZ[facing]; // thin/depth axis
    int wx = -lz, wz = lx;                // wide axis (perpendicular)
    for (int wc = 0; wc < 2; wc++) {
      double wLo = clampd(wideLo - wc, 0, 1), wHi = clampd(wideHi - wc, 0, 1);
      if (wHi <= wLo) continue;
      for (int yc = 0; yc < 2; yc++) {
        double yLoC = clampd(yLo - yc, 0, 1), yHiC = clampd(yHi - yc, 0, 1);
        if (yHiC <= yLoC) continue;
        int cx = ax + wx * wc, cy = ay + yc, cz = az + wz * wc;
        double lo[3], hi[3];
        lo[1] = yLoC;
        hi[1] = yHiC;
        if (lx != 0) { // facing along X: depth is X, wide is Z
          lo[0] = depthLo; hi[0] = depthHi;
          lo[2] = wLo; hi[2] = wHi;
        } else { // facing along Z: depth is Z, wide is X
          lo[0] = wLo; hi[0] = wHi;
          lo[2] = depthLo; hi[2] = depthHi;
        }
        addBox(cx, cy, cz, blockId, lo, hi);
      }
    }
  }

  // A fence panel: a directional wall standing wherever the player faced
  // when they placed it (see fencePanelFootprint in blocks.h), `x,y,z` being
  // the anchor (bottom corner). The stone fence ("wall") is one solid slab
  // filling the whole 2x2xTHICK box; the wood fence is 3 evenly-spaced
  // vertical pickets plus 2 evenly-spaced horizontal rails within it.
  void addFencePanel(int x, int y, int z, uint8_t blockId, int facing) {
    // Both materials are a solid slab filling the whole panel box now — the
    // wood fence's picket-and-rail look didn't read well at this scale, so
    // it matches the stone fence's "wall" shape and differs only by texture
    // (CRAFT_BLOCKS gives each its own).
    const double DEPTH_LO = 0.5 - FENCE_PANEL_THICK / 2, DEPTH_HI = 0.5 + FENCE_PANEL_THICK / 2;
    addFencePanelBar(x, y, z, blockId, facing, 0, FENCE_PANEL_WIDE, 0, FENCE_PANEL_TALL,
                     DEPTH_LO, DEPTH_HI);
  }

  // A furnace: a squat hollow box the same footprint as a chest (see
  // chest.h's CHEST_HINGE_*/CHEST_W/D), solid on every side except the one
  // it was placed to front. That side keeps its stone frame (the "M" bezel
  // in BLK_FURNACE_ROWS, item_art.cpp) but the black rectangle in the middle
  // is now a real opening rather than a painted illusion, with a small dark
  // firebox recessed behind it — the fire itself (furnace.cpp) is drawn
  // separately every frame so it can flicker and be toggled off.
  void addFurnaceBody(int x, int y, int z, uint8_t blockId, const double lo[3], const double hi[3],
                      int facing) {
    int openDir[3] = { 0, 0, 0 };
    switch (facing) {
      case 0: openDir[2] = -1; break;
      case 1: openDir[2] = 1; break;
      case 2: openDir[0] = -1; break;
      default: openDir[0] = 1; break;
    }

    for (const Face& face : FACES) {
      if (face.dir[0] == openDir[0] && face.dir[1] == openDir[1] && face.dir[2] == openDir[2]) continue;
      UVRect uv;
      bool hasUV = getBlockFaceUV(blockId, face.faceSlot, uv);
      uint32_t start = (uint32_t)(positions.size() / 3);
      for (int i = 0; i < 4; i++) {
        double px = lo[0] + face.corners[i][0] * (hi[0] - lo[0]);
        double py = lo[1] + face.corners[i][1] * (hi[1] - lo[1]);
        double pz = lo[2] + face.corners[i][2] * (hi[2] - lo[2]);
        positions.push_back((float)(x + px));
        positions.push_back((float)(y + py));
        positions.push_back((float)(z + pz));
        double s, t;
        if (face.dir[1] != 0) { s = px; t = pz; }
        else if (face.dir[0] != 0) { s = pz; t = py; }
        else { s = px; t = py; }
        if (hasUV) {
          uvs.push_back((float)(uv.u0 + s * (uv.u1 - uv.u0)));
          uvs.push_back((float)t);
        } else {
          uvs.push_back(0);
          uvs.push_back(0);
        }
        colors.push_back(face.shade);
        colors.push_back(face.shade);
        colors.push_back(face.shade);
        colors.push_back(255);
      }
      indices.push_back(start);
      indices.push_back(start + 1);
      indices.push_back(start + 2);
      indices.push_back(start);
      indices.push_back(start + 2);
      indices.push_back(start + 3);
    }

    // The opening's frame: four thin boxes tracing the bezel — columns/rows
    // 3..12 of 16 are the mouth in BLK_FURNACE_ROWS, so bezel fractions are
    // 3/16, 10/16 and 13/16 of the front face.
    const double WALL = 0.0625;
    const double BX0 = 3.0 / 16, BX1 = 13.0 / 16, BY0 = 3.0 / 16, BY1 = 10.0 / 16;
    double sx = hi[0] - lo[0], sy = hi[1] - lo[1], sz = hi[2] - lo[2];

    auto frameBox = [&](double tlo, double thi, double ylo, double yhi, double dlo, double dhi,
                        bool alongX) {
      double blo[3], bhi[3];
      blo[1] = lo[1] + ylo * sy;
      bhi[1] = lo[1] + yhi * sy;
      if (alongX) {
        blo[0] = lo[0] + tlo * sx;
        bhi[0] = lo[0] + thi * sx;
        blo[2] = dlo;
        bhi[2] = dhi;
      } else {
        blo[2] = lo[2] + tlo * sz;
        bhi[2] = lo[2] + thi * sz;
        blo[0] = dlo;
        bhi[0] = dhi;
      }
      addBox(x, y, z, blockId, blo, bhi);
    };

    double dlo, dhi;
    bool alongX;
    switch (facing) {
      case 0: dlo = lo[2]; dhi = lo[2] + WALL; alongX = true; break;
      case 1: dlo = hi[2] - WALL; dhi = hi[2]; alongX = true; break;
      case 2: dlo = lo[0]; dhi = lo[0] + WALL; alongX = false; break;
      default: dlo = hi[0] - WALL; dhi = hi[0]; alongX = false; break;
    }
    frameBox(0, 1, BY1, 1, dlo, dhi, alongX);
    frameBox(0, 1, 0, BY0, dlo, dhi, alongX);
    frameBox(0, BX0, BY0, BY1, dlo, dhi, alongX);
    frameBox(BX1, 1, BY0, BY1, dlo, dhi, alongX);

    // A thin back wall only — flush against the furnace's true interior
    // back face, reusing bedrock's near-black texture as a dark backdrop.
    // This used to be a fully opaque box spanning almost the whole depth,
    // meant to read as a hollow interior but actually filling nearly all of
    // it — the fire (drawn separately, furnace.cpp) ended up positioned
    // behind that solid geometry and lost the depth test entirely. Leaving
    // the middle of the chamber genuinely empty (nothing solid between the
    // opening and this back plate) is what actually makes it hollow.
    double ilo[3], ihi[3];
    ilo[1] = lo[1] + BY0 * sy;
    ihi[1] = lo[1] + BY1 * sy;
    if (alongX) {
      ilo[0] = lo[0] + BX0 * sx;
      ihi[0] = lo[0] + BX1 * sx;
      if (facing == 0) { ilo[2] = hi[2] - 2 * WALL; ihi[2] = hi[2] - WALL; }
      else { ilo[2] = lo[2] + WALL; ihi[2] = lo[2] + 2 * WALL; }
    } else {
      ilo[2] = lo[2] + BX0 * sz;
      ihi[2] = lo[2] + BX1 * sz;
      if (facing == 2) { ilo[0] = hi[0] - 2 * WALL; ihi[0] = hi[0] - WALL; }
      else { ilo[0] = lo[0] + WALL; ihi[0] = lo[0] + 2 * WALL; }
    }
    addBox(x, y, z, BLOCK_BEDROCK, ilo, ihi);
  }

  // A crafting table: a thin top on four corner legs rather than a cube, two
  // cells long (see tableFootprint in blocks.h). `x,y,z` is the anchor cell;
  // one addBox call per footprint cell, each stretched to the world-space
  // boundary it shares with its neighbour so the top reads as one continuous
  // slab with no seam, and legs only at the 4 true outer corners.
  void addTable(int x, int y, int z, uint8_t blockId, int facing) {
    const double TOP_Y0 = 0.75, TOP_Y1 = 0.875;
    const double MARGIN = 0.0625;
    const double LEG = 0.125;

    int cells[TABLE_FOOTPRINT_CELLS][3];
    tableFootprint(facing, cells);

    for (int i = 0; i < TABLE_FOOTPRINT_CELLS; i++) {
      int cx = cells[i][0], cz = cells[i][2];
      bool hasNegX = false, hasPosX = false, hasNegZ = false, hasPosZ = false;
      for (int j = 0; j < TABLE_FOOTPRINT_CELLS; j++) {
        if (j == i) continue;
        if (cells[j][0] == cx - 1 && cells[j][2] == cz) hasNegX = true;
        if (cells[j][0] == cx + 1 && cells[j][2] == cz) hasPosX = true;
        if (cells[j][0] == cx && cells[j][2] == cz - 1) hasNegZ = true;
        if (cells[j][0] == cx && cells[j][2] == cz + 1) hasPosZ = true;
      }
      double topLo[3] = { hasNegX ? 0 : MARGIN, TOP_Y0, hasNegZ ? 0 : MARGIN };
      double topHi[3] = { hasPosX ? 1 : 1 - MARGIN, TOP_Y1, hasPosZ ? 1 : 1 - MARGIN };
      addBox(x + cx, y, z + cz, blockId, topLo, topHi);

      // Plain planks, not the tabletop's own side texture — that texture is
      // a whole-tabletop-width shadow apron, and spread across one tiny leg
      // face it read as solid black.
      const double legLo = MARGIN, legHi = 1 - MARGIN - LEG;
      if (!hasNegX && !hasNegZ) {
        double llo[3] = { legLo, 0, legLo }, lhi[3] = { legLo + LEG, TOP_Y0, legLo + LEG };
        addBoxTile(x + cx, y, z + cz, TILE_BLK_PLANKS, llo, lhi);
      }
      if (!hasPosX && !hasNegZ) {
        double llo[3] = { legHi, 0, legLo }, lhi[3] = { legHi + LEG, TOP_Y0, legLo + LEG };
        addBoxTile(x + cx, y, z + cz, TILE_BLK_PLANKS, llo, lhi);
      }
      if (!hasNegX && !hasPosZ) {
        double llo[3] = { legLo, 0, legHi }, lhi[3] = { legLo + LEG, TOP_Y0, legHi + LEG };
        addBoxTile(x + cx, y, z + cz, TILE_BLK_PLANKS, llo, lhi);
      }
      if (!hasPosX && !hasPosZ) {
        double llo[3] = { legHi, 0, legHi }, lhi[3] = { legHi + LEG, TOP_Y0, legHi + LEG };
        addBoxTile(x + cx, y, z + cz, TILE_BLK_PLANKS, llo, lhi);
      }
    }
  }

  // A bed: a flat mattress spanning its 3x2 footprint (bedFootprint in
  // blocks.h), a raised pillow box at the head end (farthest from the
  // anchor/foot), and 4 short legs at the true outer corners. `facing` is
  // the direction the bed points, foot to head, matching whichever way the
  // player faced when they placed it.
  void addBed(int x, int y, int z, uint8_t blockId, int facing) {
    const double MAT_Y0 = 0.03, MAT_Y1 = 0.5625;
    const double PILLOW_Y1 = 0.75;
    const double MARGIN = 0.03125;
    const double LEG = 0.1875;

    int cells[BED_FOOTPRINT_CELLS][3];
    bedFootprint(facing, cells);
    const int DX[4] = { 0, 0, -1, 1 };
    const int DZ[4] = { -1, 1, 0, 0 };
    int lx = DX[facing], lz = DZ[facing]; // long axis unit vector, foot -> head

    for (int i = 0; i < BED_FOOTPRINT_CELLS; i++) {
      int cx = cells[i][0], cz = cells[i][2];
      bool hasNegX = false, hasPosX = false, hasNegZ = false, hasPosZ = false;
      for (int j = 0; j < BED_FOOTPRINT_CELLS; j++) {
        if (j == i) continue;
        if (cells[j][0] == cx - 1 && cells[j][2] == cz) hasNegX = true;
        if (cells[j][0] == cx + 1 && cells[j][2] == cz) hasPosX = true;
        if (cells[j][0] == cx && cells[j][2] == cz - 1) hasNegZ = true;
        if (cells[j][0] == cx && cells[j][2] == cz + 1) hasPosZ = true;
      }
      // Projection onto the long axis: lx,lz is a unit basis vector, so this
      // recovers the cell's row index (0..2) regardless of which of the 2
      // width columns it's in.
      bool isHead = (cx * lx + cz * lz) == 2;
      double topY = isHead ? PILLOW_Y1 : MAT_Y1;
      double matLo[3] = { hasNegX ? 0 : MARGIN, MAT_Y0, hasNegZ ? 0 : MARGIN };
      double matHi[3] = { hasPosX ? 1 : 1 - MARGIN, topY, hasPosZ ? 1 : 1 - MARGIN };
      addBox(x + cx, y, z + cz, blockId, matLo, matHi);

      const double legLo = MARGIN, legHi = 1 - MARGIN - LEG;
      if (!hasNegX && !hasNegZ) {
        double llo[3] = { legLo, 0, legLo }, lhi[3] = { legLo + LEG, MAT_Y0, legLo + LEG };
        addBox(x + cx, y, z + cz, blockId, llo, lhi);
      }
      if (!hasPosX && !hasNegZ) {
        double llo[3] = { legHi, 0, legLo }, lhi[3] = { legHi + LEG, MAT_Y0, legLo + LEG };
        addBox(x + cx, y, z + cz, blockId, llo, lhi);
      }
      if (!hasNegX && !hasPosZ) {
        double llo[3] = { legLo, 0, legHi }, lhi[3] = { legLo + LEG, MAT_Y0, legHi + LEG };
        addBox(x + cx, y, z + cz, blockId, llo, lhi);
      }
      if (!hasPosX && !hasPosZ) {
        double llo[3] = { legHi, 0, legHi }, lhi[3] = { legHi + LEG, MAT_Y0, legHi + LEG };
        addBox(x + cx, y, z + cz, blockId, llo, lhi);
      }
    }
  }

  void addPlant(int x, int y, int z, uint8_t blockId, double height, double width) {
    UVRect uv;
    if (!getBlockFaceUV(blockId, 0, uv)) return;
    double c = 0.5, half = width * 0.5;
    double lo = c - half, hi = c + half;
    const double quads[2][4][3] = {
      { { lo, 0, lo }, { hi, 0, hi }, { hi, height, hi }, { lo, height, lo } },
      { { hi, 0, lo }, { lo, 0, hi }, { lo, height, hi }, { hi, height, lo } },
    };
    const double us[4] = { 0, 1, 1, 0 };
    const double vs[4] = { 0, 0, 1, 1 };
    for (int q = 0; q < 2; q++) {
      uint32_t start = (uint32_t)(positions.size() / 3);
      for (int i = 0; i < 4; i++) {
        positions.push_back((float)(x + quads[q][i][0]));
        positions.push_back((float)(y + quads[q][i][1]));
        positions.push_back((float)(z + quads[q][i][2]));
        uvs.push_back((float)(uv.u0 + us[i] * (uv.u1 - uv.u0)));
        uvs.push_back((float)vs[i]);
        colors.push_back(235);
        colors.push_back(235);
        colors.push_back(235);
        colors.push_back(255);
      }
      indices.push_back(start);
      indices.push_back(start + 1);
      indices.push_back(start + 2);
      indices.push_back(start);
      indices.push_back(start + 2);
      indices.push_back(start + 3);
    }
  }

  bool empty() const { return positions.empty(); }

  // Compiles the geometry into a display list, with vertices translated to
  // world space (originX/originZ) so no per-frame matrix work is needed.
  unsigned int compile(int originX, int originZ) {
    if (empty()) return 0;
    for (size_t i = 0; i < positions.size(); i += 3) {
      positions[i] += (float)originX;
      positions[i + 2] += (float)originZ;
    }
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, positions.data());
    glTexCoordPointer(2, GL_FLOAT, 0, uvs.data());
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors.data());
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, indices.data());
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glEndList();
    return list;
  }
};

// Per-position hash, so a plant's size is stable across remeshes.
double plantHash(int x, int z, uint32_t salt) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + salt;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (double)h / 4294967296.0;
}

} // namespace

uint8_t waterSurfaceAlpha(int depth) {
  if (depth >= WATER_OPAQUE_DEPTH) return 255; // can't see the bottom at all
  double t = clampd((double)depth / WATER_OPAQUE_DEPTH, 0, 1);
  return (uint8_t)std::lround(140 + t * (255 - 140));
}

// Builds opaque + water geometry for one chunk. Neighbor lookups cross chunk
// boundaries via world.getBlock in world space; if a neighboring chunk hasn't
// been generated yet, its blocks read as AIR, which very occasionally
// over-draws a boundary face until the neighbor loads.
void remeshChunk(World& world, Chunk& chunk) {
  GeometryBuilder opaque, water, plants;
  int ox = chunk.worldOriginX();
  int oz = chunk.worldOriginZ();

  for (int y = 0; y < CHUNK_HEIGHT; y++) {
    for (int z = 0; z < CHUNK_SIZE; z++) {
      for (int x = 0; x < CHUNK_SIZE; x++) {
        uint8_t id = chunk.getLocal(x, y, z);
        if (id == BLOCK_AIR) continue;

        int wx = ox + x;
        int wz = oz + z;

        if (isPanel(id)) {
          plants.addLadder(x, y, z, id, world.panelFacing(wx, y, wz));
          continue;
        }

        if (isStairs(id)) {
          opaque.addStairs(x, y, z, id, world.stairFacing(wx, y, wz));
          continue;
        }

        if (isChest(id)) {
          // Only the static body: chest.cpp draws the lid separately every
          // frame so it can animate open/closed. Sized from the shared
          // constants in chest.h so the lid sits flush on top of it.
          const double lo[3] = { CHEST_HINGE_X, 0, CHEST_HINGE_Z };
          const double hi[3] = { CHEST_HINGE_X + CHEST_W, CHEST_HINGE_Y, CHEST_HINGE_Z + CHEST_D };
          opaque.addChestBody(x, y, z, id, lo, hi);
          continue;
        }

        if (isSlab(id)) {
          // The bottom half of the cell, full width — matches
          // boxCollidesSlab in physics.cpp.
          const double lo[3] = { 0, 0, 0 };
          const double hi[3] = { 1, 0.5, 1 };
          opaque.addBox(x, y, z, id, lo, hi);
          continue;
        }

        if (isCampfire(id)) {
          // A low pile, full width — matches boxCollidesCampfire in
          // physics.cpp. The permanent flame on top is drawn separately
          // every frame (campfire.cpp), the same reason a furnace's fire
          // isn't baked into its static body either.
          const double lo[3] = { 0, 0, 0 };
          const double hi[3] = { 1, CAMPFIRE_HEIGHT, 1 };
          opaque.addBox(x, y, z, id, lo, hi);
          continue;
        }

        if (isTrapdoor(id)) {
          // No static geometry: a trapdoor swings open/closed (trapdoor.cpp
          // draws it fresh every frame, the same reason a door isn't baked
          // here either), so this cell only ever holds the id trapdoor.cpp
          // and the physics read back.
          continue;
        }

        if (isDoor(id)) {
          // No static geometry: a door swings open/closed (door.cpp draws it
          // fresh every frame, the same reason a chest's lid isn't baked
          // here either), so this cell only ever holds the id doors.cpp and
          // the physics read back.
          continue;
        }

        if (isFurnace(id)) {
          // Same footprint as a chest (see chest.h) but solid on top, with a
          // real opening on the side it was placed to front.
          const double lo[3] = { CHEST_HINGE_X, 0, CHEST_HINGE_Z };
          const double hi[3] = { CHEST_HINGE_X + CHEST_W, CHEST_HINGE_Y + CHEST_LID_H, CHEST_HINGE_Z + CHEST_D };
          opaque.addFurnaceBody(x, y, z, id, lo, hi, world.furnaceFacing(wx, y, wz));
          continue;
        }

        if (isTable(id) || isBed(id) || isAnyFence(id)) {
          // Multi-cell footprint (blocks.h): every cell holds the same id,
          // but only the anchor cell actually emits geometry — the shape
          // spans the whole footprint from there, same idea as a door having
          // no static mesh at all on its own cells.
          auto it = world.furniture.find({ wx, y, wz });
          int facing = it != world.furniture.end() ? it->second.facing : 0;
          bool isAnchor = it == world.furniture.end() ||
                          (it->second.anchorX == wx && it->second.anchorY == y && it->second.anchorZ == wz);
          if (isAnchor) {
            if (isTable(id)) opaque.addTable(x, y, z, id, facing);
            else if (isBed(id)) opaque.addBed(x, y, z, id, facing);
            else opaque.addFencePanel(x, y, z, id, facing);
          }
          continue;
        }

        if (isPlant(id)) {
          double h, w;
          int stage = cropStage(id);
          if (stage >= 0) {
            // Crops grow visibly taller each stage instead of varying
            // randomly per position — the height IS the growth readout.
            h = 0.22 + stage * 0.22;
            w = 0.6;
          } else {
            // Height and width vary per position: some tufts are ankle-high,
            // others reach past a block.
            h = 0.45 + plantHash(wx, wz, 0x9E37u) * 1.05;
            w = 0.80 + plantHash(wx, wz, 0x85EBu) * 0.35;
          }
          plants.addPlant(x, y, z, id, h, w);
          continue;
        }

        if (id == BLOCK_WATER) {
          uint8_t above = world.getBlock(wx, y + 1, wz);
          if (above == BLOCK_AIR) {
            // Opacity follows how deep the water is here: a shallow pool
            // shows its bed, deep ocean hides it. Deep water is darkened
            // too, so depth reads at a glance from the surface.
            int depth = 0;
            for (int dy = 0; dy <= WATER_OPAQUE_DEPTH; dy++) {
              if (world.getBlock(wx, y - dy, wz) != BLOCK_WATER) break;
              depth++;
            }
            double t = clampd((double)depth / WATER_OPAQUE_DEPTH, 0, 1);
            water.addFace(FACES[0], x, y, z, id, waterSurfaceAlpha(depth), 1.0 - 0.35 * t);
          }
          continue;
        }

        for (const Face& face : FACES) {
          uint8_t neighbor = world.getBlock(wx + face.dir[0], y + face.dir[1], wz + face.dir[2]);
          if (isEmptyForMeshing(neighbor)) {
            opaque.addFace(face, x, y, z, id, 255);
          }
        }
      }
    }
  }

  disposeChunk(chunk);
  chunk.listOpaque = opaque.compile(ox, oz);
  chunk.listPlants = plants.compile(ox, oz);
  chunk.listWater = water.compile(ox, oz);
  chunk.dirty = false;
}

void disposeChunk(Chunk& chunk) {
  if (chunk.listOpaque) {
    glDeleteLists(chunk.listOpaque, 1);
    chunk.listOpaque = 0;
  }
  if (chunk.listPlants) {
    glDeleteLists(chunk.listPlants, 1);
    chunk.listPlants = 0;
  }
  if (chunk.listWater) {
    glDeleteLists(chunk.listWater, 1);
    chunk.listWater = 0;
  }
}
