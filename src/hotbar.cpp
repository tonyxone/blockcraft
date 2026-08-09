#include "hotbar.h"
#include "gfx.h"
#include "recipes.h"
#include "textures.h"

// Visual constants mirroring style.css (#hotbar / .slot).
static const double SLOT_SIZE = 52;
static const double SLOT_GAP = 6;
static const double ICON_SIZE = 36;
static const double BOTTOM_MARGIN = 16;

// Screen rect (top-left x/y) of a slot.
static void slotRect(int index, int winW, int winH, double& x, double& y) {
  double totalW = HOTBAR_COLS * SLOT_SIZE + (HOTBAR_COLS - 1) * SLOT_GAP;
  x = (winW - totalW) / 2 + index * (SLOT_SIZE + SLOT_GAP);
  y = winH - BOTTOM_MARGIN - SLOT_SIZE;
}

Hotbar::Hotbar(const uint8_t* blockOrder, int orderLen, const std::vector<int>& savedCounts) {
  for (int i = 0; i < HOTBAR_COLS; i++) {
    if (i < orderLen) {
      slots[i].blockId = blockOrder[i];
      slots[i].count = i < (int)savedCounts.size() ? savedCounts[i] : STARTING_COUNT;
    } else {
      slots[i].blockId = -1;
      slots[i].count = 0;
    }
  }
}

void Hotbar::select(int index) {
  if (index < 0 || index >= HOTBAR_COLS) return;
  selected = index;
}

void Hotbar::addBlock(int blockId, int amount) {
  for (Slot& s : slots) {
    if (s.blockId == blockId) {
      s.count += amount;
      return;
    }
  }
}

int Hotbar::takeSelected() {
  Slot& slot = slots[selected];
  if (slot.blockId < 0 || slot.count <= 0) return -1;
  int blockId = slot.blockId;
  slot.count -= 1;
  if (slot.count <= 0) { slot.blockId = -1; slot.count = 0; } // emptied: clear the slot
  return blockId;
}

std::vector<int> Hotbar::serialize() const {
  std::vector<int> out;
  for (const Slot& s : slots) out.push_back(s.count);
  return out;
}

void Hotbar::draw(int winW, int winH) const {
  for (int i = 0; i < SLOT_COUNT; i++) {
    double x, y;
    slotRect(i, winW, winH, x, y);
    bool sel = (i == selected);

    if (sel) {
      // stand-in for the CSS glow
      drawRect(x - 3, y - 3, SLOT_SIZE + 6, SLOT_SIZE + 6, 1, 1, 1, 0.25);
    }
    drawRect(x, y, SLOT_SIZE, SLOT_SIZE, 20 / 255.0, 20 / 255.0, 20 / 255.0, 0.55);
    drawRectOutline(x, y, SLOT_SIZE, SLOT_SIZE, 2, 1, 1, 1, sel ? 1.0 : 0.35);

    const Slot& slot = slots[i];
    if (slot.blockId >= 0) {
      int tile = craftItemTile((uint8_t)slot.blockId); // block top face, or item sprite
      if (tile >= 0) {
        drawAtlasTile(tile, x + (SLOT_SIZE - ICON_SIZE) / 2, y + (SLOT_SIZE - ICON_SIZE) / 2,
                      ICON_SIZE, ICON_SIZE);
      }
    }

    // key number, top-left: slots 0-8 are keys 1-9, slot 9 is key 0
    char key[2] = { (char)(i < 9 ? '1' + i : '0'), 0 };
    drawText(g_fontKey, x + 3, y + 1, key, 0.87, 0.87, 0.87, 1);

    // count, bottom-right (with a cheap 1px shadow like the CSS text-shadow)
    if (slot.blockId >= 0) {
      char count[16];
      std::snprintf(count, sizeof(count), "%d", slot.count);
      double tw = textWidth(g_fontCount, count);
      double tx = x + SLOT_SIZE - 3 - tw;
      double ty = y + SLOT_SIZE - 1 - g_fontCount.height;
      drawText(g_fontCount, tx + 1, ty + 1, count, 0, 0, 0, 1);
      drawText(g_fontCount, tx, ty, count, 1, 1, 1, 1);
    }
  }
}

void hotbarRect(int winW, int winH, double& x, double& y, double& w, double& h) {
  w = HOTBAR_COLS * SLOT_SIZE + (HOTBAR_COLS - 1) * SLOT_GAP;
  h = SLOT_SIZE;
  x = (winW - w) / 2;
  y = winH - BOTTOM_MARGIN - SLOT_SIZE;
}

int Hotbar::slotAt(double mx, double my, int winW, int winH) const {
  for (int i = 0; i < SLOT_COUNT; i++) {
    double x, y;
    slotRect(i, winW, winH, x, y);
    if (mx >= x && mx < x + SLOT_SIZE && my >= y && my < y + SLOT_SIZE) return i;
  }
  return -1;
}
