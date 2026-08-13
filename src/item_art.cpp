#include "item_art.h"
#include "blocks.h"

// Every hand-drawn item sprite lives here. To change how an item looks in the
// inventory, edit its rows and rebuild — nothing else needs to know. To add
// one: rows + an ItemArt below, a tile in blocks.h, a case in itemArtForTile,
// and craftItemTile() in recipes.cpp to point at it.
//
// (An item can also be replaced by a PNG without touching any of this — see
// art\README.md. This is the built-in artwork it falls back to.)
//
// Shapes are researched from the real items (minecraft.wiki): the crafting
// table carries a 3x3 grid on its top face, a chest is a wooden box with a
// banded lid and a metal latch on the front, a furnace is a stone block with
// a dark opening in its face. The wiki has no pixel-level sprite data — only
// image files — so the artwork here follows those descriptions and the
// familiar silhouettes rather than copying any texture.
namespace {

// --- palette ---------------------------------------------------------------
const uint32_t WOOD = 0x96693a, WOOD_D = 0x5c3c1a;
const uint32_t DARKWOOD = 0x6b4423, DARKWOOD_D = 0x40280f;
const uint32_t STONE = 0xa6a6aa, STONE_D = 0x6a6a6e;
const uint32_t SHADOW = 0x3a3a3d, SHADOW_D = 0x1f1f21;
const uint32_t IRON = 0xc8c8cc, IRON_D = 0x8a8a90;
const uint32_t SAND = 0xddcb8a, SAND_D = 0xc4b06a;
const uint32_t SNOW = 0xf2f6fa, SNOW_D = 0xd8e2ec;
const uint32_t ICE = 0x8fb8e8, ICE_D = 0x6e9bd6;
const uint32_t FLAME = 0xff9430, FLAME_D = 0xd8641a;
const uint32_t MEAT_RAW = 0xd9706e, MEAT_RAW_D = 0xa8433f;
const uint32_t MEAT_RAW_HI = 0xec9d94, MEAT_RAW_HI_D = 0xc97a72;
const uint32_t MEAT_RAW_DK = 0xa8433f, MEAT_RAW_DK_D = 0x7c2e2b;
const uint32_t MEAT_COOKED = 0x9a5a34, MEAT_COOKED_D = 0x6e3c1f;
const uint32_t MEAT_COOK_HI = 0xc98a5c, MEAT_COOK_HI_D = 0xa06a42;
const uint32_t MEAT_COOK_DK = 0x6e3c1f, MEAT_COOK_DK_D = 0x4a2812;
const uint32_t BONE = 0xe8e0c0, BONE_D = 0xbfb58f;
const uint32_t BONE_HI = 0xfbf6e4, BONE_HI_D = 0xd8d0b4;
const uint32_t GRILL = 0x3a2414, GRILL_D = 0x241608;
const uint32_t APPLE_RED = 0xd42a2a, APPLE_RED_D = 0x9c1c1c;
const uint32_t APPLE_HI = 0xf0524a, APPLE_HI_D = 0xc23a34;
const uint32_t LEAF_GREEN = 0x4f9c2a, LEAF_GREEN_D = 0x3a7a1e;
const uint32_t PEACH_BASE = 0xf5a15c, PEACH_BASE_D = 0xc9793a;
const uint32_t PEACH_HI = 0xffc98a, PEACH_HI_D = 0xf0a15c;
const uint32_t PEACH_CREASE = 0xb8641e, PEACH_CREASE_D = 0x8f4a12;
const uint32_t PEAR_BASE = 0xc8d24a, PEAR_BASE_D = 0xa0ab2e;
const uint32_t PEAR_HI = 0xe0ea6e, PEAR_HI_D = 0xb8c34a;
const uint32_t CHERRY_RED = 0xb81c3a, CHERRY_RED_D = 0x821229;
const uint32_t CHERRY_HI = 0xe04060, CHERRY_HI_D = 0xb02a48;
const uint32_t ORANGE_BASE = 0xf5921e, ORANGE_BASE_D = 0xc9720f;
const uint32_t ORANGE_HI = 0xffb84f, ORANGE_HI_D = 0xe89a35;
const uint32_t POTION_CORK = 0x8a5a2e, POTION_CORK_D = 0x5c3a1a;
const uint32_t POTION_GLASS = 0xdfeef7, POTION_GLASS_D = 0xb8d4e6;
const uint32_t POTION_HI = 0xff5b52, POTION_HI_D = 0xe0362c;
const uint32_t POTION_RED = 0xd81f28, POTION_RED_D = 0x9c1418;
const uint32_t POTION_DARK = 0x8f1014, POTION_DARK_D = 0x600a0d;

// --- tool shapes -----------------------------------------------------------
// 'H' head / business end, 'S' shaft. Shared between material tiers: a wooden
// pickaxe is these rows with a wooden palette.

// A wide head arc with prongs turning down at both ends, haft on the diagonal.
const char* const PICKAXE_ROWS[TILE_PX] = {
  "................",
  ".....HHHHHH.....",
  "..HHH......HHH..",
  "HH.....SS.....HH",
  ".......SS.......",
  "......SS........",
  "......SS........",
  ".....SS.........",
  ".....SS.........",
  "....SS..........",
  "....SS..........",
  "...SS...........",
  "...SS...........",
  "..SS............",
  "..SS............",
  "................",
};

// A SINGLE asymmetric blade hanging off one side, widest at the cutting edge
// — that lopsidedness is what separates it from the pickaxe at slot size.
const char* const AXE_ROWS[TILE_PX] = {
  "................",
  ".....HHHH.SS....",
  "..HHHHHHHHSSH...",
  "HHHHHHHHHHSSHH..",
  ".HHHHHHHHHSSH...",
  "...HHHHHHSSH....",
  "....HHHH.SS.....",
  "........SS......",
  "........SS......",
  ".......SS.......",
  ".......SS.......",
  "......SS........",
  "......SS........",
  ".....SS.........",
  ".....SS.........",
  "................",
};

// A broad spade blade square on the end of the haft.
const char* const SHOVEL_ROWS[TILE_PX] = {
  "................",
  ".......HHH......",
  "......HHHHH.....",
  "......HHHHH.....",
  "......HHHHH.....",
  ".......HHH......",
  ".......SS.......",
  "......SS........",
  "......SS........",
  ".....SS.........",
  ".....SS.........",
  "....SS..........",
  "....SS..........",
  "...SS...........",
  "...SS...........",
  "................",
};

// Traced from Minecraft's own wooden sword sprite (minecraft.wiki): a long
// 3px-wide blade on the diagonal from the top-right corner, a crossguard bar
// PERPENDICULAR to the blade crossing it two-thirds of the way down, then a
// short grip continuing the blade's diagonal to the bottom-left corner. The
// guard's perpendicular arm is what stops it reading as a shovel or a stick.
const char* const SWORD_ROWS[TILE_PX] = {
  ".............HHH",
  "............HHHH",
  "...........HHHHH",
  "..........HHHHH.",
  ".........HHHHH..",
  "........HHHHH...",
  "..GG...HHHHH....",
  "..GGG.HHHHH.....",
  "...GGHHHHH......",
  "...GGGHHH.......",
  "....GGGG........",
  "...SSSGGG.......",
  "..SSS.GGG.......",
  "SSS...GG........",
  "SSS.............",
  "SSS.............",
};

// Traced from the reference (hoe_ref.png / hoe_ref_big.png, project root): a
// small flat blade sitting across the top of a long diagonal handle, same
// diagonal-shaft construction as the pickaxe/axe but with a much smaller,
// flatter head — that compactness is what tells it apart from the axe.
const char* const HOE_ROWS[TILE_PX] = {
  "........HHH.....",
  "......HHHHHHH...",
  "......HHHHHHHH..",
  "........HHH.....",
  "........SS......",
  ".......SS.......",
  ".......SS.......",
  "......SS........",
  "......SS........",
  ".....SS.........",
  ".....SS.........",
  "....SS..........",
  "....SS..........",
  "...SS...........",
  "..SS............",
  "SSS.............",
};

// --- material shapes -------------------------------------------------------
// 'M' the material, 'D' its darker seam/shadow. Shared by every tier, so a
// stone slab and a wooden slab are one shape with two palettes.

// Stacked boards with seams between them.
const char* const PLANKS_ROWS[TILE_PX] = {
  "................",
  "................",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "................",
  "................",
  "................",
};

// A bare shaft on the diagonal, no head — the absence is the whole point.
const char* const STICK_ROWS[TILE_PX] = {
  "................",
  "...........SS...",
  "..........SS....",
  "..........SS....",
  ".........SS.....",
  ".........SS.....",
  "........SS......",
  "........SS......",
  ".......SS.......",
  ".......SS.......",
  "......SS........",
  "......SS........",
  ".....SS.........",
  ".....SS.........",
  "................",
  "................",
};

// The 3x3 grid on the top face is the whole identity of this block.
const char* const CRAFTING_TABLE_ROWS[TILE_PX] = {
  "................",
  "..DDDDDDDDDDDD..",
  "..DMMMDMMMDMMD..",
  "..DDDDDDDDDDDD..",
  "..DMMMDMMMDMMD..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "................",
  "................",
  "................",
};

// Banded lid with the metal latch on the front face.
const char* const CHEST_ROWS[TILE_PX] = {
  "................",
  "................",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDIIDDDDD..",
  "..MMMMMIIMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "................",
  "................",
  "................",
  "................",
  "................",
};

// Stone body with the dark opening in its face.
const char* const FURNACE_ROWS[TILE_PX] = {
  "................",
  "................",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMKKKKKKKKMM..",
  "..MMKKKKKKKKMM..",
  "..MMKKKKKKKKMM..",
  "..MMKKKKKKKKMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "................",
  "................",
  "................",
  "................",
  "................",
};

// Half height, sitting low in the slot so it reads against a full block.
const char* const SLAB_ROWS[TILE_PX] = {
  "................",
  "................",
  "................",
  "................",
  "................",
  "................",
  "................",
  "................",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "................",
  "................",
  "................",
};

// The stepped profile, seen from the side.
const char* const STAIRS_ROWS[TILE_PX] = {
  "................",
  "................",
  "................",
  ".........MMMMM..",
  ".........MMMMM..",
  ".........DDDDD..",
  "....MMMMMMMMMM..",
  "....MMMMMMMMMM..",
  "....DDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "................",
  "................",
  "................",
  "................",
};

// Courses of brick, staggered so the joints do not line up.
const char* const BRICKS_ROWS[TILE_PX] = {
  "................",
  "................",
  "..MMMMMDMMMMMM..",
  "..MMMMMDMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMDMMMMMMDMM..",
  "..MMDMMMMMMDMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMDMMMMMM..",
  "..MMMMMDMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMDMMMMMMDMM..",
  "..MMDMMMMMMDMM..",
  "................",
  "................",
  "................",
};

// A plain block with a single band, for the cut/compacted materials.
const char* const BANDED_BLOCK_ROWS[TILE_PX] = {
  "................",
  "................",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "................",
  "................",
  "................",
};

// A plain solid block, for the ones with no seams to show.
const char* const PLAIN_BLOCK_ROWS[TILE_PX] = {
  "................",
  "................",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "................",
  "................",
  "................",
};

// Two posts joined by rails.
const char* const FENCE_ROWS[TILE_PX] = {
  "................",
  "................",
  "..MM......MM....",
  "..MM......MM....",
  "..MMMMMMMMMM....",
  "..MM......MM....",
  "..MM......MM....",
  "..MMMMMMMMMM....",
  "..MM......MM....",
  "..MM......MM....",
  "..MM......MM....",
  "..MM......MM....",
  "................",
  "................",
  "................",
  "................",
};

// Tall, panelled, with a handle on the opening edge.
const char* const DOOR_ROWS[TILE_PX] = {
  "................",
  "....MMMMMMMM....",
  "....MDDDDDDM....",
  "....MDMMMMDM....",
  "....MDMMMMDM....",
  "....MDDDDDDM....",
  "....MMMMMMMM....",
  "....MMMMMMIM....",
  "....MMMMMMMM....",
  "....MDDDDDDM....",
  "....MDMMMMDM....",
  "....MDMMMMDM....",
  "....MDDDDDDM....",
  "....MMMMMMMM....",
  "................",
  "................",
};

// Lying flat, with the hinges showing at both ends.
const char* const TRAPDOOR_ROWS[TILE_PX] = {
  "................",
  "................",
  "................",
  "................",
  "..MMMMMMMMMMMM..",
  "..IMMMMMMMMMMI..",
  "..MMMMMMMMMMMM..",
  "..DDDDDDDDDDDD..",
  "..MMMMMMMMMMMM..",
  "..IMMMMMMMMMMI..",
  "..MMMMMMMMMMMM..",
  "................",
  "................",
  "................",
  "................",
  "................",
};

// Two rails and the rungs between them.
const char* const LADDER_ROWS[TILE_PX] = {
  "................",
  "....M......M....",
  "....M......M....",
  "....MMMMMMMM....",
  "....M......M....",
  "....M......M....",
  "....MMMMMMMM....",
  "....M......M....",
  "....M......M....",
  "....MMMMMMMM....",
  "....M......M....",
  "....M......M....",
  "....MMMMMMMM....",
  "....M......M....",
  "....M......M....",
  "................",
};

// Seen from above: a wood frame around a red mattress, with a pillow patch
// at one end.
const char* const BED_ROWS[TILE_PX] = {
  "................",
  "................",
  "..MMMMMMMMMMMM..",
  "..MWWWWWWWWWWM..",
  "..MWWWWWWWWWWM..",
  "..MRRRRRRRRRRM..",
  "..MRRRRRRRRRRM..",
  "..MRRRRRRRRRRM..",
  "..MRRRRRRRRRRM..",
  "..MRRRRRRRRRRM..",
  "..MRRRRRRRRRRM..",
  "..MMMMMMMMMMMM..",
  "................",
  "................",
  "................",
  "................",
};

// Seen from above (Desktop\animal\Oak_Boat_JE4_BE2.png): a hull pointed at
// both bow and stern, hollow inside with a plank seat crossing the middle.
const char* const BOAT_ROWS[TILE_PX] = {
  "................",
  ".......MM.......",
  "......MMMM......",
  ".....MDDDDM.....",
  "....MDDDDDDM....",
  "...MDDDDDDDDM...",
  "...MDDDDDDDDM...",
  "...MRRRRRRRRM...",
  "...MRRRRRRRRM...",
  "...MDDDDDDDDM...",
  "....MDDDDDDM....",
  ".....MDDDDM.....",
  "......MMMM......",
  ".......MM.......",
  "................",
  "................",
};

// A crossed pile of logs with a flame licking up from the middle.
const char* const CAMPFIRE_ROWS[TILE_PX] = {
  "................",
  ".......F........",
  "......FFF.......",
  ".....FFFFF......",
  "....FFDFFDF.....",
  "...FFFFFFFFF....",
  "..FFFDFFFDFF....",
  "................",
  "MM............MM",
  ".MM..........MM.",
  "..MM........MM..",
  "...MMMMMMMMMM...",
  "..MM........MM..",
  ".MM..........MM.",
  "MM............MM",
  "................",
};

// A meat haunch on a bone, matching the HUD's drumstick icon (main.cpp's
// drawDrumstickIcon, traced from vanilla): a rounded meat blob — highlight
// top-left, darker shade bottom-right — with a white bone sticking out of
// its lower-right corner, tipped with a shine pixel. Shared silhouette for
// raw and cooked; the palettes do the telling apart (pink raw vs browned
// cooked), per the request that they be "easy to spot" apart.
const char* const DRUMSTICK_ROWS[TILE_PX] = {
  "................",
  "................",
  "....HHMMM.......",
  "..HHHMMMMM......",
  ".HHHMMMMMMM.....",
  ".HHMMMMMMMM.....",
  ".HMMMMMMMMMD....",
  ".MMMMMMMMMDD....",
  ".MMMMMMMMMDD....",
  "..MMMMMMMDDD....",
  "...MMMMDDDDBB...",
  "....MMDD....BB..",
  ".....DD.....BB..",
  "...........BWB..",
  "...........BBB..",
  "................",
};

// A true round body (radius profile, not a hand-tapered diamond) with a
// short top stem and a leaf beside it — the familiar apple silhouette
// (minecraft.wiki): a near-circle, flattened slightly at the very top where
// the stem sits, rounding off (not pointing) at the bottom.
const char* const APPLE_ROWS[TILE_PX] = {
  ".......S........",
  ".......S.LL.....",
  ".......S........",
  ".......S........",
  ".....HHMMDD.....",
  "....HHMMMMDD....",
  "...HHMMMMMMDD...",
  "..HHMMMMMMMMDD..",
  "..HHMMMMMMMMDD..",
  "..HHMMMMMMMMDD..",
  "..HHMMMMMMMMDD..",
  "...HHMMMMMMDD...",
  "...HHMMMMMMDD...",
  "....HHMMMMDD....",
  ".....HHMMDD.....",
  "................",
};

// Same round body as the apple, but its own rows: no leaf, and a center
// crease (the characteristic cleft running pole to pole — minecraft.wiki
// describes it as the feature that most separates a peach from a plain
// round fruit) instead.
const char* const PEACH_ROWS[TILE_PX] = {
  ".......S........",
  ".......S........",
  ".......S........",
  ".......S........",
  ".....HHCMDD.....",
  "....HHMCMMDD....",
  "...HHMMCMMMDD...",
  "..HHMMMCMMMMDD..",
  "..HHMMMCMMMMDD..",
  "..HHMMMCMMMMDD..",
  "..HHMMMCMMMMDD..",
  "...HHMMCMMMDD...",
  "...HHMMCMMMDD...",
  "....HHMCMMDD....",
  ".....HHCMDD.....",
  "................",
};

// Bottom-heavy with a long, narrow neck under the stem and a ROUNDED bulb
// at the bottom (not tapered to a point) — the asymmetric silhouette
// (minecraft.wiki) that reads as "pear" instead of a lopsided apple.
const char* const PEAR_ROWS[TILE_PX] = {
  ".......S........",
  ".......HD.......",
  "......HMMD......",
  "......HMMD......",
  ".....HHMMDD.....",
  "....HHMMMMDD....",
  "...HHMMMMMMDD...",
  "..HHMMMMMMMMDD..",
  ".HHMMMMMMMMMMDD.",
  ".HHMMMMMMMMMMDD.",
  ".HHMMMMMMMMMMDD.",
  "..HHMMMMMMMMDD..",
  "...HHMMMMMMDD...",
  "....HHMMMMDD....",
  "......HMMD......",
  "................",
};

// Same round body as the apple again, but with no leaf and a small dark
// navel dimple at the bottom instead — the citrus "blossom end" mark that
// tells it apart from the apple at a glance despite the near-identical
// silhouette (real apples and oranges are both close to spherical).
const char* const ORANGE_ROWS[TILE_PX] = {
  ".......S........",
  ".......S........",
  ".......S........",
  ".......S........",
  ".....HHMMDD.....",
  "....HHMMMMDD....",
  "...HHMMMMMMDD...",
  "..HHMMMMMMMMDD..",
  "..HHMMMMMMMMDD..",
  "..HHMMMMMMMMDD..",
  "..HHMMMMMMMMDD..",
  "...HHMMMMMMDD...",
  "...HHMMMMMMDD...",
  "....HHMMMMDD....",
  ".....HHNMDD.....",
  "................",
};

// Two small round fruit joined by a pair of stems meeting at one point —
// the paired-cherry silhouette every cherry icon uses to read as "cherry"
// rather than a single generic red fruit.
const char* const CHERRY_ROWS[TILE_PX] = {
  ".......SS.......",
  "......S..SLL....",
  ".....S....S.....",
  ".....S....S.....",
  "....S......S....",
  "....S......S....",
  "...S........S...",
  "...S........S...",
  "..MMM......MMM..",
  ".MMMMM....MMMMM.",
  "MMMMMMM..MMMMMMM",
  "MHMMMDM..MHMMMDM",
  "MMMMMMD..MMMMMMD",
  ".MMMMD....MMMMD.",
  "..MMM......MMM..",
  "................",
};

// Tall and narrow: cork, thin neck, a slim shouldered body tapering to a
// point at the bottom — the familiar single-serving potion bottle
// (minecraft.wiki). 'C' cork, 'G' glass, 'H' liquid highlight, 'M' liquid
// body, 'D' liquid in shadow near the bottom.
const char* const POTION_SMALL_ROWS[TILE_PX] = {
  "................",
  ".......CC.......",
  ".......CC.......",
  "......GGGG......",
  "......GGGG......",
  ".....GHMMMG.....",
  "....GHMMMMMG....",
  "....GMMMMMMG....",
  "....GDDDDDDG....",
  "....GDDDDDDG....",
  ".....GDDDDG.....",
  "......GDDG......",
  ".......GG.......",
  "................",
  "................",
  "................",
};

// Short and wide: a wider cork over a round, squat flask — deliberately a
// different silhouette from the small bottle above (not just a bigger
// version of it), the way a splash potion reads apart from a regular one.
const char* const POTION_BIG_ROWS[TILE_PX] = {
  "................",
  "......CCCC......",
  "......CCCC......",
  ".....GGGGGG.....",
  "....GHMMMMMG....",
  "...GHMMMMMMMG...",
  "..GHMMMMMMMMMG..",
  "..GMMMMMMMMMMG..",
  "..GDDDDDDDDDDG..",
  "..GDDDDDDDDDDG..",
  "...GDDDDDDDDG...",
  "....GDDDDDDG....",
  ".....GDDDDG.....",
  "......GGGG......",
  "................",
  "................",
};

// --- world textures for placed crafted blocks ------------------------------
// These cover the WHOLE tile: a block face with holes in it would show the
// world through itself. (The outline pass only fires on transparent pixels
// touching the shape, so full coverage also means no outline is added.)
// 'M' the material, 'D' a seam, 'K' shadow/depth, 'I' metal.

const char* const BLK_PLANKS_ROWS[TILE_PX] = {
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
};

// Courses offset by half a brick, so the joints never line up.
const char* const BLK_BRICKS_ROWS[TILE_PX] = {
  "MMMMMMMMDMMMMMMM", "MMMMMMMMDMMMMMMM", "MMMMMMMMDMMMMMMM", "DDDDDDDDDDDDDDDD",
  "MMMMDMMMMMMMDMMM", "MMMMDMMMMMMMDMMM", "MMMMDMMMMMMMDMMM", "DDDDDDDDDDDDDDDD",
  "MMMMMMMMDMMMMMMM", "MMMMMMMMDMMMMMMM", "MMMMMMMMDMMMMMMM", "DDDDDDDDDDDDDDDD",
  "MMMMDMMMMMMMDMMM", "MMMMDMMMMMMMDMMM", "MMMMDMMMMMMMDMMM", "DDDDDDDDDDDDDDDD",
};

const char* const BLK_SANDSTONE_ROWS[TILE_PX] = {
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD", "MMMMMMMMMMMMMMMM",
};

// Bordered planks with a lid seam and a latch — the seam and latch land on
// both the body and the lid, which mesh as separate boxes and so each
// stretch this same texture over their own (different) height, but the
// border and plank lines still read correctly on either piece alone.
const char* const BLK_CHEST_ROWS[TILE_PX] = {
  "DDDDDDDDDDDDDDDD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD",
  "DDDDDDDDDDDDDDDD", "DMMMMMMIIMMMMMMD", "DMMMMMMIIMMMMMMD", "DMMMMMMMMMMMMMMD",
  "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DDDDDDDDDDDDDDDD", "DMMMMMMMMMMMMMMD",
  "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DDDDDDDDDDDDDDDD",
};

const char* const BLK_CHEST_TOP_ROWS[TILE_PX] = {
  "DDDDDDDDDDDDDDDD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD",
  "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD",
  "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD",
  "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DMMMMMMMMMMMMMMD", "DDDDDDDDDDDDDDDD",
};

// The mouth, which is what makes a furnace a furnace from the front.
const char* const BLK_FURNACE_ROWS[TILE_PX] = {
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMKKKKKKKKKKMMM", "MMMKKKKKKKKKKMMM",
  "MMMKKKKKKKKKKMMM", "MMMKKKKKKKKKKMMM", "MMMKKKKKKKKKKMMM", "MMMKKKKKKKKKKMMM",
  "MMMKKKKKKKKKKMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
};

const char* const BLK_FURNACE_TOP_ROWS[TILE_PX] = {
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMMMMM", "MMMMDDDDDDDDMMMM", "MMMMDDDDDDDDMMMM", "MMMMDDDDDDDDMMMM",
  "MMMMDDDDDDDDMMMM", "MMMMDDDDDDDDMMMM", "MMMMDDDDDDDDMMMM", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
};

// The 3x3 grid, seen from above.
// Plain planks, same look as BLK_PLANKS_ROWS — no crafting-grid cross-hatch.
const char* const BLK_TABLE_TOP_ROWS[TILE_PX] = {
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
};

const char* const BLK_TABLE_SIDE_ROWS[TILE_PX] = {
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD", "MMMMMMMMMMMMMMMM",
  "MMKKMMMMMMMMKKMM", "MMKKMMMMMMMMKKMM", "MMMMMMMMMMMMMMMM", "MMMMKKKKKKKKMMMM",
  "MMMMMMMMMMMMMMMM", "MMKKMMMMMMMMKKMM", "MMKKMMMMMMMMKKMM", "MMMMMMMMMMMMMMMM",
  "DDDDDDDDDDDDDDDD", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
};

// Two rails and the rungs between them, the gaps in shadow so the tile stays
// fully covered — the world draws cubes, so a ladder cannot be see-through.
const char* const BLK_LADDER_ROWS[TILE_PX] = {
  "KKMMKKKKKKKKMMKK", "KKMMKKKKKKKKMMKK", "KKMMMMMMMMMMMMKK", "KKMMKKKKKKKKMMKK",
  "KKMMKKKKKKKKMMKK", "KKMMKKKKKKKKMMKK", "KKMMMMMMMMMMMMKK", "KKMMKKKKKKKKMMKK",
  "KKMMKKKKKKKKMMKK", "KKMMKKKKKKKKMMKK", "KKMMMMMMMMMMMMKK", "KKMMKKKKKKKKMMKK",
  "KKMMKKKKKKKKMMKK", "KKMMKKKKKKKKMMKK", "KKMMMMMMMMMMMMKK", "KKMMKKKKKKKKMMKK",
};

const char* const BLK_DOOR_ROWS[TILE_PX] = {
  "MMMMMMMMMMMMMMMM", "MMDDDDDDDDDDDDMM", "MMDDDDDDDDDDDDMM", "MMDDDDDDDDDDDDMM",
  "MMDDDDDDDDDDDDMM", "MMDDDDDDDDDDDDMM", "MMDDDDDDDDDDDDMM", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMIIMM", "MMDDDDDDDDDDDDMM", "MMDDDDDDDDDDDDMM", "MMDDDDDDDDDDDDMM",
  "MMDDDDDDDDDDDDMM", "MMDDDDDDDDDDDDMM", "MMDDDDDDDDDDDDMM", "MMMMMMMMMMMMMMMM",
};

// Pillow band along the head end, mattress filling the rest with plank-style
// seams so it doesn't read as one flat slab.
const char* const BLK_BED_TOP_ROWS[TILE_PX] = {
  "WWWWWWWWWWWWWWWW", "WWWWWWWWWWWWWWWW", "WWWWWWWWWWWWWWWW", "WWWWWWWWWWWWWWWW",
  "DDDDDDDDDDDDDDDD", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR",
  "DDDDDDDDDDDDDDDD", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR",
  "DDDDDDDDDDDDDDDD", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR",
};

// Mattress edge over a wood frame band along the bottom, the same idea as
// the table's top-on-legs side texture.
const char* const BLK_BED_SIDE_ROWS[TILE_PX] = {
  "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR", "DDDDDDDDDDDDDDDD", "RRRRRRRRRRRRRRRR",
  "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR",
  "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRRRRRR",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
};

// Woven log-ends, seen from above.
const char* const BLK_CAMPFIRE_TOP_ROWS[TILE_PX] = {
  "MMMMDDDDMMMMDDDD", "MMMMDDDDMMMMDDDD", "MMMMDDDDMMMMDDDD", "MMMMDDDDMMMMDDDD",
  "DDDDMMMMDDDDMMMM", "DDDDMMMMDDDDMMMM", "DDDDMMMMDDDDMMMM", "DDDDMMMMDDDDMMMM",
  "MMMMDDDDMMMMDDDD", "MMMMDDDDMMMMDDDD", "MMMMDDDDMMMMDDDD", "MMMMDDDDMMMMDDDD",
  "DDDDMMMMDDDDMMMM", "DDDDMMMMDDDDMMMM", "DDDDMMMMDDDDMMMM", "DDDDMMMMDDDDMMMM",
};

// Stacked logs with a band of glowing embers peeking through near the base.
const char* const BLK_CAMPFIRE_SIDE_ROWS[TILE_PX] = {
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
  "FFFFFFFFFFFFFFFF", "FFDFFDFFDFFDFFDF", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
  "DDDDDDDDDDDDDDDD", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
};

const char* const BLK_TRAPDOOR_ROWS[TILE_PX] = {
  "IIMMMMMMMMMMMMII", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "DDDDDDDDDDDDDDDD",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
  "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM", "IIMMMMMMMMMMMMII", "MMMMMMMMMMMMMMMM",
};

// --- sprites ---------------------------------------------------------------
// Shape + palette. The same rows appear more than once on purpose: that is
// what makes a stone axe and a wooden axe one drawing instead of two.

const ItemArt ART_WOOD_PICKAXE = { PICKAXE_ROWS, { { 'H', WOOD, WOOD_D }, { 'S', DARKWOOD, DARKWOOD_D } } };
const ItemArt ART_STONE_PICKAXE = { PICKAXE_ROWS, { { 'H', STONE, STONE_D }, { 'S', WOOD, WOOD_D } } };
const ItemArt ART_WOOD_AXE = { AXE_ROWS, { { 'H', WOOD, WOOD_D }, { 'S', DARKWOOD, DARKWOOD_D } } };
const ItemArt ART_STONE_AXE = { AXE_ROWS, { { 'H', STONE, STONE_D }, { 'S', WOOD, WOOD_D } } };
const ItemArt ART_WOOD_SHOVEL = { SHOVEL_ROWS, { { 'H', WOOD, WOOD_D }, { 'S', DARKWOOD, DARKWOOD_D } } };
const ItemArt ART_STONE_SHOVEL = { SHOVEL_ROWS, { { 'H', STONE, STONE_D }, { 'S', WOOD, WOOD_D } } };
const ItemArt ART_WOOD_SWORD = {
  SWORD_ROWS, { { 'H', WOOD, WOOD_D }, { 'G', DARKWOOD, DARKWOOD_D }, { 'S', DARKWOOD, DARKWOOD_D } }
};
const ItemArt ART_STONE_SWORD = {
  SWORD_ROWS, { { 'H', STONE, STONE_D }, { 'G', DARKWOOD, DARKWOOD_D }, { 'S', WOOD, WOOD_D } }
};
const ItemArt ART_WOOD_HOE = { HOE_ROWS, { { 'H', WOOD, WOOD_D }, { 'S', DARKWOOD, DARKWOOD_D } } };
const ItemArt ART_STONE_HOE = { HOE_ROWS, { { 'H', STONE, STONE_D }, { 'S', WOOD, WOOD_D } } };

const ItemArt ART_PLANKS = { PLANKS_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D } } };
const ItemArt ART_STICK = { STICK_ROWS, { { 'S', WOOD, WOOD_D } } };
const ItemArt ART_CRAFTING_TABLE = {
  CRAFTING_TABLE_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D } }
};
const ItemArt ART_CHEST = {
  CHEST_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D }, { 'I', IRON, IRON_D } }
};
const ItemArt ART_FURNACE = {
  FURNACE_ROWS, { { 'M', STONE, STONE_D }, { 'K', SHADOW, SHADOW_D } }
};
const ItemArt ART_WOOD_SLAB = { SLAB_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D } } };
const ItemArt ART_STONE_SLAB = { SLAB_ROWS, { { 'M', STONE, STONE_D }, { 'D', SHADOW, SHADOW_D } } };
const ItemArt ART_WOOD_STAIRS = { STAIRS_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D } } };
const ItemArt ART_STONE_STAIRS = { STAIRS_ROWS, { { 'M', STONE, STONE_D }, { 'D', SHADOW, SHADOW_D } } };
const ItemArt ART_STONE_BRICKS = { BRICKS_ROWS, { { 'M', STONE, STONE_D }, { 'D', SHADOW, SHADOW_D } } };
const ItemArt ART_SANDSTONE = { BANDED_BLOCK_ROWS, { { 'M', SAND, SAND_D }, { 'D', 0xb59a5f, 0x8f7940 } } };
const ItemArt ART_SNOW_BLOCK = { PLAIN_BLOCK_ROWS, { { 'M', SNOW, SNOW_D }, { 'D', 0xc3cedb, 0xa8b6c6 } } };
const ItemArt ART_PACKED_ICE = { PLAIN_BLOCK_ROWS, { { 'M', ICE, ICE_D }, { 'D', 0x5f86bd, 0x466694 } } };
const ItemArt ART_FENCE = { FENCE_ROWS, { { 'M', WOOD, WOOD_D } } };
const ItemArt ART_DOOR = {
  DOOR_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D }, { 'I', IRON, IRON_D } }
};
const ItemArt ART_TRAPDOOR = {
  TRAPDOOR_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D }, { 'I', IRON, IRON_D } }
};
const ItemArt ART_LADDER = { LADDER_ROWS, { { 'M', WOOD, WOOD_D } } };
// Wood-colored throughout — the pillow band ('W') a shade lighter than the
// mattress ('R') so it still reads as a separate part, no red/white cloth.
const ItemArt ART_BED = {
  BED_ROWS, { { 'M', DARKWOOD, DARKWOOD_D }, { 'W', WOOD, WOOD_D }, { 'R', DARKWOOD, DARKWOOD_D } }
};
const ItemArt ART_CAMPFIRE = {
  CAMPFIRE_ROWS, { { 'M', WOOD, WOOD_D }, { 'F', FLAME, FLAME_D }, { 'D', FLAME_D, 0xa8420c } }
};
const ItemArt ART_RAW_MEAT = {
  DRUMSTICK_ROWS, { { 'H', MEAT_RAW_HI, MEAT_RAW_HI_D }, { 'M', MEAT_RAW, MEAT_RAW_D },
                    { 'D', MEAT_RAW_DK, MEAT_RAW_DK_D }, { 'B', BONE, BONE_D },
                    { 'W', BONE_HI, BONE_HI_D } }
};
const ItemArt ART_COOKED_MEAT = {
  DRUMSTICK_ROWS, { { 'H', MEAT_COOK_HI, MEAT_COOK_HI_D }, { 'M', MEAT_COOKED, MEAT_COOKED_D },
                    { 'D', MEAT_COOK_DK, MEAT_COOK_DK_D }, { 'B', BONE, BONE_D },
                    { 'W', BONE_HI, BONE_HI_D } }
};
const ItemArt ART_BOAT = {
  BOAT_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D }, { 'R', WOOD, WOOD_D } }
};

const ItemArt ART_APPLE = {
  APPLE_ROWS, { { 'H', APPLE_HI, APPLE_HI_D }, { 'M', APPLE_RED, APPLE_RED_D },
               { 'D', APPLE_RED_D, 0x6e1414 }, { 'S', DARKWOOD, DARKWOOD_D },
               { 'L', LEAF_GREEN, LEAF_GREEN_D } }
};
const ItemArt ART_PEACH = {
  PEACH_ROWS, { { 'H', PEACH_HI, PEACH_HI_D }, { 'M', PEACH_BASE, PEACH_BASE_D },
               { 'D', PEACH_BASE_D, 0x8f5222 }, { 'S', DARKWOOD, DARKWOOD_D },
               { 'C', PEACH_CREASE, PEACH_CREASE_D } }
};
const ItemArt ART_PEAR = {
  PEAR_ROWS, { { 'H', PEAR_HI, PEAR_HI_D }, { 'M', PEAR_BASE, PEAR_BASE_D },
              { 'D', PEAR_BASE_D, 0x767f1e }, { 'S', DARKWOOD, DARKWOOD_D } }
};
const ItemArt ART_ORANGE = {
  ORANGE_ROWS, { { 'H', ORANGE_HI, ORANGE_HI_D }, { 'M', ORANGE_BASE, ORANGE_BASE_D },
                { 'D', ORANGE_BASE_D, 0x8f5709 }, { 'S', DARKWOOD, DARKWOOD_D },
                { 'N', DARKWOOD_D, 0x241505 } }
};
const ItemArt ART_CHERRY = {
  CHERRY_ROWS, { { 'H', CHERRY_HI, CHERRY_HI_D }, { 'M', CHERRY_RED, CHERRY_RED_D },
                { 'D', CHERRY_RED_D, 0x5c0d1c }, { 'S', DARKWOOD, DARKWOOD_D },
                { 'L', LEAF_GREEN, LEAF_GREEN_D } }
};

const ItemArt ART_POTION_SMALL = {
  POTION_SMALL_ROWS, { { 'C', POTION_CORK, POTION_CORK_D }, { 'G', POTION_GLASS, POTION_GLASS_D },
                       { 'H', POTION_HI, POTION_HI_D }, { 'M', POTION_RED, POTION_RED_D },
                       { 'D', POTION_DARK, POTION_DARK_D } }
};
const ItemArt ART_POTION_BIG = {
  POTION_BIG_ROWS, { { 'C', POTION_CORK, POTION_CORK_D }, { 'G', POTION_GLASS, POTION_GLASS_D },
                     { 'H', POTION_HI, POTION_HI_D }, { 'M', POTION_RED, POTION_RED_D },
                     { 'D', POTION_DARK, POTION_DARK_D } }
};

// World textures for the placed blocks.
const ItemArt BLK_PLANKS = { BLK_PLANKS_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D } } };
const ItemArt BLK_BRICKS = { BLK_BRICKS_ROWS, { { 'M', STONE, STONE_D }, { 'D', 0x6f6f73, 0x4f4f53 } } };
const ItemArt BLK_SANDSTONE = { BLK_SANDSTONE_ROWS, { { 'M', SAND, SAND_D }, { 'D', 0xb59a5f, 0x8f7940 } } };
const ItemArt BLK_CHEST = {
  BLK_CHEST_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D }, { 'I', IRON, IRON_D } }
};
const ItemArt BLK_CHEST_TOP = {
  BLK_CHEST_TOP_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D } }
};
const ItemArt BLK_FURNACE = {
  BLK_FURNACE_ROWS, { { 'M', STONE, STONE_D }, { 'K', SHADOW, SHADOW_D } }
};
const ItemArt BLK_FURNACE_TOP = {
  BLK_FURNACE_TOP_ROWS, { { 'M', STONE, STONE_D }, { 'D', 0x6f6f73, 0x4f4f53 } }
};
const ItemArt BLK_TABLE_TOP = {
  BLK_TABLE_TOP_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D } }
};
const ItemArt BLK_TABLE_SIDE = {
  BLK_TABLE_SIDE_ROWS,
  { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D }, { 'K', SHADOW, SHADOW_D } }
};
const ItemArt BLK_LADDER = {
  BLK_LADDER_ROWS, { { 'M', WOOD, WOOD_D }, { 'K', SHADOW, SHADOW_D } }
};
const ItemArt BLK_DOOR = {
  BLK_DOOR_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D }, { 'I', IRON, IRON_D } }
};
const ItemArt BLK_TRAPDOOR = {
  BLK_TRAPDOOR_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D }, { 'I', IRON, IRON_D } }
};
// Wood-colored throughout, no red/white cloth — the pillow band ('W') a
// shade lighter than the mattress ('R') so it stays visually distinct.
const ItemArt BLK_BED_TOP = {
  BLK_BED_TOP_ROWS, { { 'W', WOOD, WOOD_D }, { 'R', DARKWOOD, DARKWOOD_D }, { 'D', DARKWOOD_D, 0x2c1a09 } }
};
const ItemArt BLK_BED_SIDE = {
  BLK_BED_SIDE_ROWS, { { 'R', DARKWOOD, DARKWOOD_D }, { 'D', DARKWOOD_D, 0x2c1a09 }, { 'M', WOOD, WOOD_D } }
};
const ItemArt BLK_CAMPFIRE_TOP = {
  BLK_CAMPFIRE_TOP_ROWS, { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D } }
};
const ItemArt BLK_CAMPFIRE_SIDE = {
  BLK_CAMPFIRE_SIDE_ROWS,
  { { 'M', WOOD, WOOD_D }, { 'D', DARKWOOD, DARKWOOD_D }, { 'F', FLAME, FLAME_D } }
};

} // namespace

const ItemArt* itemArtForTile(int tile) {
  switch (tile) {
    case TILE_PLANKS: return &ART_PLANKS;
    case TILE_STICK: return &ART_STICK;
    case TILE_CRAFTING_TABLE: return &ART_CRAFTING_TABLE;
    case TILE_CHEST: return &ART_CHEST;
    case TILE_FURNACE: return &ART_FURNACE;
    case TILE_WOOD_SLAB: return &ART_WOOD_SLAB;
    case TILE_STONE_SLAB: return &ART_STONE_SLAB;
    case TILE_WOOD_STAIRS: return &ART_WOOD_STAIRS;
    case TILE_STONE_STAIRS: return &ART_STONE_STAIRS;
    case TILE_STONE_BRICKS: return &ART_STONE_BRICKS;
    case TILE_SANDSTONE: return &ART_SANDSTONE;
    case TILE_SNOW_BLOCK: return &ART_SNOW_BLOCK;
    case TILE_PACKED_ICE: return &ART_PACKED_ICE;
    case TILE_FENCE: return &ART_FENCE;
    case TILE_DOOR: return &ART_DOOR;
    case TILE_TRAPDOOR: return &ART_TRAPDOOR;
    case TILE_LADDER: return &ART_LADDER;
    case TILE_BED: return &ART_BED;
    case TILE_CAMPFIRE: return &ART_CAMPFIRE;
    case TILE_RAW_MEAT: return &ART_RAW_MEAT;
    case TILE_COOKED_MEAT: return &ART_COOKED_MEAT;
    case TILE_BOAT: return &ART_BOAT;
    case TILE_APPLE: return &ART_APPLE;
    case TILE_PEACH: return &ART_PEACH;
    case TILE_PEAR: return &ART_PEAR;
    case TILE_CHERRY: return &ART_CHERRY;
    case TILE_ORANGE: return &ART_ORANGE;
    case TILE_POTION_SMALL: return &ART_POTION_SMALL;
    case TILE_POTION_BIG: return &ART_POTION_BIG;
    case TILE_WOOD_PICKAXE: return &ART_WOOD_PICKAXE;
    case TILE_STONE_PICKAXE: return &ART_STONE_PICKAXE;
    case TILE_WOOD_AXE: return &ART_WOOD_AXE;
    case TILE_STONE_AXE: return &ART_STONE_AXE;
    case TILE_WOOD_SHOVEL: return &ART_WOOD_SHOVEL;
    case TILE_STONE_SHOVEL: return &ART_STONE_SHOVEL;
    case TILE_WOOD_SWORD: return &ART_WOOD_SWORD;
    case TILE_STONE_SWORD: return &ART_STONE_SWORD;
    case TILE_WOOD_HOE: return &ART_WOOD_HOE;
    case TILE_STONE_HOE: return &ART_STONE_HOE;
    case TILE_BLK_PLANKS: return &BLK_PLANKS;
    case TILE_BLK_BRICKS: return &BLK_BRICKS;
    case TILE_BLK_SANDSTONE: return &BLK_SANDSTONE;
    case TILE_BLK_CHEST: return &BLK_CHEST;
    case TILE_BLK_CHEST_TOP: return &BLK_CHEST_TOP;
    case TILE_BLK_FURNACE: return &BLK_FURNACE;
    case TILE_BLK_FURNACE_TOP: return &BLK_FURNACE_TOP;
    case TILE_BLK_TABLE_TOP: return &BLK_TABLE_TOP;
    case TILE_BLK_TABLE_SIDE: return &BLK_TABLE_SIDE;
    case TILE_BLK_LADDER: return &BLK_LADDER;
    case TILE_BLK_DOOR: return &BLK_DOOR;
    case TILE_BLK_TRAPDOOR: return &BLK_TRAPDOOR;
    case TILE_BLK_BED_TOP: return &BLK_BED_TOP;
    case TILE_BLK_BED_SIDE: return &BLK_BED_SIDE;
    case TILE_BLK_CAMPFIRE_TOP: return &BLK_CAMPFIRE_TOP;
    case TILE_BLK_CAMPFIRE_SIDE: return &BLK_CAMPFIRE_SIDE;
    default: return nullptr;
  }
}
