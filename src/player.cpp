#include "player.h"

static const double WALK_SPEED = 4.3;
static const double GRAVITY = 28;
static const double JUMP_SPEED = 8.6;
static const double TERMINAL_FALL_SPEED = -50;
static const double CLIMB_SPEED = 3.0;
// Hold-to-charge jump: a quick tap gives a normal jump; holding space up to
// JUMP_CHARGE_MAX seconds before releasing ramps the launch speed up to
// JUMP_SPEED_MAX_MULT, which (since height scales with speed^2) makes the
// fully-charged jump 4x as high as a tap.
static const double JUMP_CHARGE_MAX = 0.6;
static const double JUMP_SPEED_MAX_MULT = 2.0;

void Player::update(double dt, World& world, const MoveInput& input) {
  double fx = -std::sin(yaw), fz = -std::cos(yaw);
  double rx = std::cos(yaw), rz = -std::sin(yaw);

  double vx = fx * input.forward + rx * input.right;
  double vz = fz * input.forward + rz * input.right;
  double len = std::hypot(vx, vz);
  if (len > 0) {
    vx /= len;
    vz /= len;
  }
  double speed = WALK_SPEED * (input.sprint ? 2.0 : 1.0);
  velocity.x = vx * speed;
  velocity.z = vz * speed;

  // Space is a toggle, not a hold: tap it next to a ladder to grab on (or
  // let go), then steer with W/S. Climbing also ends on its own once there's
  // nothing left to climb: stepping off the ladder's cell entirely, or
  // reaching solid footing at the top or bottom of the run.
  bool wasClimbing = climbing;
  bool nearLadder = touchingLadder(world, position.x, position.y, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT);
  bool spacePressed = input.jump && !spaceWasDown;
  spaceWasDown = input.jump;
  if (!nearLadder || (wasClimbing && onGround)) climbing = false;
  else if (spacePressed) climbing = !climbing;

  if (climbing) {
    // Grabbed on: gravity and the charge-jump are both off, and W/S (not
    // strafing) drive straight up/down instead of walking.
    jumpCharging = false;
    velocity.x = 0;
    velocity.z = 0;
    if (input.forward > 0) velocity.y = CLIMB_SPEED;
    else if (input.forward < 0) velocity.y = -CLIMB_SPEED;
    else velocity.y = 0;
  } else {
    velocity.y = std::max(velocity.y - GRAVITY * dt, TERMINAL_FALL_SPEED);

    if (onGround && input.jump && !jumpCharging) {
      jumpCharging = true;
      jumpChargeTime = 0;
    }
    if (jumpCharging) {
      if (!onGround) {
        // left the ground without releasing (e.g. walked off a ledge) - abort
        jumpCharging = false;
      } else if (input.jump) {
        jumpChargeTime = std::min(jumpChargeTime + dt, JUMP_CHARGE_MAX);
      } else {
        double t = jumpChargeTime / JUMP_CHARGE_MAX;
        double mult = 1.0 + t * (JUMP_SPEED_MAX_MULT - 1.0);
        velocity.y = JUMP_SPEED * mult;
        jumpCharging = false;
      }
    }
  }

  // Half a block plus a hair: the highest ledge the auto-step below will
  // climb. Sized for stair slabs — a full block still needs a jump.
  const double STEP_HEIGHT = 0.5 + 1e-4;

  double nx = position.x + velocity.x * dt;
  if (!boxCollides(world, nx, position.y, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
      !boxCollidesLadder(world, nx, position.y, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
      !boxCollidesStairs(world, nx, position.y, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
      !boxCollidesSubCell(world, nx, position.y, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT)) {
    position.x = nx;
  } else if (onGround && !climbing &&
             !boxCollides(world, nx, position.y + STEP_HEIGHT, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
             !boxCollidesLadder(world, nx, position.y + STEP_HEIGHT, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
             !boxCollidesStairs(world, nx, position.y + STEP_HEIGHT, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
             !boxCollidesSubCell(world, nx, position.y + STEP_HEIGHT, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT)) {
    // Auto-step: walking into a knee-high ledge (a stair slab) steps up onto
    // it instead of stopping; gravity settles the feet onto it next frames.
    position.x = nx;
    position.y += STEP_HEIGHT;
  } else {
    velocity.x = 0;
  }

  double nz = position.z + velocity.z * dt;
  if (!boxCollides(world, position.x, position.y, nz, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
      !boxCollidesLadder(world, position.x, position.y, nz, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
      !boxCollidesStairs(world, position.x, position.y, nz, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
      !boxCollidesSubCell(world, position.x, position.y, nz, PLAYER_HALF_WIDTH, PLAYER_HEIGHT)) {
    position.z = nz;
  } else if (onGround && !climbing &&
             !boxCollides(world, position.x, position.y + STEP_HEIGHT, nz, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
             !boxCollidesLadder(world, position.x, position.y + STEP_HEIGHT, nz, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
             !boxCollidesStairs(world, position.x, position.y + STEP_HEIGHT, nz, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
             !boxCollidesSubCell(world, position.x, position.y + STEP_HEIGHT, nz, PLAYER_HALF_WIDTH, PLAYER_HEIGHT)) {
    position.z = nz;
    position.y += STEP_HEIGHT;
  } else {
    velocity.z = 0;
  }

  bool wasFalling = velocity.y <= 0;
  double ny = position.y + velocity.y * dt;
  onGround = false;
  if (!boxCollides(world, position.x, ny, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
      !boxCollidesStairs(world, position.x, ny, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT) &&
      !boxCollidesSubCell(world, position.x, ny, position.z, PLAYER_HALF_WIDTH, PLAYER_HEIGHT)) {
    position.y = ny;
  } else {
    velocity.y = 0;
    if (wasFalling) onGround = true;
  }

  // Hunger drains slowly over time; health regenerates slowly while hunger
  // is high enough. Nothing lowers health yet (no fall damage, no combat
  // against the player), so this loop is stable — it drains and occasionally
  // regenerates without ever needing a death path.
  const double HUNGER_DRAIN_INTERVAL = 30.0;         // 1 point every 30s of play
  const double HUNGER_DRAIN_INTERVAL_BOOSTED = 15.0; // half as long, while hungerBoostTimer is running
  const double REGEN_INTERVAL = 4.0;                 // 1 point every 4s, while fed
  const int REGEN_HUNGER_THRESHOLD = 6;
  if (hungerBoostTimer > 0) hungerBoostTimer = std::max(0.0, hungerBoostTimer - dt);
  double hungerDrainInterval = hungerBoostTimer > 0 ? HUNGER_DRAIN_INTERVAL_BOOSTED : HUNGER_DRAIN_INTERVAL;
  hungerTimer += dt;
  while (hungerTimer >= hungerDrainInterval) {
    hungerTimer -= hungerDrainInterval;
    if (hunger > 0) hunger--;
  }
  if (hunger > REGEN_HUNGER_THRESHOLD && health < maxHealth) {
    regenTimer += dt;
    while (regenTimer >= REGEN_INTERVAL) {
      regenTimer -= REGEN_INTERVAL;
      if (health < maxHealth) health++;
    }
  } else {
    regenTimer = 0;
  }
}
