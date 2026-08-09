#pragma once
#include "common.h"
#include "win_gl.h"

// 2D drawing + text helpers on top of legacy OpenGL. Text is rasterized with
// GDI (antialiased) into small cached textures and drawn as textured quads.

const int FONT_FIRST_CHAR = 32;
const int FONT_CHAR_COUNT = 224;

struct Font {
  HFONT handle = nullptr;
  int ascent = 0;
  int height = 0;
  int widths[FONT_CHAR_COUNT] = {};
};

// Fonts mirroring the CSS sizes: h1 42px bold, buttons 16px, settings rows /
// message 14px, hint 15px, hotbar count 12px bold, hotbar key 10px.
extern Font g_fontTitle;
extern Font g_fontButton;
extern Font g_fontMsg;
extern Font g_fontHint;
extern Font g_fontCount;
extern Font g_fontKey;

// Creates fonts + uploads the texture atlas. Requires a current GL context.
void gfxInit(HDC dc);

unsigned int atlasTextureId();

void begin2D(int winW, int winH);
void end2D();

void drawRect(double x, double y, double w, double h, double r, double g, double b, double a);
// CSS-style border: strokes a `border`-thick frame just inside the rect.
void drawRectOutline(double x, double y, double w, double h, double border,
                     double r, double g, double b, double a);

double textWidth(const Font& font, const char* text);
// (x, y) is the top-left corner of the text box.
void drawText(const Font& font, double x, double y, const char* text,
              double r, double g, double b, double a);

// Draws one atlas tile as a screen-space quad (nearest filtering, canvas
// orientation: tile top at the quad top).
void drawAtlasTile(int tileIndex, double x, double y, double w, double h);
