#include "worldgen.h"
#include "noise.h"

// The map layout is distance-driven (r = distance from the center, 0..1):
//   - the inner ~80% is grassland, ocean, lakes, desert patches and sunken
//     canyons — the bulk of the world;
//   - snow only exists in a small ring near the border (far side);
//   - mountains get taller the farther out they are, which tiers them:
//     rock mountains (inner, lowest, stone tops), grass mountains (middle),
//     snow mountains (outer ring, tallest).
// Each game uses a random seed; saves persist theirs.

static uint32_t g_seed = 1337;

// Deterministic hash -> [0,1), independent of generation order, used for
// per-column decisions (e.g. tree placement) so chunks don't need neighbor
// state.
static double hash2D(int x, int z) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + g_seed;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return double(h) / 4294967296.0;
}

// Noise instances are constructed in a fixed order from one RNG stream so
// everything is deterministic per seed.
// Elevation bands, applied to normal land everywhere: any peak tall enough
// turns bare rock, and taller still gets a snow cap. This is what gives a
// big mountain its grass -> rock -> snow banding, at any distance.
static const int ROCK_LINE = 26;
static const int SNOW_LINE = 32;

// The one guaranteed landmark: a very tall snow-capped mountain, placed by
// the seed so every world has exactly one.
static const double LANDMARK_RADIUS = 36.0;
// Rise above the skirt, sized so the summit lands exactly on the world
// ceiling (CHUNK_HEIGHT-6 = 42). Overshooting would clip the cone into a
// wide flat plateau instead of a peak.
static const double LANDMARK_PEAK = 22.0;
// Profile exponent. A smoothstep flattens out at the summit (zero slope),
// which rounds off into a wide plateau; an exponent above 1 keeps the
// slope steepest at the top, giving a sharp peak over a broad base.
static const double LANDMARK_SHARPNESS = 1.35;

// Ocean shaping. A low-frequency continent mask decides where land sits at
// all; a second, finer octave breaks the coasts up and scatters islands.
// LAND_BIAS sets the sea/land balance (lower = more ocean).
static const double OCEAN_FLOOR = SEA_LEVEL - 12;
static const double LAND_BIAS = -0.06;
static const double LAND_RANGE = 0.55;

struct Gen {
  Mulberry32 rng;
  Noise2D noiseBase, noiseDetail, noiseMountain;
  Noise2D noiseTemp, noiseMoist, noiseCanyon, noiseBerg;
  Noise2D noiseContinent; // ocean basins vs. landmasses
  Noise2D noiseGrove;     // thickets of small trees
  Noise2D noiseMeadow;    // patches of ground grass
  Noise2D noiseCoal;      // coal seams in the stone (appended last)
  double landmarkX = 0, landmarkZ = 0;
  explicit Gen(uint32_t seed)
      : rng(seed),
        noiseBase(rng), noiseDetail(rng), noiseMountain(rng),
        noiseTemp(rng), noiseMoist(rng), noiseCanyon(rng), noiseBerg(rng),
        noiseContinent(rng), noiseGrove(rng), noiseMeadow(rng),
        noiseCoal(rng) {
    // Placed from a hash of the seed (not the noise RNG stream, so terrain
    // stays identical for a given seed regardless of this feature).
    uint32_t h = seed * 2654435761u;
    h ^= h >> 15;
    uint32_t h2 = (h ^ 0x9E3779B9u) * 2246822519u;
    h2 ^= h2 >> 13;
    double angle = (double)h / 4294967296.0 * 6.283185307179586;
    // Kept clear of the snow ring (r > 0.86) so the mountain's skirt is
    // grassland rather than an extension of the frozen border band.
    double target = 0.68 + (double)h2 / 4294967296.0 * 0.10; // Chebyshev r
    double ca = std::cos(angle), sa = std::sin(angle);
    double m = std::max(std::abs(ca), std::abs(sa));
    double len = target * WORLD_RADIUS / (m > 1e-6 ? m : 1.0);
    // Snapped to whole block columns so one column sits exactly at the
    // cone's apex; otherwise the summit falls between columns and the peak
    // comes out a block short of the ceiling.
    landmarkX = std::floor(ca * len);
    landmarkZ = std::floor(sa * len);
  }
};

static std::unique_ptr<Gen> g_gen;

static Gen& gen() {
  if (!g_gen) g_gen = std::make_unique<Gen>(g_seed);
  return *g_gen;
}

void setWorldSeed(uint32_t seed) {
  g_seed = seed;
  g_gen.reset();
}

uint32_t currentWorldSeed() { return g_seed; }

enum Biome { BIOME_PLAINS, BIOME_DESERT, BIOME_CANYON, BIOME_SNOW };

struct ColumnInfo {
  Biome biome;
  int surfaceY;
  double r;         // normalized distance from the map center (0..1)
  bool canyonCut;   // this column is inside a carved canyon channel
  bool canyonWalls; // this column gets red-rock strata (channel or rim)
};

static double smooth01(double t) {
  t = clampd(t, 0, 1);
  return t * t * (3 - 2 * t);
}

// Snow ring: a band hugging the border, wobbled by temperature so the edge
// looks organic. Snow also appears inland on any peak above SNOW_LINE.
static bool inSnowRing(double r, double temp) {
  return r + temp * 0.05 > 0.86;
}

static ColumnInfo columnAt(int wx, int wz) {
  Gen& g = gen();

  // Chebyshev distance: the map is a SQUARE, so this makes r = 1 along the
  // whole border (Euclidean would treat the corners as far and freeze them).
  double r = std::max(std::abs((double)wx), std::abs((double)wz)) / WORLD_RADIUS;
  r = clampd(r, 0, 1);

  double temp = g.noiseTemp(wx * 0.006, wz * 0.006);
  double moist = g.noiseMoist(wx * 0.007, wz * 0.007);

  ColumnInfo info;
  info.r = r;
  info.canyonCut = false;
  info.canyonWalls = false;

  // Landmark mountain: a smooth cone that overrides everything inside its
  // footprint. Its skirt is forced to plains so the peak reads as
  // grassland at the bottom, then bare rock, then a snow cap.
  double ldx = wx - g.landmarkX;
  double ldz = wz - g.landmarkZ;
  double ldist = std::sqrt(ldx * ldx + ldz * ldz);
  bool onLandmark = ldist < LANDMARK_RADIUS;
  double landmarkH = 0;
  if (onLandmark) {
    double u = 1.0 - ldist / LANDMARK_RADIUS; // 0 at the foot, 1 at the summit
    landmarkH = SEA_LEVEL + 2 + std::pow(u, LANDMARK_SHARPNESS) * LANDMARK_PEAK;
  }

  // A slightly wider band than the canyon biome itself gets red strata, so
  // walls exposed by a gorge stay red right up to its edge instead of
  // turning grey where the biome test happens to fall off.
  bool canyonMargin = temp > 0.20 && moist > 0.20;

  if (onLandmark) info.biome = BIOME_PLAINS;
  else if (inSnowRing(r, temp)) info.biome = BIOME_SNOW;
  else if (temp > 0.30 && moist > 0.30) info.biome = BIOME_CANYON;
  // Deserts are a minority feature (~7% of the map); most warm-but-dry land
  // stays grassland.
  else if (temp > 0.54) info.biome = BIOME_DESERT;
  else info.biome = BIOME_PLAINS;

  double base = g.noiseBase(wx * 0.008, wz * 0.008) * 10;
  double detail = g.noiseDetail(wx * 0.05, wz * 0.05) * 2.5;
  // Higher threshold -> a few isolated massifs instead of continuous ranges.
  double mountainMask = std::max(0.0, g.noiseMountain(wx * 0.003, wz * 0.003) - 0.45) / 0.55;
  double mountain = mountainMask * mountainMask * 26;

  // The same mountain noise is amplified with distance, so a massif in the
  // outer ring towers over an identical one near spawn. Together with the
  // caps below this makes peak height rise reliably outward.
  double mountainScale = 0.5 + 1.3 * smooth01(r);
  mountain *= mountainScale;

  double h;
  double mountainPart;
  if (info.biome == BIOME_DESERT) {
    h = SEA_LEVEL + 5 + base * 0.45 + detail * 0.8;
    mountainPart = mountain * 0.4;
  } else {
    // plains / snow / canyon share the classic height field
    h = SEA_LEVEL + 4 + base + detail;
    mountainPart = mountain;
  }

  // Mountain height tiers by distance, so peak type is ordered by height:
  //   inner  (r<0.45) rock mountains  -> capped 33 (lowest; tops turn rock,
  //                                     only the very highest catch snow)
  //   middle (r<0.85) grass mountains -> capped 38 (rock + snow caps)
  //   outer  ring     snow mountains  -> capped 44 (highest, farthest)
  double cap;
  if (r < 0.45) cap = 33;
  else if (r < 0.85) cap = 33 + 5 * smooth01((r - 0.45) / 0.40);
  else cap = 38 + 6 * smooth01((r - 0.85) / 0.10);
  // Ocean shaping: blend the BASE terrain down toward the sea floor wherever
  // the continent mask is low. Two octaves, so coastlines are ragged and
  // small high spots survive as islands. Mountains are added afterwards, so
  // a peak on a narrow island still reaches its full height instead of
  // being scaled down along with the surrounding sea bed.
  double cont = g.noiseContinent(wx * 0.0045, wz * 0.0045) +
                0.35 * g.noiseContinent(wx * 0.013, wz * 0.013);
  double landness = smooth01((cont - LAND_BIAS) / LAND_RANGE + 0.5);
  // the landmark mountain always stands on solid ground
  if (onLandmark) {
    landness = std::max(landness, smooth01(1.0 - ldist / (LANDMARK_RADIUS * 1.6)));
  }
  h = OCEAN_FLOOR + landness * (h - OCEAN_FLOOR);
  h += mountainPart * landness;

  h = std::min(h, cap);

  // The landmark ignores the zone cap — it is meant to tower over the map.
  if (onLandmark) h = std::max(h, landmarkH);

  // Canyons are holes in the land: normal terrain height, then channels
  // carved down below sea level. A wider band around the cut gets red-rock
  // strata so the exposed walls read as layered canyon rock.
  // Red strata are painted across the whole margin; only the canyon biome
  // proper actually gets carved.
  if (canyonMargin && info.biome != BIOME_SNOW && !onLandmark) {
    double cw = std::abs(g.noiseCanyon(wx * 0.011, wz * 0.011));
    if (cw < 0.44) info.canyonWalls = true;
  }

  if (info.biome == BIOME_CANYON) {
    // Higher-frequency ridges make winding gorges. The cut depth is
    // QUANTIZED into terraces, so the walls step outward as they rise and
    // the gorge reads as an inverted stepped pyramid from the inside
    // rather than a plain vertical-sided pit.
    // The floor sits just above sea level: the canyon is dry, and water
    // everywhere else can fill normally (a canyon meeting a lake used to
    // need a special case that left water hanging beside dry ground).
    // A low noise frequency makes the gorges broad (a grand canyon, not a
    // slot), and the floor drops below sea level so the deepest part holds
    // a river — water fills it through the normal rule, no special case.
    const double EDGE = 0.30;
    const int TERRACES = 6;
    double c = std::abs(g.noiseCanyon(wx * 0.011, wz * 0.011));
    if (c < EDGE) {
      double t = clampd((EDGE - c) / EDGE, 0, 1); // 0 at the rim, 1 at center
      double level = std::floor(t * TERRACES) / (TERRACES - 1);
      level = clampd(level, 0, 1);
      double floor = SEA_LEVEL - 6;
      double cut = level * (h - floor);
      if (cut > 0) {
        h = h - cut;
        info.canyonCut = true;
      }
    }
  }

  info.surfaceY = (int)std::max(1.0, std::min((double)(CHUNK_HEIGHT - 6), std::round(h)));
  return info;
}

void columnInfoAt(int wx, int wz, int& biomeOut, int& surfaceYOut) {
  ColumnInfo info = columnAt(wx, wz);
  biomeOut = (int)info.biome;
  surfaceYOut = info.surfaceY;
}

bool canyonCutAt(int wx, int wz) {
  return columnAt(wx, wz).canyonCut;
}

double bergValueAt(int wx, int wz) {
  return gen().noiseBerg(wx * 0.025, wz * 0.025);
}

void landmarkPosition(double& x, double& z) {
  x = gen().landmarkX;
  z = gen().landmarkZ;
}

// Trees range from 2-block saplings to 15-block giants. The size roll is
// heavily skewed (r^TREE_SIZE_SKEW), so big trees are rare; the canopy
// grows with the trunk, so a giant carries a correspondingly wide crown.
const int TREE_MIN_HEIGHT = 2;
const int TREE_MAX_HEIGHT = 15;
const int TREE_MAX_RADIUS = 4; // canopy reach; sets the chunk-edge margin
static const double TREE_SIZE_SKEW = 2.3;

static int treeCanopyRadius(int trunkHeight) {
  int radius = 1 + trunkHeight / 5;
  return std::min(radius, TREE_MAX_RADIUS);
}

// Small trees grow in thickets: a grove field marks patches where saplings
// crowd together. Big trees only stand outside them, so giants stay
// solitary landmarks rather than sprouting in the middle of a copse.
static const double GROVE_THRESHOLD = 0.25;
static const int GROVE_MAX_HEIGHT = 4; // keeps grove canopies at radius 1

// Trees sit on a jittered grid rather than an independent per-column roll,
// which is what let trunks land side by side and merge their crowns. One
// tree per cell, offset within a limited window, so the closest two trunks
// can ever be is (cell - jitter + 1): grove trees end up a clear block
// apart, and the big open-country trees much further.
static const int GROVE_CELL = 5, GROVE_JITTER = 2, GROVE_BASE = 2; // >= 4 apart
static const double GROVE_FILL = 0.85;
static const int OPEN_CELL = 12, OPEN_JITTER = 4, OPEN_BASE = 4;   // >= 9 apart
static const double OPEN_FILL = 0.9;

static bool inGrove(int wx, int wz) {
  return gen().noiseGrove(wx * 0.03, wz * 0.03) > GROVE_THRESHOLD;
}

static bool isTreeSite(int wx, int wz, int cell, int jitter, int base, double fill) {
  int cx = floorDiv(wx, cell);
  int cz = floorDiv(wz, cell);
  int ox = base + (int)(hash2D(cx * 2 + 1, cz * 2 + 7) * jitter);
  int oz = base + (int)(hash2D(cx * 2 + 13, cz * 2 + 3) * jitter);
  if (wx - cx * cell != ox || wz - cz * cell != oz) return false;
  return hash2D(cx * 7 + 5, cz * 11 + 9) < fill; // leave some cells empty
}

// Wild farm patches: crops are otherwise harvest-only with no crafting
// recipe (see blocks.h's isCrop helpers), so without a natural find a player
// could never obtain their first wheat/carrot/potato seed item at all. A
// small already-tilled clearing, found rather than grown, fixes that — same
// jittered-grid site technique as isTreeSite above, just a much larger cell
// so patches are spaced apart across the map (worth exploring for) instead
// of blanketing every field the way flowers do.
static const int FARM_CELL = 48, FARM_JITTER = 24, FARM_BASE = 8;
static const double FARM_FILL = 0.6;
const int FARM_PATCH_SIZE = 4; // 4x4 farmland cells

static bool isFarmSite(int wx, int wz) {
  int cx = floorDiv(wx, FARM_CELL);
  int cz = floorDiv(wz, FARM_CELL);
  int ox = FARM_BASE + (int)(hash2D(cx * 2 + 17, cz * 2 + 23) * FARM_JITTER);
  int oz = FARM_BASE + (int)(hash2D(cx * 2 + 29, cz * 2 + 31) * FARM_JITTER);
  if (wx - cx * FARM_CELL != ox || wz - cz * FARM_CELL != oz) return false;
  return hash2D(cx * 5 + 3, cz * 9 + 7) < FARM_FILL;
}

// Rolled from the column hash so a tree's size is stable per position.
static int treeHeightFor(int wx, int wz, int surfaceY) {
  double roll = hash2D(wx * 7 + 13, wz * 13 + 7);
  int h;
  if (inGrove(wx, wz)) {
    // thicket: small trees only, clustered together
    h = TREE_MIN_HEIGHT +
        (int)std::lround(std::pow(roll, 1.4) * (GROVE_MAX_HEIGHT - TREE_MIN_HEIGHT));
  } else {
    double skewed = std::pow(roll, TREE_SIZE_SKEW);
    h = TREE_MIN_HEIGHT + (int)std::lround(skewed * (TREE_MAX_HEIGHT - TREE_MIN_HEIGHT));
  }
  // keep the crown under the world ceiling rather than letting it clip
  int room = CHUNK_HEIGHT - 2 - surfaceY;
  return std::min(h, room);
}

// Grass-biome trees (never snow ones) have a chance to bear one of the 5
// fruits. A fruiting tree does NOT turn its whole canopy into fruiting
// leaves — only a random 1-10 of its leaf cells do, the rest stay plain
// BLOCK_LEAVES, so a fruit reads as something scattered through the crown
// rather than every single leaf carrying one. One roll per tree (keyed off
// its trunk position) decides whether it bears fruit at all, a second picks
// which of the 5 kinds, a third picks how many (1-10).
static const double FRUIT_TREE_CHANCE = 0.25;

static uint8_t fruitLeafBlockFor(int wx, int wz, bool eligible) {
  if (!eligible) return BLOCK_LEAVES;
  if (hash2D(wx * 41 + 7, wz * 43 + 19) >= FRUIT_TREE_CHANCE) return BLOCK_LEAVES;
  int kind = (int)(hash2D(wx * 47 + 23, wz * 53 + 29) * FRUIT_KIND_COUNT);
  kind = std::min(kind, FRUIT_KIND_COUNT - 1);
  return FRUIT_LEAF_BLOCKS[kind];
}

static void plantTree(Chunk& chunk, int lx, int surfaceY, int lz, int trunkHeight,
                      uint8_t fruitLeafBlock, int wx, int wz) {
  for (int dy = 1; dy <= trunkHeight; dy++) {
    chunk.setLocal(lx, surfaceY + dy, lz, BLOCK_WOOD);
  }
  int topY = surfaceY + trunkHeight;
  int radius = treeCanopyRadius(trunkHeight);
  bool bearsFruit = fruitLeafBlock != BLOCK_LEAVES;

  // Every newly-leaved cell is recorded here (only populated when this tree
  // bears fruit) so a random subset of them — not the whole canopy — can be
  // picked afterward to actually carry a fruit.
  struct LeafCell { int x, y, z; double priority; };
  std::vector<LeafCell> leafCells;

  // Canopy depth scales too, so tall trees carry a deep crown rather than
  // a flat cap perched on a bare pole. Each layer is cut to a disc by a
  // radial test — the same rule at every size, so a sapling gets the same
  // rounded silhouette as a giant instead of a bare cube.
  for (int dy = -radius; dy <= 1; dy++) {
    int rad = radius;
    if (dy == 1) rad = std::max(1, radius - 1);          // taper the top
    else if (dy == -radius) rad = std::max(1, radius - 1); // and the underside
    double limit = (rad + 0.4) * (rad + 0.4);
    for (int dx = -rad; dx <= rad; dx++) {
      for (int dz = -rad; dz <= rad; dz++) {
        if (dx == 0 && dz == 0 && dy < 1) continue; // keep trunk visible through leaves
        if (dx * dx + dz * dz > limit) continue;    // round off the layer
        int y = topY + dy;
        int x = lx + dx;
        int z = lz + dz;
        if (!Chunk::inBounds(x, y, z)) continue;
        if (chunk.getLocal(x, y, z) != BLOCK_AIR) continue;
        chunk.setLocal(x, y, z, BLOCK_LEAVES);
        if (bearsFruit) {
          int wcx = wx - lx + x, wcz = wz - lz + z; // this cell's world x/z
          leafCells.push_back({ x, y, z, hash2D(wcx * 10007 + y * 97, wcz * 10007 - y * 131) });
        }
      }
    }
  }

  if (bearsFruit && !leafCells.empty()) {
    int fruitCount = 1 + (int)(hash2D(wx * 61 + 3, wz * 67 + 5) * 10); // 1..10
    fruitCount = std::min(fruitCount, (int)leafCells.size());
    std::sort(leafCells.begin(), leafCells.end(),
             [](const LeafCell& a, const LeafCell& b) { return a.priority > b.priority; });
    for (int i = 0; i < fruitCount; i++) {
      const LeafCell& c = leafCells[i];
      chunk.setLocal(c.x, c.y, c.z, fruitLeafBlock);
    }
  }
}

// The block exposed on top of a column. Shared by terrain filling and the
// surfaceBlockAt() query so they can never disagree.
static uint8_t surfaceBlockFor(const ColumnInfo& info) {
  int surfaceY = info.surfaceY;
  bool beach = surfaceY <= SEA_LEVEL + 1;
  switch (info.biome) {
    case BIOME_DESERT:
      return BLOCK_SAND;
    case BIOME_SNOW:
      return BLOCK_SNOW;
    case BIOME_CANYON:
      if (info.canyonCut) return BLOCK_REDROCK;
      if (info.canyonWalls) return beach ? BLOCK_SAND : BLOCK_GRASS;
      // fall through to the plains rules
    default:
      if (info.canyonCut) return BLOCK_REDROCK;
      if (beach) return BLOCK_SAND;
      if (surfaceY >= SNOW_LINE) return BLOCK_SNOW;
      if (surfaceY >= ROCK_LINE) return BLOCK_STONE;
      return BLOCK_GRASS;
  }
}

uint8_t surfaceBlockAt(int wx, int wz) {
  return surfaceBlockFor(columnAt(wx, wz));
}

void findSpawnColumn(int& outX, int& outZ) {
  // Solid ground with most of its surroundings also above water, so the
  // player starts on a real island/continent rather than a sandbar.
  static const int OFF[12][2] = {
    { 8, 0 }, { -8, 0 }, { 0, 8 }, { 0, -8 },
    { 8, 8 }, { 8, -8 }, { -8, 8 }, { -8, -8 },
    { 16, 0 }, { -16, 0 }, { 0, 16 }, { 0, -16 },
  };
  int fallbackX = 0, fallbackZ = 0;
  bool haveFallback = false;

  for (int radius = 0; radius <= WORLD_RADIUS - 24; radius += 4) {
    for (int a = 0; a < 360; a += 10) {
      double rad = a * 3.14159265358979323846 / 180.0;
      int x = (int)std::lround(std::cos(rad) * radius);
      int z = (int)std::lround(std::sin(rad) * radius);
      if (!inWorldBorder(x - 20, z - 20) || !inWorldBorder(x + 20, z + 20)) continue;
      ColumnInfo info = columnAt(x, z);
      if (info.surfaceY <= SEA_LEVEL + 2 || info.canyonCut) continue;
      if (!haveFallback) {
        fallbackX = x;
        fallbackZ = z;
        haveFallback = true;
      }
      int landAround = 0;
      for (const int* o : OFF) {
        if (columnAt(x + o[0], z + o[1]).surfaceY > SEA_LEVEL) landAround++;
      }
      if (landAround >= 9) { // solid ground, not a sandbar
        outX = x;
        outZ = z;
        return;
      }
    }
  }
  outX = haveFallback ? fallbackX : 0;
  outZ = haveFallback ? fallbackZ : 0;
}

// Fills one terrain column (below and at the surface) for its biome/zone.
const int COAL_MIN_DEPTH = 5; // first coal sits 5 blocks under the surface

// Coal seams. There is only 2D noise in this engine, so the field is made
// pseudo-3D by shearing the sample point with y: each layer reads a shifted
// slice, which gives lens-shaped seams that drift as you dig instead of one
// pattern stamped identically down the whole column. The threshold is tuned
// against the measured underground share (see the coal_* selftests) rather
// than guessed — simplex output is not uniform, so a "20% of the range"
// threshold would not give 20% of the blocks.
static const double COAL_THRESHOLD = 0.415;

static bool coalAt(int wx, int y, int wz) {
  const Gen& g = gen();
  double n = g.noiseCoal(wx * 0.09 + y * 0.25, wz * 0.09 - y * 0.19);
  return n > COAL_THRESHOLD;
}

static void fillColumn(Chunk& chunk, int lx, int lz, int wx, int wz, const ColumnInfo& info) {
  int surfaceY = info.surfaceY;
  bool beach = surfaceY <= SEA_LEVEL + 1;
  // Elevation banding on normal land: bare rock above ROCK_LINE, snow cap
  // above SNOW_LINE. Applies at any distance, so a tall inland mountain
  // reads grass at the base, then rock, then snow.
  bool rocky = info.biome == BIOME_PLAINS && surfaceY >= ROCK_LINE;
  bool snowy = info.biome == BIOME_PLAINS && surfaceY >= SNOW_LINE;

  for (int y = 0; y <= surfaceY; y++) {
    uint8_t id;
    if (y == 0) {
      id = BLOCK_BEDROCK;
    } else if (info.canyonWalls && y < surfaceY) {
      // layered canyon strata, exposed as cliff faces wherever the channel
      // cuts through (rim columns keep their normal surface block on top)
      id = (y % 7 == 3) ? BLOCK_SAND : BLOCK_REDROCK;
    } else {
      switch (info.biome) {
        case BIOME_DESERT:
          id = y >= surfaceY - 3 ? BLOCK_SAND : BLOCK_STONE;
          break;
        case BIOME_SNOW:
          if (y == surfaceY) id = BLOCK_SNOW;
          else if (y >= surfaceY - 3) id = BLOCK_DIRT;
          else id = BLOCK_STONE;
          break;
        default: { // plains + canyon surface layers
          if (y == surfaceY) {
            if (info.canyonCut) id = BLOCK_REDROCK; // dry gorge floor
            else if (beach) id = BLOCK_SAND;
            else if (snowy) id = BLOCK_SNOW;
            else if (rocky) id = BLOCK_STONE;
            else id = BLOCK_GRASS;
          } else if (y >= surfaceY - 3) {
            if (rocky) id = BLOCK_STONE;
            else id = beach ? BLOCK_SAND : BLOCK_DIRT;
          } else {
            id = BLOCK_STONE;
          }
          break;
        }
      }
    }
    // Coal replaces stone only, and only well below the surface, so a
    // hillside never shows a black face and you have to dig for it.
    if (id == BLOCK_STONE && y <= surfaceY - COAL_MIN_DEPTH && coalAt(wx, y, wz)) {
      id = BLOCK_COAL;
    }
    chunk.setLocal(lx, y, lz, id);
  }

  // Water fills every column below sea level, with no canyon special case:
  // canyon floors are generated above sea level instead, so lakes and coast
  // stay continuous where they meet a gorge.
  for (int y = surfaceY + 1; y <= SEA_LEVEL; y++) {
    chunk.setLocal(lx, y, lz, BLOCK_WATER);
  }

  if (info.biome == BIOME_SNOW) {
    // No frozen surface: snow-biome lakes and ocean are ordinary water.
    // Icebergs still form there — coherent ice masses floating in open
    // water, with snow-dusted tops.
    if (surfaceY <= SEA_LEVEL - 2) {
      double berg = gen().noiseBerg(wx * 0.025, wz * 0.025);
      if (berg > 0.5) {
        int top = SEA_LEVEL + (int)std::round((berg - 0.5) * 20);
        int from = std::max(surfaceY + 1, SEA_LEVEL - 3);
        int to = std::min(top, CHUNK_HEIGHT - 1);
        for (int y = from; y <= to; y++) {
          chunk.setLocal(lx, y, lz, y >= to - 1 && to > SEA_LEVEL + 1 ? BLOCK_SNOW : BLOCK_ICE);
        }
      }
    }
  }
}

std::unique_ptr<Chunk> generateChunk(int cx, int cz) {
  auto chunk = std::make_unique<Chunk>(cx, cz);
  int ox = chunk->worldOriginX();
  int oz = chunk->worldOriginZ();

  ColumnInfo infos[CHUNK_SIZE * CHUNK_SIZE];

  for (int lz = 0; lz < CHUNK_SIZE; lz++) {
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
      ColumnInfo info = columnAt(ox + lx, oz + lz);
      infos[lz * CHUNK_SIZE + lx] = info;
      fillColumn(*chunk, lx, lz, ox + lx, oz + lz, info);
    }
  }

  // Trees. A tree is only planted if its own canopy fits inside this chunk
  // (leaf writes can't reach a neighbour that may not exist yet) — checked
  // per tree rather than with one worst-case margin, so small grove trees
  // can grow much closer to chunk edges than a 15-block giant.
  for (int lz = 0; lz < CHUNK_SIZE; lz++) {
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
      const ColumnInfo& info = infos[lz * CHUNK_SIZE + lx];
      if (info.surfaceY <= SEA_LEVEL + 1) continue; // no trees on beaches/underwater
      uint8_t surface = chunk->getLocal(lx, info.surfaceY, lz);
      int wx = cx * CHUNK_SIZE + lx;
      int wz = cz * CHUNK_SIZE + lz;
      bool onGrass = surface == BLOCK_GRASS;
      bool onSnow = surface == BLOCK_SNOW && info.biome == BIOME_SNOW;
      if (!onGrass && !onSnow) continue;
      // thickets are dense, open ground is sparse; both keep trunks apart
      bool grove = inGrove(wx, wz);
      bool site = grove
                      ? isTreeSite(wx, wz, GROVE_CELL, GROVE_JITTER, GROVE_BASE, GROVE_FILL)
                      : isTreeSite(wx, wz, OPEN_CELL, OPEN_JITTER, OPEN_BASE, OPEN_FILL);
      if (!site) continue;
      if (onSnow && hash2D(wx + 31, wz + 17) > 0.3) continue; // sparser in snow
      int height = treeHeightFor(wx, wz, info.surfaceY);
      if (height < TREE_MIN_HEIGHT) continue; // no room under the ceiling
      int radius = treeCanopyRadius(height);
      if (lx - radius < 0 || lx + radius >= CHUNK_SIZE ||
          lz - radius < 0 || lz + radius >= CHUNK_SIZE) {
        continue; // crown would cross a chunk boundary
      }
      uint8_t fruitLeafBlock = fruitLeafBlockFor(wx, wz, onGrass); // snow trees never bear fruit
      plantTree(*chunk, lx, info.surfaceY, lz, height, fruitLeafBlock, wx, wz);
    }
  }

  // Ground grass: meadow noise decides which stretches of grassland are
  // grassy at all, so some fields are lush and others bare. Within a
  // meadow, a per-column roll scatters the tufts.
  for (int lz = 0; lz < CHUNK_SIZE; lz++) {
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
      const ColumnInfo& info = infos[lz * CHUNK_SIZE + lx];
      int y = info.surfaceY;
      if (y <= SEA_LEVEL + 1 || y + 1 >= CHUNK_HEIGHT) continue;
      if (chunk->getLocal(lx, y, lz) != BLOCK_GRASS) continue;
      if (chunk->getLocal(lx, y + 1, lz) != BLOCK_AIR) continue; // keep clear of trees
      int wx = cx * CHUNK_SIZE + lx;
      int wz = cz * CHUNK_SIZE + lz;
      double meadow = gen().noiseMeadow(wx * 0.018, wz * 0.018);
      if (meadow <= 0.05) continue; // bare ground here
      // Low density: only ~15% of grass blocks grow a tuft.
      if (hash2D(wx * 3 + 11, wz * 5 + 29) < 0.15) {
        chunk->setLocal(lx, y + 1, lz, BLOCK_TALL_GRASS);
      }
    }
  }

  // Flowers: same meadow gating as the tall-grass tufts above (so they stay
  // confined to lush ground, not scattered over bare dirt patches) but much
  // sparser, and a different hash salt so the two rolls are independent —
  // a meadow cell can end up with a tuft, a flower, or (usually) neither.
  // Runs after the tall-grass pass so the "still air above" check already
  // excludes any cell that pass just claimed.
  for (int lz = 0; lz < CHUNK_SIZE; lz++) {
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
      const ColumnInfo& info = infos[lz * CHUNK_SIZE + lx];
      int y = info.surfaceY;
      if (y <= SEA_LEVEL + 1 || y + 1 >= CHUNK_HEIGHT) continue;
      if (chunk->getLocal(lx, y, lz) != BLOCK_GRASS) continue;
      if (chunk->getLocal(lx, y + 1, lz) != BLOCK_AIR) continue;
      int wx = cx * CHUNK_SIZE + lx;
      int wz = cz * CHUNK_SIZE + lz;
      double meadow = gen().noiseMeadow(wx * 0.018, wz * 0.018);
      if (meadow <= 0.05) continue;
      if (hash2D(wx * 7 + 41, wz * 11 + 53) < 0.04) {
        int kind = (int)(hash2D(wx * 13 + 61, wz * 17 + 67) * FLOWER_KIND_COUNT);
        kind = std::min(kind, FLOWER_KIND_COUNT - 1);
        chunk->setLocal(lx, y + 1, lz, FLOWER_BLOCKS[kind]);
      }
    }
  }

  // Wild farm patches (see isFarmSite): a FARM_PATCH_SIZE^2 clearing of
  // farmland with crops planted at mixed random growth stages — several
  // land mature just from the count (up to 16 cells, 1-in-4 chance each),
  // enough to reliably walk away with a seed item. Skipped entirely (like an
  // oversized tree canopy) if the footprint would cross this chunk's edge,
  // or if the anchor cell itself isn't grass — but plains terrain still
  // undulates a little even where it reads as "flat", so each of the 16
  // cells is placed at ITS OWN natural height rather than forcing the whole
  // patch level: requiring exact uniform height rejected nearly every site
  // in practice (real terrain noise, not a corner case) and left the map
  // with none at all. A slightly stepped patch still reads fine as a found
  // clearing; a cell that isn't grass (a stray tree, a dip into sand/water)
  // just stays untouched rather than aborting the whole patch. Not
  // registered in World::cropTimers (that only exists for player-driven
  // planting via tryPlantCrop) — a found immature stalk is a static
  // discovery, not a ticking plant, until a player mines and replants it.
  for (int lz = 0; lz < CHUNK_SIZE; lz++) {
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
      int wx = cx * CHUNK_SIZE + lx;
      int wz = cz * CHUNK_SIZE + lz;
      if (!isFarmSite(wx, wz)) continue;
      if (lx + FARM_PATCH_SIZE - 1 >= CHUNK_SIZE || lz + FARM_PATCH_SIZE - 1 >= CHUNK_SIZE) continue;
      const ColumnInfo& anchor = infos[lz * CHUNK_SIZE + lx];
      if (anchor.surfaceY <= SEA_LEVEL + 1 || anchor.biome != BIOME_PLAINS ||
          chunk->getLocal(lx, anchor.surfaceY, lz) != BLOCK_GRASS) {
        continue;
      }
      for (int dz = 0; dz < FARM_PATCH_SIZE; dz++) {
        for (int dx = 0; dx < FARM_PATCH_SIZE; dx++) {
          const ColumnInfo& cell = infos[(lz + dz) * CHUNK_SIZE + (lx + dx)];
          int y = cell.surfaceY;
          if (y <= SEA_LEVEL + 1 || y + 1 >= CHUNK_HEIGHT || cell.biome != BIOME_PLAINS ||
              chunk->getLocal(lx + dx, y, lz + dz) != BLOCK_GRASS ||
              chunk->getLocal(lx + dx, y + 1, lz + dz) != BLOCK_AIR) {
            continue;
          }
          int fx = wx + dx, fz = wz + dz;
          chunk->setLocal(lx + dx, y, lz + dz, BLOCK_FARMLAND);
          int kind = std::min((int)(hash2D(fx * 13 + 71, fz * 17 + 79) * CROP_KIND_COUNT), CROP_KIND_COUNT - 1);
          int stage = std::min((int)(hash2D(fx * 19 + 83, fz * 23 + 89) * CROP_STAGE_COUNT), CROP_STAGE_COUNT - 1);
          chunk->setLocal(lx + dx, y + 1, lz + dz, (uint8_t)(CROP_BASE_BLOCKS[kind] + stage));
        }
      }
    }
  }

  chunk->dirty = true;
  return chunk;
}
