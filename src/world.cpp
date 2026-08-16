#include "world.h"
#include "worldgen.h"
#include "noise.h"
#include <unordered_set>

Chunk* World::getChunk(int cx, int cz) {
  auto it = chunks.find(chunkKey(cx, cz));
  return it == chunks.end() ? nullptr : it->second.get();
}

bool World::isChunkLoadedAt(int wx, int wz) {
  return getChunk(floorDiv(wx, CHUNK_SIZE), floorDiv(wz, CHUNK_SIZE)) != nullptr;
}

// Growth-timer jitter for crops re-armed here — same "own little seeded RNG
// per subsystem" convention fish.cpp/animal.cpp each use.
namespace {
Mulberry32 g_cropRng(0x9C0Fu);
const double CROP_GROWTH_MIN_SECONDS = 40.0, CROP_GROWTH_MAX_SECONDS = 80.0;
} // namespace

void World::applyEdits(Chunk& chunk) {
  if (edits.empty()) return;
  int ox = chunk.worldOriginX();
  int oz = chunk.worldOriginZ();
  for (const auto& kv : edits) {
    int lx = kv.first.x - ox;
    int lz = kv.first.z - oz;
    if (lx >= 0 && lx < CHUNK_SIZE && lz >= 0 && lz < CHUNK_SIZE) {
      chunk.setLocal(lx, kv.first.y, lz, kv.second);
      // Re-arm growth for a non-mature crop as its chunk is (re)built —
      // fires both at session start and during ordinary chunk streaming
      // (see World::cropTimers' own comment).
      if (isCrop(kv.second) && !isMatureCrop(kv.second)) {
        cropTimers[kv.first] =
            CROP_GROWTH_MIN_SECONDS + g_cropRng.next() * (CROP_GROWTH_MAX_SECONDS - CROP_GROWTH_MIN_SECONDS);
      }
    }
  }
}

Chunk* World::ensureChunk(int cx, int cz) {
  Chunk* existing = getChunk(cx, cz);
  if (existing) return existing;
  auto chunk = generateChunk(cx, cz);
  applyEdits(*chunk);
  Chunk* raw = chunk.get();
  chunks[chunkKey(cx, cz)] = std::move(chunk);
  return raw;
}

uint8_t World::getBlock(int wx, int wy, int wz) {
  if (wy < 0 || wy >= CHUNK_HEIGHT) return BLOCK_AIR;
  int cx = floorDiv(wx, CHUNK_SIZE);
  int cz = floorDiv(wz, CHUNK_SIZE);
  Chunk* chunk = getChunk(cx, cz);
  if (!chunk) return BLOCK_AIR;
  return chunk->getLocal(wx - cx * CHUNK_SIZE, wy, wz - cz * CHUNK_SIZE);
}

int World::panelFacing(int wx, int wy, int wz) {
  auto it = panelFacings.find({ wx, wy, wz });
  if (it != panelFacings.end()) return it->second;

  const int SIDES[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
  for (int s = 0; s < 4; s++) {
    if (isSolid(getBlock(wx + SIDES[s][0], wy, wz + SIDES[s][1]))) return s;
  }
  return -1;
}

int World::stairFacing(int wx, int wy, int wz) {
  auto it = stairFacings.find({ wx, wy, wz });
  if (it != stairFacings.end()) return it->second;

  const int SIDES[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
  for (int s = 0; s < 4; s++) {
    uint8_t n = getBlock(wx + SIDES[s][0], wy, wz + SIDES[s][1]);
    // Only a plain solid block anchors a stair's facing; other stairs (and
    // panels, which are not solid anyway) must not, or stairs placed side by
    // side would turn to face each other instead of keeping one direction.
    if (isSolid(n) && !isStairs(n)) return s;
  }
  return 0; // free-standing: rise toward -Z
}

int World::furnaceFacing(int wx, int wy, int wz) {
  auto it = furnaces.find({ wx, wy, wz });
  if (it != furnaces.end()) return it->second.facing;
  return panelFacing(wx, wy, wz); // same "flush against a wall" problem
}

std::vector<Chunk*> World::setBlock(int wx, int wy, int wz, uint8_t id) {
  if (wy < 0 || wy >= CHUNK_HEIGHT) return {};
  int cx = floorDiv(wx, CHUNK_SIZE);
  int cz = floorDiv(wz, CHUNK_SIZE);
  Chunk* chunk = getChunk(cx, cz);
  if (!chunk) return {};
  int lx = wx - cx * CHUNK_SIZE;
  int lz = wz - cz * CHUNK_SIZE;
  chunk->setLocal(lx, wy, lz, id);
  edits[{ wx, wy, wz }] = id;

  std::vector<Chunk*> affected = { chunk };
  auto maybeAdd = [&](int dcx, int dcz) {
    Chunk* neighbor = getChunk(cx + dcx, cz + dcz);
    if (neighbor) {
      neighbor->dirty = true;
      affected.push_back(neighbor);
    }
  };
  if (lx == 0) maybeAdd(-1, 0);
  if (lx == CHUNK_SIZE - 1) maybeAdd(1, 0);
  if (lz == 0) maybeAdd(0, -1);
  if (lz == CHUNK_SIZE - 1) maybeAdd(0, 1);
  return affected;
}

std::vector<Chunk*> World::flowWaterInto(int sx, int sy, int sz, int maxCells) {
  std::unordered_set<Chunk*> affected;
  // Water floods empty space, and washes plants away as it goes — a grass
  // tuft is no more of a dam than open air.
  auto floodable = [](uint8_t id) { return id == BLOCK_AIR || isPlant(id); };
  if (!floodable(getBlock(sx, sy, sz))) return {};

  // A cell can only be fed by water beside it or directly above it.
  auto isFed = [&](const EditKey& c) {
    if (getBlock(c.x, c.y + 1, c.z) == BLOCK_WATER) return true;
    return getBlock(c.x + 1, c.y, c.z) == BLOCK_WATER ||
           getBlock(c.x - 1, c.y, c.z) == BLOCK_WATER ||
           getBlock(c.x, c.y, c.z + 1) == BLOCK_WATER ||
           getBlock(c.x, c.y, c.z - 1) == BLOCK_WATER;
  };

  std::vector<EditKey> queue{ { sx, sy, sz } };
  int filled = 0;
  for (size_t head = 0; head < queue.size() && filled < maxCells; head++) {
    EditKey c = queue[head];
    if (c.y < 0 || c.y >= CHUNK_HEIGHT) continue;
    if (!isChunkLoadedAt(c.x, c.z)) continue;
    if (!floodable(getBlock(c.x, c.y, c.z))) continue;
    if (!isFed(c)) continue;

    for (Chunk* chunk : setBlock(c.x, c.y, c.z, BLOCK_WATER)) affected.insert(chunk);
    filled++;

    // spread sideways and fall, never upward
    queue.push_back({ c.x + 1, c.y, c.z });
    queue.push_back({ c.x - 1, c.y, c.z });
    queue.push_back({ c.x, c.y, c.z + 1 });
    queue.push_back({ c.x, c.y, c.z - 1 });
    queue.push_back({ c.x, c.y - 1, c.z });
  }

  return std::vector<Chunk*>(affected.begin(), affected.end());
}

void World::loadEdits(const std::vector<EditEntry>& list) {
  edits.clear();
  for (const auto& e : list) edits[{ e.x, e.y, e.z }] = e.id;
}

std::vector<EditEntry> World::getEditsSnapshot() const {
  std::vector<EditEntry> out;
  out.reserve(edits.size());
  for (const auto& kv : edits) out.push_back({ kv.first.x, kv.first.y, kv.first.z, kv.second });
  return out;
}

std::vector<std::unique_ptr<Chunk>> World::updateLoadedChunks(double px, double pz) {
  int centerCx = (int)std::floor(px / CHUNK_SIZE);
  int centerCz = (int)std::floor(pz / CHUNK_SIZE);
  int rd = renderDistance;

  const int minC = -WORLD_RADIUS / CHUNK_SIZE;
  const int maxC = WORLD_RADIUS / CHUNK_SIZE - 1;
  for (int dz = -rd; dz <= rd; dz++) {
    for (int dx = -rd; dx <= rd; dx++) {
      int cx = centerCx + dx;
      int cz = centerCz + dz;
      // nothing exists beyond the world border
      if (cx < minC || cx > maxC || cz < minC || cz > maxC) continue;
      ensureChunk(cx, cz);
    }
  }

  std::vector<std::unique_ptr<Chunk>> removed;
  int evictDistance = rd + 1;
  for (auto it = chunks.begin(); it != chunks.end();) {
    Chunk* c = it->second.get();
    if (std::abs(c->cx - centerCx) > evictDistance || std::abs(c->cz - centerCz) > evictDistance) {
      removed.push_back(std::move(it->second));
      it = chunks.erase(it);
    } else {
      ++it;
    }
  }
  return removed;
}
