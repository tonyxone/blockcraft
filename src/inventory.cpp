#include "inventory.h"
#include "gfx.h"
#include "menu.h"
#include "recipes.h"
#include "textures.h"
#include "tools.h"
#include "win_gl.h" // GetTickCount64, for double-click timing

namespace {

// Screen-pixel metrics. One slot pitch matches the hotbar's look.
const double SLOT = 50;
const double GAP = 2;
const double PITCH = SLOT + GAP; // 52
const double PAD = 22;           // margin on all four sides of the panel
const double ICON = 34;
const double TAB_H = 28;
const double TAB_TOP = 14;       // margin above the tab strip
const double LABEL_GAP = 20;     // room for a section label above a grid
const double SECTION_GAP = 16;   // breathing room between sections

// Panel-relative y where the content below the tab strip starts.
const double CONTENT_TOP = TAB_TOP + TAB_H + PAD;

const double PANEL_W = PAD * 2 + INV_COLS * PITCH - GAP; // 550
const double ARMOR_H = 4 * PITCH - GAP;                  // armor column height (206)

// Player-tab geometry (panel-relative).
const double ARMOR_X = PAD;
const double PREVIEW_X = ARMOR_X + PITCH + 6;
const double PREVIEW_W = 104;
const double OFFHAND_X = PREVIEW_X + PREVIEW_W + 6;
const double CHAR_BTN_H = 22;
const double CHAR_BTN_Y = CONTENT_TOP + ARMOR_H + 8;

// Craft-tab geometry (panel-relative).
const double CRAFT_X = PAD;
const double CRAFT_LABEL_Y = CONTENT_TOP;
const double CRAFT_GRID_Y = CRAFT_LABEL_Y + LABEL_GAP;
const double CRAFT_GRID_H = 3 * PITCH - GAP;                        // 3x3 grid
// The arrow and result sit clear of the 3x3 grid, which ends at
// CRAFT_X + 3*PITCH. (The arrow used to be drawn at 2*PITCH — inside the
// grid's third column — so it covered the middle-right cell: an item left
// there hid under the arrow while silently breaking every recipe match.)
const double CRAFT_ARROW_X = CRAFT_X + 3 * PITCH + 6;
const double RESULT_X = CRAFT_X + 3 * PITCH + 34;
const double CRAFT_BTN_Y = CRAFT_GRID_Y + CRAFT_GRID_H + 8;         // confirm button
const double CRAFT_BTN_H = 26;
const double CRAFT_BTN_W = RESULT_X + SLOT - CRAFT_X;               // grid + arrow + result wide

// Book button: opens the recipe list. Sits immediately right of the Craft
// button, centred on it, so the two crafting actions live together.
const double BOOK_BTN = 30;
const double BOOK_BTN_X = CRAFT_X + CRAFT_BTN_W + 10;
const double BOOK_BTN_Y = CRAFT_BTN_Y + (CRAFT_BTN_H - BOOK_BTN) / 2;

// The recipe list itself is its own overlay, sized to hold every recipe at
// once in two columns — paging or scrolling would need input plumbing this
// panel does not have, and the whole point is to see the list.
const double BOOK_W = 760;
const double BOOK_ROW_H = 30;
const double BOOK_PAD = 20;
const double BOOK_TITLE_H = 30;
const double BOOK_ICON = 22;
const int BOOK_COLS = 2;
const unsigned long long BOOK_PRESS_FLASH_MS = 150;

int bookRows() {
  return (CRAFT_RECIPE_COUNT + BOOK_COLS - 1) / BOOK_COLS;
}
double bookH() { return BOOK_PAD * 2 + BOOK_TITLE_H + bookRows() * BOOK_ROW_H; }
double bookX(int winW) { return (winW - BOOK_W) / 2; }
double bookY(int winH) { return (winH - bookH()) / 2; }

// Which recipe row (col, row) is under (mx,my), same layout math the book's
// draw loop uses. False if the point misses every row.
bool bookRowAtPoint(double mx, double my, int winW, int winH, int& index) {
  double kx = bookX(winW), ky = bookY(winH);
  double colW = (BOOK_W - BOOK_PAD * 2) / BOOK_COLS;
  int rows = bookRows();
  double top = ky + BOOK_PAD + BOOK_TITLE_H;
  if (my < top || mx < kx + BOOK_PAD || mx >= kx + BOOK_W - BOOK_PAD) return false;
  int col = (int)((mx - (kx + BOOK_PAD)) / colW);
  int row = (int)((my - top) / BOOK_ROW_H);
  if (col < 0 || col >= BOOK_COLS || row < 0 || row >= rows) return false;
  int i = col * rows + row;
  if (i >= CRAFT_RECIPE_COUNT) return false;
  index = i;
  return true;
}

// Read-only affordability check for the hover tint — tallies across the
// whole hotbar + backpack, same as Inventory::tryCraftFromBook.
bool bookCanAfford(const Hotbar& hotbar, const Hotbar::Slot* main, const Recipe& r) {
  uint8_t items[RECIPE_MAX_INGREDIENTS];
  int need[RECIPE_MAX_INGREDIENTS];
  int n = recipeIngredients(r, items, need);
  for (int i = 0; i < n; i++) {
    int total = 0;
    for (const Hotbar::Slot& s : hotbar.slots) if (s.blockId == items[i]) total += s.count;
    for (int m = 0; m < INV_MAIN_COUNT; m++) if (main[m].blockId == items[i]) total += main[m].count;
    if (total < need[i]) return false;
  }
  return true;
}

const char* const TAB_NAMES[INV_TAB_COUNT] = { "Inventory", "Player", "Craft" };

// Height of the tab-specific top section (0 on the plain inventory tab).
double topSectionH(int tab) {
  if (tab == INV_TAB_PLAYER) return ARMOR_H + LABEL_GAP;
  if (tab == INV_TAB_CRAFT) {
    // label + 3x3 grid + confirm button, with gaps around each
    return LABEL_GAP + CRAFT_GRID_H + 8 + CRAFT_BTN_H + LABEL_GAP;
  }
  return 0;
}

// Panel-relative y of the backpack label / grid and the hotbar label / row.
double mainLabelY(int tab) { return CONTENT_TOP + topSectionH(tab); }
double mainY(int tab) { return mainLabelY(tab) + LABEL_GAP; }
double hotbarLabelY(int tab) { return mainY(tab) + (3 * PITCH - GAP) + SECTION_GAP; }
double hotbarY(int tab) { return hotbarLabelY(tab) + LABEL_GAP; }
double panelH(int tab) { return hotbarY(tab) + SLOT + PAD; }

double panelX(int winW) { return (winW - PANEL_W) / 2; }
double panelY(int winH, int tab) { return (winH - panelH(tab)) / 2; }

double tabW() { return (PANEL_W - 2 * PAD) / INV_TAB_COUNT; }

// True if (mx,my) is anywhere within the tabbed panel's own rect — not just
// over a slot. Used to tell "released on dead space inside the panel" (the
// held stack still snaps back to its source, same as always) apart from
// "dragged the item out past the window entirely" (it drops into the
// world instead).
bool insidePanel(double mx, double my, int winW, int winH, int tab) {
  double px = panelX(winW), py = panelY(winH, tab);
  return mx >= px && mx < px + PANEL_W && my >= py && my < py + panelH(tab);
}

// --- Chest screen (opened with E over a chest): its own small panel, no
// tabs — the chest's 9 slots (centred) above the player's backpack + hotbar,
// the same width as the tabbed panel above. -------------------------------
const double CHEST_PANEL_W = PANEL_W;
const double CHEST_TITLE_Y = PAD;
const double CHEST_GRID_LABEL_Y = CHEST_TITLE_Y + LABEL_GAP + 6;
const double CHEST_GRID_Y = CHEST_GRID_LABEL_Y + LABEL_GAP;
const double CHEST_GRID_W = 3 * PITCH - GAP;
const double CHEST_GRID_X = (CHEST_PANEL_W - CHEST_GRID_W) / 2;
const double CHEST_MAIN_LABEL_Y = CHEST_GRID_Y + (3 * PITCH - GAP) + SECTION_GAP;
const double CHEST_MAIN_Y = CHEST_MAIN_LABEL_Y + LABEL_GAP;
const double CHEST_HOTBAR_LABEL_Y = CHEST_MAIN_Y + (3 * PITCH - GAP) + SECTION_GAP;
const double CHEST_HOTBAR_Y = CHEST_HOTBAR_LABEL_Y + LABEL_GAP;
const double CHEST_PANEL_H = CHEST_HOTBAR_Y + SLOT + PAD;

double chestPanelX(int winW) { return (winW - CHEST_PANEL_W) / 2; }
double chestPanelY(int winH) { return (winH - CHEST_PANEL_H) / 2; }

// The slot under a screen point on the chest screen (its 9 cells, or the
// player's backpack/hotbar below), or nullptr.
Hotbar::Slot* chestSlotAtPoint(ChestState& chest, Inventory& inv, Hotbar& hotbar,
                               double mx, double my, int winW, int winH) {
  double px = chestPanelX(winW), py = chestPanelY(winH);
  struct Ref { double x, y; Hotbar::Slot* slot; };
  Ref refs[CHEST_SLOT_COUNT + INV_MAIN_COUNT + SLOT_COUNT];
  int n = 0;
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
      refs[n++] = { px + CHEST_GRID_X + c * PITCH, py + CHEST_GRID_Y + r * PITCH, &chest.slots[r * 3 + c] };
  for (int i = 0; i < INV_MAIN_COUNT; i++)
    refs[n++] = { px + PAD + (i % INV_COLS) * PITCH, py + CHEST_MAIN_Y + (i / INV_COLS) * PITCH, &inv.main[i] };
  for (int i = 0; i < SLOT_COUNT; i++)
    refs[n++] = { px + PAD + i * PITCH, py + CHEST_HOTBAR_Y, &hotbar.slots[i] };

  for (int i = 0; i < n; i++) {
    if (mx >= refs[i].x && mx < refs[i].x + SLOT && my >= refs[i].y && my < refs[i].y + SLOT) {
      return refs[i].slot;
    }
  }
  return nullptr;
}

// Minecraft slot bevel: dark top/left, light bottom/right on a gray fill.
void drawSlotBox(double x, double y, double w, double h) {
  drawRect(x, y, w, h, 139 / 255.0, 139 / 255.0, 139 / 255.0, 1);
  drawRect(x, y, w, 2, 55 / 255.0, 55 / 255.0, 55 / 255.0, 1);         // top dark
  drawRect(x, y, 2, h, 55 / 255.0, 55 / 255.0, 55 / 255.0, 1);         // left dark
  drawRect(x, y + h - 2, w, 2, 1, 1, 1, 1);                            // bottom light
  drawRect(x + w - 2, y, 2, h, 1, 1, 1, 1);                            // right light
}

void drawSlotIcon(const Hotbar::Slot& slot, double x, double y) {
  if (slot.blockId < 0 || slot.count <= 0) return;
  int tile = craftItemTile((uint8_t)slot.blockId); // block top face, or item sprite
  if (tile >= 0) drawAtlasTile(tile, x + (SLOT - ICON) / 2, y + (SLOT - ICON) / 2, ICON, ICON);
}

void drawSlotCount(const Hotbar::Slot& slot, double x, double y) {
  if (slot.blockId < 0 || slot.count <= 0) return;
  char count[16];
  std::snprintf(count, sizeof(count), "%d", slot.count);
  double tw = textWidth(g_fontCount, count);
  double tx = x + SLOT - 3 - tw;
  double ty = y + SLOT - 1 - g_fontCount.height;
  drawText(g_fontCount, tx + 1, ty + 1, count, 0, 0, 0, 1); // 1px shadow
  drawText(g_fontCount, tx, ty, count, 1, 1, 1, 1);
}

// A small closed book: cover, a lighter page block offset to one side, and a
// spine. Drawn from rects rather than an atlas tile — it is UI furniture,
// not an item, so it does not belong in the block texture atlas.
void drawBookIcon(double x, double y, double s, bool hover) {
  double lift = hover ? 0.12 : 0.0;
  drawRect(x, y, s, s, 0.30 + lift, 0.13 + lift, 0.10 + lift, 1);       // cover
  drawRectOutline(x, y, s, s, 2, 0, 0, 0, 1);
  drawRect(x + s * 0.30, y + s * 0.12, s * 0.58, s * 0.76, 0.90, 0.87, 0.76, 1); // pages
  drawRect(x + s * 0.30, y + s * 0.12, 2, s * 0.76, 0.62, 0.58, 0.48, 1);        // page edge
  drawRect(x + s * 0.16, y + s * 0.12, s * 0.10, s * 0.76, 0.22, 0.08, 0.06, 1); // spine
}

void drawSectionLabel(double x, double y, const char* text) {
  drawText(g_fontHint, x, y, text, 63 / 255.0, 63 / 255.0, 63 / 255.0, 1);
}

// Names the item under the cursor, Minecraft-style: a dark plate beside the
// pointer. Drawn last of all so it sits over the slots, and nudged back
// inside the window near an edge instead of running off it.
void drawItemTooltip(int itemId, double mx, double my, int winW, int winH) {
  const char* name = craftItemName((uint8_t)itemId);
  if (!name || !*name) return;
  // Slot names are stored lowercase ("stone", "tall grass"); a tooltip reads
  // better capitalised, and this is the only place that cares.
  char label[64];
  std::snprintf(label, sizeof(label), "%s", name);
  if (label[0] >= 'a' && label[0] <= 'z') label[0] = (char)(label[0] - 'a' + 'A');

  const double PADX = 8, PADY = 5, OFFSET = 14, MARGIN = 4;
  double w = textWidth(g_fontHint, label) + PADX * 2;
  double h = g_fontHint.height + PADY * 2;
  double x = mx + OFFSET, y = my + OFFSET;
  if (x + w > winW - MARGIN) x = mx - OFFSET - w; // flip to the cursor's left
  if (y + h > winH - MARGIN) y = my - OFFSET - h;
  if (x < MARGIN) x = MARGIN;
  if (y < MARGIN) y = MARGIN;

  drawRect(x, y, w, h, 16 / 255.0, 14 / 255.0, 24 / 255.0, 0.94);
  drawRectOutline(x, y, w, h, 2, 88 / 255.0, 70 / 255.0, 148 / 255.0, 1);
  drawText(g_fontHint, x + PADX, y + PADY, label, 1, 1, 1, 1);
}

void clearSlot(Hotbar::Slot& s) { s.blockId = -1; s.count = 0; }
bool occupied(const Hotbar::Slot& s) { return s.blockId >= 0 && s.count > 0; }

// "Food" for the context menu's Use button means anything isEatableFood
// (recipes.h) accepts — cooked meat and every fruit — plus raw meat, which
// is a food ITEM (droppable, below) but isn't itself edible; only the
// isEatableFood set restores hunger anywhere else in the game (double-click,
// drag-to-preview, right-click-in-world), so the menu matches that.
bool isFoodItem(int id) { return isEatableFood((uint8_t)id) || id == ITEM_RAW_MEAT; }
// A health potion: its own Use-button case, alongside food, since it shares
// every gesture (right-click menu, double-click, drag-to-preview) but heals
// HEALTH instead of restoring hunger (see pendingHealAmount).
bool isPotionItem(int id) { return id >= 0 && healthPotionHeal((uint8_t)id) > 0; }
bool isDroppableItem(int id) {
  return id >= 0 && (isToolItem((uint8_t)id) || isFoodItem(id) || isPotionItem(id));
}

// Context menu geometry: one button per row, Equip/Use on top (only for
// items that have one) then Drop. Position is clamped to the window so a
// click near an edge doesn't open a menu that's partly off-screen; both the
// hit-test in onMouseDown and the draw in drawContents call this, so they
// can never disagree about where the buttons actually are.
const double CTX_MENU_W = 84;
const double CTX_MENU_BTN_H = 22;
void contextMenuRect(double clickX, double clickY, int winW, int winH, bool hasTopButton,
                     double& x, double& y, double& w, double& h) {
  w = CTX_MENU_W;
  h = CTX_MENU_BTN_H * (hasTopButton ? 2 : 1);
  x = clampd(clickX, 0, winW - w);
  y = clampd(clickY, 0, winH - h);
}

// The slot under a screen point on the current tab, or nullptr.
Hotbar::Slot* slotAtPoint(Inventory& inv, Hotbar& hotbar, double mx, double my,
                          int winW, int winH) {
  double px = panelX(winW), py = panelY(winH, inv.tab);
  struct Ref { double x, y; Hotbar::Slot* slot; };
  Ref refs[INV_SAVED_SLOTS + SLOT_COUNT];
  int n = 0;
  if (inv.tab == INV_TAB_PLAYER) {
    for (int i = 0; i < INV_ARMOR_COUNT; i++)
      refs[n++] = { px + ARMOR_X, py + CONTENT_TOP + i * PITCH, &inv.armor[i] };
    refs[n++] = { px + OFFHAND_X, py + CONTENT_TOP, &inv.mainHand };
    refs[n++] = { px + OFFHAND_X, py + CONTENT_TOP + ARMOR_H - SLOT, &inv.offhand };
  } else if (inv.tab == INV_TAB_CRAFT) {
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
        refs[n++] = { px + CRAFT_X + c * PITCH, py + CRAFT_GRID_Y + r * PITCH, &inv.craft[r * 3 + c] };
  }
  for (int i = 0; i < INV_MAIN_COUNT; i++)
    refs[n++] = { px + PAD + (i % INV_COLS) * PITCH, py + mainY(inv.tab) + (i / INV_COLS) * PITCH, &inv.main[i] };
  for (int i = 0; i < SLOT_COUNT; i++)
    refs[n++] = { px + PAD + i * PITCH, py + hotbarY(inv.tab), &hotbar.slots[i] };

  for (int i = 0; i < n; i++) {
    if (mx >= refs[i].x && mx < refs[i].x + SLOT && my >= refs[i].y && my < refs[i].y + SLOT) {
      return refs[i].slot;
    }
  }
  return nullptr;
}

// One recipe as a line of icons: "1 wood + 1 stone = pickaxe". Counts are
// only printed when more than one is needed, so the common case stays quiet.
void drawRecipeLine(const Recipe& r, double x, double y, double w) {
  uint8_t items[RECIPE_MAX_INGREDIENTS];
  int counts[RECIPE_MAX_INGREDIENTS];
  int n = recipeIngredients(r, items, counts);

  double cx = x;
  double iconY = y + (BOOK_ROW_H - BOOK_ICON) / 2;
  double textY = y + (BOOK_ROW_H - g_fontHint.height) / 2;

  auto drawCount = [&](int count) {
    if (count <= 1) return;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", count);
    drawText(g_fontHint, cx + 1, textY, buf, 0.16, 0.16, 0.16, 1);
    cx += textWidth(g_fontHint, buf) + 4;
  };
  auto drawGlyph = [&](const char* g) {
    drawText(g_fontHint, cx + 3, textY, g, 0.35, 0.35, 0.35, 1);
    cx += textWidth(g_fontHint, g) + 8;
  };

  for (int i = 0; i < n; i++) {
    if (i > 0) drawGlyph("+");
    drawCount(counts[i]);
    int tile = craftItemTile(items[i]);
    if (tile >= 0) drawAtlasTile(tile, cx, iconY, BOOK_ICON, BOOK_ICON);
    cx += BOOK_ICON + 4;
  }

  drawGlyph("=");
  drawCount(r.outputCount);
  int outTile = craftItemTile(r.output);
  if (outTile >= 0) drawAtlasTile(outTile, cx, iconY, BOOK_ICON, BOOK_ICON);
  cx += BOOK_ICON + 6;

  // Name fills whatever is left, so a line still reads if the icons repeat.
  double room = x + w - cx;
  if (room > 30) drawText(g_fontHint, cx, textY, r.name, 0.24, 0.24, 0.24, 1);
}

// Two clicks on the same slot inside this window count as a double-click.
const unsigned long long DOUBLE_CLICK_MS = 400;

// Is this one of the 3x3 crafting cells? Double-clicking those would only
// shuffle items around inside the grid.
bool isCraftSlot(const Hotbar::Slot* craft, const Hotbar::Slot* s) {
  return s >= craft && s < craft + INV_CRAFT_COUNT;
}

// Moves ONE item into the crafting grid, into its own FREE CELL.
//
// Spreading rather than stacking is the whole point: a cell contributes
// exactly one ingredient to a recipe however deep its stack is (piling more
// in just lets you craft repeatedly), so two stone dropped on the same cell
// read as one stone and "1 wood + 2 stone" never matches. Only when the grid
// is full does it top up a matching pile instead. False if it cannot place
// the item at all, so the caller falls back to normal click behaviour rather
// than silently eating it.
bool sendOneToCraft(Hotbar::Slot* craft, Hotbar::Slot& src) {
  Hotbar::Slot* dest = nullptr;
  for (int i = 0; i < INV_CRAFT_COUNT && !dest; i++) {
    if (craft[i].blockId < 0 || craft[i].count <= 0) dest = &craft[i];
  }
  for (int i = 0; i < INV_CRAFT_COUNT && !dest; i++) {
    if (craft[i].blockId == src.blockId && craft[i].count > 0 &&
        craft[i].count < INV_STACK_MAX) {
      dest = &craft[i];
    }
  }
  if (!dest) return false;
  if (dest->blockId != src.blockId || dest->count <= 0) {
    dest->blockId = src.blockId;
    dest->count = 0;
  }
  dest->count++;
  src.count--;
  if (src.count <= 0) { src.blockId = -1; src.count = 0; }
  return true;
}

// Puts the dragged stack back into the slot it came from (when that slot
// still holds the same block type or is empty).
void returnToSource(Hotbar::Slot& held, Hotbar::Slot* src) {
  if (!occupied(held) || !src) return;
  if (src->blockId < 0) src->blockId = held.blockId;
  if (src->blockId == held.blockId) {
    src->count += held.count;
    clearSlot(held);
  }
}

} // namespace

// What the confirm button would craft: the grid's occupied cells (ignoring
// how many items are stacked in each — a recipe only cares whether a cell is
// filled) matched against the recipe list.
bool craftOutcome(const Hotbar::Slot craft[INV_CRAFT_COUNT], Hotbar::Slot& out) {
  uint8_t items[INV_CRAFT_COUNT];
  int counts[INV_CRAFT_COUNT];
  for (int i = 0; i < INV_CRAFT_COUNT; i++) {
    const Hotbar::Slot& s = craft[i];
    bool filled = s.blockId >= 0 && s.count > 0;
    items[i] = filled ? (uint8_t)s.blockId : (uint8_t)BLOCK_AIR;
    counts[i] = filled ? s.count : 0;
  }
  // Matched on QUANTITY, not layout: which cell an ingredient sits in and how
  // it is stacked no longer matter, only that the nine cells together hold
  // exactly what some recipe asks for.
  const Recipe* r = findRecipeByCount(items, counts);
  if (!r) return false;
  out.blockId = r->output;
  out.count = r->outputCount;
  return true;
}

bool Inventory::tryCraftFromBook(Hotbar& hotbar, const Recipe& r) {
  uint8_t items[RECIPE_MAX_INGREDIENTS];
  int need[RECIPE_MAX_INGREDIENTS];
  int n = recipeIngredients(r, items, need);

  auto tally = [&](int id) {
    int total = 0;
    for (Hotbar::Slot& s : hotbar.slots) if (s.blockId == id) total += s.count;
    for (Hotbar::Slot& s : main) if (s.blockId == id) total += s.count;
    return total;
  };
  for (int i = 0; i < n; i++) {
    if (tally(items[i]) < need[i]) return false;
  }

  auto consume = [&](int id, int amount) {
    for (Hotbar::Slot& s : hotbar.slots) {
      if (amount <= 0) break;
      if (s.blockId != id) continue;
      int take = std::min(amount, s.count);
      s.count -= take;
      amount -= take;
      if (s.count <= 0) { s.blockId = -1; s.count = 0; }
    }
    for (Hotbar::Slot& s : main) {
      if (amount <= 0) break;
      if (s.blockId != id) continue;
      int take = std::min(amount, s.count);
      s.count -= take;
      amount -= take;
      if (s.count <= 0) { s.blockId = -1; s.count = 0; }
    }
  };
  for (int i = 0; i < n; i++) consume(items[i], need[i]);

  if (!collect(hotbar, r.output, r.outputCount)) {
    // No room for the result: put everything back rather than losing it.
    for (int i = 0; i < n; i++) collect(hotbar, items[i], need[i]);
    return false;
  }
  return true;
}

bool Inventory::collect(Hotbar& hotbar, int blockId, int amount) {
  for (Hotbar::Slot& s : hotbar.slots) {
    if (s.blockId == blockId) { s.count += amount; return true; }
  }
  for (Hotbar::Slot& s : main) {
    if (s.blockId == blockId) { s.count += amount; return true; }
  }
  for (Hotbar::Slot& s : hotbar.slots) {
    if (s.blockId < 0) { s.blockId = blockId; s.count = amount; return true; }
  }
  for (Hotbar::Slot& s : main) {
    if (s.blockId < 0) { s.blockId = blockId; s.count = amount; return true; }
  }
  return false;
}

void Inventory::drawPanel(int winW, int winH) const {
  double px = panelX(winW), py = panelY(winH, tab);
  double ph = panelH(tab);

  drawRect(0, 0, winW, winH, 0, 0, 0, 0.55); // dim the world behind

  // Panel: Minecraft gray with a light top/left and dark bottom/right edge.
  drawRect(px, py, PANEL_W, ph, 198 / 255.0, 198 / 255.0, 198 / 255.0, 1);
  drawRect(px, py, PANEL_W, 3, 1, 1, 1, 1);
  drawRect(px, py, 3, ph, 1, 1, 1, 1);
  drawRect(px, py + ph - 3, PANEL_W, 3, 85 / 255.0, 85 / 255.0, 85 / 255.0, 1);
  drawRect(px + PANEL_W - 3, py, 3, ph, 85 / 255.0, 85 / 255.0, 85 / 255.0, 1);
  drawRectOutline(px, py, PANEL_W, ph, 1, 0, 0, 0, 1);

  // Player preview box (the 3D model is drawn over this between the passes).
  if (tab == INV_TAB_PLAYER) {
    drawSlotBox(px + PREVIEW_X, py + CONTENT_TOP, PREVIEW_W, ARMOR_H);
  }
}

void Inventory::drawContents(const Hotbar& hotbar, int winW, int winH,
                             double mx, double my) const {
  double px = panelX(winW), py = panelY(winH, tab);

  // Tab strip: the active tab merges with the panel, inactive ones sit darker.
  for (int t = 0; t < INV_TAB_COUNT; t++) {
    double tx = px + PAD + t * tabW(), ty = py + TAB_TOP;
    bool active = (t == tab);
    bool hover = mx >= tx && mx < tx + tabW() && my >= ty && my < ty + TAB_H;
    if (active) {
      drawRect(tx, ty, tabW(), TAB_H, 198 / 255.0, 198 / 255.0, 198 / 255.0, 1);
      drawRect(tx, ty, tabW(), 2, 1, 1, 1, 1);
      drawRect(tx, ty, 2, TAB_H, 1, 1, 1, 1);
      drawRect(tx + tabW() - 2, ty, 2, TAB_H, 1, 1, 1, 1);
    } else {
      drawRect(tx, ty + 3, tabW(), TAB_H - 3, 143 / 255.0, 143 / 255.0, 143 / 255.0, 1);
      if (hover) drawRect(tx, ty + 3, tabW(), TAB_H - 3, 1, 1, 1, 0.15);
      drawRectOutline(tx, ty + 3, tabW(), TAB_H - 3, 1, 0, 0, 0, 1);
    }
    double lw = textWidth(g_fontHint, TAB_NAMES[t]);
    // dark text on the light active tab, light text on the dark inactive ones
    double c = active ? 63 / 255.0 : 230 / 255.0;
    drawText(g_fontHint, tx + (tabW() - lw) / 2, ty + (TAB_H - g_fontHint.height) / 2 + 1,
             TAB_NAMES[t], c, c, c, 1);
  }

  // Player tab: a switch button below the character preview cycles Steve/Alex.
  if (tab == INV_TAB_PLAYER) {
    double bx = px + PREVIEW_X;
    double by = py + CHAR_BTN_Y;
    bool hover = mx >= bx && mx < bx + PREVIEW_W && my >= by && my < by + CHAR_BTN_H;
    drawRect(bx, by, PREVIEW_W, CHAR_BTN_H, 0.55, 0.55, 0.55, hover ? 0.95 : 0.75);
    drawRectOutline(bx, by, PREVIEW_W, CHAR_BTN_H, 2, 0, 0, 0, 1);
    const char* label = CHARACTER_LABELS[characterType];
    double lw = textWidth(g_fontHint, label);
    drawText(g_fontHint, bx + (PREVIEW_W - lw) / 2,
             by + (CHAR_BTN_H - g_fontHint.height) / 2 + 1, label, 1, 1, 1, 1);
  }

  // Main grid and on-screen hotbar row: separate labelled sections.
  drawSectionLabel(px + PAD, py + mainLabelY(tab), "Inventory");
  drawSectionLabel(px + PAD, py + hotbarLabelY(tab), "Hotbar");

  struct Ref { double x, y; const Hotbar::Slot* slot; };
  Ref refs[INV_SAVED_SLOTS + SLOT_COUNT];
  int n = 0;
  if (tab == INV_TAB_PLAYER) {
    for (int i = 0; i < INV_ARMOR_COUNT; i++)
      refs[n++] = { px + ARMOR_X, py + CONTENT_TOP + i * PITCH, &armor[i] };
    refs[n++] = { px + OFFHAND_X, py + CONTENT_TOP, &mainHand };
    refs[n++] = { px + OFFHAND_X, py + CONTENT_TOP + ARMOR_H - SLOT, &offhand };
  } else if (tab == INV_TAB_CRAFT) {
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
        refs[n++] = { px + CRAFT_X + c * PITCH, py + CRAFT_GRID_Y + r * PITCH, &craft[r * 3 + c] };
  }
  for (int i = 0; i < INV_MAIN_COUNT; i++)
    refs[n++] = { px + PAD + (i % INV_COLS) * PITCH, py + mainY(tab) + (i / INV_COLS) * PITCH, &main[i] };
  for (int i = 0; i < SLOT_COUNT; i++)
    refs[n++] = { px + PAD + i * PITCH, py + hotbarY(tab), &hotbar.slots[i] };

  int hoverItemId = -1; // item under the cursor, named in a tooltip at the end
  for (int i = 0; i < n; i++) {
    const Ref& ref = refs[i];
    drawSlotBox(ref.x, ref.y, SLOT, SLOT);
    drawSlotIcon(*ref.slot, ref.x, ref.y);
    drawSlotCount(*ref.slot, ref.x, ref.y);
    if (mx >= ref.x && mx < ref.x + SLOT && my >= ref.y && my < ref.y + SLOT) {
      drawRect(ref.x, ref.y, SLOT, SLOT, 1, 1, 1, 0.4); // hover highlight
      if (occupied(*ref.slot)) hoverItemId = ref.slot->blockId;
    }
  }

  if (tab == INV_TAB_PLAYER) {
    // Labels for the two hand slots, sitting far enough apart (armor-column
    // height) that they'd otherwise look like a mistake rather than a pair.
    drawSectionLabel(px + OFFHAND_X + SLOT + 6,
                      py + CONTENT_TOP + (SLOT - g_fontHint.height) / 2, "Hand");
    drawSectionLabel(px + OFFHAND_X + SLOT + 6,
                      py + CONTENT_TOP + ARMOR_H - SLOT + (SLOT - g_fontHint.height) / 2, "Off");
  }

  if (tab == INV_TAB_CRAFT) {
    // Crafting label, arrow and the result slot previewing the outcome.
    drawSectionLabel(px + CRAFT_X, py + CRAFT_LABEL_Y + 2, "Crafting");
    double arrowX = px + CRAFT_ARROW_X;
    double arrowCy = py + CRAFT_GRID_Y + CRAFT_GRID_H / 2;
    // Shaft, then a head that narrows to the right: it points FROM the
    // ingredients TO the result (it used to be drawn the other way round).
    const double AR = 122 / 255.0;
    drawRect(arrowX, arrowCy - 2, 14, 4, AR, AR, AR, 1);
    drawRect(arrowX + 10, arrowCy - 8, 2, 16, AR, AR, AR, 1);
    drawRect(arrowX + 12, arrowCy - 6, 2, 12, AR, AR, AR, 1);
    drawRect(arrowX + 14, arrowCy - 4, 2, 8, AR, AR, AR, 1);
    drawRect(arrowX + 16, arrowCy - 2, 2, 4, AR, AR, AR, 1);
    double resultY = py + CRAFT_GRID_Y + (CRAFT_GRID_H - SLOT) / 2;
    drawSlotBox(px + RESULT_X, resultY, SLOT, SLOT);

    Hotbar::Slot outcome;
    bool canCraft = craftOutcome(craft, outcome);
    if (canCraft) {
      drawSlotIcon(outcome, px + RESULT_X, resultY);
      drawSlotCount(outcome, px + RESULT_X, resultY);
      // Naming the result matters most here — it is the one slot holding
      // something the player has not seen before.
      if (mx >= px + RESULT_X && mx < px + RESULT_X + SLOT &&
          my >= resultY && my < resultY + SLOT) {
        hoverItemId = outcome.blockId;
      }
    } else {
      // Say so when the grid holds something but makes nothing, rather than
      // leaving an empty slot that looks identical to a broken screen. A
      // stray ingredient in a cell is the usual cause.
      bool anyIngredient = false;
      for (int i = 0; i < INV_CRAFT_COUNT; i++) {
        if (occupied(craft[i])) anyIngredient = true;
      }
      if (anyIngredient) {
        drawText(g_fontHint, px + RESULT_X + SLOT + 10,
                 resultY + (SLOT - g_fontHint.height) / 2,
                 "no recipe", 90 / 255.0, 90 / 255.0, 90 / 255.0, 1);
      }
    }

    // Recipe book: opens the full list of what can be made.
    double kx = px + BOOK_BTN_X, ky = py + BOOK_BTN_Y;
    bool bookHover = mx >= kx && mx < kx + BOOK_BTN && my >= ky && my < ky + BOOK_BTN;
    drawBookIcon(kx, ky, BOOK_BTN, bookHover);
    if (bookHover && !bookOpen) {
      drawText(g_fontHint, kx + BOOK_BTN + 8, ky + (BOOK_BTN - g_fontHint.height) / 2,
               "Recipes", 63 / 255.0, 63 / 255.0, 63 / 255.0, 1);
    }

    // Confirm button: consumes the grid and collects the result.
    double bx = px + CRAFT_X, by = py + CRAFT_BTN_Y;
    bool btnHover = canCraft && mx >= bx && mx < bx + CRAFT_BTN_W &&
                    my >= by && my < by + CRAFT_BTN_H;
    double fill = canCraft ? (btnHover ? 150 / 255.0 : 122 / 255.0) : 100 / 255.0;
    drawRect(bx, by, CRAFT_BTN_W, CRAFT_BTN_H, fill, fill, fill, 1);
    drawRectOutline(bx, by, CRAFT_BTN_W, CRAFT_BTN_H, 1, 0, 0, 0, 1);
    double c = canCraft ? 1.0 : 150 / 255.0;
    double lw = textWidth(g_fontHint, "Craft");
    drawText(g_fontHint, bx + (CRAFT_BTN_W - lw) / 2,
             by + (CRAFT_BTN_H - g_fontHint.height) / 2 + 1, "Craft", c, c, c, 1);
  }

  // The stack riding the cursor, drawn last so it floats above everything.
  if (occupied(held)) {
    int tile = craftItemTile((uint8_t)held.blockId);
    if (tile >= 0) drawAtlasTile(tile, mx - ICON / 2, my - ICON / 2, ICON, ICON);
    char count[16];
    std::snprintf(count, sizeof(count), "%d", held.count);
    double tx = mx + ICON / 2 - 3 - textWidth(g_fontCount, count);
    double ty = my + ICON / 2 - 1 - g_fontCount.height;
    drawText(g_fontCount, tx + 1, ty + 1, count, 0, 0, 0, 1);
    drawText(g_fontCount, tx, ty, count, 1, 1, 1, 1);
  }

  // Recipe list, over the whole screen: it is a reference sheet, so it wins
  // over the panel beneath it and swallows the tooltip while it is up.
  if (bookOpen) {
    double kx = bookX(winW), ky = bookY(winH), kh = bookH();
    drawRect(0, 0, winW, winH, 0, 0, 0, 0.55); // dim everything behind
    drawRect(kx, ky, BOOK_W, kh, 198 / 255.0, 198 / 255.0, 198 / 255.0, 1);
    drawRect(kx, ky, BOOK_W, 3, 1, 1, 1, 1);
    drawRect(kx, ky, 3, kh, 1, 1, 1, 1);
    drawRect(kx, ky + kh - 3, BOOK_W, 3, 85 / 255.0, 85 / 255.0, 85 / 255.0, 1);
    drawRect(kx + BOOK_W - 3, ky, 3, kh, 85 / 255.0, 85 / 255.0, 85 / 255.0, 1);
    drawRectOutline(kx, ky, BOOK_W, kh, 1, 0, 0, 0, 1);

    const char* title = "Recipes";
    double tw = textWidth(g_fontHint, title);
    drawText(g_fontHint, kx + (BOOK_W - tw) / 2, ky + BOOK_PAD - 4, title,
             0.16, 0.16, 0.16, 1);
    const char* hint = "click a recipe to craft it - click elsewhere to close";
    drawText(g_fontHint, kx + BOOK_W - BOOK_PAD - textWidth(g_fontHint, hint),
             ky + BOOK_PAD - 4, hint, 0.45, 0.45, 0.45, 1);

    double colW = (BOOK_W - BOOK_PAD * 2) / BOOK_COLS;
    int rows = bookRows();
    int hoverRow = -1;
    bookRowAtPoint(mx, my, winW, winH, hoverRow);
    unsigned long long nowMs = (unsigned long long)GetTickCount64();
    for (int i = 0; i < CRAFT_RECIPE_COUNT; i++) {
      int col = i / rows, row = i % rows;
      double lx = kx + BOOK_PAD + col * colW;
      double ly = ky + BOOK_PAD + BOOK_TITLE_H + row * BOOK_ROW_H;
      // A brief pressed-down flash on the row that was just crafted — a
      // visible "this button worked" pulse, distinct from the plain hover
      // tint, so a click always reads as having landed.
      bool pressed = i == bookPressedIndex && nowMs - bookPressedAtMs < BOOK_PRESS_FLASH_MS;
      if (pressed) {
        ly += 2;
        drawRect(lx - 4, ly, colW - 8, BOOK_ROW_H - 2, 0.55, 1, 0.55, 0.55);
      } else if (i == hoverRow) {
        bool affordable = bookCanAfford(hotbar, main, CRAFT_RECIPES[i]);
        drawRect(lx - 4, ly, colW - 8, BOOK_ROW_H, affordable ? 0.6 : 1, affordable ? 1 : 0.35,
                 affordable ? 0.6 : 0.35, 0.28);
      } else if (row % 2 == 0) { // faint banding, so long rows stay easy to follow
        drawRect(lx - 4, ly, colW - 8, BOOK_ROW_H, 1, 1, 1, 0.18);
      }
      drawRecipeLine(CRAFT_RECIPES[i], lx, ly, colW - 12);
    }
    return;
  }

  // Tooltip goes over everything, but not while a stack rides the cursor —
  // it would sit under the carried icon and just add clutter mid-drag.
  if (hoverItemId >= 0 && !occupied(held)) {
    drawItemTooltip(hoverItemId, mx, my, winW, winH);
  }

  // Context menu, drawn last of all so it sits above the tooltip too — it
  // is effectively modal while open (see onMouseDown).
  if (contextMenuSlot) {
    bool isFood = isEatableFood((uint8_t)contextMenuSlot->blockId);
    bool isPotion = isPotionItem(contextMenuSlot->blockId);
    bool isTool = isToolItem((uint8_t)contextMenuSlot->blockId);
    bool hasTopButton = isFood || isPotion || isTool;
    double mnx, mny, mnw, mnh;
    contextMenuRect(contextMenuX, contextMenuY, winW, winH, hasTopButton, mnx, mny, mnw, mnh);

    drawRect(mnx, mny, mnw, mnh, 20 / 255.0, 20 / 255.0, 20 / 255.0, 0.95);
    drawRectOutline(mnx, mny, mnw, mnh, 2, 1, 1, 1, 0.6);

    auto drawMenuButton = [&](double by, const char* label) {
      bool hover = mx >= mnx && mx < mnx + mnw && my >= by && my < by + CTX_MENU_BTN_H;
      if (hover) drawRect(mnx, by, mnw, CTX_MENU_BTN_H, 1, 1, 1, 0.18);
      double lw = textWidth(g_fontHint, label);
      drawText(g_fontHint, mnx + (mnw - lw) / 2, by + (CTX_MENU_BTN_H - g_fontHint.height) / 2,
               label, 1, 1, 1, 1);
    };
    if (hasTopButton) {
      drawMenuButton(mny, isTool ? "Equip" : "Use");
      drawRect(mnx, mny + CTX_MENU_BTN_H, mnw, 1, 1, 1, 1, 0.25); // divider
      drawMenuButton(mny + CTX_MENU_BTN_H, "Drop");
    } else {
      drawMenuButton(mny, "Drop");
    }
  }
}

bool Inventory::previewRect(int winW, int winH, double& x, double& y,
                            double& w, double& h) const {
  if (tab != INV_TAB_PLAYER) return false;
  x = panelX(winW) + PREVIEW_X;
  y = panelY(winH, tab) + CONTENT_TOP;
  w = PREVIEW_W;
  h = ARMOR_H;
  return true;
}

bool Inventory::characterSwitchButtonHit(double mx, double my, int winW, int winH) const {
  if (tab != INV_TAB_PLAYER) return false;
  double px = panelX(winW);
  double py = panelY(winH, tab);
  double bx = px + PREVIEW_X;
  double by = py + CHAR_BTN_Y;
  return mx >= bx && mx < bx + PREVIEW_W && my >= by && my < by + CHAR_BTN_H;
}

bool Inventory::craftButtonRect(int winW, int winH, double& x, double& y,
                                double& w, double& h) const {
  if (tab != INV_TAB_CRAFT) return false;
  x = panelX(winW) + CRAFT_X;
  y = panelY(winH, tab) + CRAFT_BTN_Y;
  w = CRAFT_BTN_W;
  h = CRAFT_BTN_H;
  return true;
}

bool Inventory::panelRect(int winW, int winH, double& x, double& y,
                          double& w, double& h) const {
  x = panelX(winW);
  y = panelY(winH, tab);
  w = PANEL_W;
  h = panelH(tab);
  return true;
}

bool Inventory::craftSlotRect(int index, int winW, int winH, double& x, double& y,
                              double& w, double& h) const {
  if (tab != INV_TAB_CRAFT || index < 0 || index >= INV_CRAFT_COUNT) return false;
  x = panelX(winW) + CRAFT_X + (index % 3) * PITCH;
  y = panelY(winH, tab) + CRAFT_GRID_Y + (index / 3) * PITCH;
  w = SLOT;
  h = SLOT;
  return true;
}

bool Inventory::mainSlotRect(int index, int winW, int winH, double& x, double& y,
                             double& w, double& h) const {
  if (index < 0 || index >= INV_MAIN_COUNT) return false;
  x = panelX(winW) + PAD + (index % INV_COLS) * PITCH;
  y = panelY(winH, tab) + mainY(tab) + (index / INV_COLS) * PITCH;
  w = SLOT;
  h = SLOT;
  return true;
}

bool Inventory::hotbarSlotRect(int index, int winW, int winH, double& x, double& y,
                               double& w, double& h) const {
  if (index < 0 || index >= SLOT_COUNT) return false;
  x = panelX(winW) + PAD + index * PITCH;
  y = panelY(winH, tab) + hotbarY(tab);
  w = SLOT;
  h = SLOT;
  return true;
}

bool Inventory::craftResultRect(int winW, int winH, double& x, double& y,
                                double& w, double& h) const {
  if (tab != INV_TAB_CRAFT) return false;
  x = panelX(winW) + RESULT_X;
  y = panelY(winH, tab) + CRAFT_GRID_Y + (CRAFT_GRID_H - SLOT) / 2;
  w = SLOT;
  h = SLOT;
  return true;
}

void Inventory::stowHeld(Hotbar& hotbar) {
  if (occupied(held) && collect(hotbar, held.blockId, held.count)) {
    clearSlot(held);
  }
  dragSrc = nullptr;
}

void Inventory::onMouseDown(Hotbar& hotbar, double mx, double my, bool rightButton,
                            int winW, int winH) {
  double px = panelX(winW), py = panelY(winH, tab);

  // The context menu takes priority over everything else while open: any
  // click either presses one of its buttons or dismisses it — same
  // "anything else closes it" rule the recipe book uses below — and never
  // falls through to whatever's underneath.
  if (contextMenuSlot) {
    Hotbar::Slot* menuSlot = contextMenuSlot;
    contextMenuSlot = nullptr;
    bool isFood = isEatableFood((uint8_t)menuSlot->blockId);
    bool isPotion = isPotionItem(menuSlot->blockId);
    bool isTool = isToolItem((uint8_t)menuSlot->blockId);
    bool hasTopButton = isFood || isPotion || isTool;
    double mnx, mny, mnw, mnh;
    contextMenuRect(contextMenuX, contextMenuY, winW, winH, hasTopButton, mnx, mny, mnw, mnh);

    if (hasTopButton && mx >= mnx && mx < mnx + mnw && my >= mny && my < mny + CTX_MENU_BTN_H) {
      if (isTool) {
        std::swap(mainHand, *menuSlot); // whatever was equipped comes back out
      } else {
        int id = menuSlot->blockId;
        menuSlot->count--;
        if (menuSlot->count <= 0) clearSlot(*menuSlot);
        if (isPotion) pendingHealAmount = healthPotionHeal((uint8_t)id);
        else pendingEatAmount = 1;
      }
      return;
    }
    double dropY = mny + (hasTopButton ? CTX_MENU_BTN_H : 0);
    if (mx >= mnx && mx < mnx + mnw && my >= dropY && my < dropY + CTX_MENU_BTN_H) {
      pendingDrop = *menuSlot;
      clearSlot(*menuSlot);
      return;
    }
    return; // clicked elsewhere: dismiss only, same as the recipe book
  }

  // The recipe list: clicking a row crafts it (if affordable) and stays
  // open, so several can be made in a row; clicking the dimmed background
  // around the rows closes it, same as before.
  if (bookOpen) {
    int i = -1;
    if (bookRowAtPoint(mx, my, winW, winH, i)) {
      if (tryCraftFromBook(hotbar, CRAFT_RECIPES[i])) {
        bookPressedIndex = i;
        bookPressedAtMs = (unsigned long long)GetTickCount64();
      }
    } else {
      bookOpen = false;
    }
    return;
  }
  if (tab == INV_TAB_CRAFT) {
    double kx = px + BOOK_BTN_X, ky = py + BOOK_BTN_Y;
    if (mx >= kx && mx < kx + BOOK_BTN && my >= ky && my < ky + BOOK_BTN) {
      bookOpen = true;
      return;
    }
  }

  // Tab headers switch tabs and never touch the slots.
  for (int t = 0; t < INV_TAB_COUNT; t++) {
    double tx = px + PAD + t * tabW();
    if (mx >= tx && mx < tx + tabW() && my >= py + TAB_TOP && my < py + TAB_TOP + TAB_H) {
      tab = t;
      return;
    }
  }

  // Craft tab: the confirm button crafts the grid into the inventory.
  if (tab == INV_TAB_CRAFT) {
    double bx = px + CRAFT_X, by = py + CRAFT_BTN_Y;
    if (mx >= bx && mx < bx + CRAFT_BTN_W && my >= by && my < by + CRAFT_BTN_H) {
      Hotbar::Slot outcome;
      if (craftOutcome(craft, outcome) && collect(hotbar, outcome.blockId, outcome.count)) {
        // A match means the grid holds EXACTLY the recipe's ingredients and
        // nothing else, so the craft consumes all of it. (Per-cell decrement
        // made sense while a cell counted as one ingredient regardless of
        // depth; now the depth is the ingredient.)
        for (Hotbar::Slot& s : craft) clearSlot(s);
      }
      return;
    }
  }

  Hotbar::Slot* target = slotAtPoint(*this, hotbar, mx, my, winW, winH);
  if (!target) return;
  if (target == &mainHand && occupied(held) && !isToolItem((uint8_t)held.blockId)) return;

  // Double-click an ingredient while the Craft tab is open and one of it
  // jumps into the grid — quicker than dragging each piece across, and the
  // recipes here only need two or three. A single click still picks the item
  // up as normal, so the shortcut costs nothing if you don't use it.
  unsigned long long nowMs = (unsigned long long)GetTickCount64();
  bool doubleClick = !rightButton && target == lastClickSlot &&
                     nowMs - lastClickMs <= DOUBLE_CLICK_MS;
  if (doubleClick && tab == INV_TAB_CRAFT && !occupied(held) &&
      !isCraftSlot(craft, target) && occupied(*target)) {
    lastClickSlot = nullptr; // a third click starts a fresh pair
    if (sendOneToCraft(craft, *target)) return;
  }
  // Double-click a food or potion stack anywhere OUTSIDE the Craft tab (so it
  // can never collide with the shortcut just above) to eat/drink one.
  if (doubleClick && tab != INV_TAB_CRAFT && !occupied(held) && occupied(*target) &&
      (isEatableFood((uint8_t)target->blockId) || isPotionItem(target->blockId))) {
    lastClickSlot = nullptr;
    int id = target->blockId;
    target->count--;
    if (target->count <= 0) clearSlot(*target);
    if (isPotionItem(id)) pendingHealAmount = healthPotionHeal((uint8_t)id);
    else pendingEatAmount = 1;
    return;
  }
  lastClickSlot = target;
  lastClickMs = nowMs;

  // Right-click a tool/weapon/food item (nothing already riding the cursor)
  // opens the context menu instead of grabbing the whole stack — every
  // other item (blocks, materials, ...) keeps that original behaviour.
  if (rightButton && !occupied(held) && occupied(*target) && isDroppableItem(target->blockId)) {
    contextMenuSlot = target;
    contextMenuX = mx;
    contextMenuY = my;
    return;
  }

  Hotbar::Slot& s = *target;

  // A stack is still riding the cursor: drop one or all items here.
  if (occupied(held)) {
    if (!occupied(s)) {
      int move = rightButton ? held.count : 1;
      s.blockId = held.blockId;
      s.count = move;
      held.count -= move;
      if (held.count <= 0) clearSlot(held);
    } else if (s.blockId == held.blockId) {
      int move = std::min(INV_STACK_MAX - s.count, held.count);
      if (!rightButton) move = std::min(move, 1);
      s.count += move;
      held.count -= move;
      if (held.count <= 0) clearSlot(held);
    } else {
      std::swap(held, s);
    }
    return;
  }

  if (!occupied(s)) return;
  if (!rightButton) {
    // Left drag: pick up exactly one item.
    held.blockId = s.blockId;
    held.count = 1;
    s.count--;
    if (s.count <= 0) clearSlot(s);
    dragSrc = target;
  } else {
    // Right drag: pick up the whole stack.
    held = s;
    clearSlot(s);
    dragSrc = target;
  }
}

void Inventory::onMouseUp(Hotbar& hotbar, double mx, double my, bool rightButton,
                          int winW, int winH) {
  if (!dragSrc) return;
  Hotbar::Slot* src = dragSrc;
  dragSrc = nullptr;
  if (!occupied(held)) return;

  // Dragging any food or potion onto the Player-tab character preview
  // consumes the WHOLE held stack at once — a bigger gesture than the
  // double-click (one) or right-click (one) shortcuts, matching "feed/dose
  // yourself" being a more deliberate action than a quick nibble.
  double px_, py_, pw_, ph_;
  bool isPotionHeld = isPotionItem(held.blockId);
  if ((isEatableFood((uint8_t)held.blockId) || isPotionHeld) &&
      previewRect(winW, winH, px_, py_, pw_, ph_) &&
      mx >= px_ && mx < px_ + pw_ && my >= py_ && my < py_ + ph_) {
    if (isPotionHeld) pendingHealAmount = healthPotionHeal((uint8_t)held.blockId) * held.count;
    else pendingEatAmount = held.count;
    clearSlot(held);
    return;
  }

  Hotbar::Slot* target = slotAtPoint(*this, hotbar, mx, my, winW, winH);
  if (target == &mainHand && !isToolItem((uint8_t)held.blockId)) target = nullptr;
  if (target && target != src) {
    if (!occupied(*target)) {
      int move = rightButton ? held.count : 1;
      target->blockId = held.blockId;
      target->count = move;
      held.count -= move;
      if (held.count <= 0) clearSlot(held);
    } else if (target->blockId == held.blockId) {
      int move = std::min(INV_STACK_MAX - target->count, held.count);
      if (!rightButton) move = std::min(move, 1);
      target->count += move;
      held.count -= move;
      if (held.count <= 0) clearSlot(held);
    } else {
      // A different item at the target: swap the two FULL stacks, not
      // whatever partial amount happens to be riding the cursor. A
      // left-click drag only picks up one item (onMouseDown leaves the rest
      // in `src`), so swapping just `held` for `*target` here used to strand
      // that single item on the cursor — its id wouldn't match whatever was
      // left behind in `src`, so the fallback returnToSource() below would
      // refuse it. Reuniting `held` with `src` first (guaranteed empty or
      // the same id, since that's exactly where `held` came from) before
      // swapping avoids that: it's always a clean stack-for-stack exchange.
      if (src->blockId < 0) *src = held;
      else src->count += held.count;
      clearSlot(held);
      std::swap(*src, *target);
    }
  }
  // Not fully dropped (released outside the slots, or a merge leftover):
  // whatever remains either goes back where it came from, or — if released
  // past the panel's own edge entirely, not just off a slot — is tossed
  // into the world instead, the same "drag it out and let go" gesture a
  // real inventory window uses to drop an item.
  if (occupied(held)) {
    if (!target && !insidePanel(mx, my, winW, winH, tab)) {
      pendingDrop = held;
      clearSlot(held);
    } else {
      returnToSource(held, src);
    }
  }
}

std::vector<int> Inventory::serializeIds() const {
  std::vector<int> out;
  for (const Hotbar::Slot& s : main) out.push_back(s.blockId);
  for (const Hotbar::Slot& s : armor) out.push_back(s.blockId);
  out.push_back(offhand.blockId);
  for (const Hotbar::Slot& s : craft) out.push_back(s.blockId);
  out.push_back(mainHand.blockId);
  return out;
}

std::vector<int> Inventory::serializeCounts() const {
  std::vector<int> out;
  for (const Hotbar::Slot& s : main) out.push_back(s.count);
  for (const Hotbar::Slot& s : armor) out.push_back(s.count);
  out.push_back(offhand.count);
  for (const Hotbar::Slot& s : craft) out.push_back(s.count);
  out.push_back(mainHand.count);
  return out;
}

void Inventory::loadSerialized(const std::vector<int>& ids, const std::vector<int>& counts) {
  // Older saves predate the mainHand slot and are one entry short; load
  // everything else and leave mainHand empty rather than discarding the lot.
  int n = INV_SAVED_SLOTS;
  bool hasMainHand = ids.size() == (size_t)n + 1 && counts.size() == (size_t)n + 1;
  if (!hasMainHand && (ids.size() != (size_t)n || counts.size() != (size_t)n)) return;
  Hotbar::Slot* all[INV_SAVED_SLOTS];
  int i = 0;
  for (Hotbar::Slot& s : main) all[i++] = &s;
  for (Hotbar::Slot& s : armor) all[i++] = &s;
  all[i++] = &offhand;
  for (Hotbar::Slot& s : craft) all[i++] = &s;
  for (int j = 0; j < n; j++) {
    all[j]->blockId = ids[j];
    all[j]->count = counts[j];
  }
  if (hasMainHand) {
    mainHand.blockId = ids[n];
    mainHand.count = counts[n];
  } else {
    clearSlot(mainHand);
  }
}

bool Inventory::chestSlotRect(int index, int winW, int winH, double& x, double& y,
                              double& w, double& h) const {
  if (index < 0 || index >= CHEST_SLOT_COUNT) return false;
  x = chestPanelX(winW) + CHEST_GRID_X + (index % 3) * PITCH;
  y = chestPanelY(winH) + CHEST_GRID_Y + (index / 3) * PITCH;
  w = SLOT;
  h = SLOT;
  return true;
}

void Inventory::drawChestPanel(int winW, int winH) const {
  double px = chestPanelX(winW), py = chestPanelY(winH);
  double ph = CHEST_PANEL_H;

  drawRect(0, 0, winW, winH, 0, 0, 0, 0.55); // dim the world behind

  drawRect(px, py, CHEST_PANEL_W, ph, 198 / 255.0, 198 / 255.0, 198 / 255.0, 1);
  drawRect(px, py, CHEST_PANEL_W, 3, 1, 1, 1, 1);
  drawRect(px, py, 3, ph, 1, 1, 1, 1);
  drawRect(px, py + ph - 3, CHEST_PANEL_W, 3, 85 / 255.0, 85 / 255.0, 85 / 255.0, 1);
  drawRect(px + CHEST_PANEL_W - 3, py, 3, ph, 85 / 255.0, 85 / 255.0, 85 / 255.0, 1);
  drawRectOutline(px, py, CHEST_PANEL_W, ph, 1, 0, 0, 0, 1);
}

void Inventory::drawChestContents(ChestState& chest, const Hotbar& hotbar, int winW, int winH,
                                  double mx, double my) const {
  double px = chestPanelX(winW), py = chestPanelY(winH);

  const char* title = "Chest";
  double tw = textWidth(g_fontHint, title);
  drawText(g_fontHint, px + (CHEST_PANEL_W - tw) / 2, py + CHEST_TITLE_Y, title,
           63 / 255.0, 63 / 255.0, 63 / 255.0, 1);
  drawSectionLabel(px + PAD, py + CHEST_MAIN_LABEL_Y, "Inventory");
  drawSectionLabel(px + PAD, py + CHEST_HOTBAR_LABEL_Y, "Hotbar");

  struct Ref { double x, y; const Hotbar::Slot* slot; };
  Ref refs[CHEST_SLOT_COUNT + INV_MAIN_COUNT + SLOT_COUNT];
  int n = 0;
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
      refs[n++] = { px + CHEST_GRID_X + c * PITCH, py + CHEST_GRID_Y + r * PITCH, &chest.slots[r * 3 + c] };
  for (int i = 0; i < INV_MAIN_COUNT; i++)
    refs[n++] = { px + PAD + (i % INV_COLS) * PITCH, py + CHEST_MAIN_Y + (i / INV_COLS) * PITCH, &main[i] };
  for (int i = 0; i < SLOT_COUNT; i++)
    refs[n++] = { px + PAD + i * PITCH, py + CHEST_HOTBAR_Y, &hotbar.slots[i] };

  int hoverItemId = -1;
  for (int i = 0; i < n; i++) {
    const Ref& ref = refs[i];
    drawSlotBox(ref.x, ref.y, SLOT, SLOT);
    drawSlotIcon(*ref.slot, ref.x, ref.y);
    drawSlotCount(*ref.slot, ref.x, ref.y);
    if (mx >= ref.x && mx < ref.x + SLOT && my >= ref.y && my < ref.y + SLOT) {
      drawRect(ref.x, ref.y, SLOT, SLOT, 1, 1, 1, 0.4); // hover highlight
      if (occupied(*ref.slot)) hoverItemId = ref.slot->blockId;
    }
  }

  // The stack riding the cursor, drawn last so it floats above everything.
  if (occupied(held)) {
    int tile = craftItemTile((uint8_t)held.blockId);
    if (tile >= 0) drawAtlasTile(tile, mx - ICON / 2, my - ICON / 2, ICON, ICON);
    char count[16];
    std::snprintf(count, sizeof(count), "%d", held.count);
    double tx = mx + ICON / 2 - 3 - textWidth(g_fontCount, count);
    double ty = my + ICON / 2 - 1 - g_fontCount.height;
    drawText(g_fontCount, tx + 1, ty + 1, count, 0, 0, 0, 1);
    drawText(g_fontCount, tx, ty, count, 1, 1, 1, 1);
  }

  if (hoverItemId >= 0 && !occupied(held)) {
    drawItemTooltip(hoverItemId, mx, my, winW, winH);
  }
}

void Inventory::chestMouseDown(ChestState& chest, Hotbar& hotbar, double mx, double my,
                               bool rightButton, int winW, int winH) {
  Hotbar::Slot* target = chestSlotAtPoint(chest, *this, hotbar, mx, my, winW, winH);
  if (!target) return;

  Hotbar::Slot& s = *target;

  // A stack is still riding the cursor: drop one or all items here.
  if (occupied(held)) {
    if (!occupied(s)) {
      int move = rightButton ? held.count : 1;
      s.blockId = held.blockId;
      s.count = move;
      held.count -= move;
      if (held.count <= 0) clearSlot(held);
    } else if (s.blockId == held.blockId) {
      int move = std::min(INV_STACK_MAX - s.count, held.count);
      if (!rightButton) move = std::min(move, 1);
      s.count += move;
      held.count -= move;
      if (held.count <= 0) clearSlot(held);
    } else {
      std::swap(held, s);
    }
    return;
  }

  if (!occupied(s)) return;
  if (!rightButton) {
    // Left drag: pick up exactly one item.
    held.blockId = s.blockId;
    held.count = 1;
    s.count--;
    if (s.count <= 0) clearSlot(s);
    dragSrc = target;
  } else {
    // Right drag: pick up the whole stack.
    held = s;
    clearSlot(s);
    dragSrc = target;
  }
}

void Inventory::chestMouseUp(ChestState& chest, Hotbar& hotbar, double mx, double my,
                             bool rightButton, int winW, int winH) {
  if (!dragSrc) return;
  Hotbar::Slot* src = dragSrc;
  dragSrc = nullptr;
  if (!occupied(held)) return;

  Hotbar::Slot* target = chestSlotAtPoint(chest, *this, hotbar, mx, my, winW, winH);
  if (target && target != src) {
    if (!occupied(*target)) {
      int move = rightButton ? held.count : 1;
      target->blockId = held.blockId;
      target->count = move;
      held.count -= move;
      if (held.count <= 0) clearSlot(held);
    } else if (target->blockId == held.blockId) {
      int move = std::min(INV_STACK_MAX - target->count, held.count);
      if (!rightButton) move = std::min(move, 1);
      target->count += move;
      held.count -= move;
      if (held.count <= 0) clearSlot(held);
    } else {
      std::swap(held, *target); // swap the two stacks
    }
  }
  if (occupied(held)) returnToSource(held, src);
}
