#include "tools.h"
#include "recipes.h"
#include "textures.h"
#include "sprites_generated.h"
#include "gfx.h"
#include "win_gl.h"

// Real Minecraft tools are flat pixel-art textures held at a diagonal and
// extruded to a thin 3D slab (researched: minecraft.wiki + modding docs on
// the item/handheld model). Extruding the sprite was tried here and looked
// worse in-hand than purpose-built geometry, so a held tool is a small
// assembly of boxes textured with the world's own block tiles: chunkier,
// and it reads at a glance from the chase camera where a flat slab does not.
//
// The 2D slot icon is separate art (the sprites in textures.cpp) — the two
// are hand-matched rather than derived from each other.
namespace {

// Every craftable tool/weapon is equippable — a wood-tier head is textured
// with BLOCK_WOOD (there is no separate world block for planks), a
// stone-tier head with BLOCK_STONE.
const ToolVisual TOOL_VISUALS[] = {
  { ITEM_WOOD_PICKAXE, BLOCK_WOOD, BLOCK_WOOD, TOOL_SHAPE_PICKAXE },
  { ITEM_STONE_PICKAXE, BLOCK_STONE, BLOCK_WOOD, TOOL_SHAPE_PICKAXE },
  { ITEM_WOOD_AXE, BLOCK_WOOD, BLOCK_WOOD, TOOL_SHAPE_AXE },
  { ITEM_STONE_AXE, BLOCK_STONE, BLOCK_WOOD, TOOL_SHAPE_AXE },
  { ITEM_WOOD_SHOVEL, BLOCK_WOOD, BLOCK_WOOD, TOOL_SHAPE_SHOVEL },
  { ITEM_STONE_SHOVEL, BLOCK_STONE, BLOCK_WOOD, TOOL_SHAPE_SHOVEL },
  { ITEM_WOOD_SWORD, BLOCK_WOOD, BLOCK_WOOD, TOOL_SHAPE_SWORD },
  { ITEM_STONE_SWORD, BLOCK_STONE, BLOCK_WOOD, TOOL_SHAPE_SWORD },
  { ITEM_WOOD_HOE, BLOCK_WOOD, BLOCK_WOOD, TOOL_SHAPE_HOE },
  { ITEM_STONE_HOE, BLOCK_STONE, BLOCK_WOOD, TOOL_SHAPE_HOE },
  { ITEM_STICK, BLOCK_WOOD, BLOCK_WOOD, TOOL_SHAPE_STICK },
  // Its own hand-drawn slot icon (art\sword.png), but the same stone-tier
  // blade geometry as ITEM_STONE_SWORD in hand — the art only replaces the
  // 2D icon, per art\README.md.
  { ITEM_SWORD, BLOCK_STONE, BLOCK_WOOD, TOOL_SHAPE_SWORD },
  // Same deal as ITEM_SWORD above: its own hand-drawn slot icon
  // (art\power_axe.png), the ordinary stone-tier axe geometry in hand.
  { ITEM_POWER_AXE, BLOCK_STONE, BLOCK_WOOD, TOOL_SHAPE_AXE },
};
const int TOOL_VISUAL_COUNT = (int)(sizeof(TOOL_VISUALS) / sizeof(TOOL_VISUALS[0]));

// Same face corners/shading as the world mesher (mesher.cpp FACES), so a
// handheld tool catches light the same way the terrain does.
struct BoxFace {
  int dir[3];
  int faceSlot; // 0 top, 1 bottom, 2 side
  uint8_t shade;
  int corners[4][3];
};
const BoxFace BOX_FACES[6] = {
  { { 0, 1, 0 }, 0, 255, { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } } },
  { { 0, -1, 0 }, 1, 154, { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } } },
  { { 1, 0, 0 }, 2, 226, { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } } },
  { { -1, 0, 0 }, 2, 188, { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } } },
  { { 0, 0, 1 }, 2, 202, { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } } },
  { { 0, 0, -1 }, 2, 177, { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } } },
};
const double UV_S[4] = { 0, 0, 1, 1 };
const double UV_T[4] = { 0, 1, 1, 0 };

// Same per-face shading every hand-rolled box in this game uses, scaled
// against each pixel's own colour instead of a fixed material — same idea
// as droppeditem.cpp's own voxelized item look.
struct VoxelFace {
  uint8_t shade;
  int corners[4][3];
};
const VoxelFace VOXEL_FACES[6] = {
  { 255, { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } } },
  { 154, { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } } },
  { 226, { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } } },
  { 188, { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } } },
  { 202, { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } } },
  { 177, { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } } },
};

void drawVoxel(double x0, double y0, double z0, double s, double depth, uint8_t r, uint8_t g,
              uint8_t b) {
  for (const VoxelFace& face : VOXEL_FACES) {
    glColor3ub((uint8_t)(r * face.shade / 255), (uint8_t)(g * face.shade / 255),
              (uint8_t)(b * face.shade / 255));
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
      glVertex3d(x0 + face.corners[i][0] * s, y0 + face.corners[i][1] * s,
                z0 + face.corners[i][2] * depth);
    }
    glEnd();
  }
}

std::unordered_map<int, GLuint> g_spriteToolLists;

// A hand-drawn tool (art\README.md) is held as its own drawing, voxel-
// extruded exactly like a dropped item's icon (droppeditem.cpp) — not the
// generic per-ToolShape box geometry drawHead() below, which for a sword
// would be visually identical to the procedural Stone Sword regardless of
// what art the slot icon carries, defeating the point of supplying custom
// art in the first place. Sampled at the atlas's full ATLAS_TILE_PX
// resolution (not the coarser TILE_PX droppeditem.cpp uses) so a sword
// drawn natively at 32x32 keeps all of its actual detail instead of losing
// three-quarters of it to a stride built for 16x16 procedural art doubled
// up to fill the same tile.
GLuint spriteToolVoxelList(uint8_t item) {
  auto found = g_spriteToolLists.find(item);
  if (found != g_spriteToolLists.end()) return found->second;

  GLuint list = glGenLists(1);
  glNewList(list, GL_COMPILE);
  int tile = craftItemTile(item);
  if (tile >= 0) {
    const Atlas& atlas = buildTextureAtlas();
    const double SIZE = 15.0; // overall height, matching the procedural sword's own scale
    const double CELL = SIZE / ATLAS_TILE_PX;
    const double DEPTH = CELL * 1.5;
    for (int gy = 0; gy < ATLAS_TILE_PX; gy++) {
      for (int gx = 0; gx < ATLAS_TILE_PX; gx++) {
        int ax = tile * ATLAS_TILE_PX + gx;
        size_t idx = (size_t)(gy * atlas.width + ax) * 4; // atlas row 0 = bottom = the grip end
        if (idx + 3 >= atlas.pixels.size() || atlas.pixels[idx + 3] < 128) continue;
        double x0 = (gx - ATLAS_TILE_PX / 2.0) * CELL;
        double y0 = gy * CELL;
        double z0 = -DEPTH / 2.0;
        drawVoxel(x0, y0, z0, CELL, DEPTH, atlas.pixels[idx], atlas.pixels[idx + 1],
                 atlas.pixels[idx + 2]);
      }
    }
  }
  glEndList();
  g_spriteToolLists[item] = list;
  return list;
}

// Immediate-mode box (small one-off shapes redrawn every frame, same style
// as playermodel.cpp's drawBox — not worth a display list).
void drawAtlasBox(double x0, double y0, double z0, double w, double h, double d, uint8_t block) {
  for (const BoxFace& face : BOX_FACES) {
    UVRect uv;
    bool hasUV = getBlockFaceUV(block, face.faceSlot, uv);
    glColor3ub(face.shade, face.shade, face.shade);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
      if (hasUV) {
        double u = uv.u0 + UV_S[i] * (uv.u1 - uv.u0);
        glTexCoord2d(u, UV_T[i]);
      }
      glVertex3d(x0 + face.corners[i][0] * w,
                 y0 + face.corners[i][1] * h,
                 z0 + face.corners[i][2] * d);
    }
    glEnd();
  }
}

// Idle grip: head raised up and FORWARD of the hand, like a tool carried
// ready rather than shouldered. The sign matters — the model faces -Z and
// the haft points up, so a positive X rotation would throw the head to +Z,
// i.e. backwards over the shoulder (which is exactly how it first looked).
const double REST_TILT_DEG = 45.0;
// No outward yaw: the tool sits square in the arm's own swing plane, facing
// front, so it lines up with the limb holding it. Yawing it out (this was
// -35) turned the head across the body and read as carrying the thing at an
// angle to yourself.
const double GRIP_ROLL_DEG = 0.0;
// Extra haft-spin for hand-drawn sprite tools (isSpriteTool) — see
// drawGrippedTool.
const double SPRITE_TOOL_GRIP_YAW_DEG = -30.0;
// The strike drives the head down and forward past horizontal; the windup
// before it is smaller, just enough to read as "raise, then smash".
const double SWING_STRIKE_DEG = 80.0;
const double SWING_LIFT_DEG = 35.0;

// Fractions of the swing spent winding up and driving down. The strike is
// quicker than the recovery, which is what makes it read as a blow landing
// rather than an arm waving back and forth.
const double PHASE_LIFT_END = 0.25;
const double PHASE_STRIKE_END = 0.55;

// Haft length: every two-handed tool (pick/axe/shovel/hoe) shares one long
// grip. A sword is gripped one-handed and reads wrong with that much handle
// under a blade — Minecraft's own sword is mostly blade — so it gets a
// shorter haft, leaving the rest of the tool's total length to the blade.
const double TOOL_HANDLE_LEN = 12.0;
const double SWORD_HANDLE_LEN = 5.0;

// The business end, mounted at the top of the haft (TOOL_HANDLE_LEN, or
// SWORD_HANDLE_LEN for a sword — see drawGrippedTool). Everything is laid
// out along Z — the axis the player faces and swings through — so the
// working edge meets the block, not the tool's flat side (the sword is the
// one exception: its blade continues straight up the haft's own axis).
void drawHead(const ToolVisual& v) {
  switch (v.shape) {
    case TOOL_SHAPE_AXE: {
      // A single blade on the FORWARD (-Z) side, built as a WEDGE: each step
      // away from the haft gets THINNER across X while growing TALLER in Y,
      // ending in a thin, tall cutting edge. Both tapers matter — a bit of
      // constant thickness is a hammer head no matter how it is shaped from
      // the side, which is exactly how the first version came out.
      drawAtlasBox(-1.0, 10.0, -3, 2.0, 4, 3, v.headBlock);    // cheek: thick, short
      drawAtlasBox(-0.7, 9.5, -5, 1.4, 5, 2, v.headBlock);     // mid: thinning, taller
      drawAtlasBox(-0.3, 9.0, -6.5, 0.6, 6, 1.5, v.headBlock); // bit: thin, tallest
      break;
    }
    case TOOL_SHAPE_SWORD: {
      // Matched to the inventory sprite (item_art.cpp SWORD_ROWS, traced
      // from Minecraft's own sword texture): the blade dominates — wider
      // than the thin one-handed grip and FLAT, rising straight up the
      // haft's own axis; a crossguard bar clearly wider than the blade
      // separates grip from blade, and a small pommel caps the grip below
      // the hand. Guard, grip and pommel are handle-material (dark wood in
      // the sprite) — only the blade is head-material.
      //
      // The flat face is wide in Z (not X): every other tool's REST_TILT
      // only rotates about the model's own X axis, so X-width stays
      // edge-on to the first-person camera no matter the tilt, and reads
      // as a bare stick — the axe avoids this because its own blade is
      // already wide in Z (its "reach" axis). Widening the sword's blade
      // in Z instead of X puts it on the same axis the axe already gets
      // right, so it is now genuinely "the same rotation as the axe": no
      // per-shape roll override needed, just building the blade on the
      // axis REST_TILT actually shows off.
      double y0 = SWORD_HANDLE_LEN;
      drawAtlasBox(-0.9, -0.8, -0.9, 1.8, 0.8, 1.8, v.handleBlock);   // pommel
      drawAtlasBox(-0.6, y0, -2.0, 1.2, 0.8, 4.0, v.handleBlock);     // crossguard: wider than blade
      drawAtlasBox(-0.4, y0 + 0.8, -1.1, 0.8, 9.0, 2.2, v.headBlock); // blade: long, wide, flat
      drawAtlasBox(-0.4, y0 + 9.8, -0.55, 0.8, 1.5, 1.1, v.headBlock);// point: narrowing to the tip
      break;
    }
    case TOOL_SHAPE_SHOVEL: {
      // A single narrow blade, tapering just enough to read as a point
      // rather than the axe's broad flared bit.
      drawAtlasBox(-0.8, 10.0, -2, 1.6, 3, 1.5, v.headBlock);
      drawAtlasBox(-0.6, 9.3, -3.2, 1.2, 2, 1.5, v.headBlock);
      break;
    }
    case TOOL_SHAPE_HOE: {
      // A flat wide blade mounted crosswise atop the haft, unlike every
      // other head here which extends forward along Z — reads as "not a
      // pick, not an axe" from its silhouette alone.
      drawAtlasBox(-2.0, 12.5, -3, 4.0, 1, 2, v.headBlock);
      break;
    }
    case TOOL_SHAPE_STICK: {
      // No head at all — the shared haft box IS the whole tool, so there is
      // nothing more to draw. Gripped exactly like every other tool, just
      // with an empty hand past the wrist.
      break;
    }
    case TOOL_SHAPE_PICKAXE:
    default: {
      // A crossbar across the top of the haft with a prong curving down at
      // each end. (Built along X in the first version, which pointed the
      // pick sideways across the body.)
      drawAtlasBox(-1, 12, -4, 2, 2, 8, v.headBlock);
      glPushMatrix();
      glTranslated(0, 13, -4);
      glRotated(-35, 1, 0, 0); // front prong: further -Z and drooping
      drawAtlasBox(-1, -1, -4, 2, 2, 4, v.headBlock);
      glPopMatrix();
      glPushMatrix();
      glTranslated(0, 13, 4);
      glRotated(35, 1, 0, 0); // rear prong: further +Z and drooping
      drawAtlasBox(-1, -1, 0, 2, 2, 4, v.headBlock);
      glPopMatrix();
      break;
    }
  }
}

} // namespace

const ToolVisual* toolVisualFor(uint8_t id) {
  for (const ToolVisual& v : TOOL_VISUALS) {
    if (v.item == id) return &v;
  }
  return nullptr;
}

bool isToolItem(uint8_t id) { return toolVisualFor(id) != nullptr; }

bool isSpriteTool(uint8_t id) {
  return generatedSpriteNamed(spriteNameForTile(craftItemTile(id))) != nullptr;
}

double attackPower(uint8_t selectedItemId) {
  switch (selectedItemId) {
    case ITEM_WOOD_PICKAXE:
    case ITEM_WOOD_HOE:
      return 0.8;
    case ITEM_STONE_PICKAXE:
    case ITEM_STONE_HOE:
      return 1.0;
    case ITEM_WOOD_AXE:
      return 1.0;
    case ITEM_STONE_AXE:
      return 2.0;
    case ITEM_WOOD_SWORD:
      return 1.0;
    case ITEM_STONE_SWORD:
      return 2.0;
    case ITEM_SWORD:
      return 3.0;
    case ITEM_POWER_AXE: // 3 stone axes forged into one — by request
      return 3.0;
    case ITEM_STICK: // equippable, but deliberately below the bare-hand floor
      return 0.5;
    default:
      return 0.1; // bare hand — the floor across every tier
  }
}

double toolWeight(uint8_t item) {
  switch (item) {
    // Light, thin-headed tools: not much more than the haft itself.
    case ITEM_WOOD_HOE: return 0.75;
    case ITEM_STONE_HOE: return 1.00;
    case ITEM_WOOD_SHOVEL: return 0.85;
    case ITEM_STONE_SHOVEL: return 1.15;
    // A sword is long but thin and one-handed — lighter than a wedge-headed
    // axe or a thick two-pronged pickaxe of the same tier.
    case ITEM_WOOD_SWORD: return 0.90;
    case ITEM_STONE_SWORD: return 1.20;
    // Forged from 3 stone swords' worth of material (see CRAFT_RECIPES) —
    // heavier than the standard stone sword it's built from.
    case ITEM_SWORD: return 1.40;
    // A dense wedge head, real axes' own reputation for being front-heavy.
    case ITEM_WOOD_AXE: return 1.10;
    case ITEM_STONE_AXE: return 1.50;
    // Forged from 3 stone axes — heavier than the standard stone axe.
    case ITEM_POWER_AXE: return 2.00;
    // The heaviest ordinary tool: a thick head split into two full prongs
    // plus a crossbar, more metal than any single-bladed shape here.
    case ITEM_WOOD_PICKAXE: return 1.25;
    case ITEM_STONE_PICKAXE: return 1.70;
    // Equippable, but barely more than swinging nothing at all.
    case ITEM_STICK: return 0.45;
    default: return 0.55; // bare hand — light and quick, heavier than a stick's whip but still fast
  }
}

double toolSwingPhase(double swingT) {
  if (swingT <= 0.0 || swingT >= 1.0) return 0.0;
  double t = clampd(swingT, 0, 1);
  if (t < PHASE_LIFT_END) {
    return -(t / PHASE_LIFT_END); // 0 -> -1: raise it back, ready to hit
  }
  if (t < PHASE_STRIKE_END) {
    // -1 -> +1: the blow itself, through rest and on down
    return -1.0 + 2.0 * (t - PHASE_LIFT_END) / (PHASE_STRIKE_END - PHASE_LIFT_END);
  }
  return 1.0 - (t - PHASE_STRIKE_END) / (1.0 - PHASE_STRIKE_END); // +1 -> 0
}

void drawGrippedTool(uint8_t item, double swingT) {
  const ToolVisual* v = toolVisualFor(item);
  if (!v) return;

  int tile = craftItemTile(item);
  const GeneratedSprite* sprite = generatedSpriteNamed(spriteNameForTile(tile));

  // Negative tilt swings the head forward (-Z). The phase adds a windup
  // backwards and then drives well past rest, so the tool comes down on the
  // block instead of merely waving toward it.
  double phase = toolSwingPhase(swingT);
  double swingDeg = phase * (phase >= 0 ? SWING_STRIKE_DEG : SWING_LIFT_DEG);
  double tilt = -(REST_TILT_DEG + swingDeg);

  glPushMatrix();
  // A sprite tool's art is a flat slab (spriteToolVoxelList extrudes it only
  // CELL*1.5 deep); held square like the procedural tools it presents nearly
  // edge-on in first person — only the thin side shows. Spinning it a little
  // around the haft turns the flat of the art toward the eye, so it actually
  // reads as what it's drawn as. Verified against the FP transform chain:
  // -30 degrees visibly widens it on screen, +30 narrows it (fully edge-on).
  // Keyed on isSpriteTool (any hand-drawn item), not ToolShape — a
  // shape-only check missed every non-sword item that later got its own
  // art (the power axe first shipped still held edge-on because of this).
  glRotated(GRIP_ROLL_DEG + (sprite && sprite->rgba ? SPRITE_TOOL_GRIP_YAW_DEG : 0), 0, 1, 0);
  glRotated(tilt, 1, 0, 0);

  if (sprite && sprite->rgba) {
    // Hand-drawn art: held as the actual drawing (see spriteToolVoxelList
    // above) rather than generic per-ToolShape geometry — blade, guard and
    // grip all come from the one image, so nothing more to draw.
    glDisable(GL_TEXTURE_2D);
    glCallList(spriteToolVoxelList(item));
    glEnable(GL_TEXTURE_2D);
  } else {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, atlasTextureId());
    // Handle: a thin haft rising from the grip. Every two-handed tool shares
    // one long haft; a sword's one-handed grip is shorter (SWORD_HANDLE_LEN)
    // AND thinner than the blade above it — a sword gripped on the same 2x2
    // haft as a pickaxe reads as a stick, not a sword.
    if (v->shape == TOOL_SHAPE_SWORD) {
      drawAtlasBox(-0.6, 0, -0.6, 1.2, SWORD_HANDLE_LEN, 1.2, v->handleBlock);
    } else {
      drawAtlasBox(-1, 0, -1, 2, TOOL_HANDLE_LEN, 2, v->handleBlock);
    }
    drawHead(*v);
  }

  glPopMatrix();
}
