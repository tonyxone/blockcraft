#pragma once
#include "common.h"

class World;

// A crafted, driveable boat — placed directly on open water (never on land;
// see canPlaceBoatAt), entered/exited with E, driven with the mouse to aim
// and W/S to move. Not a block: it has no cell in the world grid, just a
// free-floating position/yaw, closer to how DroppedItem or Fish work than
// how furniture does. Not persisted across a save (same as those two):
// a boat left in the world doesn't survive a reload.
struct Boat {
  Vec3 position; // where the hull's underside rests, same convention as Animal's feet position
  double yaw = 0;
  bool occupied = false;
};

// True if (x,z) is open enough water to launch a boat onto: the block right
// there must be water, and it must not already have a boat sitting on it.
// outY is set to the surface height (top of the water column) to place at.
bool canPlaceBoatAt(World& world, int x, int z, const std::vector<Boat>& boats, double& outY);

// Keeps the boat resting on the water's actual surface (in case the water
// around it changes) and, while occupied, drives it: the boat's own facing
// continuously matches `desiredYaw` (the player's own look yaw — steering is
// "look where you want to go"), and moveInput (-1/0/1, W/S) pushes it
// forward or back along that facing. Movement only happens while the boat
// is actually over water — beached, it just sits there inert, matching how
// it can only be placed on water in the first place.
void updateBoat(Boat& boat, World& world, double dt, double desiredYaw, int moveInput);

// Index of the closest unoccupied boat within enter-reach of `pos`, or -1.
int nearestBoat(const std::vector<Boat>& boats, const Vec3& pos);

// Draws every boat — a clinker-built wooden rowboat (planked side strakes,
// overhanging gunwale rail, both ends upswept with the bow higher, two
// bench thwarts), modelled on the reference photo at
// Desktop\blockcraft\boat.jpg.
void drawBoats(const std::vector<Boat>& boats);
