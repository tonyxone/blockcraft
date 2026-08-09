#pragma once
#include "common.h"

// Deterministic PRNG matching JS mulberry32 bit-for-bit (uint32 wraparound).
struct Mulberry32 {
  uint32_t a;
  explicit Mulberry32(uint32_t seed) : a(seed) {}
  double next() {
    a += 0x6D2B79F5u;
    uint32_t t = (a ^ (a >> 15)) * (a | 1u);
    t = (t + (t ^ (t >> 7)) * (t | 61u)) ^ t;
    t = t ^ (t >> 14);
    return double(t) / 4294967296.0;
  }
};

// 2D simplex noise, ported from the simplex-noise npm package (v4) so the
// generated terrain matches the JS build's character. The permutation table
// is seeded from the provided RNG exactly like createNoise2D(rng).
class Noise2D {
public:
  explicit Noise2D(Mulberry32& rng);
  double operator()(double x, double y) const;

private:
  uint8_t perm[512];
  double permGrad2x[512];
  double permGrad2y[512];
};
