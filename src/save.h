#pragma once
#include "chest.h"
#include "world.h"

// One saved chest: its position, which way its lid opens, and its 9 slots
// (id -1 = empty).
struct ChestSaveEntry {
  int x = 0, y = 0, z = 0;
  int facing = 1;
  int ids[CHEST_SLOT_COUNT] = {};
  int counts[CHEST_SLOT_COUNT] = {};
};

// One saved stair's position and which way it rises.
struct StairSaveEntry {
  int x = 0, y = 0, z = 0, facing = 0;
};

// One saved ladder cell's position and which wall it hangs on.
struct PanelSaveEntry {
  int x = 0, y = 0, z = 0, facing = -1;
};

// One saved table/bed cell's position plus the anchor cell + facing of the
// object it belongs to (see FurnitureState, world.h).
struct FurnitureSaveEntry {
  int x = 0, y = 0, z = 0;
  int anchorX = 0, anchorY = 0, anchorZ = 0;
  int facing = 0;
};

// Named save slots stored as text files in "saves\<name>.txt" next to the
// executable. Each save holds the block-edit diff + player pose + hotbar
// state (terrain regenerates from the fixed seed).
struct SaveState {
  bool hasPlayer = false;
  double x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
  uint32_t seed = 1337; // terrain seed (old saves without one get 1337)
  std::vector<int> hotbarCounts; // empty = fresh defaults
  int selectedSlot = -1;         // -1 = not saved
  std::vector<int> invIds;    // inventory slot block ids (empty = not saved)
  std::vector<int> invCounts; // inventory slot counts, same order as invIds
  std::vector<EditEntry> edits;
  std::vector<ChestSaveEntry> chests;
  std::vector<StairSaveEntry> stairs;
  std::vector<PanelSaveEntry> panels;
  std::vector<FurnitureSaveEntry> furniture;
};

struct SaveInfo {
  std::string name;
  std::string dateText; // "yyyy-mm-dd hh:mm" local time, for the load list
};

void saveGame(const SaveState& state, const std::string& name);
bool loadGame(SaveState& out, const std::string& name);

// Existing saves, newest first.
std::vector<SaveInfo> listSaves();

bool saveExists(const std::string& name);
bool deleteSave(const std::string& name);

// Keeps only filename-safe characters (alnum, space, dash, underscore) and
// trims; returns "" if nothing usable remains.
std::string sanitizeSaveName(const std::string& raw);

// Moves a legacy single-slot save.txt (from the first build) into saves\save.txt.
void migrateLegacySave();

// Directory of the running executable, with trailing backslash.
std::string exeDir();
