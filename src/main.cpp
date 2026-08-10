// BlockCraft (C++ port) — Win32 + classic OpenGL, no third-party libraries.
// Native rewrite of the Three.js original; module layout mirrors src/*.js.

#include <climits>
#include "common.h"
#include "win_gl.h"
#include "constants.h"
#include "blocks.h"
#include "world.h"
#include "chest.h"
#include "door.h"
#include "trapdoor.h"
#include "furnace.h"
#include "campfire.h"
#include "worldgen.h"
#include "mesher.h"
#include "physics.h"
#include "player.h"
#include "raycast.h"
#include "hotbar.h"
#include "inventory.h"
#include "menu.h"
#include "save.h"
#include "settings.h"
#include "gfx.h"
#include "textures.h"
#include "sprites_generated.h"
#include "sound.h"
#include "playermodel.h"
#include "animal.h"
#include "boat.h"
#include "droppeditem.h"
#include "fish.h"
#include "sky.h"
#include "recipes.h"
#include "tools.h"
#include "minimap.h"
#include "worldmap.h"

static const int MESH_BUDGET_PER_FRAME = 12;
// Reach is measured from the EYE along the view ray, and MINING and BUILDING
// deliberately do not share it.
//
// Mining is short: you have to walk up to a block instead of picking it off
// from across the clearing. It cannot go much below this — the block you are
// standing on is EYE_HEIGHT (1.62) straight down, so a shorter reach would
// stop you mining the ground under your own feet.
static const double MINE_REACH = 2.0;
// Building keeps the original long reach: placing is how you bridge gaps,
// pillar up and wall off, all of which need to put a block somewhere you
// cannot stand next to. Shortening this made building fail intermittently.
static const double PLACE_REACH = 6.0;
// Bound on how far water may spread into freshly mined space in one action,
// so breaching into a huge flooded cavern can't stall a frame.
static const int WATER_FILL_MAX_CELLS = 16384;

// Mirrors the worldgen elevation bands, for test assertions.
static const int ROCK_LINE_TEST = 26;
static const int SNOW_LINE_TEST = 32;
static const double BASE_MOUSE_SENSITIVITY = 0.0022;
static const double PITCH_LIMIT = 3.14159265358979323846 / 2 - 0.01;

static const double SKY_R = 0x8f / 255.0, SKY_G = 0xd0 / 255.0, SKY_B = 0xff / 255.0;

typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXT)(int);

// --- globals ---------------------------------------------------------------
static HWND g_hwnd = nullptr;
static HDC g_dc = nullptr;
static HGLRC g_rc = nullptr;
static int g_winW = 1280, g_winH = 720;
static bool g_running = true;

enum class GameState { Menu, Playing, Paused };
static GameState g_state = GameState::Menu;
static bool g_thirdPerson = false; // V toggles first/third person

// Player animation state (third-person limb movement).
static const double ARM_SWING_TIME = 0.35; // seconds for a collect/build swing
static double g_walkPhase = 0;   // gait cycle, advances with distance walked
static double g_walkAmount = 0;  // smoothed 0..1 swing strength
static double g_airAmount = 0;   // smoothed 0..1 airborne-pose blend
static double g_armSwingTimer = 0;
static bool g_swingLeftHand = false; // right hand collects, left hand builds

static std::unique_ptr<World> g_world;
static std::unique_ptr<Player> g_player;
static std::unique_ptr<Hotbar> g_hotbar;
static std::unique_ptr<Inventory> g_inventory;
static std::vector<Animal> g_animals;
static double g_animalSpawnTimer = 0; // seconds since the last spawn maintenance pass
static std::vector<DroppedItem> g_droppedItems;
static std::vector<Fish> g_fishes;
static double g_fishSpawnTimer = 0; // seconds since the last fish spawn maintenance pass
static std::vector<Boat> g_boats;
static int g_playerBoatIndex = -1; // index into g_boats while driving, -1 when on foot
static double g_boatRowPhase = 0;  // advances while actively rowing; drives paddle animation

// A floating "-1.5" that pops up over an animal's head on a hit and fades
// after half a second. World-space position, but drawn as flat 2D HUD text
// (see worldToScreen) since there's no billboarded-3D-text machinery here.
struct DamagePopup {
  Vec3 position;
  double amount;
  double timer;
};
static std::vector<DamagePopup> g_damagePopups;
const double DAMAGE_POPUP_LIFETIME = 0.5;

// Camera matrices captured once per frame right after setup3D() — the exact
// view+projection state every world-space point was drawn with — so
// worldToScreen() can place 2D HUD text over a 3D position without needing
// GLU's gluProject (build.bat links no glu32.lib, and this is the only
// place that would ever need it).
static double g_camModelview[16], g_camProjection[16];
static int g_camViewport[4];
static bool g_invOpen = false; // inventory screen overlay (I key)
static bool g_chestOpen = false; // chest screen overlay (E key on a chest)
static bool g_fullMapOpen = false; // whole-world map overlay (M key)
static int g_chestX = 0, g_chestY = 0, g_chestZ = 0; // which chest is showing
static uint32_t g_currentSeed = 1337; // this session's terrain seed
static Menu g_menu;
static Settings g_settings;

static bool g_keys[256] = {};
static std::string g_lastSaveName; // prefill for the save-name input

// Short-lived HUD message above the hotbar (e.g. why a water action refused).
static std::string g_hudMsg;
static double g_hudMsgTimer = 0;

static void hudMessage(const std::string& text) {
  g_hudMsg = text;
  g_hudMsgTimer = 2.2;
}
static bool g_cursorCaptured = false;
static bool g_cursorHidden = false;
static double g_lookDX = 0, g_lookDY = 0;
static double g_mouseX = 0, g_mouseY = 0; // client coords, for menu UI

// ---------------------------------------------------------------------------
static void setCursorHidden(bool hidden) {
  if (hidden == g_cursorHidden) return;
  g_cursorHidden = hidden;
  ShowCursor(hidden ? FALSE : TRUE);
}

static POINT clientCenterOnScreen() {
  POINT c = { g_winW / 2, g_winH / 2 };
  ClientToScreen(g_hwnd, &c);
  return c;
}

static void setCursorCaptured(bool captured) {
  if (captured == g_cursorCaptured) return;
  g_cursorCaptured = captured;
  setCursorHidden(captured);
  if (captured) {
    POINT c = clientCenterOnScreen();
    SetCursorPos(c.x, c.y);
    g_lookDX = 0;
    g_lookDY = 0;
  }
}

static void clearKeys() { std::memset(g_keys, 0, sizeof(g_keys)); }

static MoveInput getMoveInput() {
  MoveInput in;
  in.forward = (g_keys['W'] ? 1 : 0) - (g_keys['S'] ? 1 : 0);
  in.right = (g_keys['D'] ? 1 : 0) - (g_keys['A'] ? 1 : 0);
  in.jump = g_keys[VK_SPACE];
  in.sprint = g_keys[VK_SHIFT];
  return in;
}

// --- session management (mirrors main.js) ----------------------------------
// Finds dry land to start on. Most of the world is ocean now, so the origin
// is often seabed — spiral outward until a column stands above sea level.
static Vec3 spawnPositionFresh() {
  int sx = 0, sz = 0;
  findSpawnColumn(sx, sz);

  g_world->updateLoadedChunks(sx, sz); // the spawn area may be a new region
  int spawnY = CHUNK_HEIGHT - 1;
  for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
    if (isSolid(g_world->getBlock(sx, y, sz))) {
      spawnY = y + 1;
      break;
    }
  }
  return Vec3(sx + 0.5, spawnY + 0.05, sz + 0.5);
}

static void teardownSession() {
  if (!g_world) return;
  for (auto& kv : g_world->chunks) disposeChunk(*kv.second);
  g_world.reset();
  g_player.reset();
}

static uint32_t randomSeed() {
  LARGE_INTEGER qpc;
  QueryPerformanceCounter(&qpc);
  uint32_t s = (uint32_t)qpc.QuadPart ^ (uint32_t)(qpc.QuadPart >> 32) ^ GetTickCount();
  return s ? s : 1u;
}

static void createSession(const SaveState* save) {
  teardownSession();

  g_animals.clear(); // not persisted (like a furnace's lit state) — fresh each session
  g_animalSpawnTimer = 0;
  g_damagePopups.clear();
  g_droppedItems.clear(); // dropped items don't survive a reload either
  g_fishes.clear();
  g_fishSpawnTimer = 0;
  g_boats.clear(); // boats don't survive a reload either
  g_playerBoatIndex = -1;
  g_boatRowPhase = 0;
  worldMapReset(); // a new/loaded world has nothing to do with the last one's exploration

  // every new game gets a fresh random map; loads restore their saved seed
  g_currentSeed = save ? save->seed : randomSeed();
  setWorldSeed(g_currentSeed);

  g_world = std::make_unique<World>();
  g_world->renderDistance = g_settings.renderDistance;
  if (save && !save->edits.empty()) g_world->loadEdits(save->edits);
  if (save) {
    for (const ChestSaveEntry& c : save->chests) {
      ChestState& state = g_world->chests[{ c.x, c.y, c.z }];
      state.facing = c.facing;
      for (int i = 0; i < CHEST_SLOT_COUNT; i++) {
        state.slots[i].blockId = c.ids[i];
        state.slots[i].count = c.counts[i];
      }
    }
    for (const StairSaveEntry& st : save->stairs) {
      g_world->stairFacings[{ st.x, st.y, st.z }] = st.facing;
    }
    for (const PanelSaveEntry& p : save->panels) {
      g_world->panelFacings[{ p.x, p.y, p.z }] = p.facing;
    }
    for (const FurnitureSaveEntry& fe : save->furniture) {
      g_world->furniture[{ fe.x, fe.y, fe.z }] = { fe.facing, fe.anchorX, fe.anchorY, fe.anchorZ };
    }
  }
  double px = save && save->hasPlayer ? save->x : 0;
  double pz = save && save->hasPlayer ? save->z : 0;
  g_world->updateLoadedChunks(px, pz);

  Vec3 spawn = save && save->hasPlayer ? Vec3(save->x, save->y, save->z) : spawnPositionFresh();
  g_player = std::make_unique<Player>(spawn);
  if (save && save->hasPlayer) {
    g_player->yaw = save->yaw;
    g_player->pitch = save->pitch;
  }

  std::vector<int> counts = save ? save->hotbarCounts : std::vector<int>();
  g_hotbar = std::make_unique<Hotbar>(HOTBAR_ORDER, HOTBAR_ORDER_LEN, counts);
  if (save && save->selectedSlot >= 0) g_hotbar->select(save->selectedSlot);

  g_inventory = std::make_unique<Inventory>();
  if (save) g_inventory->loadSerialized(save->invIds, save->invCounts);
  g_inventory->characterType = g_settings.characterType;
  g_invOpen = false;
  g_chestOpen = false;
  g_fullMapOpen = false;
}

static void beginPlaying() {
  g_state = GameState::Playing;
  g_menu.hide();
  setCursorCaptured(true);
}

static void openInventory() {
  if (!g_inventory) return;
  g_invOpen = true;
  g_inventory->characterType = g_settings.characterType;
  clearKeys(); // release any held movement keys so the player stops
  setCursorCaptured(false); // free the mouse for the slot UI
}

static void closeInventory() {
  if (!g_invOpen) return;
  g_inventory->stowHeld(*g_hotbar); // a stack on the cursor goes back first
  g_inventory->contextMenuSlot = nullptr; // don't reopen stale on the next visit
  g_invOpen = false;
  setCursorCaptured(true);
}

static void openFullMap() {
  g_fullMapOpen = true;
  clearKeys(); // release any held movement keys so the player stops
  setCursorCaptured(false); // free the mouse for placing/removing markers
}

static void closeFullMap() {
  g_fullMapOpen = false;
  setCursorCaptured(true);
}

static void closeChest() {
  if (!g_chestOpen) return;
  g_inventory->stowHeld(*g_hotbar); // a stack on the cursor goes back first
  auto it = g_world->chests.find({ g_chestX, g_chestY, g_chestZ });
  if (it != g_world->chests.end()) it->second.open = false; // lid eases shut
  g_chestOpen = false;
  setCursorCaptured(true);
  playChestSound(false);
}

// Opens the chest at (x,y,z), creating its (empty) storage the first time
// anyone looks inside — a cell only stores the block id, so nothing else
// remembers a chest exists until this happens.
static void openChest(int x, int y, int z) {
  if (!g_inventory || !g_world) return;
  g_chestX = x;
  g_chestY = y;
  g_chestZ = z;
  g_world->chests[{ x, y, z }].open = true;
  g_chestOpen = true;
  clearKeys(); // release any held movement keys so the player stops
  setCursorCaptured(false); // free the mouse for the slot UI
  playChestSound(true);
}

// Flips a furnace's lit state — unlike a chest there's no screen to open, so
// this is the whole interaction: press E, the fire (furnace.cpp) appears or
// vanishes. Creates a (default-facing) entry the first time anyone toggles a
// furnace nobody has touched yet, same lazy pattern as a chest's storage.
static void toggleFurnace(int x, int y, int z) {
  if (!g_world) return;
  FurnaceState& f = g_world->furnaces[{ x, y, z }];
  f.lit = !f.lit;
  playFurnaceSound(f.lit);
}

// Flips a door's target swing angle. `x,y,z` may be either half — the state
// lives on the bottom cell (door.h), so a hit on the top half redirects down
// to the shared entry.
static void toggleDoor(int x, int y, int z) {
  if (!g_world) return;
  if (g_world->getBlock(x, y - 1, z) == ITEM_DOOR) y--;
  bool nowOpen = !g_world->doors[{ x, y, z }].open;
  g_world->doors[{ x, y, z }].open = nowOpen;
  playDoorSound(nowOpen);
}

static void pauseGame() {
  if (g_state != GameState::Playing) return;
  closeInventory();
  closeChest();
  g_state = GameState::Paused;
  clearKeys();
  setCursorCaptured(false);
  g_menu.showPanel(MenuPanel::Pause);
}

static void startNewGame() {
  createSession(nullptr);
  beginPlaying();
}

static void openLoadPanel() {
  std::vector<SaveInfo> saves = listSaves();
  if (saves.empty()) {
    g_menu.showMessage("No saved game found.");
    return;
  }
  g_menu.openLoadPanel(std::move(saves));
}

static void loadNamedAndPlay(const std::string& name) {
  SaveState state;
  if (!loadGame(state, name)) {
    g_menu.showMessage("Failed to load \"" + name + "\".");
    return;
  }
  g_lastSaveName = name;
  createSession(&state);
  beginPlaying();
}

static void openSavePanel() {
  std::string def = g_lastSaveName;
  if (def.empty()) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "save-%d", (int)listSaves().size() + 1);
    def = buf;
  }
  g_menu.openSavePanel(def, listSaves());
}

// Called from the save panel's Save/Enter. Prompts before overwriting an
// existing slot; performSave() does the actual write.
static void performSave(const std::string& name);

static void confirmSaveGame() {
  std::string name = sanitizeSaveName(g_menu.saveNameInput);
  if (name.empty()) {
    g_menu.showMessage("Enter a save name.");
    return;
  }
  if (saveExists(name)) {
    g_menu.openConfirm("Overwrite \"" + name + "\"?", "Overwrite",
                       MenuAction::OverwriteConfirmed, name);
    return;
  }
  performSave(name);
}

static void performSave(const std::string& name) {
  if (!g_world || !g_player || !g_hotbar) return;
  SaveState s;
  s.seed = g_currentSeed;
  s.hasPlayer = true;
  s.x = g_player->position.x;
  s.y = g_player->position.y;
  s.z = g_player->position.z;
  s.yaw = g_player->yaw;
  s.pitch = g_player->pitch;
  s.hotbarCounts = g_hotbar->serialize();
  s.selectedSlot = g_hotbar->selected;
  s.invIds = g_inventory->serializeIds();
  s.invCounts = g_inventory->serializeCounts();
  s.edits = g_world->getEditsSnapshot();
  for (auto& kv : g_world->chests) {
    ChestSaveEntry c;
    c.x = kv.first.x;
    c.y = kv.first.y;
    c.z = kv.first.z;
    c.facing = kv.second.facing;
    for (int i = 0; i < CHEST_SLOT_COUNT; i++) {
      c.ids[i] = kv.second.slots[i].blockId;
      c.counts[i] = kv.second.slots[i].count;
    }
    s.chests.push_back(c);
  }
  for (auto& kv : g_world->stairFacings) {
    s.stairs.push_back({ kv.first.x, kv.first.y, kv.first.z, kv.second });
  }
  for (auto& kv : g_world->panelFacings) {
    s.panels.push_back({ kv.first.x, kv.first.y, kv.first.z, kv.second });
  }
  for (auto& kv : g_world->furniture) {
    s.furniture.push_back({ kv.first.x, kv.first.y, kv.first.z,
                            kv.second.anchorX, kv.second.anchorY, kv.second.anchorZ, kv.second.facing });
  }
  saveGame(s, name);
  g_lastSaveName = name;
  g_menu.showPanel(g_menu.previousPanel); // back to the pause menu
  g_menu.showMessage("Saved \"" + name + "\".");
}

static void quitToMenu() {
  teardownSession();
  g_state = GameState::Menu;
  g_menu.showPanel(MenuPanel::Main);
}

// --- mining / placing --------------------------------------------------
static Vec3 lookDirection() {
  double cp = std::cos(g_player->pitch);
  return Vec3(-std::sin(g_player->yaw) * cp, std::sin(g_player->pitch), -std::cos(g_player->yaw) * cp);
}

static bool targetedBlock(RaycastHit& hit, double reach) {
  return raycastVoxel(*g_world, g_player->eyePosition(), lookDirection(), reach, hit);
}

// True if (x,y,z) is a valid cook station right now: a campfire (always
// lit — see isCampfire, blocks.h) or a lit furnace. Shared by the R-key
// cook action and its "Press R to cook" prompt so they never disagree.
static bool isLitHeatSource(int x, int y, int z) {
  if (!g_world) return false;
  uint8_t id = g_world->getBlock(x, y, z);
  if (isCampfire(id)) return true;
  if (isFurnace(id)) {
    auto it = g_world->furnaces.find({ x, y, z });
    return it != g_world->furnaces.end() && it->second.lit;
  }
  return false;
}

// True if the player is currently looking at a lit heat source within reach
// AND has raw meat selected — the one condition the R key's cook action and
// prompt both gate on.
static bool canCookNow() {
  if (!g_world || !g_player || !g_hotbar) return false;
  if (g_hotbar->selectedBlockId() != ITEM_RAW_MEAT) return false;
  RaycastHit hit;
  if (!targetedBlock(hit, MINE_REACH)) return false;
  return isLitHeatSource(hit.pos[0], hit.pos[1], hit.pos[2]);
}

// Cooks one raw meat into one cooked meat — repeatable, so pressing R
// several times cooks a whole stack one at a time rather than all at once.
static void tryCook() {
  if (!canCookNow()) return;
  Hotbar::Slot& slot = g_hotbar->slots[g_hotbar->selected];
  slot.count--;
  if (slot.count <= 0) { slot.blockId = -1; slot.count = 0; }
  if (g_inventory) g_inventory->collect(*g_hotbar, ITEM_COOKED_MEAT, 1);
}

// Cooked meat restores hunger 1:1, capped at max. Two of the three eat
// gestures (double-click, drag-to-preview) happen inside Inventory's own
// mouse handlers, which don't know about Player — they just record how much
// was consumed in pendingEatAmount, and this applies the actual hunger
// change right after each call and resets it, so Inventory never needs a
// Player reference of its own.
static void applyPendingEat() {
  if (!g_inventory || !g_player || g_inventory->pendingEatAmount <= 0) return;
  g_player->hunger = std::min(g_player->maxHunger, g_player->hunger + g_inventory->pendingEatAmount);
  g_inventory->pendingEatAmount = 0;
}

// The third eat gesture: right-click while cooked meat is selected in the
// hotbar. Checked ahead of tryPlace()'s normal build logic, since meat
// isn't placeable anyway and eating shouldn't need a valid block target the
// way building does (you can eat looking at open sky).
static bool tryEatSelected() {
  if (!g_hotbar || !g_player || g_hotbar->selectedBlockId() != ITEM_COOKED_MEAT) return false;
  if (g_player->hunger >= g_player->maxHunger) return false;
  Hotbar::Slot& slot = g_hotbar->slots[g_hotbar->selected];
  slot.count--;
  if (slot.count <= 0) { slot.blockId = -1; slot.count = 0; }
  g_player->hunger = std::min(g_player->maxHunger, g_player->hunger + 1);
  return true;
}

// Placing a boat is nothing like placing a block: no cell in the world
// grid, no footprint, just a free-floating Boat (boat.h) spawned right on
// the water's own surface. Checked ahead of tryPlace()'s normal build
// logic, and using its own raycastWater rather than the solid-block
// targetedBlock() every other placement reads, since a boat can only ever
// go on open water — never on land.
static bool tryPlaceBoat() {
  if (!g_world || !g_player || !g_hotbar || g_hotbar->selectedBlockId() != ITEM_BOAT) return false;
  RaycastHit hit;
  if (!raycastWater(*g_world, g_player->eyePosition(), lookDirection(), PLACE_REACH, hit)) return false;
  double surfaceY;
  if (!canPlaceBoatAt(*g_world, hit.pos[0], hit.pos[2], g_boats, surfaceY)) return false;
  Boat boat;
  boat.position = Vec3(hit.pos[0] + 0.5, surfaceY, hit.pos[2] + 0.5);
  boat.yaw = g_player->yaw;
  g_boats.push_back(boat);
  g_hotbar->takeSelected();
  playPlaceSound();
  return true;
}

// Tosses an item out in front of the player — a short forward-and-up arc,
// same idea as Minecraft's own Q-drop — and lets updateDroppedItem's gravity
// settle it onto whatever's below, floor or table alike.
static void spawnDroppedItem(int itemId, int count) {
  if (!g_world || !g_player || itemId < 0 || count <= 0) return;
  DroppedItem it;
  it.itemId = itemId;
  it.count = count;
  Vec3 dir = lookDirection();
  Vec3 eye = g_player->eyePosition();
  it.position = Vec3(eye.x + dir.x * 0.6, eye.y - 0.3, eye.z + dir.z * 0.6);
  it.velocity = Vec3(dir.x * 1.5, 2.5, dir.z * 1.5);
  it.bobPhase = (double)(g_droppedItems.size() % 12) * 0.5236; // spreads starting phases out
  g_droppedItems.push_back(it);
}

// Inventory has no World/Player reference of its own, so the right-click
// menu's Drop button and dragging a stack out past the panel's edge both
// just stage what to drop in pendingDrop (same pattern as pendingEatAmount)
// — this spawns the actual world item right after each mouse-handler call
// and resets it.
static void applyPendingDrop() {
  if (!g_inventory || g_inventory->pendingDrop.blockId < 0) return;
  spawnDroppedItem(g_inventory->pendingDrop.blockId, g_inventory->pendingDrop.count);
  g_inventory->pendingDrop = Hotbar::Slot();
}

// Index of the closest dropped item within pickup reach of the player, or
// -1. Proximity, not a raycast — items sit low and small on the ground, so
// "close enough to grab" reads better than "aimed at exactly". Still
// respects pickupDelay (droppeditem.h), so the item you just tossed isn't
// immediately pickable again while you're still standing on top of it.
static int nearestDroppedItem() {
  if (!g_player) return -1;
  const double PICKUP_REACH = 1.0;
  int best = -1;
  double bestDist2 = PICKUP_REACH * PICKUP_REACH;
  for (size_t i = 0; i < g_droppedItems.size(); i++) {
    const DroppedItem& it = g_droppedItems[i];
    if (it.pickupDelay > 0) continue;
    double dx = it.position.x - g_player->position.x;
    double dy = it.position.y - g_player->position.y;
    double dz = it.position.z - g_player->position.z;
    double d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < bestDist2) { bestDist2 = d2; best = (int)i; }
  }
  return best;
}

// Picks up the nearest in-reach dropped item (E key): tries to collect it
// into the hotbar/backpack, and only removes it from the world if there was
// actually somewhere to put it — a full inventory just leaves it on the
// ground rather than silently destroying it.
static void tryPickUpItem() {
  int i = nearestDroppedItem();
  if (i < 0 || !g_inventory || !g_hotbar) return;
  DroppedItem& it = g_droppedItems[(size_t)i];
  if (g_inventory->collect(*g_hotbar, it.itemId, it.count)) {
    g_droppedItems[(size_t)i] = g_droppedItems.back();
    g_droppedItems.pop_back();
  }
}

// True if any cell of a nearby bed's footprint is within a couple of blocks
// of the player. Proximity, not a raycast — you walk up to a bed to sleep in
// it, the way item pickup works, rather than needing to aim at one exact
// plank the way a chest or door does. Every cell of a placed bed's footprint
// carries the same block id (blocks.h's isBed/bedFootprint), so a plain
// getBlock scan finds it without needing the furniture map's anchor lookup.
static bool nearBed() {
  if (!g_world || !g_player) return false;
  const int REACH = 2;
  int px = (int)std::floor(g_player->position.x);
  int py = (int)std::floor(g_player->position.y);
  int pz = (int)std::floor(g_player->position.z);
  for (int dx = -REACH; dx <= REACH; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      for (int dz = -REACH; dz <= REACH; dz++) {
        if (isBed(g_world->getBlock(px + dx, py + dy, pz + dz))) return true;
      }
    }
  }
  return false;
}

// Sleeping (E key, near a bed): resets health to full for free, at the cost
// of hunger draining twice as fast for a while afterward (player.h's
// hungerBoostTimer does the actual math in Player::update). This game has
// no day/night cycle to skip past like vanilla Minecraft's own bed does —
// sleeping here is purely the health/hunger tradeoff.
const double SLEEP_HUNGER_BOOST_DURATION = 60.0; // ~2 extra hunger points lost over that span
static void trySleep() {
  if (!g_player || !nearBed()) return;
  g_player->health = g_player->maxHealth;
  g_player->hungerBoostTimer = SLEEP_HUNGER_BOOST_DURATION;
  playSleepSound();
}

// Enter/exit a boat (E key). Entering just claims it (occupied=true, the
// per-frame driving update takes over from there); exiting drops the player
// back onto foot-controls exactly where the boat was — normally that's the
// water surface, so stepping out onto open water and starting to sink is
// expected, the same as stepping off a dock.
static void tryEnterOrExitBoat() {
  if (!g_player) return;
  if (g_playerBoatIndex >= 0) {
    if (g_playerBoatIndex < (int)g_boats.size()) g_boats[g_playerBoatIndex].occupied = false;
    g_playerBoatIndex = -1;
    return;
  }
  int i = nearestBoat(g_boats, g_player->position);
  if (i < 0) return;
  g_boats[(size_t)i].occupied = true;
  g_playerBoatIndex = i;
}

static void remeshAll(const std::vector<Chunk*>& chunks) {
  for (Chunk* c : chunks) remeshChunk(*g_world, *c);
}

// A trapdoor is a trap, not a hand-toggled hatch: standing on one (or
// currently falling through it) springs it open, and it swings itself shut
// again a couple of seconds after nothing is passing through its own cell
// any more — long enough to guarantee a real window to drop, rather than
// snapping shut the instant a foot leaves the cell. Called every frame —
// unlike a one-shot latch, this doesn't gate on onGround, since that's
// exactly what lets it stay open while actively falling through the hole it
// just made. No remesh needed either way: a trapdoor draws dynamically
// every frame (trapdoor.cpp), the same reason toggling a door never
// remeshes anything.
static void checkTrapdoorTrigger(double dt) {
  if (!g_world || !g_player) return;
  const double STAY_OPEN_SEC = 2.0;
  int fx = (int)std::floor(g_player->position.x);
  int fy = (int)std::floor(g_player->position.y - 0.01); // the cell the feet are in/on
  int fz = (int)std::floor(g_player->position.z);
  bool onTrapdoor = g_world->getBlock(fx, fy, fz) == ITEM_TRAPDOOR;

  if (onTrapdoor) {
    TrapdoorState& state = g_world->trapdoors[{ fx, fy, fz }];
    if (!state.open) {
      state.open = true;
      playTrapdoorSound();
    }
    state.closeTimer = STAY_OPEN_SEC; // reset the countdown while still in active use
  }
  // Count down every OTHER known open trapdoor and only close it once its
  // timer runs out — that's what guarantees a real drop window instead of
  // slamming shut the instant a foot leaves the cell, while still never
  // blocking a fall still in progress through the one actively being used.
  for (auto& kv : g_world->trapdoors) {
    bool isActive = onTrapdoor && kv.first.x == fx && kv.first.y == fy && kv.first.z == fz;
    if (isActive || !kv.second.open) continue;
    kv.second.closeTimer -= dt;
    if (kv.second.closeTimer <= 0) kv.second.open = false;
  }
}

static void tryMine() {
  if (!g_world || !g_player) return;
  g_armSwingTimer = ARM_SWING_TIME; // swing the arm on every attempt
  g_swingLeftHand = false;          // collecting is the right hand
  // A whoosh whenever a gripped tool/weapon swings, hit or miss — unlike
  // playMineSound() below, which only fires on a successful mine.
  bool holdingTool = g_inventory && isToolItem((uint8_t)g_inventory->mainHand.blockId);
  if (holdingTool) playSwingSound();

  // An animal in reach takes priority over mining a block behind it — the
  // same swing either attacks or mines, never both.
  int animalHit = raycastAnimal(g_animals, g_player->eyePosition(), lookDirection(), MINE_REACH);
  if (animalHit >= 0) {
    Animal& target = g_animals[animalHit];
    // Damage comes from whatever's actually gripped in the hand (mainHand,
    // the same slot holdingTool above checks) — every tiered tool/weapon is
    // equippable there now, and that's what's rendered swinging in the
    // player's hand, so it is what should land the hit. Falls back to the
    // hotbar selection when nothing is equipped, so a tool that's merely
    // selected (not yet dragged into the hand slot) still counts for
    // something rather than silently attacking bare-handed.
    uint8_t weapon = holdingTool ? (uint8_t)g_inventory->mainHand.blockId
                                 : (uint8_t)(g_hotbar ? g_hotbar->selectedBlockId() : -1);
    double dmg = attackPower(weapon);
    target.health -= dmg;
    playHitSound();
    g_damagePopups.push_back(
        { Vec3(target.position.x, target.position.y + ANIMAL_SPECIES[target.species].height + 0.1,
              target.position.z),
         dmg, DAMAGE_POPUP_LIFETIME });
    if (target.health <= 0 && !target.dying) {
      target.dying = true;
      target.deathTimer = 3.0;
      target.velocity = Vec3(0, 0, 0);
      target.moving = false;
    }
    return;
  }

  RaycastHit hit;
  if (!targetedBlock(hit, MINE_REACH)) return;
  uint8_t id = g_world->getBlock(hit.pos[0], hit.pos[1], hit.pos[2]);
  if (!isMinable(id)) return; // covers placed crafted goods, not just blocks

  if (isPanel(id)) {
    // A ladder is placed (and extended) as one connected vertical run of up
    // to LADDER_PLACE_LENGTH blocks per item spent; mine any block of it and
    // the whole run comes up as a single object in one swing. Items back are
    // one per (up to) 3 blocks, not one per block — matching what placing it
    // cost, so breaking a fresh 3-tall ladder gives back exactly the 1 item
    // that built it instead of quietly tripling it.
    int px = hit.pos[0], pz = hit.pos[2];
    int yLo = hit.pos[1], yHi = hit.pos[1];
    while (g_world->getBlock(px, yLo - 1, pz) == id) yLo--;
    while (g_world->getBlock(px, yHi + 1, pz) == id) yHi++;
    std::vector<Chunk*> affected;
    for (int y = yLo; y <= yHi; y++) {
      for (Chunk* c : g_world->setBlock(px, y, pz, BLOCK_AIR)) affected.push_back(c);
      for (Chunk* c : g_world->flowWaterInto(px, y, pz, WATER_FILL_MAX_CELLS)) affected.push_back(c);
      g_world->panelFacings.erase({ px, y, pz });
    }
    remeshAll(affected);
    int blockCount = yHi - yLo + 1;
    g_inventory->collect(*g_hotbar, id, (blockCount + 2) / 3); // ceil(blockCount / 3)
    playMineSound();
    return;
  }

  if (isChest(id)) {
    // No item-drop system exists to preserve its contents, so refuse rather
    // than silently losing whatever is inside — empty it first.
    auto it = g_world->chests.find({ hit.pos[0], hit.pos[1], hit.pos[2] });
    if (it != g_world->chests.end() && !chestIsEmpty(it->second)) {
      hudMessage("Chest must be empty to break");
      return;
    }
    if (g_chestOpen && g_chestX == hit.pos[0] && g_chestY == hit.pos[1] && g_chestZ == hit.pos[2]) {
      closeChest();
    }
    g_world->chests.erase({ hit.pos[0], hit.pos[1], hit.pos[2] });
  }

  if (isDoor(id)) {
    // Two cells share one door: find the bottom (this cell, or one below it
    // if we hit the top half) and take both together, in one swing, for the
    // single item it cost to place.
    int dpx = hit.pos[0], dpz = hit.pos[2];
    int dpy = hit.pos[1];
    if (g_world->getBlock(dpx, dpy - 1, dpz) == id) dpy--;
    g_world->doors.erase({ dpx, dpy, dpz });
    std::vector<Chunk*> affected = g_world->setBlock(dpx, dpy, dpz, BLOCK_AIR);
    for (Chunk* c : g_world->setBlock(dpx, dpy + 1, dpz, BLOCK_AIR)) affected.push_back(c);
    for (Chunk* c : g_world->flowWaterInto(dpx, dpy, dpz, WATER_FILL_MAX_CELLS)) affected.push_back(c);
    for (Chunk* c : g_world->flowWaterInto(dpx, dpy + 1, dpz, WATER_FILL_MAX_CELLS)) affected.push_back(c);
    remeshAll(affected);
    g_inventory->collect(*g_hotbar, id, 1);
    playMineSound();
    return;
  }

  if (isTable(id) || isBed(id) || isAnyFence(id)) {
    // Every cell of the footprint shares one FurnitureState (world.h);
    // recompute the whole footprint from its anchor+facing and take it all
    // in one swing, same idea as a door's two cells, for the single item it
    // cost to place.
    auto it = g_world->furniture.find({ hit.pos[0], hit.pos[1], hit.pos[2] });
    int ax = hit.pos[0], ay = hit.pos[1], az = hit.pos[2], facing = 0;
    if (it != g_world->furniture.end()) {
      ax = it->second.anchorX;
      ay = it->second.anchorY;
      az = it->second.anchorZ;
      facing = it->second.facing;
    }
    int cells[BED_FOOTPRINT_CELLS][3]; // largest of the three footprints
    int count;
    if (isTable(id)) {
      tableFootprint(facing, cells);
      count = TABLE_FOOTPRINT_CELLS;
    } else if (isBed(id)) {
      bedFootprint(facing, cells);
      count = BED_FOOTPRINT_CELLS;
    } else {
      fencePanelFootprint(facing, cells);
      count = FENCE_PANEL_FOOTPRINT_CELLS;
    }
    std::vector<Chunk*> affected;
    for (int i = 0; i < count; i++) {
      int cx = ax + cells[i][0], cy = ay + cells[i][1], cz = az + cells[i][2];
      g_world->furniture.erase({ cx, cy, cz });
      for (Chunk* c : g_world->setBlock(cx, cy, cz, BLOCK_AIR)) affected.push_back(c);
    }
    remeshAll(affected);
    g_inventory->collect(*g_hotbar, id, 1);
    playMineSound();
    return;
  }

  if (isStairs(id)) g_world->stairFacings.erase({ hit.pos[0], hit.pos[1], hit.pos[2] });
  if (isFurnace(id)) g_world->furnaces.erase({ hit.pos[0], hit.pos[1], hit.pos[2] });
  std::vector<Chunk*> affected = g_world->setBlock(hit.pos[0], hit.pos[1], hit.pos[2], BLOCK_AIR);
  // A plant rooted on the removed block loses its support, so clear it too.
  uint8_t above = g_world->getBlock(hit.pos[0], hit.pos[1] + 1, hit.pos[2]);
  if (isPlant(above)) {
    for (Chunk* c : g_world->setBlock(hit.pos[0], hit.pos[1] + 1, hit.pos[2], BLOCK_AIR))
      affected.push_back(c);
  }
  // Water flows into the space that was just opened up, so digging into a
  // shoreline floods the cut.
  for (Chunk* c : g_world->flowWaterInto(hit.pos[0], hit.pos[1], hit.pos[2], WATER_FILL_MAX_CELLS)) {
    affected.push_back(c);
  }
  remeshAll(affected);
  // Neither grass variant is collectible: grass blocks drop dirt (like
  // Minecraft), tall grass plants drop nothing.
  if (id == BLOCK_GRASS) g_inventory->collect(*g_hotbar, BLOCK_DIRT, 1);
  else if (!isPlant(id)) g_inventory->collect(*g_hotbar, id, 1);
  playMineSound();
}

// The player's horizontal look direction, snapped to the nearest of the 4
// cardinal directions: 0 -Z, 1 +Z, 2 -X, 3 +X (matching panelFacing's/
// stairFacing's convention). Used to orient whatever gets placed toward
// however the player was facing at that moment.
static int playerCardinalFacing() {
  double fx = -std::sin(g_player->yaw), fz = -std::cos(g_player->yaw);
  return std::fabs(fx) > std::fabs(fz) ? (fx > 0 ? 3 : 2) : (fz > 0 ? 1 : 0);
}

static bool placementOverlapsPlayer(int px, int py, int pz) {
  const Vec3& p = g_player->position;
  return px + 1 > p.x - PLAYER_HALF_WIDTH &&
         px < p.x + PLAYER_HALF_WIDTH &&
         pz + 1 > p.z - PLAYER_HALF_WIDTH &&
         pz < p.z + PLAYER_HALF_WIDTH &&
         py + 1 > p.y &&
         py < p.y + PLAYER_HEIGHT;
}

static void tryPlace() {
  if (!g_world || !g_player) return;
  if (tryEatSelected()) return;
  if (tryPlaceBoat()) return;
  g_armSwingTimer = ARM_SWING_TIME;
  g_swingLeftHand = true; // building is the left hand
  RaycastHit hit;
  if (!targetedBlock(hit, PLACE_REACH)) return;
  int px = hit.pos[0] + hit.normal[0];
  int py = hit.pos[1] + hit.normal[1];
  int pz = hit.pos[2] + hit.normal[2];
  // Plants are replaceable: building on ground that carries a grass tuft
  // overwrites the tuft rather than being refused.
  uint8_t target = g_world->getBlock(px, py, pz);
  if (target != BLOCK_AIR && !isPlant(target)) return;
  if (placementOverlapsPlayer(px, py, pz)) return;
  int selected = g_hotbar->selectedBlockId();
  if (selected < 0) return;
  // Checked BEFORE the item is spent: a tool or a stick is not building
  // material, and putting one in the world used to leave an untextured block
  // that could not be mined back.
  if (!isPlaceable((uint8_t)selected)) return;
  // A door stands two cells tall: the cell above must be just as clear, or
  // it would poke through whatever is up there.
  if (isDoor((uint8_t)selected)) {
    uint8_t above = g_world->getBlock(px, py + 1, pz);
    if (above != BLOCK_AIR && !isPlant(above)) return;
    if (placementOverlapsPlayer(px, py + 1, pz)) return;
  }
  // A table/bed/fence-panel claims a whole footprint of cells, not just the
  // one aimed at (see tableFootprint/bedFootprint/fencePanelFootprint in
  // blocks.h) — every cell in it must be just as clear, or the placement is
  // refused outright rather than dropping a partial shape.
  int furnitureFacing = playerCardinalFacing();
  int tableCells[TABLE_FOOTPRINT_CELLS][3];
  int bedCells[BED_FOOTPRINT_CELLS][3];
  int fenceCells[FENCE_PANEL_FOOTPRINT_CELLS][3];
  int furnitureCellCount = 0;
  int (*furnitureCells)[3] = nullptr;
  if (isTable((uint8_t)selected)) {
    tableFootprint(furnitureFacing, tableCells);
    furnitureCellCount = TABLE_FOOTPRINT_CELLS;
    furnitureCells = tableCells;
  } else if (isBed((uint8_t)selected)) {
    bedFootprint(furnitureFacing, bedCells);
    furnitureCellCount = BED_FOOTPRINT_CELLS;
    furnitureCells = bedCells;
  } else if (isAnyFence((uint8_t)selected)) {
    fencePanelFootprint(furnitureFacing, fenceCells);
    furnitureCellCount = FENCE_PANEL_FOOTPRINT_CELLS;
    furnitureCells = fenceCells;
  }
  if (furnitureCells) {
    for (int i = 1; i < furnitureCellCount; i++) { // cell 0 is (px,py,pz), already checked
      int cx = px + furnitureCells[i][0], cy = py + furnitureCells[i][1], cz = pz + furnitureCells[i][2];
      uint8_t cellBlock = g_world->getBlock(cx, cy, cz);
      if (cellBlock != BLOCK_AIR && !isPlant(cellBlock)) return;
      if (placementOverlapsPlayer(cx, cy, cz)) return;
    }
  }

  int blockId = g_hotbar->takeSelected();
  if (blockId < 0) return;

  std::vector<Chunk*> affected = g_world->setBlock(px, py, pz, (uint8_t)blockId);
  // A ladder climbs LADDER_PLACE_LENGTH blocks in one go rather than one rung
  // at a time: fill upward as far as it's clear, stopping at the first
  // obstruction (or the world top). Placing another ladder above an existing
  // run continues it the same way, since the run above is open air again.
  if (blockId == ITEM_LADDER) {
    // One facing for the whole run, not read back per cell: a run taller
    // than the wall behind it used to have its upper cells fall back to
    // panelFacing()'s free-standing guess (nothing solid beside them up
    // there) and render centred instead of flush, splitting one ladder into
    // two visibly different shapes. Extending an existing run inherits its
    // facing so the join matches; a fresh run reads it off the base cell's
    // neighbours, same as panelFacing() would anyway.
    int facing;
    auto belowFacing = g_world->panelFacings.find({ px, py - 1, pz });
    if (g_world->getBlock(px, py - 1, pz) == ITEM_LADDER && belowFacing != g_world->panelFacings.end()) {
      facing = belowFacing->second;
    } else {
      facing = g_world->panelFacing(px, py, pz);
    }
    g_world->panelFacings[{ px, py, pz }] = facing;

    const int LADDER_PLACE_LENGTH = 3;
    for (int i = 1; i < LADDER_PLACE_LENGTH; i++) {
      uint8_t above = g_world->getBlock(px, py + i, pz);
      if (above != BLOCK_AIR && !isPlant(above)) break;
      for (Chunk* c : g_world->setBlock(px, py + i, pz, (uint8_t)blockId)) affected.push_back(c);
      g_world->panelFacings[{ px, py + i, pz }] = facing;
    }
  }
  if (isChest((uint8_t)blockId)) {
    // The lid opens toward whoever placed it: its front (the edge that
    // lifts) faces back at the player, the opposite of their own facing.
    g_world->chests[{ px, py, pz }].facing = playerCardinalFacing() ^ 1; // 0<->1, 2<->3
  }
  if (isStairs((uint8_t)blockId)) {
    // Climbs away from whoever placed it, in the direction they were facing
    // — the low step lands right in front of them.
    g_world->stairFacings[{ px, py, pz }] = playerCardinalFacing();
  }
  if (isTrapdoor((uint8_t)blockId)) {
    // Hinge edge picked from whoever placed it, same convention as a stair's
    // rise direction — it previously got no facing at all.
    g_world->trapdoors[{ px, py, pz }].facing = playerCardinalFacing();
  }
  if (isFurnace((uint8_t)blockId)) {
    // Opens toward whoever placed it, same convention as a chest's lid.
    g_world->furnaces[{ px, py, pz }].facing = playerCardinalFacing() ^ 1;
  }
  if (isDoor((uint8_t)blockId)) {
    // Two cells tall, sharing one state keyed by the bottom cell (door.h) —
    // flush against whichever wall the base cell leans on.
    g_world->doors[{ px, py, pz }].facing = g_world->panelFacing(px, py, pz);
    for (Chunk* c : g_world->setBlock(px, py + 1, pz, (uint8_t)blockId)) affected.push_back(c);
  }
  if (furnitureCells) {
    // Every cell of the footprint gets the block id + a FurnitureState
    // pointing back at the anchor (px,py,pz), so mining any of them finds
    // the whole object (see tryMine) and the mesher knows which one cell
    // should actually draw the merged shape.
    for (int i = 0; i < furnitureCellCount; i++) {
      int cx = px + furnitureCells[i][0], cy = py + furnitureCells[i][1], cz = pz + furnitureCells[i][2];
      if (i > 0) {
        for (Chunk* c : g_world->setBlock(cx, cy, cz, (uint8_t)blockId)) affected.push_back(c);
      }
      g_world->furniture[{ cx, cy, cz }] = { furnitureFacing, px, py, pz };
    }
  }
  remeshAll(affected);
  playPlaceSound();
}

// True while the window is borderless (kept above the taskbar when focused).
static bool g_borderless = false;

// Applies the resolution and display-mode settings to the window.
//
// Display mode "Window" always uses a normal framed window, clamped to fit
// the monitor's work area if the requested resolution is too big for it.
// Display mode "Fullscreen" keeps the previous adaptive behavior: if a
// framed window at the requested client size fits inside the work area, use
// a normal window centered there (the taskbar can't overlap the work area).
// If it doesn't fit — e.g. a 1440-tall client on a 1440-tall screen — go
// borderless at exactly the requested size, centered on the monitor, and
// keep it above the taskbar while the game has focus.
static void applyWindowMode() {
  int w = g_settings.resolutionW;
  int h = g_settings.resolutionH;
  HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {};
  mi.cbSize = sizeof(mi);
  GetMonitorInfo(mon, &mi);
  int screenW = (int)(mi.rcMonitor.right - mi.rcMonitor.left);
  int screenH = (int)(mi.rcMonitor.bottom - mi.rcMonitor.top);
  int workW = (int)(mi.rcWork.right - mi.rcWork.left);
  int workH = (int)(mi.rcWork.bottom - mi.rcWork.top);

  RECT r = { 0, 0, w, h };
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  int frameW = (int)(r.right - r.left) - w;
  int frameH = (int)(r.bottom - r.top) - h;

  bool forceWindowed = g_settings.displayMode == 1;
  bool fitsFramed = (w + frameW) <= workW && (h + frameH) <= workH;

  if (fitsFramed || forceWindowed) {
    g_borderless = false;
    int cw = forceWindowed ? std::min(w, workW - frameW) : w;
    int ch = forceWindowed ? std::min(h, workH - frameH) : h;
    int ww = cw + frameW;
    int wh = ch + frameH;
    SetWindowLongA(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    int x = (int)mi.rcWork.left + (workW - ww) / 2;
    int y = (int)mi.rcWork.top + (workH - wh) / 2;
    SetWindowPos(g_hwnd, HWND_NOTOPMOST, x, y, ww, wh, SWP_FRAMECHANGED);
  } else {
    g_borderless = true;
    int cw = std::min(w, screenW);
    int ch = std::min(h, screenH);
    int x = (int)mi.rcMonitor.left + (screenW - cw) / 2;
    int y = (int)mi.rcMonitor.top + (screenH - ch) / 2;
    SetWindowLongA(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, cw, ch, SWP_FRAMECHANGED);
  }
}

// --- underwater view ----------------------------------------------------
// Submerged rendering follows the usual approach for voxel games: swap the
// fog for a short-range water-coloured one so visibility collapses, and lay
// a tint over the whole frame. Depth is handled with Beer-Lambert style
// exponential attenuation — sunlight is absorbed as it travels down, so the
// deeper the eye, the darker and denser the water reads.
struct WaterView {
  bool submerged = false;
  double depth = 0;              // blocks below the surface
  double r = 0, g = 0, b = 0;    // attenuated water colour
  double tintAlpha = 0;          // strength of the full-screen tint
  double visibility = 0;         // fog end distance, in blocks
};

// Shallow water colour, before depth attenuation.
static const double WATER_TINT_R = 0.16, WATER_TINT_G = 0.42, WATER_TINT_B = 0.72;
static const double WATER_ABSORB = 0.085;  // per block of depth
static const double WATER_MAX_DEPTH = 28;  // beyond this it is as dark as it gets

// Pure function so the colour ramp can be checked without a GL context.
static void underwaterTint(double depth, WaterView& out) {
  double d = clampd(depth, 0, WATER_MAX_DEPTH);
  double light = std::exp(-WATER_ABSORB * d); // fraction of surface light left
  out.depth = depth;
  out.r = WATER_TINT_R * light;
  out.g = WATER_TINT_G * light;
  out.b = WATER_TINT_B * light;
  out.tintAlpha = 0.42 + 0.35 * (1.0 - light); // denser as it darkens
  out.visibility = 4.0 + 12.0 * light;         // you can see less further down
}

// Recomputed once per frame, before rendering.
static WaterView g_waterView;
// Seconds since the session started, used to drift the clouds.
static double g_elapsedTime = 0;

static WaterView waterViewState() {
  WaterView view;
  if (!g_world || !g_player) return view;
  Vec3 eye = g_player->eyePosition();
  int ex = (int)std::floor(eye.x), ey = (int)std::floor(eye.y), ez = (int)std::floor(eye.z);
  if (!isWater(g_world->getBlock(ex, ey, ez))) return view;

  view.submerged = true;
  // distance up to open air: how much water is overhead
  int depth = 0;
  for (int y = ey; y < CHUNK_HEIGHT; y++) {
    if (!isWater(g_world->getBlock(ex, y, ez))) break;
    depth++;
  }
  underwaterTint(depth, view);
  return view;
}

// --- menu actions ------------------------------------------------------
static void applyCharacterType(int type);

static void handleMenuAction(MenuAction action) {
  switch (action) {
    case MenuAction::Start: startNewGame(); break;
    case MenuAction::Load: openLoadPanel(); break;
    case MenuAction::LoadNamed: loadNamedAndPlay(g_menu.selectedSave); break;
    case MenuAction::Resume: beginPlaying(); break;
    case MenuAction::Restart: startNewGame(); break;
    case MenuAction::Save: openSavePanel(); break;
    case MenuAction::ConfirmSave: confirmSaveGame(); break;
    case MenuAction::OverwriteConfirmed: performSave(g_menu.confirmData); break;
    case MenuAction::DeleteSave:
      g_menu.openConfirm("Delete \"" + g_menu.selectedSave + "\"?", "Delete",
                         MenuAction::DeleteConfirmed, g_menu.selectedSave);
      break;
    case MenuAction::DeleteConfirmed: {
      deleteSave(g_menu.confirmData);
      std::vector<SaveInfo> saves = listSaves();
      if (saves.empty()) {
        // load panel would be empty; fall back to the menu it was opened from
        g_menu.showPanel(g_menu.previousPanel);
      } else {
        g_menu.refreshLoadPanel(std::move(saves));
      }
      g_menu.showMessage("Deleted \"" + g_menu.confirmData + "\".");
      break;
    }
    case MenuAction::Quit: g_running = false; break;
    case MenuAction::QuitToMenu: quitToMenu(); break;
    case MenuAction::SensitivityChanged:
      g_settings.sensitivity = g_menu.sensitivity;
      saveSettings(g_settings);
      break;
    case MenuAction::RenderDistanceChanged:
      g_settings.renderDistance = g_menu.renderDistance;
      if (g_world) g_world->renderDistance = g_menu.renderDistance;
      saveSettings(g_settings);
      break;
    case MenuAction::ResolutionChanged: {
      const Resolution& res = RESOLUTIONS[g_menu.resolutionIndex];
      g_settings.resolutionW = res.w;
      g_settings.resolutionH = res.h;
      saveSettings(g_settings);
      applyWindowMode();
      break;
    }
    case MenuAction::DisplayModeChanged:
      g_settings.displayMode = g_menu.displayMode;
      saveSettings(g_settings);
      applyWindowMode();
      break;
    case MenuAction::CharacterChanged:
      applyCharacterType(g_menu.characterType);
      break;
    default: break;
  }
}

static void applyCharacterType(int type) {
  g_settings.characterType = type;
  g_menu.characterType = type;
  if (g_inventory) g_inventory->characterType = type;
  playerModelSetCharacter(type == 1 ? PlayerCharacter::Alex : PlayerCharacter::Steve);
  saveSettings(g_settings);
}

// Third-person camera: pulled back from the eye along the view direction,
// stopping short of any solid block so it never clips into terrain.
static Vec3 thirdPersonCameraPos(const Vec3& eye) {
  const double MAX_DIST = 4.0;
  Vec3 f = lookDirection();
  double d = 0;
  while (d < MAX_DIST) {
    double nd = std::min(d + 0.1, MAX_DIST);
    int bx = (int)std::floor(eye.x - f.x * nd);
    int by = (int)std::floor(eye.y - f.y * nd);
    int bz = (int)std::floor(eye.z - f.z * nd);
    if (isSolid(g_world->getBlock(bx, by, bz))) break;
    d = nd;
  }
  d = std::max(0.75, d - 0.25);
  return { eye.x - f.x * d, eye.y - f.y * d, eye.z - f.z * d };
}

// --- rendering ---------------------------------------------------------
static void setup3D() {
  double aspect = (double)g_winW / g_winH;
  double fovY = 75.0 * 3.14159265358979323846 / 180.0;
  double zNear = 0.1, zFar = 1000;
  double top = zNear * std::tan(fovY / 2);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glFrustum(-top * aspect, top * aspect, -top, top, zNear, zFar);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  double deg = 180.0 / 3.14159265358979323846;
  Vec3 eye = g_player->eyePosition();
  Vec3 cam = g_thirdPerson ? thirdPersonCameraPos(eye) : eye;
  glRotated(-g_player->pitch * deg, 1, 0, 0);
  glRotated(-g_player->yaw * deg, 0, 1, 0);
  glTranslated(-cam.x, -cam.y, -cam.z);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  const WaterView& water = g_waterView;
  glEnable(GL_FOG);
  glFogi(GL_FOG_MODE, GL_LINEAR);
  glHint(GL_FOG_HINT, GL_NICEST);
  if (water.submerged) {
    // short-range, water-coloured fog: visibility collapses underwater and
    // shortens further with depth
    GLfloat fogColor[4] = { (GLfloat)water.r, (GLfloat)water.g, (GLfloat)water.b, 1.0f };
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogf(GL_FOG_START, (GLfloat)(water.visibility * 0.15));
    glFogf(GL_FOG_END, (GLfloat)water.visibility);
  } else {
    // fog matches THREE.Fog(sky, CS*(rd-1.5), CS*(rd+0.5))
    int rd = g_world->renderDistance;
    GLfloat fogColor[4] = { (GLfloat)SKY_R, (GLfloat)SKY_G, (GLfloat)SKY_B, 1.0f };
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogf(GL_FOG_START, (GLfloat)(CHUNK_SIZE * (rd - 1.5)));
    glFogf(GL_FOG_END, (GLfloat)(CHUNK_SIZE * (rd + 0.5)));
  }
}

static void drawCrosshair() {
  double cx = g_winW / 2.0, cy = g_winH / 2.0;
  drawRect(cx - 1, cy - 9, 2, 18, 1, 1, 1, 0.85);
  drawRect(cx - 9, cy - 1, 18, 2, 1, 1, 1, 0.85);
}

// "Press E to ..." under the crosshair while something E would actually act
// on is in reach — a dropped item, a bed, or (aimed at) a chest, door or
// furnace — using the same target checks and state lookups the E key itself
// uses, so the prompt is never shown (or worded) for something E wouldn't
// actually do.
static void drawInteractPrompt() {
  if (!g_world || !g_player) return;

  // Already driving takes priority over everything — E always exits.
  // Otherwise a nearby dropped item, bed or boat wins over the raycast
  // fallback below — proximity, not aim, so any of them shows up whether or
  // not the player happens to be LOOKING at it too.
  const char* label = nullptr;
  if (g_playerBoatIndex >= 0) {
    label = "Press E to exit boat";
  } else if (nearestDroppedItem() >= 0) {
    label = "Press E to pick up";
  } else if (nearBed()) {
    label = "Press E to sleep";
  } else if (nearestBoat(g_boats, g_player->position) >= 0) {
    label = "Press E to enter boat";
  } else {
    RaycastHit hit;
    if (targetedBlock(hit, MINE_REACH)) {
      int hx = hit.pos[0], hy = hit.pos[1], hz = hit.pos[2];
      uint8_t id = g_world->getBlock(hx, hy, hz);
      if (canCookNow() && isLitHeatSource(hx, hy, hz)) {
        label = "Press R to cook";
      } else if (isChest(id)) {
        label = "Press E to open";
      } else if (isFurnace(id)) {
        auto it = g_world->furnaces.find({ hx, hy, hz });
        label = (it != g_world->furnaces.end() && it->second.lit) ? "Press E to put out"
                                                                   : "Press E to ignite";
      } else if (isDoor(id)) {
        int dy = hy;
        if (g_world->getBlock(hx, hy - 1, hz) == id) dy--; // state lives on the bottom cell
        auto it = g_world->doors.find({ hx, dy, hz });
        label = (it != g_world->doors.end() && it->second.open) ? "Press E to close"
                                                                  : "Press E to open";
      }
    }
  }
  if (!label) return;

  double cx = g_winW / 2.0, cy = g_winH / 2.0;
  double tx = cx - textWidth(g_fontButton, label) / 2;
  double ty = cy + 22; // just below the crosshair
  drawText(g_fontButton, tx + 1, ty + 1, label, 0, 0, 0, 1); // shadow
  drawText(g_fontButton, tx, ty, label, 1, 1, 1, 1);
}

// Vanilla's own 9x9 HUD icons, traced pixel-for-pixel from the sprites
// minecraft.wiki uses in its health/hunger tables (Heart (icon).png and
// Hunger (icon).png — the same art as the game's icons.png): a black
// outline, the main fill, a lighter top-left highlight and a darker
// bottom-right shade. Each cell paints as one rect of ICON/9 px — the same
// free-hand-rects approach drawCrosshair already uses for simple HUD
// shapes. `fraction` (0..1) clips columns from the right for the half-pip
// case — 1.0 is a full pip, 0.5 a half pip, 0 draws nothing (just the dim
// background drawStatRow already laid down).
struct IconColor {
  char key; // the pattern character this paints, 0 ends the palette
  double r, g, b;
};

static void drawPixelIcon(double x, double y, double size, double fraction,
                          const char* const* rows, const IconColor* palette) {
  if (fraction <= 0) return;
  const double CELL = size / 9.0;
  const double maxW = size * fraction;
  for (int row = 0; row < 9; row++) {
    for (int col = 0; col < 9; col++) {
      char ch = rows[row][col];
      if (ch == '.') continue;
      double r = 1, g = 1, b = 1; // a missing palette entry paints loud white
      for (const IconColor* p = palette; p->key; p++) {
        if (p->key == ch) { r = p->r; g = p->g; b = p->b; break; }
      }
      double w = clampd(maxW - col * CELL, 0, CELL);
      if (w > 0) drawRect(x + col * CELL, y + row * CELL, w, CELL, r, g, b, 1);
    }
  }
}

static void drawHeartIcon(double x, double y, double size, double fraction) {
  static const char* const ROWS[9] = {
    "..##.##..",
    ".#RR#RR#.",
    "#RHRRRRR#",
    "#RRRRRRR#",
    "#DRRRRRD#",
    ".#DRRRD#.",
    "..#DRD#..",
    "...#D#...",
    "....#....",
  };
  static const IconColor PALETTE[] = {
    { '#', 0.00, 0.00, 0.00 }, // outline
    { 'R', 1.00, 0.07, 0.07 }, // main red
    { 'D', 0.73, 0.07, 0.07 }, // bottom-right shade
    { 'H', 1.00, 0.78, 0.78 }, // top-left shine
    { 0, 0, 0, 0 },
  };
  drawPixelIcon(x, y, size, fraction, ROWS, PALETTE);
}

static void drawDrumstickIcon(double x, double y, double size, double fraction) {
  static const char* const ROWS[9] = {
    "..##.....",
    ".#Rr#....",
    "#RTRM#...",
    "#rRMmM#..",
    ".#dDmm#..",
    "..#dDD#..",
    "...###B##",
    "......#W#",
    "......##.",
  };
  static const IconColor PALETTE[] = {
    { '#', 0.00, 0.00, 0.00 }, // outline
    { 'R', 0.83, 0.16, 0.16 }, // red highlight
    { 'r', 0.70, 0.09, 0.09 }, // highlight shade
    { 'T', 0.87, 0.69, 0.56 }, // light tan
    { 'M', 0.72, 0.52, 0.35 }, // meat
    { 'm', 0.62, 0.43, 0.26 }, // meat shade
    { 'D', 0.48, 0.32, 0.18 }, // dark meat
    { 'd', 0.38, 0.24, 0.11 }, // darkest meat
    { 'B', 0.89, 0.84, 0.67 }, // bone
    { 'W', 1.00, 0.97, 0.86 }, // bone shine
    { 0, 0, 0, 0 },
  };
  drawPixelIcon(x, y, size, fraction, ROWS, PALETTE);
}

// Pip size and spacing for the health/hunger rows, shared by drawStatRow
// and drawHealthHunger so the two can't drift apart (they used to each
// declare their own copy).
const double STAT_ICON = 28, STAT_GAP = 4;

static void drawStatRow(int value, int maxValue, double x, double y, bool heart) {
  int pips = maxValue / 2;
  for (int i = 0; i < pips; i++) {
    double ix = x + i * (STAT_ICON + STAT_GAP);
    drawRect(ix, y, STAT_ICON, STAT_ICON, 0.12, 0.12, 0.12, 0.55);
    drawRectOutline(ix, y, STAT_ICON, STAT_ICON, 1, 0, 0, 0, 0.6);
    int pipValue = value - i * 2; // 2 = full, 1 = half, <=0 = empty
    double fraction = pipValue >= 2 ? 1.0 : pipValue == 1 ? 0.5 : 0.0;
    if (heart) drawHeartIcon(ix, y, STAT_ICON, fraction);
    else drawDrumstickIcon(ix, y, STAT_ICON, fraction);
  }
}

// Health (hearts, left) and hunger (drumsticks, right) above the hotbar —
// researched from vanilla's own HUD layout (a row of icons each). At the
// doubled pip size the two full rows would overlap in the middle (574px of
// hotbar vs 632px of pips), so the hunger row drops its two leftmost
// drumsticks to open a gap between the rows; the remaining 8 pips still
// cover the low 16 hunger points 2-per-pip, the top 4 points simply share
// the last pip's full state.
const int HUNGER_PIPS_SHOWN = 8;
static void drawHealthHunger() {
  if (!g_player) return;
  double hx, hy, hw, hh;
  hotbarRect(g_winW, g_winH, hx, hy, hw, hh);
  const double ROW_GAP = 6;
  double rowY = hy - ROW_GAP - STAT_ICON;
  drawStatRow(g_player->health, g_player->maxHealth, hx, rowY, true);
  double drumstickRowW = HUNGER_PIPS_SHOWN * (STAT_ICON + STAT_GAP) - STAT_GAP;
  drawStatRow(g_player->hunger, HUNGER_PIPS_SHOWN * 2, hx + hw - drumstickRowW, rowY, false);
}

// Manual gluProject: multiplies through the camera matrices captured at the
// top of render() and maps clip space to screen pixels. False if the point
// is behind the camera (nothing sensible to draw there).
static bool worldToScreen(const Vec3& p, double& outX, double& outY) {
  const double* mv = g_camModelview;
  double ex = mv[0] * p.x + mv[4] * p.y + mv[8] * p.z + mv[12];
  double ey = mv[1] * p.x + mv[5] * p.y + mv[9] * p.z + mv[13];
  double ez = mv[2] * p.x + mv[6] * p.y + mv[10] * p.z + mv[14];
  double ew = mv[3] * p.x + mv[7] * p.y + mv[11] * p.z + mv[15];

  const double* pr = g_camProjection;
  double cx = pr[0] * ex + pr[4] * ey + pr[8] * ez + pr[12] * ew;
  double cy = pr[1] * ex + pr[5] * ey + pr[9] * ez + pr[13] * ew;
  double cw = pr[3] * ex + pr[7] * ey + pr[11] * ez + pr[15] * ew;
  if (cw <= 1e-4) return false;

  double ndcX = cx / cw, ndcY = cy / cw;
  outX = (ndcX * 0.5 + 0.5) * g_camViewport[2] + g_camViewport[0];
  outY = (1.0 - (ndcY * 0.5 + 0.5)) * g_camViewport[3] + g_camViewport[1]; // NDC +Y is up, screen +Y is down
  return true;
}

static void drawDamagePopups() {
  for (const DamagePopup& p : g_damagePopups) {
    double sx, sy;
    if (!worldToScreen(p.position, sx, sy)) continue;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "-%.1f", p.amount);
    double fade = clampd(p.timer / DAMAGE_POPUP_LIFETIME, 0, 1);
    double tw = textWidth(g_fontButton, buf);
    // Drifts up slightly over its lifetime, fading out.
    double ty = sy - (1.0 - fade) * 16;
    drawText(g_fontButton, sx - tw / 2 + 1, ty + 1, buf, 0, 0, 0, fade * 0.8); // shadow
    drawText(g_fontButton, sx - tw / 2, ty, buf, 1, 0.25, 0.25, fade);
  }
}

static void render() {
  glViewport(0, 0, g_winW, g_winH);
  g_waterView = waterViewState();
  if (g_world) {
    if (g_waterView.submerged) {
      // no sky underwater: the background is the water itself
      glClearColor((GLfloat)g_waterView.r, (GLfloat)g_waterView.g, (GLfloat)g_waterView.b, 1);
    } else {
      glClearColor((GLfloat)SKY_R, (GLfloat)SKY_G, (GLfloat)SKY_B, 1);
    }
  } else {
    glClearColor(0, 0, 0, 1);
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (g_world && g_player) {
    setup3D();
    // Grab the camera transform right as it's set — every world-space point
    // drawn this frame goes through exactly this modelview/projection, so
    // this is what worldToScreen() needs to place damage-popup text later
    // in the 2D HUD pass.
    glGetDoublev(GL_MODELVIEW_MATRIX, g_camModelview);
    glGetDoublev(GL_PROJECTION_MATRIX, g_camProjection);
    glGetIntegerv(GL_VIEWPORT, g_camViewport);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, atlasTextureId());

    for (auto& kv : g_world->chunks) {
      if (kv.second->listOpaque) glCallList(kv.second->listOpaque);
    }

    // Chest lids, door panels and lit furnace fires: each block's static
    // part (if it has one) is already in the list above; these add the bits
    // that change frame to frame — a swinging lid, a swinging door, a
    // flickering flame — none of which the mesher bakes in.
    drawChestLids(*g_world);
    drawDoors(*g_world);
    drawTrapdoors(*g_world);
    drawFurnaceFires(*g_world, g_elapsedTime);
    drawCampfireFires(*g_world, g_elapsedTime);

    // Plants: cutout billboards, so alpha-tested rather than blended, and
    // double-sided since a billboard has no back.
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glDisable(GL_CULL_FACE);
    for (auto& kv : g_world->chunks) {
      if (kv.second->listPlants) glCallList(kv.second->listPlants);
    }
    glEnable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);

    // clouds, above everything and skipped underwater (nothing of the sky
    // is visible from down there)
    if (!g_waterView.submerged) {
      drawClouds(g_elapsedTime, g_player->position.x, g_player->position.z);
    }

    if (g_thirdPerson) {
      PlayerAnim anim;
      anim.walkPhase = g_walkPhase;
      anim.walkAmount = g_walkAmount;
      anim.air = g_airAmount;
      anim.swing = g_armSwingTimer > 0 ? 1.0 - g_armSwingTimer / ARM_SWING_TIME : 0.0;
      anim.swingLeft = g_swingLeftHand;
      anim.heldTool = g_inventory ? g_inventory->mainHand.blockId : -1;
      anim.boating = g_playerBoatIndex >= 0;
      anim.rowPhase = g_boatRowPhase;
      drawPlayerModel(*g_player, anim);
      glBindTexture(GL_TEXTURE_2D, atlasTextureId()); // restore for water
    }

    // Animals are always drawn (they're NPCs, not the player's own body —
    // unlike drawPlayerModel above, not gated on third-person view).
    drawAnimals(g_animals);
    drawDroppedItems(g_droppedItems, g_elapsedTime);
    drawFishes(g_fishes);
    drawBoats(g_boats);

    // water: transparent, no depth write (matches the JS material).
    // Culling is off so the surface is still there when seen from below —
    // the faces only face upward, and would otherwise vanish underwater.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    for (auto& kv : g_world->chunks) {
      if (kv.second->listWater) glCallList(kv.second->listWater);
    }
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // world border: four translucent aqua faces (Minecraft-style), drawn
    // only where they're within fog range of the player
    {
      double R = WORLD_RADIUS;
      double px = g_player->position.x;
      double pz = g_player->position.z;
      double reachDist = CHUNK_SIZE * (g_world->renderDistance + 0.5);
      glDisable(GL_TEXTURE_2D);
      glDisable(GL_CULL_FACE);
      glEnable(GL_BLEND);
      glDepthMask(GL_FALSE);
      glColor4d(0.25, 0.75, 0.95, 0.35);
      auto wallX = [&](double x) {
        if (std::abs(x - px) > reachDist) return;
        double z0 = std::max(-R, pz - reachDist), z1 = std::min(R, pz + reachDist);
        glBegin(GL_QUADS);
        glVertex3d(x, 0, z0);
        glVertex3d(x, CHUNK_HEIGHT, z0);
        glVertex3d(x, CHUNK_HEIGHT, z1);
        glVertex3d(x, 0, z1);
        glEnd();
      };
      auto wallZ = [&](double z) {
        if (std::abs(z - pz) > reachDist) return;
        double x0 = std::max(-R, px - reachDist), x1 = std::min(R, px + reachDist);
        glBegin(GL_QUADS);
        glVertex3d(x0, 0, z);
        glVertex3d(x0, CHUNK_HEIGHT, z);
        glVertex3d(x1, CHUNK_HEIGHT, z);
        glVertex3d(x1, 0, z);
        glEnd();
      };
      wallX(-R);
      wallX(R);
      wallZ(-R);
      wallZ(R);

      // bright grid lines so the wall reads clearly against sky and water
      glColor4d(0.35, 0.9, 1.0, 0.8);
      glLineWidth(2.0f);
      glBegin(GL_LINES);
      auto gridX = [&](double x) {
        if (std::abs(x - px) > reachDist) return;
        double z0 = std::max(-R, pz - reachDist), z1 = std::min(R, pz + reachDist);
        for (double z = std::ceil(z0 / 8) * 8; z <= z1; z += 8) {
          glVertex3d(x, 0, z);
          glVertex3d(x, CHUNK_HEIGHT, z);
        }
        for (int y = 0; y <= CHUNK_HEIGHT; y += 8) {
          glVertex3d(x, y, z0);
          glVertex3d(x, y, z1);
        }
      };
      auto gridZ = [&](double z) {
        if (std::abs(z - pz) > reachDist) return;
        double x0 = std::max(-R, px - reachDist), x1 = std::min(R, px + reachDist);
        for (double x = std::ceil(x0 / 8) * 8; x <= x1; x += 8) {
          glVertex3d(x, 0, z);
          glVertex3d(x, CHUNK_HEIGHT, z);
        }
        for (int y = 0; y <= CHUNK_HEIGHT; y += 8) {
          glVertex3d(x0, y, z);
          glVertex3d(x1, y, z);
        }
      };
      gridX(-R);
      gridX(R);
      gridZ(-R);
      gridZ(R);
      glEnd();
      glLineWidth(1.0f);
      glDepthMask(GL_TRUE);
      glDisable(GL_BLEND);
      glEnable(GL_CULL_FACE);
    }

    // first-person viewmodel: your own arm swinging when you mine or place
    if (!g_thirdPerson) {
      int heldTool = g_inventory ? g_inventory->mainHand.blockId : -1;
      double swing = g_armSwingTimer > 0 ? 1.0 - g_armSwingTimer / ARM_SWING_TIME : 0.0;
      bool swinging = g_armSwingTimer > 0;
      // Right hand: carries the tool, so it stays on screen whenever one is
      // equipped, and animates when mining. Left hand (building) is still
      // only drawn for the length of its swing.
      if (heldTool >= 0 || (swinging && !g_swingLeftHand)) {
        double rightSwing = (swinging && !g_swingLeftHand) ? swing : 0.0;
        drawFirstPersonArm(rightSwing, false, heldTool, g_winW, g_winH);
      }
      if (swinging && g_swingLeftHand) {
        drawFirstPersonArm(swing, true, -1, g_winW, g_winH);
      }
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
  }

  begin2D(g_winW, g_winH);
  // Submerged tint over the whole frame, under the HUD so it stays readable.
  if (g_waterView.submerged) {
    drawRect(0, 0, g_winW, g_winH, g_waterView.r, g_waterView.g, g_waterView.b,
             g_waterView.tintAlpha);
  }
  if (g_world && g_hotbar && !g_invOpen && !g_chestOpen && !g_fullMapOpen) g_hotbar->draw(g_winW, g_winH);
  if (g_state == GameState::Playing && !g_invOpen && !g_chestOpen && !g_fullMapOpen) {
    drawHealthHunger();
    drawDamagePopups();
    drawCrosshair();
    drawInteractPrompt();
    drawMinimap(g_winW, g_winH, g_player ? g_player->yaw : 0.0, g_boats);
    if (g_hudMsgTimer > 0 && !g_hudMsg.empty()) {
      double tw = textWidth(g_fontMsg, g_hudMsg.c_str());
      double tx = (g_winW - tw) / 2;
      double ty = g_winH - 16 - 52 - 28; // just above the hotbar
      drawText(g_fontMsg, tx + 1, ty + 1, g_hudMsg.c_str(), 0, 0, 0, 1);
      drawText(g_fontMsg, tx, ty, g_hudMsg.c_str(), 255 / 255.0, 227 / 255.0, 138 / 255.0, 1);
    }
  }
  if (g_invOpen && g_inventory) g_inventory->drawPanel(g_winW, g_winH);
  if (g_chestOpen && g_inventory) g_inventory->drawChestPanel(g_winW, g_winH);
  end2D();

  // The inventory's character preview is a small 3D pass sandwiched between
  // the panel background and the slot contents (Player tab only).
  if (g_invOpen && g_inventory) {
    double vx, vy, vw, vh;
    if (g_inventory->previewRect(g_winW, g_winH, vx, vy, vw, vh)) {
      drawInventoryPlayerPreview(g_winW, g_winH, vx, vy, vw, vh);
    }
  }

  begin2D(g_winW, g_winH);
  if (g_invOpen && g_inventory) {
    g_inventory->drawContents(*g_hotbar, g_winW, g_winH, g_mouseX, g_mouseY);
  }
  if (g_chestOpen && g_inventory && g_world) {
    g_inventory->drawChestContents(g_world->chests[{ g_chestX, g_chestY, g_chestZ }], *g_hotbar,
                                   g_winW, g_winH, g_mouseX, g_mouseY);
  }
  if (g_fullMapOpen && g_player) {
    drawFullMap(g_winW, g_winH, g_player->position, g_player->yaw, g_boats);
  }
  if (g_state != GameState::Playing) {
    drawRect(0, 0, g_winW, g_winH, 0, 0, 0, 1.0); // opaque menu backdrop
    g_menu.draw(g_winW, g_winH, g_mouseX, g_mouseY);
  }
  end2D();
}

// --- per-frame update --------------------------------------------------
static void updateFrame(double dt) {
  g_elapsedTime += dt;
  g_menu.update(dt);
  if (g_hudMsgTimer > 0) g_hudMsgTimer -= dt;

  if (g_state == GameState::Playing && g_world && g_player) {
    updateChestAnimations(*g_world, dt); // lids ease open/closed even while the screen has the cursor
    updateDoorAnimations(*g_world, dt);
    updateTrapdoorAnimations(*g_world, dt);

    if (g_cursorCaptured) {
      // accumulate relative mouse movement by recentering the cursor
      POINT p, c = clientCenterOnScreen();
      GetCursorPos(&p);
      g_lookDX += p.x - c.x;
      g_lookDY += p.y - c.y;
      SetCursorPos(c.x, c.y);

      double s = BASE_MOUSE_SENSITIVITY * g_settings.sensitivity;
      g_player->yaw -= g_lookDX * s;
      g_player->pitch = clampd(g_player->pitch - g_lookDY * s, -PITCH_LIMIT, PITCH_LIMIT);
      g_lookDX = 0;
      g_lookDY = 0;

      if (g_playerBoatIndex >= 0 && g_playerBoatIndex < (int)g_boats.size()) {
        // Driving: steering is "look where you want to go" (mouse, already
        // applied to g_player->yaw above) — W/S push the boat forward/back
        // along that facing, A/D are unused (a boat doesn't strafe). Normal
        // on-foot physics/gravity/trapdoor-triggering are skipped entirely
        // while riding; the player's own position just tracks the boat's.
        MoveInput input = getMoveInput();
        updateBoat(g_boats[g_playerBoatIndex], *g_world, dt, g_player->yaw, input.forward);
        g_player->position = g_boats[g_playerBoatIndex].position;
        g_player->velocity = Vec3(0, 0, 0);
        if (input.forward != 0) g_boatRowPhase += dt * 6.0;
      } else {
        g_player->update(dt, *g_world, getMoveInput());
        checkTrapdoorTrigger(dt);
      }
    }

    // limb animation: gait advances with actual ground movement; the cycle
    // freezes mid-air (humans don't keep striding while jumping)
    double hspeed = std::hypot(g_player->velocity.x, g_player->velocity.z);
    double walkTarget = std::min(1.0, hspeed / 4.3);
    g_walkAmount += (walkTarget - g_walkAmount) * std::min(1.0, dt * 10);
    if (g_player->onGround) g_walkPhase += hspeed * dt * 3.0;
    double airTarget = g_player->onGround ? 0.0 : 1.0;
    g_airAmount += (airTarget - g_airAmount) * std::min(1.0, dt * 8);
    if (g_armSwingTimer > 0) g_armSwingTimer = std::max(0.0, g_armSwingTimer - dt);

    auto removed = g_world->updateLoadedChunks(g_player->position.x, g_player->position.z);
    for (auto& chunk : removed) disposeChunk(*chunk);
    // Explored state must be fresh before the corner map samples it below,
    // since minimapUpdate now paints unexplored cells as fog too.
    worldMapUpdate(*g_world, g_player->position.x, g_player->position.z);
    minimapUpdate(*g_world, g_player->position.x, g_player->position.z);

    for (Animal& a : g_animals) updateAnimal(a, *g_world, dt);

    // Death sequence: count down the lie-down, then grant meat and remove.
    for (size_t i = 0; i < g_animals.size();) {
      Animal& a = g_animals[i];
      if (a.dying) {
        a.deathTimer -= dt;
        if (a.deathTimer <= 0) {
          if (g_inventory && g_hotbar) {
            g_inventory->collect(*g_hotbar, ITEM_RAW_MEAT, meatDropFor(a.species));
          }
          g_animals[i] = g_animals.back();
          g_animals.pop_back();
          continue; // don't advance i — a new element just landed here
        }
      }
      i++;
    }

    // Floating damage numbers fade out and go away on their own.
    for (size_t i = 0; i < g_damagePopups.size();) {
      g_damagePopups[i].timer -= dt;
      if (g_damagePopups[i].timer <= 0) {
        g_damagePopups[i] = g_damagePopups.back();
        g_damagePopups.pop_back();
        continue;
      }
      i++;
    }

    // Dropped items: fall/settle physics only — picking one up is a
    // deliberate E press now (tryPickUpItem), not automatic on approach.
    for (DroppedItem& it : g_droppedItems) updateDroppedItem(it, *g_world, dt);

    g_animalSpawnTimer += dt;
    const double ANIMAL_SPAWN_INTERVAL = 3.0;
    if (g_animalSpawnTimer >= ANIMAL_SPAWN_INTERVAL) {
      g_animalSpawnTimer = 0;
      maintainAnimalSpawns(*g_world, g_animals, g_player->position.x, g_player->position.z,
                          g_world->renderDistance);
    }

    for (Fish& f : g_fishes) updateFish(f, *g_world, dt, g_player->position);
    g_fishSpawnTimer += dt;
    const double FISH_SPAWN_INTERVAL = 3.0;
    if (g_fishSpawnTimer >= FISH_SPAWN_INTERVAL) {
      g_fishSpawnTimer = 0;
      maintainFishSpawns(*g_world, g_fishes, g_player->position.x, g_player->position.z,
                        g_world->renderDistance);
    }

    // Keeps every boat resting on the water's own surface even when nobody's
    // riding it — the ridden one (if any) already got a driving update above,
    // with live input, while the cursor was captured.
    for (size_t i = 0; i < g_boats.size(); i++) {
      if ((int)i == g_playerBoatIndex) continue;
      updateBoat(g_boats[i], *g_world, dt, g_boats[i].yaw, 0);
    }

    int budget = MESH_BUDGET_PER_FRAME;
    for (auto& kv : g_world->chunks) {
      if (budget <= 0) break;
      if (kv.second->dirty) {
        remeshChunk(*g_world, *kv.second);
        budget--;
      }
    }
  }
}

// --- window proc -------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CLOSE:
      g_running = false;
      return 0;

    case WM_SIZE:
      g_winW = std::max(1, (int)LOWORD(lParam));
      g_winH = std::max(1, (int)HIWORD(lParam));
      return 0;

    case WM_KILLFOCUS:
      // Drop below the taskbar again so other apps are usable while paused.
      if (g_borderless) {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      }
      // Mirrors losing pointer lock in the browser build.
      pauseGame();
      return 0;

    case WM_SETFOCUS:
      if (g_borderless) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      }
      return 0;

    case WM_KEYDOWN: {
      if (wParam < 256) g_keys[wParam] = true;
      if (g_state == GameState::Playing && g_hotbar && !g_invOpen && !g_fullMapOpen) {
        if (wParam >= '1' && wParam <= '9') g_hotbar->select((int)(wParam - '1'));
        else if (wParam == '0') g_hotbar->select(9); // 0 key = the tenth slot
      }
      if (g_state == GameState::Playing && wParam == 'V' && !(lParam & 0x40000000) && !g_invOpen) {
        g_thirdPerson = !g_thirdPerson;
        g_settings.thirdPerson = g_thirdPerson; // remembered across sessions
        saveSettings(g_settings);
      }
      if (g_state == GameState::Playing && wParam == 'M' && !(lParam & 0x40000000) && !g_invOpen &&
          !g_chestOpen) {
        if (g_fullMapOpen) closeFullMap();
        else openFullMap();
      }
      if (g_state == GameState::Playing && wParam == 'I' && !(lParam & 0x40000000) && !g_chestOpen &&
          !g_fullMapOpen) {
        if (g_invOpen) closeInventory();
        else openInventory();
      }
      // Fast path to the recipe book: straight to the Craft tab with it
      // already open, same toggle shape as 'I'.
      if (g_state == GameState::Playing && wParam == 'R' && !(lParam & 0x40000000) && !g_chestOpen &&
          !g_fullMapOpen) {
        // Cooking takes priority: if you're at a lit heat source with raw
        // meat selected, R cooks instead of touching the recipe book at all
        // — the same context-sensitive-key idea E already uses across
        // chest/door/furnace, just gated on hotbar selection too here.
        if (!g_invOpen && canCookNow()) {
          tryCook();
        } else if (g_invOpen) {
          closeInventory();
        } else {
          openInventory();
          g_inventory->tab = INV_TAB_CRAFT;
          g_inventory->bookOpen = true;
        }
      }
      if (g_state == GameState::Playing && wParam == 'E' && !(lParam & 0x40000000) && !g_fullMapOpen) {
        if (g_playerBoatIndex >= 0) {
          tryEnterOrExitBoat(); // always exits, regardless of anything else nearby
        } else if (g_chestOpen) {
          closeChest();
        } else if (!g_invOpen && nearestDroppedItem() >= 0) {
          tryPickUpItem();
        } else if (!g_invOpen && nearBed()) {
          trySleep();
        } else if (!g_invOpen && nearestBoat(g_boats, g_player->position) >= 0) {
          tryEnterOrExitBoat(); // enters
        } else if (!g_invOpen) {
          RaycastHit hit;
          if (targetedBlock(hit, MINE_REACH)) {
            uint8_t id = g_world->getBlock(hit.pos[0], hit.pos[1], hit.pos[2]);
            if (isChest(id)) {
              openChest(hit.pos[0], hit.pos[1], hit.pos[2]);
            } else if (isFurnace(id)) {
              toggleFurnace(hit.pos[0], hit.pos[1], hit.pos[2]);
            } else if (isDoor(id)) {
              toggleDoor(hit.pos[0], hit.pos[1], hit.pos[2]);
            }
          }
        }
      }
      if (wParam == VK_ESCAPE) {
        if (g_invOpen && g_inventory && g_inventory->recipeBookOpen()) {
          g_inventory->closeRecipeBook(); // back to the panel, not out of it
        } else if (g_invOpen) {
          closeInventory();
        } else if (g_chestOpen) {
          closeChest();
        } else if (g_fullMapOpen) {
          closeFullMap();
        } else if (g_state == GameState::Playing) {
          pauseGame();
        } else if (g_menu.isSubPanel()) {
          g_menu.closeSubPanel();
        }
      }
      return 0;
    }
    case WM_KEYUP:
      if (wParam < 256) g_keys[wParam] = false;
      return 0;

    case WM_CHAR:
      if (g_state != GameState::Playing && wParam != VK_ESCAPE) {
        handleMenuAction(g_menu.onChar((unsigned char)wParam));
      }
      return 0;

    case WM_MOUSEMOVE:
      g_mouseX = (double)(short)LOWORD(lParam);
      g_mouseY = (double)(short)HIWORD(lParam);
      if (g_state != GameState::Playing) {
        handleMenuAction(g_menu.onMouseMove(g_mouseX, g_mouseY));
      }
      return 0;

    case WM_LBUTTONDOWN:
      if (g_state == GameState::Playing) {
        if (g_fullMapOpen && g_player) {
          double wx, wz;
          if (fullMapScreenToWorld(g_mouseX, g_mouseY, g_winW, g_winH, g_player->position, wx, wz))
            addMapMarker(wx, wz);
        } else if (g_invOpen && g_inventory && g_inventory->characterSwitchButtonHit(g_mouseX, g_mouseY, g_winW, g_winH)) {
          applyCharacterType(1 - g_settings.characterType);
        } else if (g_invOpen) {
          g_inventory->onMouseDown(*g_hotbar, g_mouseX, g_mouseY, false, g_winW, g_winH);
          applyPendingEat();
          applyPendingDrop();
        } else if (g_chestOpen) {
          g_inventory->chestMouseDown(g_world->chests[{ g_chestX, g_chestY, g_chestZ }], *g_hotbar,
                                      g_mouseX, g_mouseY, false, g_winW, g_winH);
        } else {
          tryMine();
        }
      } else {
        handleMenuAction(g_menu.onMouseDown(g_mouseX, g_mouseY));
      }
      return 0;

    case WM_LBUTTONUP:
      if (g_state == GameState::Playing && g_invOpen && g_inventory) {
        g_inventory->onMouseUp(*g_hotbar, g_mouseX, g_mouseY, false, g_winW, g_winH);
        applyPendingEat();
        applyPendingDrop();
      }
      if (g_state == GameState::Playing && g_chestOpen && g_inventory) {
        g_inventory->chestMouseUp(g_world->chests[{ g_chestX, g_chestY, g_chestZ }], *g_hotbar,
                                  g_mouseX, g_mouseY, false, g_winW, g_winH);
      }
      g_menu.onMouseUp();
      return 0;

    case WM_RBUTTONDOWN:
      if (g_state == GameState::Playing) {
        if (g_fullMapOpen && g_player) {
          double wx, wz;
          if (fullMapScreenToWorld(g_mouseX, g_mouseY, g_winW, g_winH, g_player->position, wx, wz))
            removeNearestMapMarker(wx, wz);
        } else if (g_invOpen) {
          g_inventory->onMouseDown(*g_hotbar, g_mouseX, g_mouseY, true, g_winW, g_winH);
          applyPendingEat();
          applyPendingDrop();
        } else if (g_chestOpen) {
          g_inventory->chestMouseDown(g_world->chests[{ g_chestX, g_chestY, g_chestZ }], *g_hotbar,
                                      g_mouseX, g_mouseY, true, g_winW, g_winH);
        } else tryPlace();
      }
      return 0;

    case WM_RBUTTONUP:
      if (g_state == GameState::Playing && g_invOpen && g_inventory) {
        g_inventory->onMouseUp(*g_hotbar, g_mouseX, g_mouseY, true, g_winW, g_winH);
        applyPendingEat();
        applyPendingDrop();
      }
      if (g_state == GameState::Playing && g_chestOpen && g_inventory) {
        g_inventory->chestMouseUp(g_world->chests[{ g_chestX, g_chestY, g_chestZ }], *g_hotbar,
                                  g_mouseX, g_mouseY, true, g_winW, g_winH);
      }
      return 0;

    case WM_MOUSEWHEEL:
      if (g_state == GameState::Playing && g_fullMapOpen) {
        // (short)HIWORD(wParam) is the wheel delta — windowsx.h's
        // GET_WHEEL_DELTA_WPARAM macro without pulling in the header.
        int notches = (short)HIWORD(wParam) / WHEEL_DELTA;
        worldMapAdjustZoom(notches);
      }
      return 0;

    case WM_SETCURSOR:
      if (g_cursorCaptured && LOWORD(lParam) == HTCLIENT) {
        SetCursor(nullptr);
        return TRUE;
      }
      break;
  }
  return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// --- GL context --------------------------------------------------------
static bool initGL() {
  PIXELFORMATDESCRIPTOR pfd = {};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  pfd.iLayerType = PFD_MAIN_PLANE;

  g_dc = GetDC(g_hwnd);
  int pf = ChoosePixelFormat(g_dc, &pfd);
  if (!pf || !SetPixelFormat(g_dc, pf, &pfd)) return false;
  g_rc = wglCreateContext(g_dc);
  if (!g_rc || !wglMakeCurrent(g_dc, g_rc)) return false;

  PFNWGLSWAPINTERVALEXT swapInterval =
      (PFNWGLSWAPINTERVALEXT)wglGetProcAddress("wglSwapIntervalEXT");
  if (swapInterval) swapInterval(1); // vsync

  glShadeModel(GL_SMOOTH);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  return true;
}

// --- screenshot / selftest modes ----------------------------------------
static bool writeBMP(const char* path, int w, int h, const std::vector<uint8_t>& rgb) {
  int rowSize = (w * 3 + 3) & ~3;
  int imageSize = rowSize * h;
  BITMAPFILEHEADER bf = {};
  BITMAPINFOHEADER bi = {};
  bf.bfType = 0x4D42;
  bf.bfOffBits = sizeof(bf) + sizeof(bi);
  bf.bfSize = bf.bfOffBits + imageSize;
  bi.biSize = sizeof(bi);
  bi.biWidth = w;
  bi.biHeight = h; // bottom-up, matches glReadPixels
  bi.biPlanes = 1;
  bi.biBitCount = 24;
  bi.biCompression = BI_RGB;

  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  std::fwrite(&bf, sizeof(bf), 1, f);
  std::fwrite(&bi, sizeof(bi), 1, f);
  std::vector<uint8_t> row(rowSize, 0);
  for (int y = 0; y < h; y++) {
    const uint8_t* src = &rgb[(size_t)y * w * 3];
    for (int x = 0; x < w; x++) { // RGB -> BGR
      row[x * 3 + 0] = src[x * 3 + 2];
      row[x * 3 + 1] = src[x * 3 + 1];
      row[x * 3 + 2] = src[x * 3 + 0];
    }
    std::fwrite(row.data(), 1, rowSize, f);
  }
  std::fclose(f);
  return true;
}

static bool captureFrame(const char* path) {
  std::vector<uint8_t> rgb((size_t)g_winW * g_winH * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, g_winW, g_winH, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
  return writeBMP(path, g_winW, g_winH, rgb);
}

static int runScreenshotMode(const char* path) {
  // fixed seed so automated shots are reproducible
  SaveState fixedSeed;
  fixedSeed.seed = 1337;

  // main menu first (text/UI rendering check)
  for (int frame = 0; frame < 5; frame++) {
    render();
    SwapBuffers(g_dc);
  }
  render();
  captureFrame((exeDir() + "screenshot_menu.bmp").c_str());
  SwapBuffers(g_dc);

  g_menu.showPanel(MenuPanel::SettingsPanel);
  render();
  captureFrame((exeDir() + "screenshot_settings.bmp").c_str());
  SwapBuffers(g_dc);
  g_menu.showPanel(MenuPanel::Main);

  createSession(&fixedSeed);
  g_state = GameState::Playing;
  g_player->pitch = -0.25; // look slightly down so terrain fills the frame

  for (int frame = 0; frame < 30; frame++) {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    g_world->updateLoadedChunks(g_player->position.x, g_player->position.z);
    for (auto& kv : g_world->chunks) {
      if (kv.second->dirty) remeshChunk(*g_world, *kv.second);
    }
    render();
    SwapBuffers(g_dc);
  }

  render();
  bool ok = captureFrame(path);
  SwapBuffers(g_dc);

  // first-person: the arm viewmodel — left hand builds, right hand collects
  g_thirdPerson = false;
  g_armSwingTimer = ARM_SWING_TIME * 0.5;
  g_swingLeftHand = true; // building
  render();
  captureFrame((exeDir() + "screenshot_hand_left.bmp").c_str());
  SwapBuffers(g_dc);
  g_swingLeftHand = false; // collecting
  g_armSwingTimer = ARM_SWING_TIME * 0.85; // early, arm still rising
  render();
  captureFrame((exeDir() + "screenshot_hand_start.bmp").c_str());
  SwapBuffers(g_dc);
  g_armSwingTimer = ARM_SWING_TIME * 0.5; // peak of the swing
  render();
  captureFrame((exeDir() + "screenshot_hand.bmp").c_str());
  SwapBuffers(g_dc);
  g_armSwingTimer = 0;

  // pickaxe gripped in the right hand — idle grip, then the mining swing at
  // its peak, where the tool should read as clearly upside down
  g_inventory->mainHand = { ITEM_WOOD_PICKAXE, 1 };
  render();
  captureFrame((exeDir() + "screenshot_pickaxe_hand_idle.bmp").c_str());
  SwapBuffers(g_dc);
  // the smash, in order: wound up with the tool raised, then driving down
  g_armSwingTimer = ARM_SWING_TIME * 0.88; // early: lifting back
  render();
  captureFrame((exeDir() + "screenshot_pickaxe_hand_lift.bmp").c_str());
  SwapBuffers(g_dc);
  g_armSwingTimer = ARM_SWING_TIME * 0.5; // peak of the swing
  render();
  captureFrame((exeDir() + "screenshot_pickaxe_hand_swing.bmp").c_str());
  SwapBuffers(g_dc);
  // building (left hand) while a tool is carried: both viewmodel arms are on
  // screen at once — the only frame that draws two of them
  g_swingLeftHand = true;
  render();
  captureFrame((exeDir() + "screenshot_pickaxe_hand_build.bmp").c_str());
  SwapBuffers(g_dc);
  g_swingLeftHand = false;
  g_armSwingTimer = 0;

  // third-person view of the player character, posed mid-stride with the
  // collect swing half-way so the limb animation is visible
  g_thirdPerson = true;
  g_walkPhase = 1.2;
  g_walkAmount = 1.0;
  render();
  captureFrame((exeDir() + "screenshot_pickaxe_third_idle.bmp").c_str());
  SwapBuffers(g_dc);
  g_armSwingTimer = ARM_SWING_TIME * 0.5;
  render();
  captureFrame((exeDir() + "screenshot_pickaxe_third_swing.bmp").c_str());
  SwapBuffers(g_dc);
  // the axe: same grip machinery, different silhouette. First person first,
  // where the tool is nearest the camera and its blade profile is clearest.
  g_thirdPerson = false;
  g_inventory->mainHand = { ITEM_WOOD_AXE, 1 };
  g_armSwingTimer = 0;
  render();
  captureFrame((exeDir() + "screenshot_axe_hand_idle.bmp").c_str());
  SwapBuffers(g_dc);
  g_armSwingTimer = ARM_SWING_TIME * 0.5;
  render();
  captureFrame((exeDir() + "screenshot_axe_hand_swing.bmp").c_str());
  SwapBuffers(g_dc);
  g_thirdPerson = true;
  g_armSwingTimer = 0;
  render();
  captureFrame((exeDir() + "screenshot_axe_third_idle.bmp").c_str());
  SwapBuffers(g_dc);
  g_armSwingTimer = ARM_SWING_TIME * 0.5;
  render();
  captureFrame((exeDir() + "screenshot_axe_third_swing.bmp").c_str());
  SwapBuffers(g_dc);
  g_inventory->mainHand = Hotbar::Slot();
  g_armSwingTimer = ARM_SWING_TIME * 0.5;
  render();
  captureFrame((exeDir() + "screenshot_third.bmp").c_str());
  SwapBuffers(g_dc);
  g_thirdPerson = false;
  g_walkPhase = 0;
  g_walkAmount = 0;
  g_armSwingTimer = 0;

  // inventory screen: a few stacks placed so slots, counts and the held-item
  // layer are all exercised (I key in game); one shot per tab
  g_invOpen = true;
  g_inventory->main[0] = { BLOCK_STONE, 47 };
  g_inventory->main[9] = { BLOCK_WOOD, 12 };
  g_inventory->main[10] = { BLOCK_SAND, 3 };
  g_inventory->craft[0] = { ITEM_PLANKS, 1 };
  g_inventory->craft[1] = { ITEM_PLANKS, 1 };
  g_inventory->craft[2] = { ITEM_PLANKS, 1 };
  g_inventory->craft[4] = { ITEM_STICK, 1 };
  g_inventory->craft[7] = { ITEM_STICK, 1 }; // the wood pickaxe's own shape, so the
                                              // craft-tab shot shows a real,
                                              // matched recipe in the result slot
  g_inventory->armor[0] = { BLOCK_SNOW, 1 };
  g_inventory->main[11] = { ITEM_WOOD_PICKAXE, 1 }; // crafted item, in the backpack
  g_inventory->mainHand = { ITEM_WOOD_PICKAXE, 1 }; // and equipped in the Hand slot
  g_inventory->tab = INV_TAB_INVENTORY;
  render();
  captureFrame((exeDir() + "screenshot_inventory.bmp").c_str());
  SwapBuffers(g_dc);
  g_inventory->tab = INV_TAB_PLAYER; // front-facing character with its face
  render();
  captureFrame((exeDir() + "screenshot_inventory_player.bmp").c_str());
  SwapBuffers(g_dc);
  g_inventory->tab = INV_TAB_CRAFT;
  render();
  captureFrame((exeDir() + "screenshot_inventory_craft.bmp").c_str());
  SwapBuffers(g_dc);
  // hover tooltips: over a backpack block, and over the crafted result
  {
    double sx, sy, sw, sh;
    double savedX = g_mouseX, savedY = g_mouseY;
    if (g_inventory->craftResultRect(g_winW, g_winH, sx, sy, sw, sh)) {
      g_mouseX = sx + sw / 2;
      g_mouseY = sy + sh / 2;
      render();
      captureFrame((exeDir() + "screenshot_tooltip_result.bmp").c_str());
      SwapBuffers(g_dc);
    }
    g_inventory->tab = INV_TAB_INVENTORY;
    if (g_inventory->mainSlotRect(0, g_winW, g_winH, sx, sy, sw, sh)) {
      g_mouseX = sx + sw / 2;
      g_mouseY = sy + sh / 2;
      render();
      captureFrame((exeDir() + "screenshot_tooltip_block.bmp").c_str());
      SwapBuffers(g_dc);
    }
    g_mouseX = savedX;
    g_mouseY = savedY;
    g_inventory->tab = INV_TAB_CRAFT;
  }
  // a stray ingredient in the cell the arrow used to cover: the grid now
  // makes nothing, and the panel has to say so rather than look broken
  g_inventory->craft[5] = { BLOCK_SAND, 1 };
  render();
  captureFrame((exeDir() + "screenshot_inventory_craft_norecipe.bmp").c_str());
  SwapBuffers(g_dc);
  g_inventory->craft[5] = Hotbar::Slot();
  // the axe recipe: the pickaxe's grid plus one more stone
  // both stone stacked in ONE cell and shoved to the far corner: layout and
  // stacking no longer matter, only that the grid totals 1 wood + 2 stone
  g_inventory->craft[1] = Hotbar::Slot();
  g_inventory->craft[8] = { BLOCK_STONE, 2 };
  render();
  captureFrame((exeDir() + "screenshot_inventory_craft_axe.bmp").c_str());
  SwapBuffers(g_dc);
  g_inventory->craft[2] = Hotbar::Slot();
  // the recipe list, opened from the book button
  g_inventory->bookOpen = true;
  render();
  captureFrame((exeDir() + "screenshot_recipe_book.bmp").c_str());
  SwapBuffers(g_dc);
  g_inventory->bookOpen = false;
  g_invOpen = false;
  *g_inventory = Inventory();

  // biome + border shots: fly the camera to each biome and to the border
  auto flyShot = [&](double x, double y, double z, double yaw, double pitch, const char* file) {
    g_player->position = Vec3(x, y, z);
    g_player->yaw = yaw;
    g_player->pitch = pitch;
    auto removed = g_world->updateLoadedChunks(x, z);
    for (auto& c : removed) disposeChunk(*c);
    for (auto& kv : g_world->chunks) {
      if (kv.second->dirty) remeshChunk(*g_world, *kv.second);
    }
    minimapUpdate(*g_world, x, z); // the HUD map follows the camera
    render();
    captureFrame((exeDir() + file).c_str());
    SwapBuffers(g_dc);
  };
  // Pick the interior biome column closest to the map center (edge columns
  // would put the border/other biomes in frame instead).
  auto findBiome = [&](int want, bool ocean, double& outX, double& outZ) {
    double bestD = 1e18;
    bool found = false;
    for (int x = -WORLD_RADIUS + 4; x < WORLD_RADIUS - 4; x += 4) {
      for (int z = -WORLD_RADIUS + 4; z < WORLD_RADIUS - 4; z += 4) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (b != want) continue;
        if (ocean ? sy > SEA_LEVEL - 3 : sy <= SEA_LEVEL + 1) continue;
        bool interior = true;
        // small offsets: the snow ring is only ~18 blocks wide
        const int OFFS[4][2] = { { -8, 0 }, { 8, 0 }, { 0, -8 }, { 0, 8 } };
        for (const int* o : OFFS) {
          int b2, s2;
          columnInfoAt(x + o[0], z + o[1], b2, s2);
          if (b2 != want) interior = false;
        }
        if (!interior) continue;
        double d = (double)x * x + (double)z * z;
        if (d < bestD) {
          bestD = d;
          outX = x + 0.5;
          outZ = z + 0.5;
          found = true;
        }
      }
    }
    return found;
  };
  double bx, bz;
  auto yawToCenter = [](double x, double z) { return std::atan2(x, z); };
  if (findBiome(1, false, bx, bz)) flyShot(bx, 40, bz, yawToCenter(bx, bz), -0.35, "screenshot_desert.bmp");
  // canyon: find the deepest carved gorge (biggest rim-to-floor drop) and
  // stand back on the rim looking into it
  {
    int bestDrop = 0;
    double gx = 0, gz = 0, rimY = 0;
    for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS - 8; x += 4) {
      for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS - 8; z += 4) {
        if (!canyonCutAt(x, z)) continue;
        int b, floorY;
        columnInfoAt(x, z, b, floorY);
        // highest rim within 12 blocks
        int rim = floorY;
        for (int d = 4; d <= 12; d += 4) {
          int bb, sy;
          columnInfoAt(x + d, z, bb, sy); rim = std::max(rim, sy);
          columnInfoAt(x - d, z, bb, sy); rim = std::max(rim, sy);
          columnInfoAt(x, z + d, bb, sy); rim = std::max(rim, sy);
          columnInfoAt(x, z - d, bb, sy); rim = std::max(rim, sy);
        }
        if (rim - floorY > bestDrop) {
          bestDrop = rim - floorY;
          gx = x + 0.5;
          gz = z + 0.5;
          rimY = rim;
        }
      }
    }
    if (bestDrop > 0) {
      // stand inside the gorge, on the floor, looking along it: this is the
      // view that shows the terraced walls stepping outward as they rise
      int b, floorY;
      columnInfoAt((int)gx, (int)gz, b, floorY);
      int savedRd = g_world->renderDistance;
      g_world->renderDistance = 6;
      flyShot(gx, floorY + 2.0, gz, -1.5708, 0.15, "screenshot_canyon.bmp");
      // and a view down into it from the rim
      flyShot(gx - 14, rimY + 10, gz, -1.5708, -0.45, "screenshot_canyon_rim.bmp");
      g_world->renderDistance = savedRd;
    }
  }
  if (findBiome(3, false, bx, bz)) {
    // look tangentially along the ring so the snow band fills the frame
    int b, sy;
    columnInfoAt((int)bx, (int)bz, b, sy);
    flyShot(bx, sy + 8, bz, yawToCenter(bx, bz) + 1.5708, -0.2, "screenshot_snowbiome.bmp");
  }
  // iceberg: find a frozen-ocean column where the berg noise actually peaks
  {
    double bestD = 1e18;
    bool found = false;
    double ix = 0, iz = 0;
    for (int x = -WORLD_RADIUS + 4; x < WORLD_RADIUS - 4; x += 4) {
      for (int z = -WORLD_RADIUS + 4; z < WORLD_RADIUS - 4; z += 4) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (b != 3 || sy > SEA_LEVEL - 3 || bergValueAt(x, z) < 0.7) continue;
        double d = (double)x * x + (double)z * z;
        if (d < bestD) {
          bestD = d;
          ix = x + 0.5;
          iz = z + 0.5;
          found = true;
        }
      }
    }
    if (found) flyShot(ix - 24, 30, iz, -1.5708, -0.2, "screenshot_iceberg.bmp");
  }
  // Crafted goods standing in the world: a row of them on open ground, to
  // confirm they are textured cubes rather than the untextured ghosts they
  // used to leave behind.
  {
    int px = 0, pz = 0, bestY = -1;
    for (int x = -120; x <= 120; x += 4) {
      for (int z = -120; z <= 120; z += 4) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (sy > bestY && sy > SEA_LEVEL + 2) { bestY = sy; px = x; pz = z; }
      }
    }
    if (bestY > 0) {
      g_world->updateLoadedChunks(px, pz);
      const uint8_t ROW[] = { ITEM_FENCE, ITEM_DOOR, ITEM_TRAPDOOR,
                              ITEM_CHEST, ITEM_FURNACE, ITEM_CRAFTING_TABLE,
                              ITEM_PLANKS, ITEM_STONE_BRICKS, ITEM_SANDSTONE,
                              ITEM_WOOD_SLAB, ITEM_STONE_SLAB };
      int n = (int)(sizeof(ROW) / sizeof(ROW[0]));
      for (int i = 0; i < n; i++) {
        int bpx = px - n / 2 + i, bpy = bestY + 1, bpz = pz + 3;
        g_world->setBlock(bpx, bpy, bpz, ROW[i]);
        // Pin the furnace's facing before it's ever meshed, same as tryPlace
        // does — otherwise the static body (built on the first render, off
        // the neighbour fallback) and the fire (read from this map) could
        // disagree on which side is open.
        if (ROW[i] == ITEM_FURNACE) g_world->furnaces[{ bpx, bpy, bpz }].facing = 1;
      }
      // Ladders climbing a wall: a stone column with ladders down its near
      // face, which is what exercises the facing-from-neighbours logic.
      int lx = px - n / 2 - 3;
      for (int dy = 1; dy <= 4; dy++) {
        g_world->setBlock(lx, bestY + dy, pz + 3, BLOCK_STONE);
        g_world->setBlock(lx, bestY + dy, pz + 4, ITEM_LADDER);
      }
      // Two flights of stairs (wood and stone) climbing away from the camera
      // toward -Z, each step propped by a stone column. The column tops sit
      // at the next step's level, which is what anchors every stair's facing
      // (World::stairFacing) so the flight reads as one continuous staircase.
      int sx = px + n / 2 + 2;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < i; j++) {
          g_world->setBlock(sx, bestY + 1 + j, pz + 8 - i, BLOCK_STONE);
          g_world->setBlock(sx + 1, bestY + 1 + j, pz + 8 - i, BLOCK_STONE);
        }
        g_world->setBlock(sx, bestY + 1 + i, pz + 8 - i, ITEM_WOOD_STAIRS);
        g_world->setBlock(sx + 1, bestY + 1 + i, pz + 8 - i, ITEM_STONE_STAIRS);
      }
      // A run of fence posts (connecting to each other) ending against a
      // plain stone block (connecting to that too), so the rails/arms
      // actually show up rather than a bare post — see fenceConnects.
      int fx = px - n / 2, fz = pz + 6;
      for (int i = 0; i < 3; i++) g_world->setBlock(fx + i, bestY + 1, fz, ITEM_FENCE);
      g_world->setBlock(fx + 3, bestY + 1, fz, BLOCK_STONE);
      for (int i = 0; i < 3; i++) g_world->setBlock(fx + i, bestY + 1, fz + 2, ITEM_STONE_BRICKS);
      g_world->setBlock(fx + 3, bestY + 1, fz + 2, BLOCK_STONE);

      // yaw 0 looks along -Z, back toward the row we just laid down
      flyShot(px + 0.5, bestY + 3.5, pz + 15.5, 0.0, -0.12,
              "screenshot_crafted_blocks.bmp");

      // Same scene with the furnace lit and the door swung open, to check
      // the two E-toggled states actually render.
      g_world->furnaces[{ px - n / 2 + 4, bestY + 1, pz + 3 }].lit = true;
      g_world->doors[{ px - n / 2 + 1, bestY + 1, pz + 3 }].open = true;
      for (int frame = 0; frame < 20; frame++) updateDoorAnimations(*g_world, 1.0 / 60);
      flyShot(px + 0.5, bestY + 3.5, pz + 15.5, 0.0, -0.12,
              "screenshot_crafted_blocks_active.bmp");
      // Close on the lit furnace's front (it opens +Z, see the facing set
      // above), to check the fire itself reads clearly.
      flyShot(px - n / 2.0 + 4.5, bestY + 1.35, pz + 6.0, 0.0, -0.08,
              "screenshot_furnace_lit.bmp");
    }
  }
  // Coal seams: they only exist well below ground, so hollow out a chamber
  // under a hillside and look at the exposed wall.
  {
    // Take the tallest land column in range rather than a fixed height bar,
    // so this always finds somewhere to dig whatever the seed throws up.
    int coalX = 0, coalZ = 0, bestY = -1;
    for (int x = -120; x <= 120; x += 4) {
      for (int z = -120; z <= 120; z += 4) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (sy > bestY) { bestY = sy; coalX = x; coalZ = z; }
      }
    }
    if (bestY > SEA_LEVEL + 3) {
      int b, sy;
      columnInfoAt(coalX, coalZ, b, sy);
      int camY = sy - 10; // well past COAL_MIN_DEPTH
      g_world->updateLoadedChunks(coalX, coalZ); // carve only loaded chunks
      for (int dx = -3; dx <= 1; dx++) {
        for (int dz = -2; dz <= 2; dz++) {
          for (int dy = 0; dy <= 3; dy++) {
            g_world->setBlock(coalX + dx, camY + dy, coalZ + dz, BLOCK_AIR);
          }
        }
      }
      flyShot(coalX - 1.5, camY + 1.0, coalZ + 0.5, -1.5708, 0.0, "screenshot_coal.bmp");
    }
  }
  // A thicket, seen from just above: checks that grove trees stand close
  // together but keep their crowns separate.
  {
    int bestTrees = 0;
    double tx = 0.5, tz = 0.5;
    int tyTop = SEA_LEVEL + 6;
    for (int x = -140; x <= 140; x += 8) {
      for (int z = -140; z <= 140; z += 8) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (sy <= SEA_LEVEL + 2 || surfaceBlockAt(x, z) != BLOCK_GRASS) continue;
        int grass = 0;
        for (int dx = -12; dx <= 12; dx += 3) {
          for (int dz = -12; dz <= 12; dz += 3) {
            if (surfaceBlockAt(x + dx, z + dz) == BLOCK_GRASS) grass++;
          }
        }
        if (grass > bestTrees) {
          bestTrees = grass;
          tx = x + 0.5;
          tz = z + 0.5;
          tyTop = sy;
        }
      }
    }
    flyShot(tx - 20, tyTop + 8, tz, -1.5708, -0.18, "screenshot_grove.bmp");
  }

  // sky: look up from open ground to frame the cloud layer
  {
    int sx = 0, sz = 0;
    findSpawnColumn(sx, sz);
    int b, sy;
    columnInfoAt(sx, sz, b, sy);
    int savedRd = g_world->renderDistance;
    g_world->renderDistance = 6;
    flyShot(sx + 0.5, sy + 3.0, sz + 0.5, 0.7, 0.55, "screenshot_sky.bmp");
    g_world->renderDistance = savedRd;
  }

  // underwater: just below the surface, then well down, showing the tint
  // darkening with depth
  {
    int wx = INT_MIN, wz = 0;
    for (int x = -WORLD_RADIUS + 40; x < WORLD_RADIUS - 40 && wx == INT_MIN; x += 4) {
      for (int z = -WORLD_RADIUS + 40; z < WORLD_RADIUS - 40; z += 4) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (b != 3 && sy <= SEA_LEVEL - 9) { // deep, non-polar ocean
          wx = x;
          wz = z;
          break;
        }
      }
    }
    if (wx != INT_MIN) {
      flyShot(wx + 0.5, SEA_LEVEL - 1 - EYE_HEIGHT, wz + 0.5, 0.6, -0.15,
              "screenshot_underwater_shallow.bmp");
      flyShot(wx + 0.5, SEA_LEVEL - 7 - EYE_HEIGHT, wz + 0.5, 0.6, -0.15,
              "screenshot_underwater_deep.bmp");
    }
  }

  // Landmark snow mountain: stand well back on the grassland so the whole
  // grass -> rock -> snow profile is in frame. Needs a wider view distance
  // than gameplay uses, otherwise fog swallows the summit.
  {
    double lx, lz;
    landmarkPosition(lx, lz);
    int savedRd = g_world->renderDistance;
    g_world->renderDistance = 8;
    flyShot(lx - 105, SEA_LEVEL + 12, lz, -1.5708, 0.12, "screenshot_snowmtn.bmp");
    g_world->renderDistance = savedRd;
  }
  // rock mountain: tallest inner-zone peak (bare stone top)
  {
    int best = -1;
    double rx = 0, rz = 0;
    for (int x = -WORLD_RADIUS; x < WORLD_RADIUS; x += 4) {
      for (int z = -WORLD_RADIUS; z < WORLD_RADIUS; z += 4) {
        double r = std::max(std::abs((double)x), std::abs((double)z)) / WORLD_RADIUS;
        if (r >= 0.45) continue;
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (b != 0 || sy <= best) continue;
        best = sy;
        rx = x + 0.5;
        rz = z + 0.5;
      }
    }
    if (best > 0) flyShot(rx - 26, best + 6, rz, -1.5708, -0.2, "screenshot_rockmtn.bmp");
  }
  flyShot(WORLD_RADIUS - 30.5, 42, 0.5, -1.5708, -0.35, "screenshot_border.bmp");
  g_player->position = Vec3(0.5, 40, 0.5); // back near spawn for the menu shots
  g_player->yaw = 0;

  // save panel (text input) and load panel (slot picker)
  pauseGame();
  openSavePanel();
  g_menu.saveNameInput = "my world";
  render();
  captureFrame((exeDir() + "screenshot_save.bmp").c_str());
  SwapBuffers(g_dc);

  confirmSaveGame();
  openLoadPanel();
  render();
  captureFrame((exeDir() + "screenshot_load.bmp").c_str());
  SwapBuffers(g_dc);

  g_menu.openConfirm("Delete \"my world\"?", "Delete", MenuAction::DeleteConfirmed, "my world");
  render();
  captureFrame((exeDir() + "screenshot_confirm.bmp").c_str());
  SwapBuffers(g_dc);
  DeleteFileA((exeDir() + "saves\\my world.txt").c_str());

  teardownSession();
  return ok ? 0 : 1;
}

// End-to-end water tool test through the real click handlers (tryMine =
// pour, tryPlace = drain), with the player aiming into a built basin.
static int runWaterTest() {
  FILE* f = std::fopen((exeDir() + "watertest_result.txt").c_str(), "w");
  if (!f) return 1;
  int failures = 0;
  auto check = [&](bool ok, const char* name) {
    std::fprintf(f, "%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
  };

  SaveState fixedSeed;
  fixedSeed.seed = 1337;
  createSession(&fixedSeed);
  g_state = GameState::Playing;

  // Points the camera at a world position from wherever the player stands.
  // Reach is one block now, so these tests have to walk up to what they touch
  // exactly like the player does — they used to hover a few blocks up and
  // aim straight down, which no longer reaches anything.
  auto lookAt = [&](double tx, double ty, double tz) {
    Vec3 eye = g_player->eyePosition();
    double dx = tx - eye.x, dy = ty - eye.y, dz = tz - eye.z;
    double horiz = std::sqrt(dx * dx + dz * dz);
    g_player->pitch = std::atan2(dy, horiz);
    g_player->yaw = std::atan2(-dx, -dz);
  };

  // Ice is a normal collectable block in hotbar slot 6: it carries a count,
  // placing one spends it, and mining it back returns it.
  g_hotbar->select(5);
  check(g_hotbar->selectedBlockId() == BLOCK_ICE, "ice_slot_selected");

  for (int bx = 2; bx <= 5; bx++) {
    for (int bz = 2; bz <= 5; bz++) g_world->setBlock(bx, 43, bz, BLOCK_STONE);
  }
  // Stand ON the slab and build onto the column alongside: placing into the
  // cell straight below would be inside the player's own feet, which
  // placementOverlapsPlayer refuses.
  g_player->position = Vec3(3.5, 44.0, 3.5);
  lookAt(4.2, 44.0, 3.5); // top face of the next column along

  int iceBefore = g_hotbar->slots[5].count;
  tryPlace();
  check(g_world->getBlock(4, 44, 3) == BLOCK_ICE, "place_ice");
  check(g_hotbar->slots[5].count == iceBefore - 1, "placing_ice_spends_one");

  tryMine(); // mine it back
  check(g_world->getBlock(4, 44, 3) == BLOCK_AIR &&
            g_hotbar->slots[5].count == iceBefore,
        "collect_ice");

  // Underwater view: submerged only when the eye is in water, and the tint
  // darkens with depth while visibility shortens.
  {
    // find open sea and drop the eye just under the surface
    int wx = INT_MIN, wz = 0;
    for (int x = -40; x <= 40 && wx == INT_MIN; x++) {
      for (int z = -40; z <= 40; z++) {
        if (g_world->getBlock(x, SEA_LEVEL, z) == BLOCK_WATER &&
            g_world->getBlock(x, SEA_LEVEL - 6, z) == BLOCK_WATER) {
          wx = x;
          wz = z;
          break;
        }
      }
    }
    bool found = wx != INT_MIN;
    check(found, "found_deep_water");
    if (found) {
      // above the surface: not submerged
      g_player->position = Vec3(wx + 0.5, SEA_LEVEL + 2.0, wz + 0.5);
      WaterView above = waterViewState();
      check(!above.submerged, "above_water_not_tinted");

      // just under the surface
      g_player->position = Vec3(wx + 0.5, SEA_LEVEL - EYE_HEIGHT, wz + 0.5);
      WaterView shallow = waterViewState();
      // deeper down
      g_player->position = Vec3(wx + 0.5, SEA_LEVEL - 5 - EYE_HEIGHT, wz + 0.5);
      WaterView deep = waterViewState();

      check(shallow.submerged && deep.submerged, "underwater_detected");
      check(deep.depth > shallow.depth, "depth_increases_downward");
      // Beer-Lambert: light left falls off with depth, so the tint darkens,
      // thickens and visibility shortens
      check(deep.b < shallow.b && deep.g < shallow.g, "tint_darkens_with_depth");
      check(deep.tintAlpha > shallow.tintAlpha, "tint_thickens_with_depth");
      check(deep.visibility < shallow.visibility, "visibility_drops_with_depth");
    }
  }

  // Building at arm's length and beyond, through the real right-click path.
  // Mining is deliberately short-range now, but placing is not: bridging and
  // pillaring need to put a block where you cannot stand. Shortening both at
  // once made building fail intermittently, so this pins the behaviour down.
  {
    for (int bx = 30; bx <= 40; bx++) {
      for (int bz = 30; bz <= 32; bz++) g_world->setBlock(bx, 43, bz, BLOCK_STONE);
    }
    for (int by = 44; by <= 47; by++) {
      for (int bx = 30; bx <= 40; bx++) {
        for (int bz = 30; bz <= 32; bz++) g_world->setBlock(bx, by, bz, BLOCK_AIR);
      }
    }
    g_hotbar->select(1);
    uint8_t want = (uint8_t)g_hotbar->selectedBlockId();
    g_player->position = Vec3(31.5, 44.0, 31.5);

    // a target four blocks away along the floor — well outside mining reach
    lookAt(36.2, 44.0, 31.5);
    int before = g_hotbar->slots[1].count;
    tryPlace();
    bool placedFar = want > 0 && g_world->getBlock(36, 44, 31) == want &&
                     g_hotbar->slots[1].count == before - 1;

    // and the same distant block is NOT minable: reach is split, not just
    // restored, so this fails if the two are ever merged back together
    tryMine();
    bool stillThere = g_world->getBlock(36, 44, 31) == want;

    check(placedFar && stillThere, "build_reaches_beyond_mining");
    if (!(placedFar && stillThere)) {
      std::fprintf(f, "  (placedFar=%d stillThere=%d block=%d)\n",
                   placedFar, stillThere, g_world->getBlock(36, 44, 31));
    }
  }

  // Building onto ground that carries a grass tuft must work: the tuft is
  // replaced, not treated as an obstruction.
  {
    g_world->setBlock(20, 43, 20, BLOCK_GRASS);
    g_world->setBlock(20, 44, 20, BLOCK_TALL_GRASS); // tuft on top
    g_world->setBlock(19, 43, 20, BLOCK_GRASS);      // somewhere to stand
    g_world->setBlock(19, 44, 20, BLOCK_AIR);
    g_hotbar->select(1);
    // read the block from the slot rather than assuming one, so reordering
    // the hotbar can't silently invalidate this test
    uint8_t want = (uint8_t)g_hotbar->selectedBlockId();
    int held = g_hotbar->slots[1].count;
    // stand alongside and aim down at the tufted column's top face
    g_player->position = Vec3(19.5, 44.0, 20.5);
    lookAt(20.2, 44.0, 20.5);
    tryPlace();
    check(want > 0 && g_world->getBlock(20, 44, 20) == want &&
              g_hotbar->slots[1].count == held - 1,
          "build_over_grass_tuft");
  }

  // Mining a block next to water floods the gap, through the real
  // left-click path (this is the shoreline-digging case).
  {
    for (int bx = 10; bx <= 16; bx++) {
      for (int bz = 9; bz <= 11; bz++) {
        g_world->setBlock(bx, 43, bz, BLOCK_STONE); // floor
        g_world->setBlock(bx, 44, bz, BLOCK_STONE); // the mass to dig into
      }
    }
    g_world->setBlock(11, 44, 10, BLOCK_WATER); // water at one end

    g_hotbar->select(2);
    // stand on top of the mass, so the block underfoot is within reach
    g_player->position = Vec3(12.5, 45.0, 10.5);
    g_player->yaw = 0;
    g_player->pitch = -PITCH_LIMIT; // straight down at (12,44,10)
    tryMine();
    check(g_world->getBlock(12, 44, 10) == BLOCK_WATER, "mine_next_to_water_floods");
  }

  // Stairs are walkable up AND down without jumping: a two-stair flight
  // rising toward -Z, climbed by holding W. The auto-step lifts the player
  // half a block per slab; walking back down just falls from step to step.
  {
    // Extend the cleared strip the build-reach test above already made
    // (loaded chunks known good) so the flight stands on a real floor.
    for (int bx = 33; bx <= 37; bx++) {
      for (int bz = 26; bz <= 34; bz++) {
        g_world->setBlock(bx, 43, bz, BLOCK_STONE); // floor
        for (int by = 44; by <= 48; by++) g_world->setBlock(bx, by, bz, BLOCK_AIR);
      }
    }
    // Step 1 is free-standing (default facing, rises toward -Z); step 2
    // leans on the landing block, which anchors the same facing. The setBlock
    // remesh also exercises the stair meshing path.
    remeshAll(g_world->setBlock(35, 44, 31, ITEM_WOOD_STAIRS)); // surfaces 44.5 / 45
    g_world->setBlock(35, 45, 30, ITEM_STONE_STAIRS);           // surfaces 45.5 / 46
    g_world->setBlock(35, 45, 29, BLOCK_STONE);                 // landing, top at 46

    g_player->position = Vec3(35.5, 44.0, 33.5);
    g_player->velocity = Vec3(0, 0, 0);
    g_player->yaw = 0; // forward = -Z, toward the flight
    g_player->onGround = true;

    MoveInput walk{};
    walk.forward = 1;
    for (int i = 0; i < 240 && g_player->position.z > 29.5; i++) {
      g_player->update(1.0 / 60.0, *g_world, walk);
    }
    check(g_player->position.y > 45.9 && g_player->position.z < 29.5,
          "stairs_walk_up");
    if (!(g_player->position.y > 45.9 && g_player->position.z < 29.5)) {
      std::fprintf(f, "  (y=%.2f z=%.2f)\n", g_player->position.y, g_player->position.z);
    }

    walk.forward = -1; // S: back down the flight toward +Z
    for (int i = 0; i < 240 && g_player->position.z < 33.0; i++) {
      g_player->update(1.0 / 60.0, *g_world, walk);
    }
    check(g_player->position.y < 44.1 && g_player->position.z > 33.0,
          "stairs_walk_down");
    if (!(g_player->position.y < 44.1 && g_player->position.z > 33.0)) {
      std::fprintf(f, "  (y=%.2f z=%.2f)\n", g_player->position.y, g_player->position.z);
    }
  }

  std::fprintf(f, "%s\n", failures == 0 ? "ALL_PASS" : "HAS_FAILURES");
  std::fclose(f);
  teardownSession();
  return failures == 0 ? 0 : 1;
}

// Diagnostic: writes an ASCII map + per-biome stats for the given seed, so
// world layout can be inspected without eyeballing screenshots.
static int runMapDump(uint32_t seed) {
  setWorldSeed(seed);
  FILE* f = std::fopen((exeDir() + "mapdump.txt").c_str(), "w");
  if (!f) return 1;

  std::fprintf(f, "seed %u  radius %d\n", seed, WORLD_RADIUS);

  const int STEP = 8; // one char per 8 blocks -> 64x64 map
  // legend: . ocean/lake  , beach  g grass  ^ grass mtn  R rock mtn
  //         d desert  C canyon floor  c canyon rim  S snow  M snow mtn
  for (int z = -WORLD_RADIUS; z < WORLD_RADIUS; z += STEP) {
    for (int x = -WORLD_RADIUS; x < WORLD_RADIUS; x += STEP) {
      int b, sy;
      columnInfoAt(x, z, b, sy);
      double r = std::sqrt((double)x * x + (double)z * z) / WORLD_RADIUS;
      char ch;
      if (b == 3) ch = sy >= 36 ? 'M' : (sy <= SEA_LEVEL ? '~' : 'S');
      else if (b == 2) ch = sy <= SEA_LEVEL - 2 ? 'C' : 'c';
      else if (b == 1) ch = 'd';
      else if (sy <= SEA_LEVEL) ch = '.';
      else if (sy <= SEA_LEVEL + 1) ch = ',';
      else if (sy >= 25 && r < 0.45) ch = 'R';
      else if (sy >= 30) ch = '^';
      else ch = 'g';
      std::fputc(ch, f);
    }
    std::fputc('\n', f);
  }

  // stats
  struct Stat { int count = 0; int minY = 999; int maxY = -1; double sumR = 0; };
  Stat st[4];
  int deepCanyon = 0, canyonRim = 0;
  int maxByBand[4] = { 0, 0, 0, 0 }; // r<.4, .4-.6, .6-.85, >.85
  for (int x = -WORLD_RADIUS; x < WORLD_RADIUS; x += 2) {
    for (int z = -WORLD_RADIUS; z < WORLD_RADIUS; z += 2) {
      int b, sy;
      columnInfoAt(x, z, b, sy);
      double r = std::sqrt((double)x * x + (double)z * z) / WORLD_RADIUS;
      Stat& s = st[b];
      s.count++;
      s.minY = std::min(s.minY, sy);
      s.maxY = std::max(s.maxY, sy);
      s.sumR += r;
      if (b == 2 && sy <= SEA_LEVEL - 2) deepCanyon++;
      if (b == 2 && sy >= SEA_LEVEL + 3) canyonRim++;
      int band = r < 0.4 ? 0 : (r < 0.6 ? 1 : (r < 0.85 ? 2 : 3));
      maxByBand[band] = std::max(maxByBand[band], sy);
    }
  }
  // water coverage + landmass structure (islands vs. one continent)
  {
    const int STEP2 = 4;
    const int N = (WORLD_RADIUS * 2) / STEP2;
    std::vector<uint8_t> land((size_t)N * N, 0);
    int landCells = 0;
    for (int i = 0; i < N; i++) {
      for (int j = 0; j < N; j++) {
        int x = -WORLD_RADIUS + i * STEP2;
        int z = -WORLD_RADIUS + j * STEP2;
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (sy > SEA_LEVEL) {
          land[(size_t)i * N + j] = 1;
          landCells++;
        }
      }
    }
    std::fprintf(f, "water %.1f%%  land %.1f%%\n",
                 100.0 * (N * N - landCells) / (N * N), 100.0 * landCells / (N * N));

    // flood fill to count separate landmasses
    std::vector<uint8_t> seen((size_t)N * N, 0);
    std::vector<int> sizes;
    for (int i = 0; i < N; i++) {
      for (int j = 0; j < N; j++) {
        size_t idx = (size_t)i * N + j;
        if (!land[idx] || seen[idx]) continue;
        int count = 0;
        std::vector<int> stack{ (int)idx };
        seen[idx] = 1;
        while (!stack.empty()) {
          int cur = stack.back();
          stack.pop_back();
          count++;
          int ci = cur / N, cj = cur % N;
          const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
          for (const int* d : D) {
            int ni = ci + d[0], nj = cj + d[1];
            if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
            size_t nidx = (size_t)ni * N + nj;
            if (!land[nidx] || seen[nidx]) continue;
            seen[nidx] = 1;
            stack.push_back((int)nidx);
          }
        }
        sizes.push_back(count);
      }
    }
    std::sort(sizes.begin(), sizes.end(), std::greater<int>());
    int islands = 0;
    for (size_t i = 1; i < sizes.size(); i++) {
      if (sizes[i] >= 4) islands++; // >= ~16x16 blocks
    }
    std::fprintf(f, "landmasses %d (largest %d cells, %d sizeable islands)\n",
                 (int)sizes.size(), sizes.empty() ? 0 : sizes[0], islands);
  }

  const char* names[4] = { "plains", "desert", "canyon", "snow" };
  int total = st[0].count + st[1].count + st[2].count + st[3].count;
  for (int i = 0; i < 4; i++) {
    std::fprintf(f, "%-7s %5.1f%%  y %d..%d  avg_r %.2f\n", names[i],
                 100.0 * st[i].count / total, st[i].minY, st[i].maxY,
                 st[i].count ? st[i].sumR / st[i].count : 0.0);
  }
  std::fprintf(f, "canyon floor cells %d, rim cells %d\n", deepCanyon, canyonRim);

  // canyon geometry: how deep the carved gorges actually are
  {
    int cutCells = 0, minFloor = 999, maxFloor = -1, bestDrop = 0, bx = 0, bz = 0;
    long long dropSum = 0;
    for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS - 8; x += 4) {
      for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS - 8; z += 4) {
        if (!canyonCutAt(x, z)) continue;
        int b, fy;
        columnInfoAt(x, z, b, fy);
        cutCells++;
        minFloor = std::min(minFloor, fy);
        maxFloor = std::max(maxFloor, fy);
        int rim = fy;
        for (int d = 4; d <= 12; d += 4) {
          int bb, sy;
          columnInfoAt(x + d, z, bb, sy); rim = std::max(rim, sy);
          columnInfoAt(x - d, z, bb, sy); rim = std::max(rim, sy);
          columnInfoAt(x, z + d, bb, sy); rim = std::max(rim, sy);
          columnInfoAt(x, z - d, bb, sy); rim = std::max(rim, sy);
        }
        dropSum += rim - fy;
        if (rim - fy > bestDrop) { bestDrop = rim - fy; bx = x; bz = z; }
      }
    }
    std::fprintf(f, "canyon cut cells %d  floor y %d..%d  avg drop %.1f  max drop %d at (%d,%d)\n",
                 cutCells, minFloor, maxFloor,
                 cutCells ? (double)dropSum / cutCells : 0.0, bestDrop, bx, bz);

    // vertical cross-section through the deepest gorge (proof of shape)
    std::fprintf(f, "cross-section z=%d, x=%d..%d:\n", bz, bx - 24, bx + 24);
    for (int y = 40; y >= 8; y -= 2) {
      std::fprintf(f, "y%2d ", y);
      for (int x = bx - 24; x <= bx + 24; x++) {
        int b, sy;
        columnInfoAt(x, bz, b, sy);
        char ch;
        if (y > sy) ch = (y <= SEA_LEVEL && !canyonCutAt(x, bz)) ? '~' : ' ';
        else if (canyonCutAt(x, bz) || (b == 2 && y > sy - 4)) ch = '#'; // red rock
        else ch = 'o';
        std::fputc(ch, f);
      }
      std::fputc('\n', f);
    }
  }
  std::fprintf(f, "max height by band: r<.4=%d .4-.6=%d .6-.85=%d r>.85=%d\n",
               maxByBand[0], maxByBand[1], maxByBand[2], maxByBand[3]);

  // LAND-only peak trend (what the mountains_taller_farther test measures)
  {
    double lx, lz;
    landmarkPosition(lx, lz);
    std::vector<int> inner, outer;
    for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS; x += 4) {
      for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS; z += 4) {
        if (std::hypot(x - lx, z - lz) < 60) continue;
        double r = std::max(std::abs((double)x), std::abs((double)z)) / WORLD_RADIUS;
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (sy <= SEA_LEVEL) continue;
        if (r < 0.45) inner.push_back(sy);
        else if (r > 0.85) outer.push_back(sy);
      }
    }
    auto topMean = [](std::vector<int>& v) {
      std::sort(v.begin(), v.end(), std::greater<int>());
      int n = std::min<int>(30, (int)v.size());
      if (n == 0) return 0.0;
      double sum = 0;
      for (int i = 0; i < n; i++) sum += v[i];
      return sum / n;
    };
    int innerN = (int)inner.size(), outerN = (int)outer.size();
    int innerMax = inner.empty() ? 0 : *std::max_element(inner.begin(), inner.end());
    std::fprintf(f, "land peaks: inner n=%d topMean=%.1f max=%d | outer n=%d topMean=%.1f\n",
                 innerN, topMean(inner), innerMax, outerN, topMean(outer));
  }

  // tree size histogram
  {
    int hist[16] = {};
    int total = 0;
    for (int ccx = -6; ccx <= 6; ccx++) {
      for (int ccz = -6; ccz <= 6; ccz++) {
        auto ch = generateChunk(ccx, ccz);
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
          for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int y = 1; y < CHUNK_HEIGHT - 1; y++) {
              if (ch->getLocal(lx, y, lz) != BLOCK_WOOD) continue;
              if (ch->getLocal(lx, y - 1, lz) == BLOCK_WOOD) continue;
              int h = 0;
              while (y + h < CHUNK_HEIGHT && ch->getLocal(lx, y + h, lz) == BLOCK_WOOD) h++;
              if (h >= 0 && h < 16) hist[h]++;
              total++;
              break;
            }
          }
        }
      }
    }
    std::fprintf(f, "trees %d, by height:", total);
    for (int h = 2; h <= 15; h++) std::fprintf(f, " %d:%d", h, hist[h]);
    std::fputc('\n', f);

    // clustering: neighbours within 10 blocks, small trees vs big ones
    struct T { int x, z, h; };
    std::vector<T> all;
    for (int ccx = -6; ccx <= 6; ccx++) {
      for (int ccz = -6; ccz <= 6; ccz++) {
        auto ch = generateChunk(ccx, ccz);
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
          for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int y = 1; y < CHUNK_HEIGHT - 1; y++) {
              if (ch->getLocal(lx, y, lz) != BLOCK_WOOD) continue;
              if (ch->getLocal(lx, y - 1, lz) == BLOCK_WOOD) continue;
              int h = 0;
              while (y + h < CHUNK_HEIGHT && ch->getLocal(lx, y + h, lz) == BLOCK_WOOD) h++;
              all.push_back({ ccx * CHUNK_SIZE + lx, ccz * CHUNK_SIZE + lz, h });
              break;
            }
          }
        }
      }
    }
    double smallN = 0, bigN = 0;
    int smallC = 0, bigC = 0;
    for (const T& a : all) {
      int nearby = 0; // NB: `near` is a legacy macro from windows.h
      for (const T& b : all) {
        if (&a == &b) continue;
        if (std::abs(a.x - b.x) <= 10 && std::abs(a.z - b.z) <= 10) nearby++;
      }
      if (a.h <= 5) { smallN += nearby; smallC++; }
      else if (a.h >= 8) { bigN += nearby; bigC++; }
    }
    std::fprintf(f, "clustering: small n=%d avg neighbours %.2f | big n=%d avg %.2f\n",
                 smallC, smallC ? smallN / smallC : 0.0,
                 bigC, bigC ? bigN / bigC : 0.0);
  }

  // where a fresh game would drop the player (must be substantial land)
  {
    int sx = 0, sz = 0;
    findSpawnColumn(sx, sz);
    int b, ssy;
    columnInfoAt(sx, sz, b, ssy);
    int landAround = 0;
    for (int dx = -16; dx <= 16; dx += 8) {
      for (int dz = -16; dz <= 16; dz += 8) {
        int bb, sy;
        columnInfoAt(sx + dx, sz + dz, bb, sy);
        if (sy > SEA_LEVEL) landAround++;
      }
    }
    std::fprintf(f, "spawn at (%d,%d) surfaceY=%d (sea %d) land nearby %d/25\n",
                 sx, sz, ssy, SEA_LEVEL, landAround);
  }

  // landmark mountain: position, summit, and the flank block sequence
  {
    double lx, lz;
    landmarkPosition(lx, lz);
    int lb, lpeak;
    columnInfoAt((int)lx, (int)lz, lb, lpeak);
    double lr = std::max(std::abs(lx), std::abs(lz)) / WORLD_RADIUS;
    std::fprintf(f, "landmark at (%.0f,%.0f) r=%.2f biome=%d summit y=%d\n",
                 lx, lz, lr, lb, lpeak);
    // how wide the summit plateau is (columns at the peak height)
    int flatCols = 0;
    for (int dx = -20; dx <= 20; dx++) {
      for (int dz = -20; dz <= 20; dz++) {
        int bb, sy;
        columnInfoAt((int)lx + dx, (int)lz + dz, bb, sy);
        if (sy >= lpeak) flatCols++;
      }
    }
    std::fprintf(f, "  summit plateau: %d columns at y%d\n", flatCols, lpeak);
    std::fprintf(f, "  near-summit d: ");
    for (int d = 0; d <= 12; d++) {
      int bb, sy;
      columnInfoAt((int)lx + d, (int)lz, bb, sy);
      std::fprintf(f, "%d:y%d ", d, sy);
    }
    std::fputc('\n', f);
    std::fprintf(f, "  flank d: ");
    for (int d = 0; d <= 70; d += 5) {
      int bb, sy;
      columnInfoAt((int)lx + d, (int)lz, bb, sy);
      uint8_t s = surfaceBlockAt((int)lx + d, (int)lz);
      const char* nm = s < BLOCK_TYPE_COUNT ? BLOCKS[s].name : "?";
      std::fprintf(f, "%d:y%d/%s ", d, sy, nm);
    }
    std::fputc('\n', f);
  }
  std::fclose(f);
  return 0;
}

static int runSelftest() {
  std::string path = exeDir() + "selftest_result.txt";
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) return 1;
  int failures = 0;
  auto check = [&](bool ok, const char* name) {
    std::fprintf(f, "%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
  };

  setWorldSeed(1337); // fixed reference world for the tests below

  // worldgen determinism + structure
  auto c1 = generateChunk(0, 0);
  auto c2 = generateChunk(0, 0);
  check(c1->blocks == c2->blocks, "worldgen_deterministic");
  bool bedrockOk = true;
  for (int z = 0; z < CHUNK_SIZE; z++)
    for (int x = 0; x < CHUNK_SIZE; x++)
      if (c1->getLocal(x, 0, z) != BLOCK_BEDROCK) bedrockOk = false;
  check(bedrockOk, "bedrock_layer");

  // world get/set + edit replay onto regenerated chunks
  World w;
  w.updateLoadedChunks(0, 0);
  uint8_t before = w.getBlock(3, 20, 3);
  w.setBlock(3, 20, 3, BLOCK_STONE);
  check(w.getBlock(3, 20, 3) == BLOCK_STONE, "set_get_block");
  World w2;
  w2.loadEdits(w.getEditsSnapshot());
  w2.updateLoadedChunks(0, 0);
  check(w2.getBlock(3, 20, 3) == BLOCK_STONE, "edit_replay");
  (void)before;

  // Water surface opacity rises with depth: a shallow pool shows its bed,
  // water WATER_OPAQUE_DEPTH blocks deep or more hides it completely.
  {
    bool ramp = true;
    for (int d = 1; d < WATER_OPAQUE_DEPTH; d++) {
      if (waterSurfaceAlpha(d) <= waterSurfaceAlpha(d - 1)) ramp = false;
      if (waterSurfaceAlpha(d) >= 255) ramp = false; // still see-through
    }
    check(ramp && waterSurfaceAlpha(WATER_OPAQUE_DEPTH) == 255 &&
              waterSurfaceAlpha(WATER_OPAQUE_DEPTH + 3) == 255,
          "deep_water_hides_bottom");
  }

  // Water flows into freshly mined space: digging a tunnel in from a water
  // edge floods it, rather than leaving a dry hole.
  {
    // stone corridor above the terrain, capped so the flood stays contained:
    // floor y43, roof y45, walls around a 1-wide run at y44
    for (int bx = 10; bx <= 22; bx++) {
      for (int bz = 2; bz <= 4; bz++) {
        w.setBlock(bx, 43, bz, BLOCK_STONE);
        w.setBlock(bx, 45, bz, BLOCK_STONE);
        w.setBlock(bx, 44, bz, BLOCK_STONE);
      }
    }
    w.setBlock(10, 44, 3, BLOCK_WATER); // the water body at one end

    // dig along the corridor; each cell should flood as it opens. One cell
    // is left carrying a grass tuft: the water must wash it away rather
    // than treat it as a dam.
    bool flooded = true;
    for (int bx = 11; bx <= 20; bx++) {
      w.setBlock(bx, 44, 3, bx == 15 ? BLOCK_TALL_GRASS : BLOCK_AIR);
      w.flowWaterInto(bx, 44, 3, 4096);
      if (w.getBlock(bx, 44, 3) != BLOCK_WATER) flooded = false;
    }
    check(flooded, "water_flows_into_mined_space");
    check(w.getBlock(15, 44, 3) == BLOCK_WATER, "water_washes_away_plants");

    // and it must not climb: a cell opened above the water line stays dry
    w.setBlock(15, 45, 3, BLOCK_AIR);
    w.flowWaterInto(15, 45, 3, 4096);
    check(w.getBlock(15, 45, 3) == BLOCK_AIR, "water_does_not_flow_upward");
  }

  // raycast straight down from above the spawn column hits a solid top face
  int topY = -1;
  for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
    if (isSolid(w.getBlock(0, y, 0))) { topY = y; break; }
  }
  RaycastHit hit;
  bool hitOk = raycastVoxel(w, Vec3(0.5, CHUNK_HEIGHT + 2.0, 0.5), Vec3(0, -1, 0), 100, hit);
  check(hitOk && hit.pos[1] == topY && hit.normal[1] == 1, "raycast_down");

  // MINING reach is short enough that you must stand next to a block, but
  // never so short that the ground under your own feet falls out of range.
  // BUILDING keeps the long reach — the two are separate on purpose, because
  // sharing them made placing fail wherever you couldn't stand next to the
  // spot. Both are checked with the real raycast from a real eye.
  {
    Vec3 feet(0.5, (double)(topY + 1), 0.5); // standing on the spawn column
    Vec3 eye(feet.x, feet.y + EYE_HEIGHT, feet.z);
    RaycastHit h;
    // straight down: the block underfoot must always be minable
    bool underfoot = raycastVoxel(w, eye, Vec3(0, -1, 0), MINE_REACH, h) && h.pos[1] == topY;

    // level with the eye, a wall of stone: near enough to touch is in range,
    // a few blocks off is not
    int eyeY = (int)std::floor(eye.y);
    for (int d = 1; d <= 6; d++) w.setBlock(d, eyeY, 0, BLOCK_STONE);
    Vec3 fwd(1, 0, 0);
    // the adjacent column (its face 0.5 away) is reachable
    bool nearOk = raycastVoxel(w, eye, fwd, MINE_REACH, h) && h.pos[0] == 1;
    // from four blocks back, the same wall is out of MINING range...
    Vec3 farEye(-3.5, eye.y, 0.5);
    bool farNotMinable = !raycastVoxel(w, farEye, fwd, MINE_REACH, h);
    // ...but still well within BUILDING range, so you can bridge and pillar
    // exactly as before. This is the regression that shipped when one reach
    // served both.
    bool farStillBuildable = raycastVoxel(w, farEye, fwd, PLACE_REACH, h);
    for (int d = 1; d <= 6; d++) w.setBlock(d, eyeY, 0, BLOCK_AIR);

    bool ok = underfoot && nearOk && farNotMinable && farStillBuildable &&
              MINE_REACH >= EYE_HEIGHT && PLACE_REACH > MINE_REACH;
    check(ok, "mine_reach_short_build_reach_long");
    if (!ok) {
      std::fprintf(f, "  (underfoot=%d near=%d farNotMinable=%d farBuildable=%d mine=%.2f place=%.2f)\n",
                   underfoot, nearOk, farNotMinable, farStillBuildable, MINE_REACH, PLACE_REACH);
    }
  }

  // physics: box on solid ground collides, box in air doesn't
  check(boxCollides(w, 0.5, (double)topY, 0.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT), "collide_ground");
  check(!boxCollides(w, 0.5, topY + 1.05, 0.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT), "clear_above_ground");

  // An unloaded chunk acts as a solid wall, same as the world border —
  // World::getBlock silently reports it as air, so without this an animal
  // (or anything else using gravity) wandering to the edge of the loaded
  // area could fall straight through into an undefined void and sink
  // forever, vanishing with no death sequence at all.
  {
    World fresh; // nothing loaded anywhere in it yet
    bool blockedInVoid = boxCollides(fresh, 0.5, 40.0, 0.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
    fresh.updateLoadedChunks(0, 0);
    bool clearOnceLoaded = !boxCollides(fresh, 0.5, 40.0, 0.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
    check(blockedInVoid && clearOnceLoaded, "unloaded_chunk_blocks_like_a_wall");
    if (!(blockedInVoid && clearOnceLoaded)) {
      std::fprintf(f, "  (blockedInVoid=%d clearOnceLoaded=%d)\n", blockedInVoid, clearOnceLoaded);
    }
  }

  // Sleeping's hunger boost (Player::hungerBoostTimer, set by main.cpp's
  // trySleep): hunger normally drains once every 30s of play, drains twice
  // as fast (every 15s) while the boost is running, and the boost itself
  // counts back down to 0 and stops on its own.
  {
    MoveInput noInput;

    Player normal(Vec3(0, 60, 0));
    for (int i = 0; i < (int)(29.9 * 60); i++) normal.update(1.0 / 60.0, w, noInput);
    bool noDrainBeforeInterval = normal.hunger == normal.maxHunger;
    for (int i = 0; i < (int)(0.2 * 60); i++) normal.update(1.0 / 60.0, w, noInput);
    bool drainsAtNormalRate = normal.hunger == normal.maxHunger - 1;

    Player boosted(Vec3(0, 60, 0));
    boosted.hungerBoostTimer = 100.0; // comfortably longer than this test needs
    for (int i = 0; i < 15 * 60; i++) boosted.update(1.0 / 60.0, w, noInput);
    bool drainsAtBoostedRate = boosted.hunger == boosted.maxHunger - 1;

    Player expiring(Vec3(0, 60, 0));
    expiring.hungerBoostTimer = 5.0;
    for (int i = 0; i < (int)(5.2 * 60); i++) expiring.update(1.0 / 60.0, w, noInput);
    bool boostExpiresOnItsOwn = expiring.hungerBoostTimer == 0;

    bool ok = noDrainBeforeInterval && drainsAtNormalRate && drainsAtBoostedRate && boostExpiresOnItsOwn;
    check(ok, "sleep_hunger_boost_doubles_drain_rate");
    if (!ok) {
      std::fprintf(f, "  (noDrain=%d normalDrain=%d/%d boostedDrain=%d/%d expires=%d/%.2f)\n",
                   noDrainBeforeInterval, drainsAtNormalRate, normal.hunger,
                   drainsAtBoostedRate, boosted.hunger, boostExpiresOnItsOwn, expiring.hungerBoostTimer);
    }
  }

  // named save/load roundtrip + listing
  SaveState s;
  s.hasPlayer = true;
  s.x = 1.5; s.y = 30.25; s.z = -7.5; s.yaw = 0.5; s.pitch = -0.25;
  s.seed = 777;
  s.selectedSlot = 3;
  s.hotbarCounts = { 1, 2, 3, 4, 5, 6, 0, 0, 7, 8 };
  s.edits = { { 3, 20, 3, BLOCK_STONE }, { -5, 21, 9, BLOCK_AIR } };
  saveGame(s, "selftest");
  SaveState l;
  bool loadOk = loadGame(l, "selftest");
  check(loadOk && l.hasPlayer && l.x == s.x && l.y == s.y && l.z == s.z &&
            l.yaw == s.yaw && l.pitch == s.pitch && l.selectedSlot == 3 &&
            l.seed == 777 &&
            l.hotbarCounts == s.hotbarCounts && l.edits.size() == 2 &&
            l.edits[1].x == -5 && l.edits[1].id == BLOCK_AIR,
        "save_load_roundtrip");
  bool listed = false;
  for (const SaveInfo& info : listSaves()) {
    if (info.name == "selftest") listed = true;
  }
  check(listed, "list_saves");
  check(sanitizeSaveName("  my/wo:rld*1  ") == "myworld1" &&
            sanitizeSaveName("///") == "",
        "sanitize_save_name");
  check(saveExists("selftest"), "save_exists");
  check(deleteSave("selftest") && !saveExists("selftest"), "delete_save");

  // settings roundtrip (restore the user's real settings afterwards)
  Settings orig = loadSettings();
  Settings st;
  st.sensitivity = 1.7;
  st.renderDistance = 6;
  st.resolutionW = 2560;
  st.resolutionH = 1440;
  st.displayMode = 1;
  st.thirdPerson = true;
  st.characterType = 1;
  saveSettings(st);
  Settings lt = loadSettings();
  check(std::abs(lt.sensitivity - 1.7) < 1e-9 && lt.renderDistance == 6 &&
            lt.resolutionW == 2560 && lt.resolutionH == 1440 &&
            lt.displayMode == 1 && lt.thirdPerson && lt.characterType == 1,
        "settings_roundtrip");
  saveSettings(orig);

  // the seed controls terrain: same seed = same chunk, new seed = new map
  {
    setWorldSeed(1337);
    auto ca = generateChunk(2, 3);
    setWorldSeed(424242);
    auto cb = generateChunk(2, 3);
    setWorldSeed(1337);
    auto cc = generateChunk(2, 3);
    check(ca->blocks == cc->blocks && ca->blocks != cb->blocks, "seed_controls_terrain");
  }

  // snow stays in a small far ring on every seed; the center never freezes
  {
    bool ok = true;
    const uint32_t seeds[] = { 1337, 99, 424242 };
    for (uint32_t s : seeds) {
      setWorldSeed(s);
      int b, sy;
      columnInfoAt(WORLD_RADIUS - 8, 0, b, sy);
      ok = ok && b == 3;
      columnInfoAt(-(WORLD_RADIUS - 8), 0, b, sy);
      ok = ok && b == 3;
      columnInfoAt(0, WORLD_RADIUS - 8, b, sy);
      ok = ok && b == 3;
      for (int x = -120; x <= 120; x += 40) {
        for (int z = -120; z <= 120; z += 40) {
          columnInfoAt(x, z, b, sy);
          if (b == 3) ok = false;
        }
      }
    }
    setWorldSeed(1337);
    check(ok, "snow_ring_far_and_small");
  }

  // Canyons are holes in the land: a carved floor well below its own rim.
  {
    int bestDrop = 0;
    for (int x = -WORLD_RADIUS + 16; x < WORLD_RADIUS - 16; x += 4) {
      for (int z = -WORLD_RADIUS + 16; z < WORLD_RADIUS - 16; z += 4) {
        if (!canyonCutAt(x, z)) continue;
        int b, fy;
        columnInfoAt(x, z, b, fy);
        int rim = fy;
        for (int d = 4; d <= 16; d += 4) {
          int bb, sy;
          columnInfoAt(x + d, z, bb, sy); rim = std::max(rim, sy);
          columnInfoAt(x - d, z, bb, sy); rim = std::max(rim, sy);
          columnInfoAt(x, z + d, bb, sy); rim = std::max(rim, sy);
          columnInfoAt(x, z - d, bb, sy); rim = std::max(rim, sy);
        }
        bestDrop = std::max(bestDrop, rim - fy);
      }
    }
    check(bestDrop >= 10, "canyon_carved_below_land");
  }

  // Canyon walls are terraced (stepped inward like an inverted pyramid),
  // not a single vertical drop: crossing one must pass several distinct
  // height plateaus rather than falling straight to the floor.
  {
    int bestSteps = 0;
    for (int z = -WORLD_RADIUS + 16; z < WORLD_RADIUS - 16 && bestSteps < 4; z += 8) {
      for (int x = -WORLD_RADIUS + 16; x < WORLD_RADIUS - 48; x += 8) {
        if (!canyonCutAt(x, z)) continue;
        // walk out of the gorge and count distinct descending levels
        std::vector<int> levels;
        int prev = -1;
        for (int d = 0; d <= 40; d++) {
          int bb, sy;
          columnInfoAt(x + d, z, bb, sy);
          if (sy != prev) {
            levels.push_back(sy);
            prev = sy;
          }
        }
        // count runs where height stays flat for >= 2 columns (a terrace)
        int terraces = 0, run = 1, last = -999;
        for (int d = 0; d <= 40; d++) {
          int bb, sy;
          columnInfoAt(x + d, z, bb, sy);
          if (sy == last) {
            run++;
          } else {
            if (run >= 2 && last != -999) terraces++;
            run = 1;
            last = sy;
          }
        }
        bestSteps = std::max(bestSteps, terraces);
        if (bestSteps >= 4) break;
      }
    }
    check(bestSteps >= 4, "canyon_terraced_walls");
  }

  // mountains: inner peaks are capped low (rock), the far ring holds the
  // tallest (snow) — sampled max heights must respect the ordering
  {
    // Mountains trend taller with distance. Compared as the mean of each
    // band's tallest peaks rather than a single max: whether any one band
    // happens to contain a massif is down to noise, so a lone max is a
    // brittle probe. The landmark peak is excluded — it is a deliberate
    // one-off exception to the trend.
    bool ordered = true;
    double innerTotal = 0, outerTotal = 0;
    int seedCount = 0;
    const uint32_t seeds[] = { 1337, 7, 99, 424242, 20260727 };
    for (uint32_t s : seeds) {
      setWorldSeed(s);
      double lx, lz;
      landmarkPosition(lx, lz);
      std::vector<int> inner, outer;
      int maxInner = 0;
      for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS; x += 4) {
        for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS; z += 4) {
          if (std::hypot(x - lx, z - lz) < 60) continue; // skip the landmark
          double r = std::max(std::abs((double)x), std::abs((double)z)) / WORLD_RADIUS;
          int b, sy;
          columnInfoAt(x, z, b, sy);
          if (sy <= SEA_LEVEL) continue; // mountains are a land feature
          if (r < 0.45) {
            inner.push_back(sy);
            maxInner = std::max(maxInner, sy);
          } else if (r > 0.85) {
            outer.push_back(sy);
          }
        }
      }
      auto topMean = [](std::vector<int>& v) {
        std::sort(v.begin(), v.end(), std::greater<int>());
        int n = std::min<int>(30, (int)v.size());
        if (n == 0) return 0.0;
        double sum = 0;
        for (int i = 0; i < n; i++) sum += v[i];
        return sum / n;
      };
      // Per seed, the hard design bound: inner peaks stay in the rock tier.
      if (maxInner > 33) ordered = false;
      innerTotal += topMean(inner);
      outerTotal += topMean(outer);
      seedCount++;
    }
    setWorldSeed(1337);
    // The height trend itself is statistical — whether any single band of
    // one seed happens to contain a massif is down to noise — so it is
    // asserted across seeds rather than on each individually.
    double innerAvg = seedCount ? innerTotal / seedCount : 0;
    double outerAvg = seedCount ? outerTotal / seedCount : 0;
    check(ordered && outerAvg > innerAvg + 3.0, "mountains_taller_farther");
  }

  // Every world has one very tall landmark mountain, banded grass -> rock
  // -> snow from bottom to top, and it is the highest point on the map.
  {
    bool ok = true;
    const uint32_t seeds[] = { 1337, 7, 99, 424242, 20260727 };
    for (uint32_t s : seeds) {
      setWorldSeed(s);
      double lx, lz;
      landmarkPosition(lx, lz);
      int b, peak;
      columnInfoAt((int)lx, (int)lz, b, peak);
      if (peak < 40 || b != 0) ok = false; // very tall, plains-based

      // Walk down the flanks: the exposed blocks must run snow -> rock ->
      // grass, i.e. a snow cap over a rocky middle over a grassy base.
      // Checked on 8 rays and satisfied by any of them — neighbouring
      // terrain can be high in a given direction, which is not a defect.
      bool capIsSnow = surfaceBlockAt((int)lx, (int)lz) == BLOCK_SNOW;
      bool banded = false;
      for (int dir = 0; dir < 8 && !banded; dir++) {
        double a = dir * 0.7853981634;
        double ux = std::cos(a), uz = std::sin(a);
        int firstStone = -1, firstGrass = -1;
        for (int d = 0; d <= 70; d += 2) {
          uint8_t s = surfaceBlockAt((int)(lx + ux * d), (int)(lz + uz * d));
          if (s == BLOCK_STONE && firstStone < 0) firstStone = d;
          if (s == BLOCK_GRASS && firstGrass < 0 && firstStone >= 0) firstGrass = d;
        }
        if (firstStone >= 0 && firstGrass > firstStone) banded = true;
      }
      if (!capIsSnow || !banded) ok = false;

      // nothing else on the map out-tops it
      for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS; x += 8) {
        for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS; z += 8) {
          int bb, sy;
          columnInfoAt(x, z, bb, sy);
          if (sy > peak) ok = false;
        }
      }
    }
    setWorldSeed(1337);
    check(ok, "landmark_snow_mountain");
  }

  // snow also appears inland on tall peaks, not only in the border ring
  {
    bool inlandSnow = false;
    for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS && !inlandSnow; x += 4) {
      for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS; z += 4) {
        double r = std::max(std::abs((double)x), std::abs((double)z)) / WORLD_RADIUS;
        if (r > 0.7) continue; // well inside the snow ring
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (b == 0 && sy >= SNOW_LINE_TEST) { inlandSnow = true; break; }
      }
    }
    check(inlandSnow, "inland_snow_peaks");
  }

  // Ocean world: at least half the map is water, and the land is broken
  // into several masses (a main continent plus islands), not one blob.
  {
    bool ok = true;
    const uint32_t seeds[] = { 1337, 7, 99, 424242, 20260727 };
    for (uint32_t s : seeds) {
      setWorldSeed(s);
      const int STEP = 4;
      const int N = (WORLD_RADIUS * 2) / STEP;
      std::vector<uint8_t> land((size_t)N * N, 0);
      int landCells = 0;
      for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
          int b, sy;
          columnInfoAt(-WORLD_RADIUS + i * STEP, -WORLD_RADIUS + j * STEP, b, sy);
          if (sy > SEA_LEVEL) {
            land[(size_t)i * N + j] = 1;
            landCells++;
          }
        }
      }
      double waterPct = 100.0 * (N * N - landCells) / (N * N);
      if (waterPct < 50.0) ok = false;

      // count separate landmasses of a meaningful size
      std::vector<uint8_t> seen((size_t)N * N, 0);
      int sizeable = 0;
      for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
          size_t idx = (size_t)i * N + j;
          if (!land[idx] || seen[idx]) continue;
          int count = 0;
          std::vector<int> stack{ (int)idx };
          seen[idx] = 1;
          while (!stack.empty()) {
            int cur = stack.back();
            stack.pop_back();
            count++;
            int ci = cur / N, cj = cur % N;
            const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const int* d : D) {
              int ni = ci + d[0], nj = cj + d[1];
              if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
              size_t nidx = (size_t)ni * N + nj;
              if (!land[nidx] || seen[nidx]) continue;
              seen[nidx] = 1;
              stack.push_back((int)nidx);
            }
          }
          if (count >= 4) sizeable++;
        }
      }
      if (sizeable < 3) ok = false; // a continent plus at least two islands
    }
    setWorldSeed(1337);
    check(ok, "ocean_majority_with_islands");
  }

  // Trees vary in size: heights span the full range, taller trees carry
  // wider canopies, and big trees are much rarer than small ones.
  {
    struct Tree { int height; int radius; int x; int z; bool roundCrown; };
    std::vector<Tree> trees;
    for (int ccx = -6; ccx <= 6; ccx++) {
      for (int ccz = -6; ccz <= 6; ccz++) {
        auto ch = generateChunk(ccx, ccz);
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
          for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int y = 1; y < CHUNK_HEIGHT - 1; y++) {
              if (ch->getLocal(lx, y, lz) != BLOCK_WOOD) continue;
              uint8_t below = ch->getLocal(lx, y - 1, lz);
              if (below == BLOCK_WOOD) continue; // not the trunk base
              int h = 0;
              while (y + h < CHUNK_HEIGHT && ch->getLocal(lx, y + h, lz) == BLOCK_WOOD) h++;
              // This tree's own canopy: the contiguous run of leaves out
              // from the trunk along each axis at crown level, taking the
              // smallest. Scanning a box instead would pick up leaves from
              // neighbouring trees and report every tree as wide.
              int topY = y + h - 1;
              const int AX[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
              int reach = 99;
              for (const int* a : AX) {
                int run = 0;
                for (int step = 1; step <= 5; step++) {
                  int nx = lx + a[0] * step, nz = lz + a[1] * step;
                  if (!Chunk::inBounds(nx, topY, nz)) break;
                  if (ch->getLocal(nx, topY, nz) != BLOCK_LEAVES) break;
                  run = step;
                }
                reach = std::min(reach, run);
              }
              if (reach == 99) reach = 0;
              // A rounded crown never fills its bounding square: the
              // diagonal corner at the layer's reach must be clear. A cube
              // canopy (the old small-tree shape) would have it filled.
              bool cornerClear = true;
              if (reach >= 1) {
                const int C[4][2] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
                for (const int* c : C) {
                  int nx = lx + c[0] * reach, nz = lz + c[1] * reach;
                  if (!Chunk::inBounds(nx, topY, nz)) continue;
                  if (ch->getLocal(nx, topY, nz) == BLOCK_LEAVES) cornerClear = false;
                }
              }
              trees.push_back({ h, reach, ccx * CHUNK_SIZE + lx, ccz * CHUNK_SIZE + lz,
                                cornerClear });
              break;
            }
          }
        }
      }
    }

    int minH = 99, maxH = 0, small = 0, big = 0;
    double smallReach = 0, bigReach = 0;
    for (const Tree& t : trees) {
      minH = std::min(minH, t.height);
      maxH = std::max(maxH, t.height);
      if (t.height <= 5) { small++; smallReach += t.radius; }
      if (t.height >= 11) { big++; bigReach += t.radius; }
    }
    bool spans = !trees.empty() && minH <= TREE_MIN_HEIGHT + 1 && maxH >= TREE_MAX_HEIGHT - 2;
    bool rarer = big > 0 && small > big * 2;          // big trees clearly scarcer
    bool wider = big > 0 && small > 0 &&
                 (bigReach / big) > (smallReach / small) + 1.0; // taller = leafier
    bool mostlySmall = !trees.empty() && small * 10 >= (int)trees.size() * 6; // >= 60%

    // every crown is rounded, small trees included (they used to be cubes)
    int cubic = 0, smallCubic = 0;
    for (const Tree& t : trees) {
      if (t.roundCrown) continue;
      cubic++;
      if (t.height <= 5) smallCubic++;
    }
    check(cubic == 0, "tree_crowns_rounded");
    if (cubic) {
      std::fprintf(f, "  (%d square crowns, %d of them small)\n", cubic, smallCubic);
    }
    check(spans && rarer && wider && mostlySmall, "tree_size_variety");
    if (!(spans && rarer && wider && mostlySmall)) {
      std::fprintf(f, "  (trees=%d h=%d..%d small=%d big=%d reach small=%.2f big=%.2f)\n",
                   (int)trees.size(), minH, maxH, small, big,
                   small ? smallReach / small : 0.0, big ? bigReach / big : 0.0);
    }

    // Small trees grow in thickets; big ones stand alone. Measured as the
    // average count of other trees within 10 blocks.
    double smallNear = 0, bigNear = 0;
    int smallCount = 0, bigCount = 0;
    for (const Tree& a : trees) {
      int nearby = 0; // NB: `near` is a legacy macro from windows.h
      for (const Tree& b : trees) {
        if (&a == &b) continue;
        if (std::abs(a.x - b.x) <= 10 && std::abs(a.z - b.z) <= 10) nearby++;
      }
      if (a.height <= 5) { smallNear += nearby; smallCount++; }
      else if (a.height >= 8) { bigNear += nearby; bigCount++; }
    }
    double smallAvg = smallCount ? smallNear / smallCount : 0;
    double bigAvg = bigCount ? bigNear / bigCount : 0;
    bool clustered = smallCount > 0 && bigCount > 0 && smallAvg > bigAvg * 1.3 && smallAvg > 2.0;
    check(clustered, "small_trees_cluster");
    if (!clustered) {
      std::fprintf(f, "  (small n=%d avg %.2f | big n=%d avg %.2f)\n",
                   smallCount, smallAvg, bigCount, bigAvg);
    }
  }

  // all four biomes and a frozen-ocean (iceberg) zone exist inside the border
  {
    bool seen[4] = {};
    bool polarOcean = false; // open water in the snow biome (iceberg zone)
    for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS; x += 16) {
      for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS; z += 16) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (b >= 0 && b < 4) seen[b] = true;
        if (b == 3 && sy <= SEA_LEVEL - 2) polarOcean = true;
      }
    }
    check(seen[0] && seen[1] && seen[2] && seen[3], "biomes_present");
    check(polarOcean, "polar_ocean_present");
  }

  // Fish: spawn a real variety of kinds and sizes over many rounds, and a
  // fish placed swimming in open water never wanders out of it. Uses its
  // own fresh World (not the shared `w` above) — moving `w`'s loaded-chunk
  // window to a distant test column would silently invalidate every edit
  // later tests still depend on being there.
  {
    World fw;
    int waterX = 0, waterZ = 0, waterSurfaceY = 0;
    bool foundWater = false;
    for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS && !foundWater; x += 16) {
      for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS && !foundWater; z += 16) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (sy <= SEA_LEVEL - 4) { waterX = x; waterZ = z; waterSurfaceY = sy; foundWater = true; }
      }
    }
    fw.updateLoadedChunks(waterX, waterZ);

    std::vector<Fish> fishes;
    int speciesSeen[FISH_SPECIES_COUNT] = {};
    double minScaleSeen = 999, maxScaleSeen = -999;
    for (int iter = 0; iter < 200; iter++) {
      fishes.clear();
      maintainFishSpawns(fw, fishes, waterX, waterZ, 6);
      for (const Fish& f : fishes) {
        speciesSeen[f.species]++;
        minScaleSeen = std::min(minScaleSeen, f.scale);
        maxScaleSeen = std::max(maxScaleSeen, f.scale);
      }
    }
    int distinctSpecies = 0;
    for (int c : speciesSeen) if (c > 0) distinctSpecies++;
    bool variedKinds = distinctSpecies >= 3;
    bool variedSizes = foundWater && (maxScaleSeen - minScaleSeen) > 0.3;

    Fish testFish;
    testFish.species = FISH_COD;
    testFish.position = Vec3(waterX + 0.5, waterSurfaceY + 1.5, waterZ + 0.5);
    bool staysInWater = foundWater;
    for (int i = 0; i < 600 && staysInWater; i++) {
      updateFish(testFish, fw, 1.0 / 60.0, Vec3(0, 0, 0));
      int fx = (int)std::floor(testFish.position.x), fy = (int)std::floor(testFish.position.y),
          fz = (int)std::floor(testFish.position.z);
      if (!isWater(fw.getBlock(fx, fy, fz))) staysInWater = false;
    }

    bool ok = foundWater && variedKinds && variedSizes && staysInWater;
    check(ok, "fish_spawn_variety_and_stays_in_water");
    if (!ok) {
      std::fprintf(f, "  (foundWater=%d kinds=%d sizeSpread=%.2f staysInWater=%d)\n",
                   foundWater, distinctSpecies, maxScaleSeen - minScaleSeen, staysInWater);
    }
  }

  // Boats: placeable on water, refused on land or on top of another boat;
  // drives forward while occupied and on water, sits inert while beached.
  // Each check gets its own fresh World, same lesson as the fish test above
  // — reloading one shared World's chunks at a distant column mid-test would
  // silently invalidate whatever the earlier part of the test still needs.
  {
    World waterWorld;
    int waterX = 0, waterZ = 0;
    bool foundWater = false;
    for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS && !foundWater; x += 16) {
      for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS && !foundWater; z += 16) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (sy <= SEA_LEVEL - 4) { waterX = x; waterZ = z; foundWater = true; }
      }
    }
    waterWorld.updateLoadedChunks(waterX, waterZ);

    std::vector<Boat> boats;
    double surfaceY = 0;
    bool placesOnWater = foundWater && canPlaceBoatAt(waterWorld, waterX, waterZ, boats, surfaceY);
    bool refusesOverlap = false;
    if (placesOnWater) {
      Boat parked;
      parked.position = Vec3(waterX + 0.5, surfaceY, waterZ + 0.5);
      boats.push_back(parked);
      double dummyY;
      refusesOverlap = !canPlaceBoatAt(waterWorld, waterX, waterZ, boats, dummyY);
    }

    World landWorld;
    int landX = 0, landZ = 0, landSurfaceY = 0;
    bool foundLand = false;
    for (int x = -WORLD_RADIUS + 8; x < WORLD_RADIUS && !foundLand; x += 16) {
      for (int z = -WORLD_RADIUS + 8; z < WORLD_RADIUS && !foundLand; z += 16) {
        int b, sy;
        columnInfoAt(x, z, b, sy);
        if (sy > SEA_LEVEL + 2) { landX = x; landZ = z; landSurfaceY = sy; foundLand = true; }
      }
    }
    landWorld.updateLoadedChunks(landX, landZ);
    double landDummyY;
    std::vector<Boat> noBoats;
    bool refusesLand = foundLand && !canPlaceBoatAt(landWorld, landX, landZ, noBoats, landDummyY);

    // Driving: moves forward along its facing while occupied and on water.
    Boat driving;
    driving.position = Vec3(waterX + 0.5, surfaceY, waterZ + 0.5);
    driving.occupied = true;
    double startZ = driving.position.z;
    for (int i = 0; i < 60; i++) updateBoat(driving, waterWorld, 1.0 / 60.0, 0.0, 1);
    bool drivesOnWater = std::abs(driving.position.z - startZ) > 0.5;

    // Beached: sits inert even with the same forward input, since it isn't
    // over water.
    Boat beached;
    beached.position = Vec3(landX + 0.5, landSurfaceY + 1.0, landZ + 0.5);
    beached.occupied = true;
    double beachedStartX = beached.position.x, beachedStartZ = beached.position.z;
    for (int i = 0; i < 60; i++) updateBoat(beached, landWorld, 1.0 / 60.0, 0.0, 1);
    bool staysBeached = std::abs(beached.position.x - beachedStartX) < 1e-6 &&
                        std::abs(beached.position.z - beachedStartZ) < 1e-6;

    // nearestBoat skips occupied ones.
    std::vector<Boat> mixedBoats;
    Boat free; free.position = Vec3(waterX + 0.5, surfaceY, waterZ + 0.5); free.occupied = false;
    Boat taken; taken.position = Vec3(waterX + 0.5, surfaceY, waterZ + 1.5); taken.occupied = true;
    mixedBoats.push_back(taken);
    mixedBoats.push_back(free);
    bool skipsOccupied = nearestBoat(mixedBoats, free.position) == 1;

    bool ok = placesOnWater && refusesOverlap && refusesLand && drivesOnWater && staysBeached &&
              skipsOccupied;
    check(ok, "boat_placement_driving_and_beaching");
    if (!ok) {
      std::fprintf(f, "  (placesOnWater=%d refusesOverlap=%d refusesLand=%d drivesOnWater=%d "
                      "staysBeached=%d skipsOccupied=%d)\n",
                   placesOnWater, refusesOverlap, refusesLand, drivesOnWater, staysBeached,
                   skipsOccupied);
    }
  }

  // world border: no chunks/terrain beyond it, and it blocks movement
  {
    World wb;
    wb.renderDistance = 2;
    wb.updateLoadedChunks(WORLD_RADIUS - 2, 0);
    check(wb.getChunk(WORLD_RADIUS / CHUNK_SIZE, 0) == nullptr &&
              wb.getBlock(WORLD_RADIUS + 1, 20, 0) == BLOCK_AIR,
          "border_no_terrain_outside");
    check(boxCollides(wb, WORLD_RADIUS - 0.1, 30, 0.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
              !boxCollides(wb, WORLD_RADIUS - 2.0, 45, 0.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT),
          "border_blocks_movement");
  }

  // Ground grass: grows only on grassland, in patches — some fields carry
  // it, others are bare — and never blocks movement.
  {
    int grassBlocks = 0, withPlant = 0, bare = 0, wrongHost = 0;
    int lushCells = 0, bareCells = 0; // 16x16 samples, to prove patchiness
    for (int ccx = -5; ccx <= 5; ccx++) {
      for (int ccz = -5; ccz <= 5; ccz++) {
        auto ch = generateChunk(ccx, ccz);
        int cellPlants = 0, cellGrass = 0;
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
          for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int y = 1; y < CHUNK_HEIGHT - 1; y++) {
              uint8_t here = ch->getLocal(lx, y, lz);
              if (here == BLOCK_TALL_GRASS) {
                withPlant++;
                cellPlants++;
                if (ch->getLocal(lx, y - 1, lz) != BLOCK_GRASS) wrongHost++;
              } else if (here == BLOCK_GRASS && ch->getLocal(lx, y + 1, lz) == BLOCK_AIR) {
                grassBlocks++;
                bare++;
                cellGrass++;
              }
            }
          }
        }
        if (cellGrass + cellPlants > 40) {
          // Target density is ~15% within a meadow, so a cell counts as lush
          // when plants cover more than ~1/12 of its surface grass.
          if (cellPlants > (cellGrass + cellPlants) / 12) lushCells++;
          else if (cellPlants == 0) bareCells++;
        }
      }
    }
    bool present = withPlant > 50;
    bool patchy = lushCells > 0 && bareCells > 0; // both lush and bare ground
    bool rooted = wrongHost == 0;                 // only ever on grass blocks
    check(present && patchy && rooted && !isSolid(BLOCK_TALL_GRASS),
          "ground_grass_patches");
    if (!(present && patchy && rooted)) {
      std::fprintf(f, "  (plants=%d bare=%d lushCells=%d bareCells=%d wrongHost=%d)\n",
                   withPlant, bare, lushCells, bareCells, wrongHost);
    }
  }

  // Coal: black seams buried in the stone. Three separate promises — it is
  // there at all, it never appears within COAL_MIN_DEPTH of the surface (so
  // you must dig for it), and it fills roughly a fifth of the underground.
  {
    long long stoneOrCoal = 0, coal = 0;
    int tooShallow = 0, aboveSurface = 0, nonStoneReplaced = 0;
    for (int cx = -2; cx <= 2; cx++) {
      for (int cz = -2; cz <= 2; cz++) {
        std::unique_ptr<Chunk> ch = generateChunk(cx, cz);
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
          for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int wx = cx * CHUNK_SIZE + lx, wz = cz * CHUNK_SIZE + lz;
            int biome, surfaceY;
            columnInfoAt(wx, wz, biome, surfaceY);
            for (int y = 1; y <= surfaceY; y++) {
              uint8_t b = ch->getLocal(lx, y, lz);
              if (b == BLOCK_STONE || b == BLOCK_COAL) stoneOrCoal++;
              if (b != BLOCK_COAL) continue;
              coal++;
              if (y > surfaceY - COAL_MIN_DEPTH) tooShallow++;
              if (y > surfaceY) aboveSurface++;
            }
            // nothing above ground may be coal either
            for (int y = surfaceY + 1; y < CHUNK_HEIGHT; y++) {
              if (ch->getLocal(lx, y, lz) == BLOCK_COAL) nonStoneReplaced++;
            }
          }
        }
      }
    }
    double share = stoneOrCoal > 0 ? (double)coal / (double)stoneOrCoal : 0.0;
    bool present = coal > 1000;
    bool buried = tooShallow == 0 && aboveSurface == 0 && nonStoneReplaced == 0;
    bool aboutAFifth = share > 0.15 && share < 0.25;
    // and it behaves as a block you can dig up and carry
    bool collectable = isSolid(BLOCK_COAL) && BLOCKS[BLOCK_COAL].minable &&
                       !isWater(BLOCK_COAL) && !isPlant(BLOCK_COAL);
    // you never start with any: it has to be mined
    bool notInStartingKit = true;
    for (int i = 0; i < HOTBAR_ORDER_LEN; i++) {
      if (HOTBAR_ORDER[i] == BLOCK_COAL) notInStartingKit = false;
    }
    check(present && buried && aboutAFifth && collectable && notInStartingKit,
          "coal_seams_underground");
    std::fprintf(f, "  (coal=%lld of %lld stone = %.1f%%, shallow=%d, surface=%d)\n",
                 coal, stoneOrCoal, share * 100.0, tooShallow, nonStoneReplaced);
  }

  // Craft list: every recipe must match its own pattern, no two recipes may
  // claim the same grid, and an empty grid must craft nothing.
  {
    bool selfMatch = true, unique = true, named = true;
    for (int i = 0; i < CRAFT_RECIPE_COUNT; i++) {
      const Recipe& r = CRAFT_RECIPES[i];
      uint8_t grid[9];
      for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
          char c = r.rows[row][col];
          uint8_t id = BLOCK_AIR;
          if (c != '.') {
            for (const Recipe::KeyEntry& k : r.keys) {
              if (k.key == c) id = k.item;
            }
            if (id == BLOCK_AIR) named = false; // pattern uses an unmapped key
          }
          grid[row * 3 + col] = id;
        }
      }
      const Recipe* hit = findRecipe(grid);
      if (hit != &r) {
        // Another recipe claimed this grid first, or it matched nothing.
        if (hit == nullptr) selfMatch = false;
        else unique = false;
      }
      // every output is a crafted good in a sane quantity
      if (r.outputCount <= 0 || r.output < BLOCK_TYPE_COUNT ||
          r.output >= CRAFT_ITEM_COUNT) {
        named = false;
      }
    }
    uint8_t empty[9];
    for (int i = 0; i < 9; i++) empty[i] = BLOCK_AIR;
    bool emptyMakesNothing = findRecipe(empty) == nullptr;

    check(selfMatch && unique && named && emptyMakesNothing && CRAFT_RECIPE_COUNT >= 20,
          "craft_recipes");
    if (!(selfMatch && unique && named && emptyMakesNothing)) {
      std::fprintf(f, "  (selfMatch=%d unique=%d named=%d empty=%d count=%d)\n",
                   selfMatch, unique, named, emptyMakesNothing, CRAFT_RECIPE_COUNT);
    }
  }

  // Equippable tools: every tiered tool/weapon is registered now, not just
  // the two now-removed generic pickaxe/axe items, and each recipe's own
  // shape still produces exactly the right one.
  {
    bool genericLookup = isToolItem(ITEM_WOOD_PICKAXE) && isToolItem(ITEM_WOOD_AXE) &&
                          !isToolItem(BLOCK_DIRT) && !isToolItem(ITEM_PLANKS);
    // the wood pickaxe's own shape: 3 planks across the top, a two-stick handle
    uint8_t grid[9] = {};
    grid[0] = ITEM_PLANKS; grid[1] = ITEM_PLANKS; grid[2] = ITEM_PLANKS;
    grid[4] = ITEM_STICK; grid[7] = ITEM_STICK;
    const Recipe* hit = findRecipe(grid);
    bool pickaxeRecipe = hit && hit->output == ITEM_WOOD_PICKAXE && hit->outputCount == 1;
    // the axe: same two ingredient types, arranged differently — shape (not
    // just totals) is what tells them apart
    uint8_t axeGrid[9] = {};
    axeGrid[0] = ITEM_PLANKS; axeGrid[1] = ITEM_PLANKS; axeGrid[3] = ITEM_PLANKS;
    axeGrid[4] = ITEM_STICK; axeGrid[7] = ITEM_STICK;
    const Recipe* axeHit = findRecipe(axeGrid);
    bool axeRecipe = axeHit && axeHit->output == ITEM_WOOD_AXE && axeHit->outputCount == 1;
    // Each equippable tool needs its own silhouette, or they are one tool
    // wearing two names. The 3D model and the slot icon are separate art, so
    // both are checked: a distinct head shape, and distinct atlas tiles.
    const ToolVisual* pv = toolVisualFor(ITEM_WOOD_PICKAXE);
    const ToolVisual* av = toolVisualFor(ITEM_WOOD_AXE);
    bool distinctShapes = pv && av && pv->shape != av->shape;
    bool distinctIcons = false;
    if (pv && av) {
      int pTile = craftItemTile(ITEM_WOOD_PICKAXE), aTile = craftItemTile(ITEM_WOOD_AXE);
      const Atlas& at = buildTextureAtlas();
      if (pTile >= 0 && aTile >= 0 && pTile != aTile) {
        for (int y = 0; y < ATLAS_TILE_PX && !distinctIcons; y++) {
          for (int x = 0; x < ATLAS_TILE_PX; x++) {
            size_t pi = (size_t)(y * at.width + pTile * ATLAS_TILE_PX + x) * 4;
            size_t ai = (size_t)(y * at.width + aTile * ATLAS_TILE_PX + x) * 4;
            if (std::memcmp(&at.pixels[pi], &at.pixels[ai], 4) != 0) {
              distinctIcons = true;
              break;
            }
          }
        }
      }
    }
    bool ok = genericLookup && pickaxeRecipe && axeRecipe &&
              distinctShapes && distinctIcons;
    check(ok, "tool_equip");
    if (!ok) {
      std::fprintf(f, "  (generic=%d pickaxe=%d axe=%d shapes=%d icons=%d)\n",
                   genericLookup, pickaxeRecipe, axeRecipe, distinctShapes, distinctIcons);
    }
  }

  // Hand-drawn Sword: 3 stone + 1 stick (heavier than the standard Stone
  // Sword's 2 stone, for +1 attack power), with its own art\sword.png icon
  // instead of the procedural drawing every other tool gets.
  {
    uint8_t grid[9] = {};
    grid[0] = BLOCK_STONE; grid[3] = BLOCK_STONE; grid[6] = BLOCK_STONE; grid[7] = ITEM_STICK;
    const Recipe* hit = findRecipe(grid);
    bool recipeOk = hit && hit->output == ITEM_SWORD && hit->outputCount == 1;

    // findRecipeByCount is what the real crafting UI actually uses (see
    // craftOutcome) — matches on totals regardless of layout, so it must
    // agree the same 3 stone + 1 stick makes a Sword and nothing else does.
    uint8_t items[9] = { BLOCK_STONE, ITEM_STICK };
    int counts[9] = { 3, 1 };
    const Recipe* byCount = findRecipeByCount(items, counts);
    bool countOk = byCount && byCount->output == ITEM_SWORD;

    bool powerOk = attackPower(ITEM_SWORD) == 3.0;
    bool equippable = isToolItem(ITEM_SWORD);
    const ToolVisual* sv = toolVisualFor(ITEM_SWORD);
    bool sameHeldGeometryAsStoneSword =
        sv && toolVisualFor(ITEM_STONE_SWORD) && sv->shape == toolVisualFor(ITEM_STONE_SWORD)->shape;

    // The slot icon must actually be the supplied artwork, not a blank tile
    // or a silent fallback to procedural art.
    const GeneratedSprite* sprite = generatedSpriteNamed("sword");
    bool artSupplied = sprite && sprite->rgba;
    bool iconIsTheArt = false;
    int tile = craftItemTile(ITEM_SWORD);
    if (artSupplied && tile == TILE_SWORD) {
      const Atlas& at = buildTextureAtlas();
      iconIsTheArt = true;
      // The atlas is GL-order (row 0 = bottom); the sprite is canvas-order
      // (row 0 = top) — see TileCtx::putRGBA's flip in textures.cpp.
      for (int y = 0; y < ATLAS_TILE_PX && iconIsTheArt; y++) {
        int atlasRow = ATLAS_TILE_PX - 1 - y;
        for (int x = 0; x < ATLAS_TILE_PX; x++) {
          size_t ai = (size_t)(atlasRow * at.width + tile * ATLAS_TILE_PX + x) * 4;
          size_t si = (size_t)(y * ATLAS_TILE_PX + x) * 4;
          if (std::memcmp(&at.pixels[ai], &sprite->rgba[si], 4) != 0) iconIsTheArt = false;
        }
      }
    }

    bool ok = recipeOk && countOk && powerOk && equippable && sameHeldGeometryAsStoneSword &&
              artSupplied && iconIsTheArt;
    check(ok, "hand_drawn_sword_craft_item");
    if (!ok) {
      std::fprintf(f,
                   "  (recipe=%d count=%d power=%d equip=%d heldGeom=%d art=%d icon=%d)\n",
                   recipeOk, countOk, powerOk, equippable, sameHeldGeometryAsStoneSword, artSupplied,
                   iconIsTheArt);
    }
  }

  // Craft-tab wiring, end to end through the real UI entry points: put wood
  // and stone in the grid, press the actual Craft button, and check a pickaxe
  // lands in the player's stuff and the ingredients were consumed. Driving
  // onMouseDown (rather than calling craftOutcome directly) is the point —
  // the bug this guards against was the UI never consulting findRecipe at
  // all, which a recipe-table-only test cannot see.
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    for (Hotbar::Slot& s : hotbar.slots) { s.blockId = -1; s.count = 0; }
    Inventory inv;
    inv.tab = INV_TAB_CRAFT;
    inv.craft[0] = { ITEM_PLANKS, 1 };
    inv.craft[1] = { ITEM_PLANKS, 1 };
    inv.craft[2] = { ITEM_PLANKS, 1 };
    inv.craft[4] = { ITEM_STICK, 1 };
    inv.craft[7] = { ITEM_STICK, 1 };

    // The preview the player sees in the result slot before clicking.
    Hotbar::Slot preview;
    bool previewed = craftOutcome(inv.craft, preview) &&
                     preview.blockId == ITEM_WOOD_PICKAXE && preview.count == 1;

    double bx, by, bw, bh;
    bool haveBtn = inv.craftButtonRect(W, H, bx, by, bw, bh);
    if (haveBtn) inv.onMouseDown(hotbar, bx + bw / 2, by + bh / 2, false, W, H);

    int pickaxes = 0;
    for (const Hotbar::Slot& s : hotbar.slots) {
      if (s.blockId == ITEM_WOOD_PICKAXE) pickaxes += s.count;
    }
    for (const Hotbar::Slot& s : inv.main) {
      if (s.blockId == ITEM_WOOD_PICKAXE) pickaxes += s.count;
    }
    bool gridConsumed = true;
    for (int i : { 0, 1, 2, 4, 7 }) {
      if (inv.craft[i].blockId >= 0) gridConsumed = false;
    }
    bool ok = previewed && haveBtn && pickaxes == 1 && gridConsumed;
    check(ok, "craft_click_makes_pickaxe");
    if (!ok) {
      std::fprintf(f, "  (previewed=%d btn=%d pickaxes=%d consumed=%d)\n",
                   previewed, haveBtn, pickaxes, gridConsumed);
    }
  }

  // The same thing again, but reaching the grid the way a player does: DRAG
  // wood and stone out of the backpack into the crafting cells, rather than
  // assigning to inv.craft[]. A direct-assignment test cannot see bugs in
  // the drag path itself, which is the half the player actually touches.
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    for (Hotbar::Slot& s : hotbar.slots) { s.blockId = -1; s.count = 0; }
    Inventory inv;
    inv.tab = INV_TAB_CRAFT;
    inv.main[0] = { ITEM_PLANKS, 1 };
    inv.main[1] = { ITEM_PLANKS, 1 };
    inv.main[2] = { ITEM_PLANKS, 1 };
    inv.main[3] = { ITEM_STICK, 1 };
    inv.main[4] = { ITEM_STICK, 1 };

    // Drag backpack slot `from` onto craft cell `to`: press on the source,
    // release over the destination.
    auto drag = [&](int from, int to) {
      double sx, sy, sw, sh, dx, dy, dw, dh;
      if (!inv.mainSlotRect(from, W, H, sx, sy, sw, sh)) return false;
      if (!inv.craftSlotRect(to, W, H, dx, dy, dw, dh)) return false;
      inv.onMouseDown(hotbar, sx + sw / 2, sy + sh / 2, false, W, H);
      inv.onMouseUp(hotbar, dx + dw / 2, dy + dh / 2, false, W, H);
      return true;
    };
    // the wood pickaxe's own shape: 3 planks across the top, 2 sticks below
    bool dragged = drag(0, 0) && drag(1, 1) && drag(2, 2) && drag(3, 4) && drag(4, 7);
    bool landed = inv.craft[0].blockId == ITEM_PLANKS && inv.craft[1].blockId == ITEM_PLANKS &&
                  inv.craft[2].blockId == ITEM_PLANKS && inv.craft[4].blockId == ITEM_STICK &&
                  inv.craft[7].blockId == ITEM_STICK;

    Hotbar::Slot preview;
    bool previewed = craftOutcome(inv.craft, preview) && preview.blockId == ITEM_WOOD_PICKAXE;

    double bx, by, bw, bh;
    bool haveBtn = inv.craftButtonRect(W, H, bx, by, bw, bh);
    if (haveBtn) inv.onMouseDown(hotbar, bx + bw / 2, by + bh / 2, false, W, H);

    int pickaxes = 0;
    for (const Hotbar::Slot& s : hotbar.slots) {
      if (s.blockId == ITEM_WOOD_PICKAXE) pickaxes += s.count;
    }
    for (const Hotbar::Slot& s : inv.main) {
      if (s.blockId == ITEM_WOOD_PICKAXE) pickaxes += s.count;
    }
    bool ok = dragged && landed && previewed && pickaxes == 1;
    check(ok, "craft_drag_flow");
    if (!ok) {
      std::fprintf(f, "  (dragged=%d landed=%d craft0=%d craft1=%d previewed=%d pickaxes=%d)\n",
                   dragged, landed, inv.craft[0].blockId, inv.craft[1].blockId,
                   previewed, pickaxes);
    }
  }

  // Double-click shortcut: two quick clicks on a hotbar stack while the
  // Craft tab is open drop one of it into the grid, without dragging.
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    Inventory inv;
    inv.tab = INV_TAB_CRAFT;

    int woodSlot = -1;
    for (int i = 0; i < SLOT_COUNT; i++) {
      if (hotbar.slots[i].blockId == BLOCK_WOOD) woodSlot = i;
    }
    double sx, sy, sw, sh;
    bool have = woodSlot >= 0 && inv.hotbarSlotRect(woodSlot, W, H, sx, sy, sw, sh);
    int before = have ? hotbar.slots[woodSlot].count : 0;
    if (have) {
      double cx = sx + sw / 2, cy = sy + sh / 2;
      // click, release (which puts the item back), then click again
      inv.onMouseDown(hotbar, cx, cy, false, W, H);
      inv.onMouseUp(hotbar, cx, cy, false, W, H);
      inv.onMouseDown(hotbar, cx, cy, false, W, H);
      inv.onMouseUp(hotbar, cx, cy, false, W, H);
    }
    int inGrid = 0;
    for (const Hotbar::Slot& c : inv.craft) {
      if (c.blockId == BLOCK_WOOD) inGrid += c.count;
    }
    bool moved = have && inGrid == 1 && hotbar.slots[woodSlot].count == before - 1;
    // nothing should be left stuck on the cursor
    bool handEmpty = inv.held.blockId < 0 || inv.held.count <= 0;
    check(moved && handEmpty, "craft_double_click_sends_ingredient");
    if (!(moved && handEmpty)) {
      std::fprintf(f, "  (have=%d inGrid=%d src=%d/%d held=%d)\n",
                   have, inGrid, have ? hotbar.slots[woodSlot].count : -1, before,
                   inv.held.count);
    }
  }

  // Double-clicking the SAME ingredient repeatedly must fill separate cells,
  // not pile them into one: a cell counts as a single ingredient however
  // deep it is stacked, so piling them made "3 wood + 2 coal" match nothing.
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    // Coal isn't part of the starting kit (see coal_seams_underground's own
    // notInStartingKit check) — it has to be mined, so drop some into one of
    // the hotbar's two slots past HOTBAR_ORDER_LEN that start empty, the
    // same way a real player's hotbar would look after a short dig.
    hotbar.slots[HOTBAR_ORDER_LEN] = { BLOCK_COAL, STARTING_COUNT };
    Inventory inv;
    inv.tab = INV_TAB_CRAFT;

    int woodSlot = -1, coalSlot = -1;
    for (int i = 0; i < SLOT_COUNT; i++) {
      if (hotbar.slots[i].blockId == BLOCK_WOOD) woodSlot = i;
      if (hotbar.slots[i].blockId == BLOCK_COAL) coalSlot = i;
    }
    auto doubleClick = [&](int slot) {
      double sx, sy, sw, sh;
      if (slot < 0 || !inv.hotbarSlotRect(slot, W, H, sx, sy, sw, sh)) return false;
      double cx = sx + sw / 2, cy = sy + sh / 2;
      for (int i = 0; i < 2; i++) {
        inv.onMouseDown(hotbar, cx, cy, false, W, H);
        inv.onMouseUp(hotbar, cx, cy, false, W, H);
      }
      return true;
    };
    // the campfire: wood three times, then coal twice
    bool clicked = doubleClick(woodSlot) && doubleClick(woodSlot) && doubleClick(woodSlot) &&
                   doubleClick(coalSlot) && doubleClick(coalSlot);

    int filledCells = 0, coalCells = 0;
    for (const Hotbar::Slot& c : inv.craft) {
      if (c.blockId >= 0 && c.count > 0) filledCells++;
      if (c.blockId == BLOCK_COAL && c.count > 0) coalCells++;
    }
    Hotbar::Slot outcome;
    bool makesCampfire = craftOutcome(inv.craft, outcome) && outcome.blockId == ITEM_CAMPFIRE;
    bool ok = clicked && filledCells == 5 && coalCells == 2 && makesCampfire;
    check(ok, "craft_double_click_spreads_across_cells");
    if (!ok) {
      std::fprintf(f, "  (clicked=%d filled=%d coalCells=%d campfire=%d)\n",
                   clicked, filledCells, coalCells, makesCampfire);
    }
  }

  // Placed crafted goods must behave like real blocks: solid, textured on
  // every face, and minable back. They used to render from stale UVs (the
  // stray green blocks) and refuse to be dug up, stranding them forever.
  {
    bool blocksOk = true, toolsOk = true;
    int firstBadBlock = -1, firstBadTool = -1;
    const uint8_t PLACEABLE[] = {
      ITEM_PLANKS, ITEM_CRAFTING_TABLE, ITEM_CHEST, ITEM_FURNACE,
      ITEM_WOOD_SLAB, ITEM_STONE_SLAB, ITEM_WOOD_STAIRS, ITEM_STONE_STAIRS,
      ITEM_STONE_BRICKS, ITEM_SANDSTONE, ITEM_SNOW_BLOCK, ITEM_PACKED_ICE,
      ITEM_FENCE, ITEM_DOOR, ITEM_TRAPDOOR, ITEM_LADDER,
    };
    for (uint8_t id : PLACEABLE) {
      bool good = isPlaceable(id) && isMinable(id);
      if (isPanel(id)) {
        // A panel is the other way round on purpose: it does NOT fill its
        // cell, so it must be non-solid (you walk through it) and must not
        // occlude, or the wall it hangs on would lose the face behind it.
        good = good && !isSolid(id) && isEmptyForMeshing(id);
      } else if (isStairs(id) || isChest(id) || isSlab(id) || isAnyFence(id) || isDoor(id) ||
                isTrapdoor(id) || isFurnace(id) || isTable(id)) {
        // Partial-cell shapes: unlike a panel, still solid (you can't walk
        // through any of them) — but also marked empty-for-meshing, since
        // none of them fills its cell, so a neighbour sharing the uncovered
        // part still has to draw its face.
        good = good && isSolid(id) && isEmptyForMeshing(id);
      } else {
        good = good && isSolid(id) && !isEmptyForMeshing(id);
      }
      for (int face = 0; face < 3; face++) {
        if (faceTexture(id, face) < 0) good = false; // no UVs -> stale texture
      }
      if (!good) { blocksOk = false; if (firstBadBlock < 0) firstBadBlock = id; }
    }
    // ...and a panel still has to be aim-able with the REAL raycast, or it
    // could be placed and never mined back.
    {
      w.setBlock(60, 40, 60, ITEM_LADDER);
      RaycastHit lh;
      bool aimable = raycastVoxel(w, Vec3(60.5, 40.5, 66.0), Vec3(0, 0, -1), 12.0, lh) &&
                     lh.pos[0] == 60 && lh.pos[1] == 40 && lh.pos[2] == 60;
      // and it must not block movement the way a full block does
      bool walkThrough = !boxCollides(w, 60.5, 40.0, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
      w.setBlock(60, 40, 60, BLOCK_AIR);
      if (!aimable || !walkThrough) blocksOk = false;
    }
    // Slab, fence, door and trapdoor must each collide as their own sub-cell
    // shape (boxCollidesSubCell) rather than as a full cube (boxCollides
    // alone skips them, same as it skips stairs).
    {
      w.setBlock(60, 40, 60, ITEM_WOOD_SLAB);
      bool slabTop = !boxCollides(w, 60.5, 40.0, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
                     boxCollidesSubCell(w, 60.5, 40.0, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
      // Standing just above the slab's half-height top: no longer colliding.
      bool slabAbove = !boxCollidesSubCell(w, 60.5, 40.51, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
      w.setBlock(60, 40, 60, BLOCK_AIR);

      w.setBlock(60, 40, 60, ITEM_FENCE);
      bool fenceCenter = !boxCollides(w, 60.5, 40.5, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
                        boxCollidesSubCell(w, 60.5, 40.5, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
      w.setBlock(60, 40, 60, BLOCK_AIR);

      // A door needs a solid neighbour to read a facing off, same as a panel.
      w.setBlock(59, 40, 60, BLOCK_STONE);
      w.setBlock(60, 40, 60, ITEM_DOOR);
      bool doorFlush = !boxCollides(w, 60.5, 40.5, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
                       boxCollidesSubCell(w, 60.1, 40.5, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
      w.setBlock(60, 40, 60, BLOCK_AIR);
      w.setBlock(59, 40, 60, BLOCK_AIR);

      w.setBlock(60, 40, 60, ITEM_TRAPDOOR);
      bool trapdoorLow = !boxCollides(w, 60.5, 40.0, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
                        boxCollidesSubCell(w, 60.5, 40.0, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
      bool trapdoorAbove = !boxCollidesSubCell(w, 60.5, 40.2, 60.5, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
      w.setBlock(60, 40, 60, BLOCK_AIR);

      if (!slabTop || !slabAbove || !fenceCenter || !doorFlush || !trapdoorLow || !trapdoorAbove) {
        blocksOk = false;
      }
    }
    // ...and the ones that are NOT building material must be refused, or
    // they leave exactly the broken block this check exists to prevent.
    const uint8_t NOT_PLACEABLE[] = {
      ITEM_STICK, ITEM_WOOD_PICKAXE, ITEM_WOOD_AXE, ITEM_WOOD_SWORD, ITEM_STONE_HOE,
      ITEM_WOOD_SHOVEL, ITEM_BOAT,
    };
    for (uint8_t id : NOT_PLACEABLE) {
      if (isPlaceable(id)) { toolsOk = false; if (firstBadTool < 0) firstBadTool = id; }
    }
    // Their world textures must cover the whole tile. A block face is not a
    // slot icon: a transparent pixel there is a hole you can see the world
    // through, and these tiles sit in the art range so the general
    // opaque-blocks check deliberately skips them.
    const Atlas& at = buildTextureAtlas();
    bool facesOpaque = true;
    int seeThroughTile = -1;
    for (uint8_t id : PLACEABLE) {
      for (int face = 0; face < 3 && facesOpaque; face++) {
        int tile = faceTexture(id, face);
        if (tile < 0) continue;
        for (int y = 0; y < ATLAS_TILE_PX && facesOpaque; y++) {
          for (int x = 0; x < ATLAS_TILE_PX; x++) {
            size_t a = (size_t)(y * at.width + tile * ATLAS_TILE_PX + x) * 4 + 3;
            if (at.pixels[a] != 255) {
              facesOpaque = false;
              seeThroughTile = tile;
              break;
            }
          }
        }
      }
    }
    check(blocksOk && toolsOk && facesOpaque, "crafted_blocks_place_and_mine");
    if (!(blocksOk && toolsOk && facesOpaque)) {
      std::fprintf(f, "  (blocksOk=%d firstBadBlock=%d toolsOk=%d firstBadTool=%d opaque=%d tile=%d)\n",
                   blocksOk, firstBadBlock, toolsOk, firstBadTool, facesOpaque, seeThroughTile);
    }
  }

  // Crafting counts ingredients across the whole grid: neither WHICH cells
  // hold them nor HOW they are stacked changes the outcome, only the totals
  // — as long as those totals aren't shared with another recipe (see the
  // door/stairs case below for what happens when they are).
  {
    auto outcomeOf = [](const Hotbar::Slot cells[INV_CRAFT_COUNT]) {
      Hotbar::Slot out;
      return craftOutcome(cells, out) ? out.blockId : -1;
    };
    Hotbar::Slot g[INV_CRAFT_COUNT];
    auto clear = [&]() { for (Hotbar::Slot& s : g) { s.blockId = -1; s.count = 0; } };

    // the wood shovel: 1 plank + 2 sticks, a total no other recipe shares,
    // three ways of arranging the same amounts
    clear();
    g[0] = { ITEM_PLANKS, 1 }; g[1] = { ITEM_STICK, 1 }; g[2] = { ITEM_STICK, 1 };
    int spread = outcomeOf(g);
    clear();
    g[0] = { ITEM_PLANKS, 1 }; g[1] = { ITEM_STICK, 2 }; // stacked in one cell
    int stacked = outcomeOf(g);
    clear();
    g[8] = { ITEM_STICK, 2 }; g[4] = { ITEM_PLANKS, 1 }; // scattered, reordered
    int scattered = outcomeOf(g);
    bool sameEverywhere = spread == ITEM_WOOD_SHOVEL && stacked == ITEM_WOOD_SHOVEL &&
                          scattered == ITEM_WOOD_SHOVEL;

    // one stick short, or one stick over, both match nothing at all
    clear();
    g[0] = { ITEM_PLANKS, 1 }; g[1] = { ITEM_STICK, 1 };
    bool tooFewIsNothing = outcomeOf(g) == -1;
    clear();
    g[0] = { ITEM_PLANKS, 1 }; g[1] = { ITEM_STICK, 3 };
    bool tooManyIsNothing = outcomeOf(g) == -1;

    // recipes that differ ONLY in layout still need their layout: 2 planks
    // could be a door or wood stairs, so the shape decides.
    clear();
    g[0] = { ITEM_PLANKS, 1 }; g[1] = { ITEM_PLANKS, 1 }; // row: a door
    bool doorByShape = outcomeOf(g) == ITEM_DOOR;
    clear();
    g[0] = { ITEM_PLANKS, 1 }; g[4] = { ITEM_PLANKS, 1 }; // diagonal: wood stairs
    bool stairsByShape = outcomeOf(g) == ITEM_WOOD_STAIRS;

    bool ok = sameEverywhere && tooFewIsNothing && tooManyIsNothing &&
              doorByShape && stairsByShape;
    check(ok, "craft_matches_on_totals_not_layout");
    if (!ok) {
      std::fprintf(f, "  (spread=%d stacked=%d scattered=%d fewer=%d over=%d door=%d stairs=%d)\n",
                   spread, stacked, scattered, tooFewIsNothing, tooManyIsNothing,
                   doorByShape, stairsByShape);
    }
  }

  // Recipe list: every recipe must break down into ingredients the list can
  // actually draw — a name, an icon, and a sane count for each.
  {
    bool allListable = true;
    int firstBad = -1;
    for (int i = 0; i < CRAFT_RECIPE_COUNT; i++) {
      uint8_t items[RECIPE_MAX_INGREDIENTS];
      int counts[RECIPE_MAX_INGREDIENTS];
      int n = recipeIngredients(CRAFT_RECIPES[i], items, counts);
      if (n <= 0 || n > RECIPE_MAX_INGREDIENTS) { allListable = false; firstBad = i; break; }
      for (int k = 0; k < n; k++) {
        if (counts[k] <= 0 || counts[k] > 9 || craftItemTile(items[k]) < 0) {
          allListable = false;
          firstBad = i;
        }
      }
    }
    // the campfire recipe (two raw-block ingredients) is what the user reads it for
    uint8_t it[RECIPE_MAX_INGREDIENTS];
    int ct[RECIPE_MAX_INGREDIENTS];
    bool campfireReads = false;
    for (int i = 0; i < CRAFT_RECIPE_COUNT; i++) {
      if (CRAFT_RECIPES[i].output != ITEM_CAMPFIRE) continue;
      int n = recipeIngredients(CRAFT_RECIPES[i], it, ct);
      // 3 wood + 2 coal, in either order
      campfireReads = n == 2 &&
                 ((it[0] == BLOCK_WOOD && ct[0] == 3 && it[1] == BLOCK_COAL && ct[1] == 2) ||
                  (it[1] == BLOCK_WOOD && ct[1] == 3 && it[0] == BLOCK_COAL && ct[0] == 2));
    }
    check(allListable && campfireReads, "recipe_book_lists_every_recipe");
    if (!(allListable && campfireReads)) {
      std::fprintf(f, "  (allListable=%d firstBad=%d campfireReads=%d)\n",
                   allListable, firstBad, campfireReads);
    }
  }

  // And once more dragging from the HOTBAR row with full 32-block starting
  // stacks — that is where a new game actually puts the player's wood, and
  // (after a short dig, since it isn't part of the starting kit) coal ends
  // up the same way — so this is the likeliest real path of all. A left
  // drag moves one item into the cell; the recipe must still match (a
  // cell's count is irrelevant).
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    hotbar.slots[HOTBAR_ORDER_LEN] = { BLOCK_COAL, STARTING_COUNT };
    Inventory inv;
    inv.tab = INV_TAB_CRAFT;

    int woodSlot = -1, coalSlot = -1;
    for (int i = 0; i < SLOT_COUNT; i++) {
      if (hotbar.slots[i].blockId == BLOCK_WOOD) woodSlot = i;
      if (hotbar.slots[i].blockId == BLOCK_COAL) coalSlot = i;
    }

    auto drag = [&](int fromHotbar, int toCell) {
      double sx, sy, sw, sh, dx, dy, dw, dh;
      if (!inv.hotbarSlotRect(fromHotbar, W, H, sx, sy, sw, sh)) return false;
      if (!inv.craftSlotRect(toCell, W, H, dx, dy, dw, dh)) return false;
      inv.onMouseDown(hotbar, sx + sw / 2, sy + sh / 2, false, W, H);
      inv.onMouseUp(hotbar, dx + dw / 2, dy + dh / 2, false, W, H);
      return true;
    };
    // the campfire's own shape: 3 wood across the top, 2 coal below
    bool dragged = woodSlot >= 0 && coalSlot >= 0 &&
                   drag(woodSlot, 0) && drag(woodSlot, 1) && drag(woodSlot, 2) &&
                   drag(coalSlot, 3) && drag(coalSlot, 4);
    bool landed = inv.craft[0].blockId == BLOCK_WOOD && inv.craft[1].blockId == BLOCK_WOOD &&
                  inv.craft[2].blockId == BLOCK_WOOD && inv.craft[3].blockId == BLOCK_COAL &&
                  inv.craft[4].blockId == BLOCK_COAL &&
                  inv.craft[0].count == 1; // left drag moves one item

    Hotbar::Slot preview;
    bool previewed = craftOutcome(inv.craft, preview) && preview.blockId == ITEM_CAMPFIRE;

    double bx, by, bw, bh;
    if (inv.craftButtonRect(W, H, bx, by, bw, bh)) {
      inv.onMouseDown(hotbar, bx + bw / 2, by + bh / 2, false, W, H);
    }
    int campfires = 0;
    for (const Hotbar::Slot& s : hotbar.slots) {
      if (s.blockId == ITEM_CAMPFIRE) campfires += s.count;
    }
    for (const Hotbar::Slot& s : inv.main) {
      if (s.blockId == ITEM_CAMPFIRE) campfires += s.count;
    }
    // one crafted, and every occupied cell was consumed
    bool consumedOne = inv.craft[0].count == 0 && inv.craft[1].count == 0 &&
                       inv.craft[2].count == 0 && inv.craft[3].count == 0 &&
                       inv.craft[4].count == 0;
    bool ok = dragged && landed && previewed && campfires == 1 && consumedOne;
    check(ok, "craft_drag_from_hotbar");
    if (!ok) {
      std::fprintf(f, "  (wood=%d coal=%d dragged=%d landed=%d prev=%d camp=%d)\n",
                   woodSlot, coalSlot, dragged, landed, previewed, campfires);
    }
  }

  // A right drag moves the WHOLE stack into the cell; the recipe must still
  // match and crafting still consumes just one from each occupied cell.
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    Inventory inv;
    inv.tab = INV_TAB_CRAFT;

    int woodSlot = -1, stoneSlot = -1;
    for (int i = 0; i < SLOT_COUNT; i++) {
      if (hotbar.slots[i].blockId == BLOCK_WOOD) woodSlot = i;
      if (hotbar.slots[i].blockId == BLOCK_STONE) stoneSlot = i;
    }

    auto drag = [&](int fromHotbar, int toCell) {
      double sx, sy, sw, sh, dx, dy, dw, dh;
      if (!inv.hotbarSlotRect(fromHotbar, W, H, sx, sy, sw, sh)) return false;
      if (!inv.craftSlotRect(toCell, W, H, dx, dy, dw, dh)) return false;
      inv.onMouseDown(hotbar, sx + sw / 2, sy + sh / 2, true, W, H);
      inv.onMouseUp(hotbar, dx + dw / 2, dy + dh / 2, true, W, H);
      return true;
    };
    bool dragged = woodSlot >= 0 && stoneSlot >= 0 &&
                   drag(woodSlot, 0) && drag(stoneSlot, 1);
    bool landed = inv.craft[0].blockId == BLOCK_WOOD &&
                  inv.craft[1].blockId == BLOCK_STONE &&
                  inv.craft[0].count == STARTING_COUNT; // whole stack moved

    // Crafting matches on QUANTITY now, so a whole stack is simply the wrong
    // amount: 32 wood + 32 stone is not the pickaxe's 1 + 1, and tipping
    // everything in must NOT quietly craft one and swallow the rest.
    Hotbar::Slot preview;
    bool wholeStackMakesNothing = !craftOutcome(inv.craft, preview);

    double bx, by, bw, bh;
    if (inv.craftButtonRect(W, H, bx, by, bw, bh)) {
      inv.onMouseDown(hotbar, bx + bw / 2, by + bh / 2, false, W, H);
    }
    int pickaxes = 0;
    for (const Hotbar::Slot& s : hotbar.slots) {
      if (s.blockId == ITEM_WOOD_PICKAXE) pickaxes += s.count;
    }
    for (const Hotbar::Slot& s : inv.main) {
      if (s.blockId == ITEM_WOOD_PICKAXE) pickaxes += s.count;
    }
    bool nothingLost = inv.craft[0].count == STARTING_COUNT &&
                       inv.craft[1].count == STARTING_COUNT;
    bool ok = dragged && landed && wholeStackMakesNothing && pickaxes == 0 && nothingLost;
    check(ok, "craft_wrong_quantity_makes_nothing");
    if (!ok) {
      std::fprintf(f, "  (wood=%d stone=%d dragged=%d landed=%d c0=%d/%d c1=%d/%d nothing=%d pick=%d)\n",
                   woodSlot, stoneSlot, dragged, landed,
                   inv.craft[0].blockId, inv.craft[0].count,
                   inv.craft[1].blockId, inv.craft[1].count,
                   wholeStackMakesNothing, pickaxes);
    }
  }

  // Craft-tab layout: the result slot (and the arrow gap before it) must sit
  // entirely clear of the 3x3 grid. The arrow was once drawn inside the
  // grid's third column, covering a real cell — an ingredient left in it hid
  // under the decoration while silently blocking every recipe match.
  {
    const int W = 1280, H = 720;
    Inventory inv;
    inv.tab = INV_TAB_CRAFT;
    double rx, ry, rw, rh;
    bool haveResult = inv.craftResultRect(W, H, rx, ry, rw, rh);
    bool disjoint = haveResult;
    double gridRight = 0;
    for (int i = 0; i < INV_CRAFT_COUNT && haveResult; i++) {
      double cx, cy, cw, ch;
      if (!inv.craftSlotRect(i, W, H, cx, cy, cw, ch)) { disjoint = false; break; }
      gridRight = std::max(gridRight, cx + cw);
      bool overlap = cx < rx + rw && rx < cx + cw && cy < ry + rh && ry < cy + ch;
      if (overlap) disjoint = false;
    }
    bool arrowGap = haveResult && rx - gridRight >= 16; // room for the arrow
    check(disjoint && arrowGap, "craft_result_clear_of_grid");
    if (!(disjoint && arrowGap)) {
      std::fprintf(f, "  (disjoint=%d gap=%.1f)\n", disjoint, haveResult ? rx - gridRight : 0.0);
    }
  }

  // Everything that can sit in a slot needs a real display name, or the
  // hover tooltip shows craftItemName's "?" placeholder.
  {
    auto named = [](uint8_t id) {
      const char* s = craftItemName(id);
      return s && *s && std::strcmp(s, "?") != 0;
    };
    bool blocksNamed = true, itemsNamed = true;
    int firstBad = -1;
    for (int id = 1; id < BLOCK_TYPE_COUNT; id++) { // skip air, never in a slot
      if (!named((uint8_t)id)) { blocksNamed = false; if (firstBad < 0) firstBad = id; }
    }
    for (int i = 0; i < CRAFT_RECIPE_COUNT; i++) {
      uint8_t out = CRAFT_RECIPES[i].output;
      if (!named(out)) { itemsNamed = false; if (firstBad < 0) firstBad = out; }
    }
    check(blocksNamed && itemsNamed, "slot_items_have_names");
    if (!(blocksNamed && itemsNamed)) {
      std::fprintf(f, "  (blocks=%d items=%d firstBad=%d)\n", blocksNamed, itemsNamed, firstBad);
    }
  }

  // No craftable good may be invisible in a slot: every recipe output needs
  // an icon tile, or the player crafts something and sees an empty square
  // (exactly how the missing pickaxe icon showed up).
  {
    bool allDrawable = true;
    int firstBad = -1;
    for (int i = 0; i < CRAFT_RECIPE_COUNT; i++) {
      if (craftItemTile(CRAFT_RECIPES[i].output) < 0) {
        allDrawable = false;
        if (firstBad < 0) firstBad = CRAFT_RECIPES[i].output;
      }
    }
    bool pickaxeArt = craftItemTile(ITEM_WOOD_PICKAXE) == TILE_WOOD_PICKAXE;
    // ...and its OWN art, not a shared material tile: two items resolving to
    // the same tile are indistinguishable in the slot and in the recipe list.
    bool allDistinct = true;
    int dupeA = -1, dupeB = -1;
    for (int i = 0; i < CRAFT_RECIPE_COUNT && allDistinct; i++) {
      for (int j = i + 1; j < CRAFT_RECIPE_COUNT; j++) {
        uint8_t a = CRAFT_RECIPES[i].output, b = CRAFT_RECIPES[j].output;
        if (a == b) continue; // same item listed twice is fine
        if (craftItemTile(a) == craftItemTile(b)) {
          allDistinct = false;
          dupeA = a;
          dupeB = b;
          break;
        }
      }
    }
    bool ok = allDrawable && pickaxeArt && allDistinct;
    check(ok, "craft_items_have_icons");
    if (!ok) {
      std::fprintf(f, "  (allDrawable=%d firstBad=%d pickaxeArt=%d distinct=%d dupe=%d/%d)\n",
                   allDrawable, firstBad, pickaxeArt, allDistinct, dupeA, dupeB);
    }
  }

  // Right-click a tool: opens the context menu (not the old grab-the-stack
  // behaviour) and its top button reads "Equip" — pressing it swaps the
  // item into the mainHand slot and closes the menu.
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    Inventory inv;
    inv.main[0] = { ITEM_WOOD_PICKAXE, 1 };

    double sx, sy, sw, sh;
    inv.mainSlotRect(0, W, H, sx, sy, sw, sh);
    double cx = sx + sw / 2, cy = sy + sh / 2;

    inv.onMouseDown(hotbar, cx, cy, true, W, H); // right-click
    bool menuOpened = inv.contextMenuSlot == &inv.main[0] &&
                      !(inv.held.blockId >= 0 && inv.held.count > 0);

    inv.onMouseDown(hotbar, cx, cy, false, W, H); // top button, at the click point
    bool equipped = inv.mainHand.blockId == ITEM_WOOD_PICKAXE && inv.main[0].blockId < 0;
    bool menuClosed = inv.contextMenuSlot == nullptr;

    bool ok = menuOpened && equipped && menuClosed;
    check(ok, "context_menu_equips_tool");
    if (!ok) {
      std::fprintf(f, "  (menuOpened=%d equipped=%d menuClosed=%d mainHand=%d main0=%d)\n",
                   menuOpened, equipped, menuClosed, inv.mainHand.blockId, inv.main[0].blockId);
    }
  }

  // Right-click cooked meat: top button reads "Use" and eats one (the same
  // pendingEatAmount hand-off the double-click and drag-to-preview gestures
  // already use). Raw meat isn't itself edible, so its menu has no top
  // button at all — a right-click there lands straight on Drop.
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    Inventory inv;
    inv.main[0] = { ITEM_COOKED_MEAT, 3 };
    inv.main[1] = { ITEM_RAW_MEAT, 2 };

    double sx, sy, sw, sh;
    inv.mainSlotRect(0, W, H, sx, sy, sw, sh);
    double cx = sx + sw / 2, cy = sy + sh / 2;
    inv.onMouseDown(hotbar, cx, cy, true, W, H);
    inv.onMouseDown(hotbar, cx, cy, false, W, H); // top button ("Use")
    bool ate = inv.pendingEatAmount == 1 && inv.main[0].count == 2;

    inv.mainSlotRect(1, W, H, sx, sy, sw, sh);
    double rx = sx + sw / 2, ry = sy + sh / 2;
    inv.onMouseDown(hotbar, rx, ry, true, W, H);
    bool noTopButtonForRawMeat = inv.contextMenuSlot == &inv.main[1];
    inv.onMouseDown(hotbar, rx, ry, false, W, H); // lands on Drop (no top row)
    bool rawMeatDropped = inv.pendingDrop.blockId == ITEM_RAW_MEAT &&
                          inv.pendingDrop.count == 2 && inv.main[1].blockId < 0;

    bool ok = ate && noTopButtonForRawMeat && rawMeatDropped;
    check(ok, "context_menu_use_and_raw_meat_drop");
    if (!ok) {
      std::fprintf(f, "  (ate=%d eatAmt=%d meatLeft=%d rawMenu=%d rawDropped=%d/%d)\n",
                   ate, inv.pendingEatAmount, inv.main[0].count, noTopButtonForRawMeat,
                   inv.pendingDrop.blockId, inv.pendingDrop.count);
    }
  }

  // The context menu's own Drop button (as opposed to raw meat's single-
  // button menu above) — second row, below the top button.
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    Inventory inv;
    inv.main[0] = { ITEM_WOOD_SWORD, 1 };

    double sx, sy, sw, sh;
    inv.mainSlotRect(0, W, H, sx, sy, sw, sh);
    double cx = sx + sw / 2, cy = sy + sh / 2;
    inv.onMouseDown(hotbar, cx, cy, true, W, H);
    inv.onMouseDown(hotbar, cx, cy + 22, false, W, H); // second row: Drop

    bool ok = inv.pendingDrop.blockId == ITEM_WOOD_SWORD && inv.pendingDrop.count == 1 &&
              inv.main[0].blockId < 0 && inv.contextMenuSlot == nullptr;
    check(ok, "context_menu_drop_button");
    if (!ok) {
      std::fprintf(f, "  (pendingDrop=%d/%d main0=%d menuOpen=%d)\n",
                   inv.pendingDrop.blockId, inv.pendingDrop.count, inv.main[0].blockId,
                   inv.contextMenuSlot != nullptr);
    }
  }

  // Dragging any item (not just tools/food) out past the panel's own edge
  // drops it instead of snapping back to its source slot — releasing on
  // dead space INSIDE the panel still returns it, unchanged.
  {
    const int W = 1280, H = 720;
    std::vector<int> noCounts;
    Hotbar hotbar(HOTBAR_ORDER, HOTBAR_ORDER_LEN, noCounts);
    Inventory inv;
    inv.main[0] = { BLOCK_STONE, 5 };

    double sx, sy, sw, sh;
    inv.mainSlotRect(0, W, H, sx, sy, sw, sh);
    double cx = sx + sw / 2, cy = sy + sh / 2;
    inv.onMouseDown(hotbar, cx, cy, true, W, H); // right drag: whole stack
    bool picked = inv.held.blockId == BLOCK_STONE && inv.held.count == 5;

    inv.onMouseUp(hotbar, -50, -50, true, W, H); // released well outside the window
    bool droppedOutside = inv.pendingDrop.blockId == BLOCK_STONE && inv.pendingDrop.count == 5 &&
                          inv.held.blockId < 0;

    // Same drag again, released on dead panel space this time (below the
    // hotbar row, still inside the panel rect) — goes back to its source.
    inv.main[0] = { BLOCK_STONE, 5 };
    inv.pendingDrop = Hotbar::Slot();
    inv.onMouseDown(hotbar, cx, cy, true, W, H);
    double px, py, pw, ph;
    inv.panelRect(W, H, px, py, pw, ph);
    inv.onMouseUp(hotbar, px + 5, py + ph - 2, true, W, H); // inside the panel, off any slot
    bool returnedInside = inv.main[0].blockId == BLOCK_STONE && inv.main[0].count == 5 &&
                          inv.pendingDrop.blockId < 0;

    bool ok = picked && droppedOutside && returnedInside;
    check(ok, "drag_outside_panel_drops");
    if (!ok) {
      std::fprintf(f, "  (picked=%d droppedOutside=%d pendingDrop=%d/%d returnedInside=%d main0=%d/%d)\n",
                   picked, droppedOutside, inv.pendingDrop.blockId, inv.pendingDrop.count,
                   returnedInside, inv.main[0].blockId, inv.main[0].count);
    }
  }

  // Dropped-item physics: tossed from up in the air, it falls and settles
  // exactly on top of solid ground, the same boxCollides family every other
  // physics object here uses — a table's own raised top would catch it the
  // same way, no special-casing needed for "floor or table".
  {
    w.setBlock(66, 44, 66, BLOCK_STONE);
    w.setBlock(66, 45, 66, BLOCK_AIR);
    w.setBlock(66, 46, 66, BLOCK_AIR);

    DroppedItem it;
    it.itemId = ITEM_WOOD_PICKAXE;
    it.count = 1;
    it.position = Vec3(66.5, 50.0, 66.5);
    for (int i = 0; i < 300 && !it.onGround; i++) updateDroppedItem(it, w, 1.0 / 60.0);

    // Same discrete-step collision every other physics object here uses
    // (animal.cpp's fall check is identical): a fast-falling step can
    // overshoot slightly before being rejected, so it settles a little
    // above the exact surface rather than snapping to it pixel-perfect —
    // "clearly resting on this block, not sunk in or still airborne" is
    // what actually matters.
    bool ok = it.onGround && it.velocity.y == 0 && it.position.y >= 45.0 && it.position.y < 45.5;
    check(ok, "dropped_item_settles_on_ground");
    if (!ok) {
      std::fprintf(f, "  (onGround=%d y=%.3f)\n", it.onGround, it.position.y);
    }
    w.setBlock(66, 44, 66, BLOCK_AIR);
  }

  // Minimap is north-up: the player arrow must point up when facing north
  // (yaw 0), right when facing east, and so on. HUD y grows downward.
  {
    const double PI = 3.14159265358979323846;
    double dx, dy;
    bool ok = true;
    minimapArrowDir(0, dx, dy);              // north
    ok = ok && dy < -0.9 && std::abs(dx) < 0.1;
    minimapArrowDir(PI, dx, dy);             // south
    ok = ok && dy > 0.9 && std::abs(dx) < 0.1;
    minimapArrowDir(-PI / 2, dx, dy);        // east (yaw turns toward west)
    ok = ok && dx > 0.9 && std::abs(dy) < 0.1;
    minimapArrowDir(PI / 2, dx, dy);         // west
    ok = ok && dx < -0.9 && std::abs(dy) < 0.1;
    check(ok, "minimap_arrow_north_up");
  }

  // Map colours must actually match their block: this used to sample the
  // wrong tile (a stride bug — see minimap.cpp's buildPalette), so grass
  // came out desaturated and unrelated blocks could share a colour.
  {
    int sr, sg, sb, wr, wg, wb, gr, gg, gb;
    minimapBlockColor(BLOCK_STONE, sr, sg, sb);
    minimapBlockColor(BLOCK_WATER, wr, wg, wb);
    minimapBlockColor(BLOCK_GRASS, gr, gg, gb);
    // Stone reads as gray: no channel far from the average of the three.
    int savg = (sr + sg + sb) / 3;
    bool stoneGray = std::abs(sr - savg) < 20 && std::abs(sg - savg) < 20 && std::abs(sb - savg) < 20;
    // Water reads as blue: blue is the strongest channel, clearly so.
    bool waterBlue = wb > wr + 15 && wb > wg + 5;
    // Grass reads as green: green is the strongest channel.
    bool grassGreen = gg > gr && gg > gb;
    check(stoneGray, "map_color_stone_is_gray");
    check(waterBlue, "map_color_water_is_blue");
    check(grassGreen, "map_color_grass_is_green");
    if (!stoneGray) std::fprintf(f, "  (stone rgb=%d,%d,%d)\n", sr, sg, sb);
    if (!waterBlue) std::fprintf(f, "  (water rgb=%d,%d,%d)\n", wr, wg, wb);
    if (!grassGreen) std::fprintf(f, "  (grass rgb=%d,%d,%d)\n", gr, gg, gb);
  }

  // Whole-world map: exploring loads chunks near the player and records
  // columns within REVEAL_RADIUS of the player permanently, leaving
  // everywhere else — including columns whose chunk is merely loaded but
  // the player hasn't actually walked near — as unexplored mist, and stays
  // that way even after the chunk that recorded a column unloads again.
  // Its own fresh World, same reason as the fish/boat tests above:
  // reloading a shared World's chunk window at a distant column would
  // silently invalidate whatever earlier tests still depend on being there.
  {
    World mapWorld;
    mapWorld.updateLoadedChunks(0, 0);
    worldMapUpdate(mapWorld, 0, 0);
    bool nearExplored = worldMapExplored(0, 0) && worldMapExplored(10, -10);
    bool outsideRevealRadiusStaysMist = !worldMapExplored(40, 40); // loaded chunk, but far from (0,0)
    bool farUnexplored = !worldMapExplored(-WORLD_RADIUS + 4, WORLD_RADIUS - 4);
    check(nearExplored, "world_map_explores_near_player");
    check(outsideRevealRadiusStaysMist, "world_map_reveal_radius_is_tight");
    check(farUnexplored, "world_map_unexplored_stays_mist");

    // Now move far away: the near chunks unload, but they were already
    // recorded, so they must still read as explored.
    mapWorld.updateLoadedChunks(400, 400);
    worldMapUpdate(mapWorld, 400, 400);
    check(worldMapExplored(0, 0), "world_map_exploration_is_permanent");
  }

  // Map markers: placed by left-click on the full map (see
  // fullMapScreenToWorld in worldmap.cpp), shown on both the full map and
  // the corner minimap, removable with a right-click near one.
  {
    size_t before = mapMarkers().size();
    addMapMarker(10, 20);
    addMapMarker(-50, 5);
    bool added = mapMarkers().size() == before + 2;
    removeNearestMapMarker(11, 19); // closer to (10,20) than to (-50,5)
    bool removedNearest = mapMarkers().size() == before + 1 &&
                          std::abs(mapMarkers().back().x - (-50)) < 0.01;
    check(added, "map_marker_add");
    check(removedNearest, "map_marker_remove_nearest");
    removeNearestMapMarker(-50, 5);
  }

  // clouds: a broken layer (neither empty sky nor solid overcast), wrapping
  // seamlessly so the drifting tiles line up
  {
    double cover = cloudCoverage();
    bool wraps = cloudAt(0, 0) == cloudAt(CLOUD_GRID, CLOUD_GRID) &&
                 cloudAt(3, 5) == cloudAt(3 + CLOUD_GRID, 5 - CLOUD_GRID);
    // a few scattered puffs, not overcast
    check(cover > 0.05 && cover < 0.18 && wraps, "cloud_layer");
    // and the layer sits above every possible peak
    check(CLOUD_HEIGHT - CLOUD_THICKNESS > CHUNK_HEIGHT - 6, "clouds_above_terrain");
  }

  // sound buffers are valid WAV images
  soundInit();
  auto wavOk = [](const std::vector<uint8_t>& w) {
    if (w.size() <= 44 || std::memcmp(w.data(), "RIFF", 4) != 0 ||
        std::memcmp(w.data() + 8, "WAVE", 4) != 0) return false;
    for (size_t i = 44; i < w.size(); i++) {
      if (w[i] != 0) return true; // audible, not silence
    }
    return false;
  };
  check(wavOk(mineWavData()) && wavOk(placeWavData()), "sound_wav_build");

  // Texture atlas well-formed: right size, and block tiles opaque everywhere.
  // Sprite tiles are the exception — the tall-grass billboard and the item
  // icons are shapes on a transparent field, so they are checked the other
  // way round: they must contain at least one OPAQUE pixel, which is what
  // catches a sprite that paints nothing (an invisible item). It used to also
  // demand some transparency, but hand-drawn art (art\*.png) is free to fill
  // the whole tile, and that is not an error.
  const Atlas& atlas = buildTextureAtlas();
  bool atlasOk = atlas.width == ATLAS_TILE_PX * TILE_COUNT && atlas.height == ATLAS_TILE_PX;
  // Derived, not listed: every item tile is a sprite, plus the tall-grass
  // billboard. A hand-maintained list would have to be extended for each new
  // item and would silently stop covering the ones nobody remembered.
  auto isSprite = [&](int tile) {
    return tile == TILE_TALL_GRASS || tile >= TILE_FIRST_ITEM;
  };
  int spriteClear[TILE_COUNT] = {}, spriteSolid[TILE_COUNT] = {};
  for (size_t i = 3; i < atlas.pixels.size(); i += 4) {
    int tileX = (int)((i / 4) % (size_t)atlas.width) / ATLAS_TILE_PX;
    if (isSprite(tileX)) {
      if (atlas.pixels[i] == 0) spriteClear[tileX]++;
      else if (atlas.pixels[i] == 255) spriteSolid[tileX]++;
      continue;
    }
    if (atlas.pixels[i] != 255) atlasOk = false;
  }
  for (int t = 0; t < TILE_COUNT; t++) {
    if (isSprite(t) && spriteSolid[t] == 0) atlasOk = false; // nothing drawn
  }
  // No magenta anywhere: drawItemSprite paints that for a character with no
  // palette entry, so any appearance means a typo in the artwork.
  int unmappedTile = -1;
  for (size_t i = 0; i + 3 < atlas.pixels.size(); i += 4) {
    if (atlas.pixels[i] == 255 && atlas.pixels[i + 1] == 0 && atlas.pixels[i + 2] == 255) {
      unmappedTile = (int)((i / 4) % (size_t)atlas.width) / ATLAS_TILE_PX;
      atlasOk = false;
      break;
    }
  }
  check(atlasOk, "texture_atlas");
  if (unmappedTile >= 0) {
    std::fprintf(f, "  (unmapped art character in tile %d)\n", unmappedTile);
  }

  // Hand-drawn art override: any art\*.png the converter turned into source
  // must actually reach the atlas, pixel for pixel. With no art supplied the
  // list is empty and every tile keeps its procedural drawing, which is the
  // default this ships in.
  {
    bool overridesApplied = true;
    int checkedSprites = 0;
    for (int i = 0; i < GENERATED_SPRITE_COUNT; i++) {
      const GeneratedSprite& gs = GENERATED_SPRITES[i];
      if (!gs.name || !gs.rgba) continue;
      int tile = -1;
      for (int t = 0; t < TILE_COUNT; t++) {
        const char* n = spriteNameForTile(t);
        if (n && std::strcmp(n, gs.name) == 0) tile = t;
      }
      if (tile < 0) continue; // art with no matching tile is simply unused
      checkedSprites++;
      for (int y = 0; y < ATLAS_TILE_PX; y++) {
        for (int x = 0; x < ATLAS_TILE_PX; x++) {
          const unsigned char* src = &gs.rgba[((size_t)y * ATLAS_TILE_PX + x) * 4];
          int row = ATLAS_TILE_PX - 1 - y; // atlas rows are bottom-up
          const uint8_t* dst =
              &atlas.pixels[(size_t)(row * atlas.width + tile * ATLAS_TILE_PX + x) * 4];
          for (int ch = 0; ch < 4; ch++) {
            if (dst[ch] != src[ch]) overridesApplied = false;
          }
        }
      }
    }
    check(overridesApplied, "hand_drawn_sprites_applied");
    if (!overridesApplied) {
      std::fprintf(f, "  (generated=%d, matched to tiles=%d)\n",
                   GENERATED_SPRITE_COUNT, checkedSprites);
    }
  }

  std::fprintf(f, "%s\n", failures == 0 ? "ALL_PASS" : "HAS_FAILURES");
  std::fclose(f);
  return failures == 0 ? 0 : 1;
}

// --- entry point ---------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
  bool screenshotMode = std::strstr(lpCmdLine, "--screenshot") != nullptr;
  bool selftestMode = std::strstr(lpCmdLine, "--selftest") != nullptr;
  bool waterTestMode = std::strstr(lpCmdLine, "--watertest") != nullptr;
  const char* mapDumpArg = std::strstr(lpCmdLine, "--mapdump");
  if (mapDumpArg) {
    uint32_t seed = 1337;
    const char* eq = std::strchr(mapDumpArg, '=');
    if (eq) seed = (uint32_t)std::strtoul(eq + 1, nullptr, 10);
    return runMapDump(seed);
  }

  if (selftestMode) return runSelftest();

  SetProcessDPIAware();

  // Settings decide the initial window size, so load them before creating it.
  g_settings = loadSettings();
  g_winW = g_settings.resolutionW;
  g_winH = g_settings.resolutionH;
  g_thirdPerson = g_settings.thirdPerson;

  WNDCLASSA wc = {};
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // embedded icon resource (101, icon\blockcraft.rc); falls back to the
  // stock application icon if the build ran without rc.exe
  wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(101));
  if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.lpszClassName = "BlockCraftWindow";
  RegisterClassA(&wc);

  RECT rect = { 0, 0, g_winW, g_winH };
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  g_hwnd = CreateWindowA(wc.lpszClassName, "BlockCraft", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT,
                         rect.right - rect.left, rect.bottom - rect.top,
                         nullptr, nullptr, hInstance, nullptr);
  if (!g_hwnd || !initGL()) {
    MessageBoxA(nullptr, "Failed to create OpenGL window.", "BlockCraft", MB_ICONERROR);
    return 1;
  }
  ShowWindow(g_hwnd, SW_SHOW);
  applyWindowMode();

  gfxInit(g_dc);
  soundInit();
  playerModelInit();
  skyInit();
  minimapInit();
  worldMapInit();
  migrateLegacySave();
  g_menu.sensitivity = g_settings.sensitivity;
  g_menu.renderDistance = g_settings.renderDistance;
  g_menu.displayMode = g_settings.displayMode;
  g_menu.characterType = g_settings.characterType;
  playerModelSetCharacter(g_settings.characterType == 1 ? PlayerCharacter::Alex : PlayerCharacter::Steve);
  for (int i = 0; i < RESOLUTION_COUNT; i++) {
    if (RESOLUTIONS[i].w == g_settings.resolutionW && RESOLUTIONS[i].h == g_settings.resolutionH) {
      g_menu.resolutionIndex = i;
    }
  }
  g_menu.showPanel(MenuPanel::Main);

  if (screenshotMode || waterTestMode) {
    int code;
    if (waterTestMode) {
      code = runWaterTest();
    } else {
      std::string out = exeDir() + "screenshot.bmp";
      code = runScreenshotMode(out.c_str());
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_rc);
    return code;
  }

  LARGE_INTEGER freq, prev, now;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&prev);

  while (g_running) {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) g_running = false;
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    if (!g_running) break;

    QueryPerformanceCounter(&now);
    double dt = (double)(now.QuadPart - prev.QuadPart) / freq.QuadPart;
    prev = now;
    dt = std::min(dt, 0.05);

    updateFrame(dt);
    render();
    SwapBuffers(g_dc);
  }

  teardownSession();
  setCursorCaptured(false);
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(g_rc);
  ReleaseDC(g_hwnd, g_dc);
  DestroyWindow(g_hwnd);
  return 0;
}
