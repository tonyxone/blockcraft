#pragma once
#include "chest.h"
#include "chunk.h"
#include "door.h"
#include "furnace.h"
#include "trapdoor.h"

struct EditKey {
  int x, y, z;
  bool operator==(const EditKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct EditKeyHash {
  size_t operator()(const EditKey& k) const {
    uint64_t h = (uint64_t)(uint32_t)k.x * 0x9E3779B185EBCA87ull;
    h ^= (uint64_t)(uint32_t)k.y * 0xC2B2AE3D27D4EB4Full;
    h ^= ((uint64_t)(uint32_t)k.z << 1) * 0x165667B19E3779F9ull;
    h ^= h >> 29;
    return (size_t)h;
  }
};

struct EditEntry { int x, y, z; uint8_t id; };

// A table or bed spans several cells (see tableFootprint/bedFootprint in
// blocks.h). Every cell of the footprint gets one of these, all pointing at
// the same anchor cell + facing so any cell can be mined to find (and clear)
// the whole object, and the mesher can tell "am I the anchor?" to know
// whether to draw the merged shape or nothing.
struct FurnitureState {
  int facing = 0;
  int anchorX = 0, anchorY = 0, anchorZ = 0;
};

class World {
public:
  std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks;
  int renderDistance = RENDER_DISTANCE;
  // Manual block edits (mine/place). Terrain is deterministic from the seed,
  // so only the diff needs to be saved; it's replayed onto freshly generated
  // chunks in applyEdits().
  std::unordered_map<EditKey, uint8_t, EditKeyHash> edits;

  // Contents + lid animation of every chest that has been opened or loaded
  // this session, keyed by the position the block itself sits at. A chest
  // nobody has touched yet just isn't in the map (see chest.h).
  std::unordered_map<EditKey, ChestState, EditKeyHash> chests;

  // Which way each stair rises, set from the player's own facing at the
  // moment it was placed (see tryPlace in main.cpp) so it climbs away from
  // whoever built it. A stair placed before this existed, or with no entry
  // for any other reason, falls back to stairFacing()'s neighbour-based
  // guess below.
  std::unordered_map<EditKey, int, EditKeyHash> stairFacings;

  // Which wall each ladder cell hangs on, set once at placement time (see
  // tryPlace in main.cpp) from whichever wall the run's base cell leans
  // against, and copied to every cell the run extends to — so a run taller
  // than its supporting wall stays one consistent shape instead of the
  // cells above the wall falling back to panelFacing()'s free-standing
  // guess and rendering centred instead of flush. A ladder cell with no
  // entry (placed before this existed) falls back to that guess too.
  std::unordered_map<EditKey, int, EditKeyHash> panelFacings;

  // Lit state + facing of every furnace that has been toggled or loaded
  // this session, keyed the same way as chests (see furnace.h).
  std::unordered_map<EditKey, FurnaceState, EditKeyHash> furnaces;

  // Swing state + facing of every door that has been toggled or loaded this
  // session, keyed by its BOTTOM cell (see door.h).
  std::unordered_map<EditKey, DoorState, EditKeyHash> doors;

  // Anchor + facing of every table/bed/fence-panel cell placed or loaded
  // this session, keyed by that cell's own position (see FurnitureState
  // above).
  std::unordered_map<EditKey, FurnitureState, EditKeyHash> furniture;

  // Swing state + facing of every trapdoor stepped on this session, keyed
  // by its own cell (see TrapdoorState, trapdoor.h). Not persisted to
  // saves — this game already doesn't save a furnace's lit state or a
  // door's open state either, only position/facing-type data.
  std::unordered_map<EditKey, TrapdoorState, EditKeyHash> trapdoors;

  static uint64_t chunkKey(int cx, int cz) {
    return ((uint64_t)(uint32_t)cx << 32) | (uint64_t)(uint32_t)cz;
  }

  Chunk* getChunk(int cx, int cz);
  Chunk* ensureChunk(int cx, int cz);
  bool isChunkLoadedAt(int wx, int wz);

  uint8_t getBlock(int wx, int wy, int wz);

  // Which wall a panel (ladder) at this cell hangs on: 0 -Z, 1 +Z, 2 -X, 3
  // +X, or -1 if nothing solid is next to it. Checks panelFacings first;
  // failing that, falls back to reading it off the neighbours (a cell alone
  // stores only an id). Shared by the mesher (to orient its geometry) and
  // the physics (to give the panel a matching collision box).
  int panelFacing(int wx, int wy, int wz);

  // Which side a stair at this cell rises toward: 0 -Z, 1 +Z, 2 -X, 3 +X.
  // Checks stairFacings first (the player's own facing when it was placed);
  // failing that, falls back to reading it off the neighbours — leans the
  // high half against the first plain solid neighbour (other stairs and
  // panels don't count, so a row of side-by-side stairs keeps one consistent
  // facing), defaulting to 0 (-Z) if free-standing. Shared by the mesher (to
  // orient the steps) and the physics (to give the stair matching collision
  // boxes).
  int stairFacing(int wx, int wy, int wz);

  // Which side a furnace at this cell opens toward: 0 -Z, 1 +Z, 2 -X, 3 +X.
  // Checks furnaces first (set at placement, opening toward whoever placed
  // it — see tryPlace in main.cpp, same convention as a chest's lid);
  // failing that, falls back to the neighbour-based guess panelFacing()
  // already does, since it's the same "flush against a wall" problem.
  int furnaceFacing(int wx, int wy, int wz);

  // Sets a block and returns the list of chunks that need remeshing
  // (the owning chunk, plus any neighbor whose boundary face culling
  // depends on this block).
  std::vector<Chunk*> setBlock(int wx, int wy, int wz, uint8_t id);

  // Lets water flow into a cell that has just been opened up (e.g. by
  // mining). The cell fills if a neighbour at its own level or directly
  // above holds water, and the flow then spreads sideways and downwards —
  // so a tunnel dug in from a shoreline floods into a river/canal.
  // Water never climbs: a cell above the source level stays dry.
  std::vector<Chunk*> flowWaterInto(int sx, int sy, int sz, int maxCells);

  // Replaces the edit log wholesale, e.g. right after construction when
  // restoring a save. Must be called before any chunks are generated.
  void loadEdits(const std::vector<EditEntry>& list);
  std::vector<EditEntry> getEditsSnapshot() const;

  // Ensures chunks within renderDistance of (px, pz) exist, and evicts chunks
  // that have drifted out of range. Evicted chunks are returned so the caller
  // can free their GL resources before they're destroyed.
  std::vector<std::unique_ptr<Chunk>> updateLoadedChunks(double px, double pz);

private:
  void applyEdits(Chunk& chunk);
};
