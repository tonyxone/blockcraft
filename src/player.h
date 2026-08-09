#pragma once
#include "physics.h"

struct MoveInput {
  int forward = 0; // W - S
  int right = 0;   // D - A
  bool jump = false;
  bool sprint = false; // Shift held: 2x walk speed
};

class Player {
public:
  Vec3 position; // feet position
  Vec3 velocity;
  double yaw = 0;
  double pitch = 0;
  bool onGround = false;
  bool jumpCharging = false;
  double jumpChargeTime = 0;
  bool climbing = false;    // grabbed onto a ladder: gravity is off, W/S move vertically
  bool spaceWasDown = false; // last frame's jump input, to catch the tap that toggles climbing

  // Survival stats, vanilla's own 20-point scale (each heart/drumstick is 2
  // points). No damage source exists yet (no fall damage, no combat against
  // the player), so health only ever moves via hunger-driven regen for now
  // — see Player::update.
  int health = 20, maxHealth = 20;
  int hunger = 20, maxHunger = 20;
  double hungerTimer = 0; // seconds since hunger last ticked down
  double regenTimer = 0;  // seconds since health last regenerated

  // Sleeping in a bed (main.cpp's trySleep) resets health to full for free,
  // but leaves the player drowsy afterward: while this counts down, hunger
  // drains at double speed (see Player::update) — the tradeoff for the
  // instant heal. 0 means no boost active.
  double hungerBoostTimer = 0;

  explicit Player(const Vec3& spawn) : position(spawn) {}

  Vec3 eyePosition() const { return { position.x, position.y + EYE_HEIGHT, position.z }; }

  void update(double dt, World& world, const MoveInput& input);
};
