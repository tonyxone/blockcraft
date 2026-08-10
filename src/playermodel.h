#pragma once
#include "player.h"

// Blocky player character (classic Minecraft-Steve proportions: 8x8x8 head,
// 8x12x4 torso, 4x12x4 limbs) with a procedurally painted skin texture.
// Requires a current GL context.
void playerModelInit();

// Selectable character look. Alex uses the slimmer 3px-wide arms and a
// distinct skin (longer hair, green eyes, pink shirt, blue jeans).
enum class PlayerCharacter { Steve, Alex };
void playerModelSetCharacter(PlayerCharacter c);

// Animation inputs, all smoothed/advanced by the caller each frame.
struct PlayerAnim {
  double walkPhase = 0;  // radians; advances with distance walked on ground
  double walkAmount = 0; // 0..1, how hard the limbs swing (scales with speed)
  double air = 0;        // 0..1 blend into the airborne (jump) pose
  double swing = 0;      // 0..1 progress of the collect/build arm swing
  bool swingLeft = false; // which arm swings: right collects, left builds
  int heldTool = -1;     // CraftItem id gripped in the right hand, or -1 for none

  // Riding a boat (boat.h): overrides the walk/air/swing pose above
  // entirely — both legs bend forward at the hip (sitting; there's no knee
  // joint to bend instead, same single-box-limb simplification the walk
  // cycle already makes) and both hands grip a paddle instead of whatever
  // heldTool/swing would otherwise show. rowPhase advances while actually
  // rowing (the boat's own forward/back input), frozen while just sitting.
  bool boating = false;
  double rowPhase = 0;
};

// Draws the character at the player's position, facing the player's yaw.
// Call inside the 3D pass (third-person view).
void drawPlayerModel(const Player& player, const PlayerAnim& anim);

// First-person viewmodel: the player's own arm swinging up into view when
// mining or placing — the right arm for collecting, the left for building,
// each entering from its own side of the screen. `swing` is 0..1 across the
// swing; nothing is drawn outside that. Rendered in its own projection with
// the depth buffer cleared, so the arm can never clip into nearby terrain.
void drawFirstPersonArm(double swing, bool leftHand, int heldTool, int winW, int winH);

// Inventory-screen preview: the character standing inside the given screen
// rect (top-left origin, px), facing forward so its face is visible. Rendered
// in its own viewport/projection with the depth buffer cleared, so it can be
// drawn between the 2D panel and the 2D slot contents.
void drawInventoryPlayerPreview(int winW, int winH, double x, double y, double w, double h);
