#pragma once
#include "blocks.h"

// On-screen hotbar: one row of 10 slots, keys 1-9 and 0 pick a slot.
const int HOTBAR_COLS = 10;
const int SLOT_COUNT = HOTBAR_COLS;
const int STARTING_COUNT = 32;

class Hotbar {
public:
  struct Slot {
    int blockId = -1; // -1 = empty slot
    int count = 0;
  };

  Slot slots[SLOT_COUNT];
  int selected = 0; // index 0..9

  // savedCounts: slot counts from a save file (empty vector = defaults).
  Hotbar(const uint8_t* blockOrder, int orderLen, const std::vector<int>& savedCounts);

  void select(int index);
  int selectedBlockId() const { return slots[selected].blockId; }
  void addBlock(int blockId, int amount = 1);
  // Attempts to consume one of the currently selected block; returns the
  // block id consumed, or -1 if the slot is empty.
  int takeSelected();
  std::vector<int> serialize() const; // counts of all slots

  void draw(int winW, int winH) const;
  // Slot index under a screen point, or -1 (used by the inventory screen for
  // drag-and-drop onto the hotbar).
  int slotAt(double mx, double my, int winW, int winH) const;
};

// Screen rect of the whole hotbar strip — for HUD elements (health/hunger)
// that need to sit directly above it without duplicating its layout
// constants.
void hotbarRect(int winW, int winH, double& x, double& y, double& w, double& h);
