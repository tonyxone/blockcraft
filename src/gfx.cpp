#include "gfx.h"
#include "textures.h"
#include <map>

Font g_fontTitle, g_fontButton, g_fontMsg, g_fontHint, g_fontCount, g_fontKey;

static GLuint g_atlasTex = 0;

static void makeFont(HDC dc, Font& font, int pixelHeight, bool bold) {
  // Antialiased outline font; the handle is kept for the text rasterizer.
  font.handle = CreateFontA(-pixelHeight, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                            FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
  HGDIOBJ old = SelectObject(dc, font.handle);

  TEXTMETRICA tm;
  GetTextMetricsA(dc, &tm);
  font.ascent = tm.tmAscent;
  font.height = tm.tmHeight;
  GetCharWidth32A(dc, FONT_FIRST_CHAR, FONT_FIRST_CHAR + FONT_CHAR_COUNT - 1, font.widths);

  SelectObject(dc, old);
}

// --- text rasterization + cache ------------------------------------------
// Each unique (font, string) pair is rendered once with GDI (white on black,
// antialiased), the luminance becomes the texture's alpha channel, and the
// texture is cached so text costs one textured quad per frame afterwards.
namespace {

struct TextTex {
  GLuint tex = 0;
  int w = 0, h = 0;
};

std::map<std::pair<const Font*, std::string>, TextTex> g_textCache;
const size_t TEXT_CACHE_MAX = 512; // counts + labels + menu strings are few

TextTex rasterizeText(const Font& font, const char* text) {
  TextTex out;
  char buf[512];
  int len = 0;
  for (const unsigned char* p = (const unsigned char*)text; *p && len < 511; p++) {
    buf[len++] = *p >= FONT_FIRST_CHAR ? (char)*p : '?';
  }
  buf[len] = 0;
  if (len == 0) return out;

  HDC mem = CreateCompatibleDC(nullptr);
  HGDIOBJ oldFont = SelectObject(mem, font.handle);
  SIZE sz;
  GetTextExtentPoint32A(mem, buf, len, &sz);
  out.w = sz.cx + 2;  // 1px padding each side for the AA fringe
  out.h = font.height + 2;

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = out.w;
  bmi.bmiHeader.biHeight = -out.h; // top-down: row 0 is the text top
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ oldBmp = SelectObject(mem, dib);
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(255, 255, 255));
  TextOutA(mem, 1, 1, buf, len);
  GdiFlush();

  std::vector<uint8_t> px((size_t)out.w * out.h * 4);
  const uint8_t* src = (const uint8_t*)bits;
  for (size_t i = 0; i < (size_t)out.w * out.h; i++) {
    px[i * 4 + 0] = 255;
    px[i * 4 + 1] = 255;
    px[i * 4 + 2] = 255;
    px[i * 4 + 3] = src[i * 4]; // red channel = coverage of white-on-black AA
  }

  glGenTextures(1, &out.tex);
  glBindTexture(GL_TEXTURE_2D, out.tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, out.w, out.h, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  SelectObject(mem, oldBmp);
  SelectObject(mem, oldFont);
  DeleteObject(dib);
  DeleteDC(mem);
  return out;
}

const TextTex& textTexture(const Font& font, const char* text) {
  auto key = std::make_pair(&font, std::string(text));
  auto it = g_textCache.find(key);
  if (it != g_textCache.end()) return it->second;
  if (g_textCache.size() >= TEXT_CACHE_MAX) {
    for (auto& kv : g_textCache) glDeleteTextures(1, &kv.second.tex);
    g_textCache.clear();
  }
  return g_textCache.emplace(key, rasterizeText(font, text)).first->second;
}

} // namespace

void gfxInit(HDC dc) {
  makeFont(dc, g_fontTitle, 42, true);
  makeFont(dc, g_fontButton, 16, false);
  makeFont(dc, g_fontMsg, 14, false);
  makeFont(dc, g_fontHint, 15, false);
  makeFont(dc, g_fontCount, 12, true);
  makeFont(dc, g_fontKey, 10, false);

  const Atlas& atlas = buildTextureAtlas();
  glGenTextures(1, &g_atlasTex);
  glBindTexture(GL_TEXTURE_2D, g_atlasTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.width, atlas.height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, atlas.pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

unsigned int atlasTextureId() { return g_atlasTex; }

void begin2D(int winW, int winH) {
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, winW, winH, 0, -1, 1); // top-left origin, CSS-style
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_FOG);
  glDisable(GL_CULL_FACE);
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void end2D() {
  glDisable(GL_BLEND);
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
}

void drawRect(double x, double y, double w, double h, double r, double g, double b, double a) {
  glDisable(GL_TEXTURE_2D);
  glColor4d(r, g, b, a);
  glBegin(GL_QUADS);
  glVertex2d(x, y);
  glVertex2d(x + w, y);
  glVertex2d(x + w, y + h);
  glVertex2d(x, y + h);
  glEnd();
}

void drawRectOutline(double x, double y, double w, double h, double border,
                     double r, double g, double b, double a) {
  drawRect(x, y, w, border, r, g, b, a);
  drawRect(x, y + h - border, w, border, r, g, b, a);
  drawRect(x, y + border, border, h - 2 * border, r, g, b, a);
  drawRect(x + w - border, y + border, border, h - 2 * border, r, g, b, a);
}

double textWidth(const Font& font, const char* text) {
  double w = 0;
  for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
    int idx = *p - FONT_FIRST_CHAR;
    if (idx >= 0 && idx < FONT_CHAR_COUNT) w += font.widths[idx];
  }
  return w;
}

void drawText(const Font& font, double x, double y, const char* text,
              double r, double g, double b, double a) {
  if (!text || !*text) return;
  const TextTex& t = textTexture(font, text);
  if (!t.tex) return;
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, t.tex);
  glColor4d(r, g, b, a); // tints the white glyph texture
  glBegin(GL_QUADS);
  // the DIB was top-down: v=0 is the text top
  glTexCoord2d(0, 0); glVertex2d(x, y);
  glTexCoord2d(1, 0); glVertex2d(x + t.w, y);
  glTexCoord2d(1, 1); glVertex2d(x + t.w, y + t.h);
  glTexCoord2d(0, 1); glVertex2d(x, y + t.h);
  glEnd();
  glDisable(GL_TEXTURE_2D);
}

void drawAtlasTile(int tileIndex, double x, double y, double w, double h) {
  UVRect uv = tileUV(tileIndex);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_atlasTex);
  glColor4d(1, 1, 1, 1);
  glBegin(GL_QUADS);
  // v=1 is the tile's top; screen y grows downward.
  glTexCoord2d(uv.u0, 1); glVertex2d(x, y);
  glTexCoord2d(uv.u1, 1); glVertex2d(x + w, y);
  glTexCoord2d(uv.u1, 0); glVertex2d(x + w, y + h);
  glTexCoord2d(uv.u0, 0); glVertex2d(x, y + h);
  glEnd();
  glDisable(GL_TEXTURE_2D);
}
