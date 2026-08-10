#include "textures.h"
#include "blocks.h"
#include "item_art.h"
#include "noise.h"
#include "sprites_generated.h"

namespace {

struct Color { int r, g, b; };

constexpr Color hex(uint32_t rgb) {
  return { int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF) };
}

Color mix(Color a, Color b, double t) {
  return {
    (int)std::lround(a.r + (b.r - a.r) * t),
    (int)std::lround(a.g + (b.g - a.g) * t),
    (int)std::lround(a.b + (b.b - a.b) * t),
  };
}

// Mimics the 2D canvas context the JS tile drawers used: fillStyle + fillRect
// into one tile of the atlas. Canvas y=0 is the tile's TOP row; we flip to
// OpenGL bottom-up row order as we write.
struct TileCtx {
  Atlas* atlas;
  int tileBase; // x pixel offset of this tile
  Color fill = { 0, 0, 0 };

  void setFill(Color c) { fill = c; }

  // ATLAS-space pixel with an explicit alpha, for supplied art: coordinates
  // run 0..ATLAS_TILE_PX so the full detail of the image is kept, and
  // fillRect's always-opaque write would flatten its transparency.
  void putRGBA(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || x >= ATLAS_TILE_PX || y < 0 || y >= ATLAS_TILE_PX) return;
    int row = ATLAS_TILE_PX - 1 - y; // canvas top row -> last GL row
    uint8_t* p = &atlas->pixels[(size_t)(row * atlas->width + tileBase + x) * 4];
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
  }

  // Procedural drawers work on the TILE_PX grid; each logical pixel lands as
  // an ATLAS_SCALE square, so raising the storage resolution leaves every
  // block texture byte-for-byte what it was.
  void fillRect(int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; yy++) {
      for (int xx = x; xx < x + w; xx++) {
        if (xx < 0 || xx >= TILE_PX || yy < 0 || yy >= TILE_PX) continue;
        for (int sy = 0; sy < ATLAS_SCALE; sy++) {
          for (int sx = 0; sx < ATLAS_SCALE; sx++) {
            int ax = xx * ATLAS_SCALE + sx;
            int ay = yy * ATLAS_SCALE + sy;
            int row = ATLAS_TILE_PX - 1 - ay;
            uint8_t* p = &atlas->pixels[(size_t)(row * atlas->width + tileBase + ax) * 4];
            p[0] = (uint8_t)fill.r;
            p[1] = (uint8_t)fill.g;
            p[2] = (uint8_t)fill.b;
            p[3] = 255;
          }
        }
      }
    }
  }
};

void speckle(TileCtx& ctx, Mulberry32& rng, Color base, Color dark, Color light,
             double darkChance = 0.35, double lightChance = 0.15) {
  ctx.setFill(base);
  ctx.fillRect(0, 0, TILE_PX, TILE_PX);
  for (int y = 0; y < TILE_PX; y++) {
    for (int x = 0; x < TILE_PX; x++) {
      double r = rng.next();
      if (r < darkChance) {
        ctx.setFill(mix(base, dark, 0.35 + rng.next() * 0.5));
        ctx.fillRect(x, y, 1, 1);
      } else if (r > 1 - lightChance) {
        ctx.setFill(mix(base, light, 0.35 + rng.next() * 0.5));
        ctx.fillRect(x, y, 1, 1);
      }
    }
  }
}

void drawGrassTop(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0x5fa832), hex(0x3f7a1f), hex(0x7cc44a), 0.4, 0.2);
}

void drawGrassSide(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0x8a5a34), hex(0x6b431f), hex(0xa0724a), 0.35, 0.1);
  // grass overhang on top edge
  for (int x = 0; x < TILE_PX; x++) {
    int h = 4 + (int)(rng.next() * 2);
    for (int y = 0; y < h; y++) {
      double r = rng.next();
      c.setFill(r < 0.5 ? hex(0x5fa832) : r < 0.8 ? hex(0x4c8f27) : hex(0x7cc44a));
      c.fillRect(x, y, 1, 1);
    }
  }
}

void drawDirt(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0x7a5230), hex(0x5c3c20), hex(0x93673f), 0.35, 0.12);
}

void drawStone(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0x8a8a8e), hex(0x6a6a6e), hex(0xa6a6aa), 0.35, 0.15);
}

void drawSand(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0xddcb8a), hex(0xc4b06a), hex(0xeee0ab), 0.3, 0.15);
}

void drawWoodTop(TileCtx& c, Mulberry32& rng) {
  c.setFill(hex(0xb98a52));
  c.fillRect(0, 0, TILE_PX, TILE_PX);
  double cx = TILE_PX / 2.0 - 0.5;
  double cy = TILE_PX / 2.0 - 0.5;
  for (int y = 0; y < TILE_PX; y++) {
    for (int x = 0; x < TILE_PX; x++) {
      double d = std::hypot(x - cx, y - cy);
      int ring = (int)std::floor(d) % 3;
      double jitter = rng.next() * 0.15;
      c.setFill(ring == 0 ? mix(hex(0xb98a52), hex(0x7a5227), 0.5 + jitter) : hex(0xb98a52));
      c.fillRect(x, y, 1, 1);
    }
  }
}

void drawWoodSide(TileCtx& c, Mulberry32& rng) {
  for (int x = 0; x < TILE_PX; x++) {
    bool stripe = (x % 4 == 0);
    for (int y = 0; y < TILE_PX; y++) {
      Color base = stripe ? hex(0x7a5227) : hex(0x96693a);
      c.setFill(mix(base, hex(0x5c3c1a), rng.next() * 0.25));
      c.fillRect(x, y, 1, 1);
    }
  }
}

void drawLeaves(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0x3f8f2a), hex(0x256b16), hex(0x5db83a), 0.45, 0.2);
}

void drawWater(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0x2f6fd6), hex(0x1f4fa8), hex(0x5b96ec), 0.3, 0.2);
}

void drawBedrock(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0x3a3a3d), hex(0x1f1f21), hex(0x57575b), 0.4, 0.1);
}

void drawSnow(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0xf2f6fa), hex(0xd8e2ec), hex(0xffffff), 0.3, 0.25);
}

void drawIce(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0x8fb8e8), hex(0x6e9bd6), hex(0xbfdcf5), 0.3, 0.2);
}

void drawRedrock(TileCtx& c, Mulberry32& rng) {
  // layered terracotta: banded base with speckle
  for (int y = 0; y < TILE_PX; y++) {
    Color base = (y % 5 < 2) ? hex(0x8f4526) : hex(0xa6502e);
    for (int x = 0; x < TILE_PX; x++) {
      double t = rng.next() * 0.3;
      c.setFill(mix(base, hex(0x6e3418), t));
      c.fillRect(x, y, 1, 1);
    }
  }
}

// Black coal: a near-black base lifted by a few charcoal glints so the face
// still reads as a textured block rather than a flat silhouette underground,
// where there is little light to separate it from shadow.
void drawCoal(TileCtx& c, Mulberry32& rng) {
  speckle(c, rng, hex(0x191919), hex(0x000000), hex(0x4a4a4a), 0.35, 0.18);
}

// Blades rising from the bottom edge, everything else left transparent so
// the billboard reads as grass rather than a green square. The atlas starts
// zeroed, so untouched pixels keep alpha 0.
void drawTallGrass(TileCtx& c, Mulberry32& rng) {
  for (int x = 0; x < TILE_PX; x++) {
    if (rng.next() < 0.3) continue; // gaps between blades
    int h = 3 + (int)(rng.next() * 5);
    int lean = rng.next() < 0.5 ? 0 : (rng.next() < 0.5 ? -1 : 1);
    Color base = rng.next() < 0.5 ? hex(0x4f9c2a) : hex(0x62b23a);
    for (int k = 0; k < h; k++) {
      int y = TILE_PX - 1 - k;
      int bx = x + (k > h / 2 ? lean : 0);
      if (bx < 0 || bx >= TILE_PX) continue;
      // tips lighter, bases darker, like real blades
      double t = (double)k / std::max(1, h - 1);
      Color col = mix(mix(base, hex(0x2f6b18), 0.45), hex(0x8fd45a), t * 0.7);
      c.setFill(col);
      c.fillRect(bx, y, 1, 1);
    }
  }
}

// Sprites need a dark outline to read against the inventory slot: the stone
// gray used for tool heads (0x8a8a8e) is almost exactly the slot's own gray
// (0x8b8b8b), so an un-outlined pickaxe head would be invisible.
void drawItemSprite(TileCtx& c, Mulberry32& rng, const ItemArt& sprite) {
  const char* const* art = sprite.rows;
  const Color OUTLINE = hex(0x2b2118);
  // Loud magenta for a character with no palette entry — a typo in the art
  // should be obvious on screen, not quietly painted in some other colour.
  const Color UNMAPPED = hex(0xff00ff);
  auto filled = [&](int x, int y) {
    if (x < 0 || x >= TILE_PX || y < 0 || y >= TILE_PX) return false;
    return art[y][x] != '.';
  };

  // Outline first: any transparent pixel touching the shape (incl. diagonals).
  for (int y = 0; y < TILE_PX; y++) {
    for (int x = 0; x < TILE_PX; x++) {
      if (filled(x, y)) continue;
      bool touches = false;
      for (int dy = -1; dy <= 1 && !touches; dy++) {
        for (int dx = -1; dx <= 1 && !touches; dx++) {
          if (dx || dy) touches = filled(x + dx, y + dy);
        }
      }
      if (touches) {
        c.setFill(OUTLINE);
        c.fillRect(x, y, 1, 1);
      }
    }
  }

  // Then the shape itself, jittered toward its dark tone like the block tiles.
  for (int y = 0; y < TILE_PX; y++) {
    for (int x = 0; x < TILE_PX; x++) {
      char ch = art[y][x];
      if (ch == '.') continue;
      Color base = UNMAPPED, dark = UNMAPPED;
      for (const ItemArtColor& p : sprite.palette) {
        if (p.key == 0) break; // palette is 0-terminated
        if (p.key != ch) continue;
        base = hex(p.base);
        dark = hex(p.dark);
        break;
      }
      c.setFill(mix(base, dark, rng.next() * 0.45));
      c.fillRect(x, y, 1, 1);
    }
  }
}

// Copies a generated sprite over the tile, alpha and all. Source rows run
// top-down, matching TileCtx's canvas convention.
void blitGeneratedSprite(TileCtx& c, const unsigned char* rgba) {
  for (int y = 0; y < ATLAS_TILE_PX; y++) {
    for (int x = 0; x < ATLAS_TILE_PX; x++) {
      const unsigned char* p = &rgba[((size_t)y * ATLAS_TILE_PX + x) * 4];
      c.putRGBA(x, y, p[0], p[1], p[2], p[3]);
    }
  }
}

typedef void (*TileDrawer)(TileCtx&, Mulberry32&);

// Order must match the Tile enum in blocks.h. BLOCK tiles only — item tiles
// are data (item_art.cpp) rather than code, so they need no entry here.
const TileDrawer TILE_DRAWERS[TILE_FIRST_ITEM] = {
  drawGrassTop, drawGrassSide, drawDirt, drawStone, drawSand,
  drawWoodTop, drawWoodSide, drawLeaves, drawWater, drawBedrock,
  drawSnow, drawIce, drawRedrock, drawTallGrass, drawCoal,
};

} // namespace

// A tile named here can be replaced by art\<name>.png (see
// sprites_generated.h). Tiles absent from this list, and named tiles with no
// art supplied, keep their procedural drawing — the two coexist per tile.
const char* spriteNameForTile(int tile) {
  if (tile == TILE_SWORD) return "sword";
  return nullptr;
}

const Atlas& buildTextureAtlas() {
  static Atlas atlas;
  static bool built = false;
  if (built) return atlas;

  atlas.width = ATLAS_TILE_PX * TILE_COUNT;
  atlas.height = ATLAS_TILE_PX;
  atlas.pixels.assign((size_t)atlas.width * atlas.height * 4, 0);

  for (int i = 0; i < TILE_COUNT; i++) {
    // Same per-tile seed as the JS version (int32 multiply wraparound).
    Mulberry32 rng(0x9e3779b9u ^ (uint32_t)((uint64_t)i * 2654435761ull));
    TileCtx ctx;
    ctx.atlas = &atlas;
    ctx.tileBase = i * ATLAS_TILE_PX;
    // Supplied PNG art wins where it exists; then an item's own sprite; then
    // the block tile draws itself procedurally.
    const GeneratedSprite* png = generatedSpriteNamed(spriteNameForTile(i));
    const ItemArt* item = itemArtForTile(i);
    if (png && png->rgba) blitGeneratedSprite(ctx, png->rgba);
    else if (item) drawItemSprite(ctx, rng, *item);
    else if (i < TILE_FIRST_ITEM) TILE_DRAWERS[i](ctx, rng);
  }

  built = true;
  return atlas;
}

UVRect tileUV(int tileIndex) {
  const Atlas& atlas = buildTextureAtlas();
  double u0 = (double)(tileIndex * ATLAS_TILE_PX) / atlas.width;
  double u1 = (double)((tileIndex + 1) * ATLAS_TILE_PX) / atlas.width;
  return { u0, 0.0, u1, 1.0 };
}

bool getBlockFaceUV(uint8_t blockId, int face, UVRect& out) {
  int tile = faceTexture(blockId, face);
  if (tile < 0) return false;
  out = tileUV(tile);
  return true;
}
