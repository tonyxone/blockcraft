#pragma once
#include "chest.h"
#include "hotbar.h"

struct Recipe; // recipes.h

// Survival inventory screen with three tabs (like the Minecraft recipe-book
// style UI): "Inventory" (backpack grid + on-screen hotbar row), "Player"
// (armor column, clothes and a front-facing character preview) and "Craft"
// (3x3 crafting area + result with a confirm button). The backpack grid and the hotbar row are
// separate sections shown below the tab content. Opened with the I key.
const int INV_COLS = SLOT_COUNT;         // one column per hotbar slot (10)
const int INV_MAIN_COUNT = INV_COLS * 3; // 30 backpack slots
const int INV_ARMOR_COUNT = 4;
const int INV_CRAFT_COUNT = 9; // 3x3 grid
const int INV_STACK_MAX = 99;
// Slots written to the save file: backpack grid + armor + offhand + crafting.
const int INV_SAVED_SLOTS = INV_MAIN_COUNT + INV_ARMOR_COUNT + 1 + INV_CRAFT_COUNT;

enum InventoryTab {
  INV_TAB_INVENTORY = 0,
  INV_TAB_PLAYER = 1,
  INV_TAB_CRAFT = 2,
  INV_TAB_COUNT = 3,
};

// What crafting the given 3x3 grid would produce, via recipes.h::findRecipe.
// False (out untouched) when the grid matches no recipe. Exposed at
// namespace scope (rather than kept file-local to inventory.cpp) so the
// selftest can exercise the exact function the confirm button calls, not
// just the underlying recipe table.
bool craftOutcome(const Hotbar::Slot craft[INV_CRAFT_COUNT], Hotbar::Slot& out);

class Inventory {
public:
  Hotbar::Slot main[INV_MAIN_COUNT];
  Hotbar::Slot armor[INV_ARMOR_COUNT];
  Hotbar::Slot offhand;
  Hotbar::Slot mainHand; // gripped tool (see tools.h); only accepts isToolItem()
  Hotbar::Slot craft[INV_CRAFT_COUNT];
  Hotbar::Slot held; // stack riding the mouse cursor
  int tab = INV_TAB_INVENTORY;
  int characterType = 0; // 0 = Steve, 1 = Alex; mirrored from Settings for UI label

  // Drag & drop state: the slot the dragged stack was taken from (used to
  // return items on an invalid drop).
  Hotbar::Slot* dragSrc = nullptr;

  // Double-click tracking, for the Craft tab shortcut that sends an
  // ingredient straight to the grid.
  Hotbar::Slot* lastClickSlot = nullptr;
  unsigned long long lastClickMs = 0;

  // Recipe list: opened by the book button on the Craft tab. Reference only,
  // so any click dismisses it rather than falling through to the slots.
  bool bookOpen = false;
  bool recipeBookOpen() const { return bookOpen; }
  void closeRecipeBook() { bookOpen = false; }

  // Which row last crafted successfully and when (GetTickCount64(), same
  // clock the double-click timer uses) — drives a brief "pressed" flash so
  // clicking a recipe reads as a real button push, not a silent no-op.
  int bookPressedIndex = -1;
  unsigned long long bookPressedAtMs = 0;

  // How many cooked meat onMouseDown/onMouseUp just consumed via an eat
  // gesture (double-click, or drag onto the Player-tab preview) — Inventory
  // doesn't know about Player/hunger itself, so it just counts here and the
  // caller (main.cpp) applies the actual hunger change and resets this to 0
  // right after each call.
  int pendingEatAmount = 0;

  // Same hand-off, for health potions: how many HEALTH points (not hearts —
  // see healthPotionHeal in recipes.h) the last drink gesture just earned.
  // Inventory doesn't know about Player/health either, so the caller applies
  // it and resets this to 0 right after each call, same as pendingEatAmount.
  int pendingHealAmount = 0;

  // Right-click context menu on a tool/weapon/food slot: Equip (tools) or
  // Use (cooked meat) on top, Drop below. Opened by right-clicking an
  // eligible, occupied slot; closed by any subsequent click, whether or not
  // it lands on a button — same "any click elsewhere dismisses it"
  // convention the recipe book uses. contextMenuX/Y is the raw click point;
  // onMouseDown's hit-test and drawContents both clamp it to the window the
  // same way, so the two always agree on where the menu actually is.
  Hotbar::Slot* contextMenuSlot = nullptr;
  double contextMenuX = 0, contextMenuY = 0;

  // What onMouseDown/onMouseUp just pulled out of the inventory to toss into
  // the world — the context menu's Drop button, or a drag released past the
  // panel's edge. Inventory has no World/Player reference of its own, so
  // (same pattern as pendingEatAmount) it just stages the item+count here;
  // the caller (main.cpp) spawns the actual dropped-item entity and resets
  // this to empty right after each call. blockId < 0 means nothing pending.
  Hotbar::Slot pendingDrop;

  // Picked-up blocks: top up existing stacks (hotbar first, then backpack),
  // then an unassigned hotbar slot, then an empty backpack slot. Returns
  // false when there is nowhere to put the items.
  bool collect(Hotbar& hotbar, int blockId, int amount = 1);

  // Click-to-craft from the recipe book: unlike the 3x3 grid (craftOutcome),
  // this tallies ingredients across the WHOLE hotbar + backpack rather than
  // reading nine literal cells, so it works no matter where the materials
  // are sitting. False (nothing touched) if any ingredient is short, or if
  // consuming them would leave nowhere to put the result.
  bool tryCraftFromBook(Hotbar& hotbar, const Recipe& r);

  // Puts the held stack back into the grid (called when the screen closes).
  void stowHeld(Hotbar& hotbar);

  // First pass: dim backdrop + panel frame + tab strip + preview box, drawn
  // before the 3D player model so the model lands on top of it.
  void drawPanel(int winW, int winH) const;
  // Second pass: slots, icons, counts, labels, hover and the held stack,
  // drawn after the 3D player preview.
  void drawContents(const Hotbar& hotbar, int winW, int winH, double mx, double my) const;
  // Screen rect (top-left origin, px) of the player-preview box. Returns
  // false when the current tab has no preview.
  bool previewRect(int winW, int winH, double& x, double& y, double& w, double& h) const;
  // Screen rect of the Craft tab's confirm button. False on other tabs.
  // Exposed so tests can press the real button instead of hardcoding
  // coordinates that silently rot when the panel layout changes.
  bool craftButtonRect(int winW, int winH, double& x, double& y, double& w, double& h) const;
  // Screen rect of the whole tabbed panel, not just a slot — so tests can
  // build a "released inside the panel but off any slot" point (or an
  // outside-the-panel one) without hardcoding coordinates that would rot if
  // the layout changes.
  bool panelRect(int winW, int winH, double& x, double& y, double& w, double& h) const;
  // Screen rects of craft-grid cell i (0..8, Craft tab only) and backpack
  // slot i, so tests can drive real drags through onMouseDown/onMouseUp
  // rather than assigning to the slot arrays and missing drag-path bugs.
  bool craftSlotRect(int index, int winW, int winH, double& x, double& y, double& w, double& h) const;
  bool mainSlotRect(int index, int winW, int winH, double& x, double& y, double& w, double& h) const;
  // Same, for the on-screen hotbar row drawn at the bottom of every tab.
  bool hotbarSlotRect(int index, int winW, int winH, double& x, double& y, double& w, double& h) const;
  // Result slot, and the gap reserved for the arrow between grid and result.
  bool craftResultRect(int winW, int winH, double& x, double& y, double& w, double& h) const;
  // True if (mx,my) is over the Player-tab character-switch button.
  bool characterSwitchButtonHit(double mx, double my, int winW, int winH) const;
  // Mouse press on the panel: switches tabs, confirms a craft, starts a
  // drag (left = one item, right = whole stack), or drops the held stack.
  void onMouseDown(Hotbar& hotbar, double mx, double my, bool rightButton, int winW, int winH);
  // Mouse release: finishes a drag — left drops one item, right drops the
  // whole held stack.
  void onMouseUp(Hotbar& hotbar, double mx, double my, bool rightButton, int winW, int winH);

  std::vector<int> serializeIds() const;
  std::vector<int> serializeCounts() const;
  void loadSerialized(const std::vector<int>& ids, const std::vector<int>& counts);

  // Chest screen (opened with E over a chest): its own small panel — the
  // chest's 9 slots above the player's backpack + hotbar — sharing this same
  // `held`/`dragSrc` drag state as every other screen, so items move freely
  // between the chest and the inventory. Not part of the tabbed panel above.
  bool chestSlotRect(int index, int winW, int winH, double& x, double& y, double& w, double& h) const;
  void drawChestPanel(int winW, int winH) const;
  void drawChestContents(ChestState& chest, const Hotbar& hotbar, int winW, int winH,
                         double mx, double my) const;
  void chestMouseDown(ChestState& chest, Hotbar& hotbar, double mx, double my,
                      bool rightButton, int winW, int winH);
  void chestMouseUp(ChestState& chest, Hotbar& hotbar, double mx, double my,
                    bool rightButton, int winW, int winH);
};
