#pragma once
#include "blocks.h"

// Equippable tools: items that can be dragged into the player's main-hand
// slot (Inventory::mainHand) and rendered gripped in the right hand, with a
// swing animation while mining. New tools plug in by adding a row to
// TOOL_VISUALS in tools.cpp — nothing else in the game needs to change.

// How the business end is built. The haft is common to every tool; only the
// head differs, so a new tool is usually an existing shape with different
// materials, and a genuinely new silhouette is one more case in drawHead.
enum ToolShape : uint8_t {
  TOOL_SHAPE_PICKAXE, // symmetric arc, a prong drooping fore and aft
  TOOL_SHAPE_AXE,     // single asymmetric blade, flaring on the forward side
  TOOL_SHAPE_SWORD,   // straight tapering blade with a crossguard
  TOOL_SHAPE_SHOVEL,  // single narrow tapering blade, symmetric
  TOOL_SHAPE_HOE,     // flat wide blade mounted perpendicular to the haft
  TOOL_SHAPE_STICK,   // no head at all — just the bare haft, gripped plain
};

struct ToolVisual {
  uint8_t item;
  uint8_t headBlock;   // block whose texture colors the business end
  uint8_t handleBlock; // block whose texture colors the haft
  uint8_t shape;       // ToolShape
};

// nullptr if `id` is not an equippable tool.
const ToolVisual* toolVisualFor(uint8_t id);
bool isToolItem(uint8_t id);

// True for a tool/weapon held as its own hand-drawn art (art\README.md —
// currently the sword and the power axe) rather than the generic
// per-ToolShape box geometry. These are a thin extruded slab, not a
// silhouette built wide on the axis the camera sees, so both
// drawGrippedTool and drawFirstPersonArm give them the same extra grip
// yaw/lean/tip a plain ToolShape-keyed check would miss for anything
// that isn't literally shaped like a sword.
bool isSpriteTool(uint8_t id);

// How much damage a swing with this item does against an animal. The
// caller (main.cpp's tryMine) passes whatever's gripped in the mainHand
// slot when something equippable (toolVisualFor/isToolItem above) is
// there — that's what's actually rendered swinging in the player's hand —
// falling back to the hotbar selection otherwise. Pickaxe/hoe 1.0, axe 1.5,
// sword 2.5, +0.5 for the stone tier of each; anything else (a block, an
// empty slot, ...) is a bare hand at 1.0. The one deliberate exception is a
// bare stick at 0.5, below even the bare-hand floor — it is equippable but
// not meant as a real weapon.
double attackPower(uint8_t selectedItemId);

// Shape of a tool swing over `swingT` (0..1): -1 is fully wound up with the
// tool raised back, +1 is driving down through the strike, 0 is rest.
// Deliberately asymmetric — a smash lifts briefly, comes down hard, then
// eases back, unlike the even out-and-back arc of a bare-handed punch. The
// arm and the tool both follow this so they move as one piece.
double toolSwingPhase(double swingT);

// Draws the given tool gripped in the hand. The caller must have already
// translated the modelview so (0,0,0) is the grip point, in the player
// model's own "px" units (the same scale as its box parts, e.g. the
// 4x12x4 arm); this function then applies its own relative grip roll and
// tilt (driven by `swingT`, 0..1 across a mining swing, 0 outside of one)
// on top of whatever ambient rotation is already active. Callers may cancel
// the arm bone's own rotation first for a fully independent tilt, or leave
// it in place to have the tool ride rigidly with the swing — whichever
// keeps the tool from lining up edge-on with the camera for that view.
// Binds the block texture atlas itself.
void drawGrippedTool(uint8_t item, double swingT);
