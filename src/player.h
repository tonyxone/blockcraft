#pragma once
#include "physics.h"

struct MoveInput {
  int forward = 0; // W - S
  int right = 0;   // D - A
  bool jump = false;
  bool sprint = false;   // Shift held: 2x walk speed
  bool swimDown = false; // C held: float down while swimming (see Player::swimming)
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

  // Swimming: touching water and not climbing (see Player::update) — gravity
  // is replaced by a gentle passive sink, Space/C swim up/down instead of
  // jumping, and WASD keeps moving the same as on foot. playermodel.cpp
  // reads this to tip the whole body into a near-horizontal float.
  bool swimming = false;

  // Breath, vanilla's own 10-bubble scale. Drains only while the EYE (not
  // just the feet) is underwater, refills instantly at the surface, and once
  // it bottoms out at 0 costs health the same way starving does.
  int oxygen = 10, maxOxygen = 10;
  double oxygenTimer = 0; // seconds since oxygen last ticked down
  double drownTimer = 0;  // seconds since health last dropped from drowning

  // Survival stats, vanilla's own 20-point scale (each heart/drumstick is 2
  // points). Health regenerates slowly while hunger is high enough (see
  // Player::update) and drains on its own once hunger bottoms out at 0 —
  // starving, not just going hungry, is what actually costs health.
  int health = 20, maxHealth = 20;
  int hunger = 20, maxHunger = 20;
  double hungerTimer = 0; // seconds since hunger last ticked down
  double regenTimer = 0;  // seconds since health last regenerated
  double starveTimer = 0; // seconds since health last dropped from starvation

  // Set once health reaches 0 (Player::update) and left set until main.cpp's
  // respawn resets it — main.cpp reads this once per frame to know when to
  // switch to the death screen, rather than re-deriving "just died" from
  // health alone (which stays 0, not just the instant it got there).
  bool dead = false;

  // Sleeping in a bed (main.cpp's trySleep) resets health to full for free,
  // but leaves the player drowsy afterward: while this counts down, hunger
  // drains at double speed (see Player::update) — the tradeoff for the
  // instant heal. 0 means no boost active.
  double hungerBoostTimer = 0;

  explicit Player(const Vec3& spawn) : position(spawn) {}

  Vec3 eyePosition() const { return { position.x, position.y + EYE_HEIGHT, position.z }; }

  void update(double dt, World& world, const MoveInput& input);
};
