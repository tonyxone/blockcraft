#include "noise.h"

static const double F2 = 0.5 * (std::sqrt(3.0) - 1.0);
static const double G2 = (3.0 - std::sqrt(3.0)) / 6.0;

static const double GRAD2[24] = {
  1, 1, -1, 1, 1, -1, -1, -1,
  1, 0, -1, 0, 1, 0, -1, 0,
  0, 1, 0, -1, 0, 1, 0, -1,
};

Noise2D::Noise2D(Mulberry32& rng) {
  // buildPermutationTable(random) from simplex-noise v4
  for (int i = 0; i < 256; i++) perm[i] = (uint8_t)i;
  for (int i = 0; i < 255; i++) {
    int r = i + (int)(rng.next() * (256 - i));
    uint8_t aux = perm[i];
    perm[i] = perm[r];
    perm[r] = aux;
  }
  for (int i = 256; i < 512; i++) perm[i] = perm[i - 256];

  for (int i = 0; i < 512; i++) {
    int v = (perm[i] % 12) * 2;
    permGrad2x[i] = GRAD2[v];
    permGrad2y[i] = GRAD2[v + 1];
  }
}

double Noise2D::operator()(double x, double y) const {
  double n0 = 0, n1 = 0, n2 = 0;

  double s = (x + y) * F2;
  int i = (int)std::floor(x + s);
  int j = (int)std::floor(y + s);
  double t = (i + j) * G2;
  double X0 = i - t;
  double Y0 = j - t;
  double x0 = x - X0;
  double y0 = y - Y0;

  int i1, j1;
  if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }

  double x1 = x0 - i1 + G2;
  double y1 = y0 - j1 + G2;
  double x2 = x0 - 1.0 + 2.0 * G2;
  double y2 = y0 - 1.0 + 2.0 * G2;

  int ii = i & 255;
  int jj = j & 255;

  double t0 = 0.5 - x0 * x0 - y0 * y0;
  if (t0 >= 0) {
    int gi0 = ii + perm[jj];
    t0 *= t0;
    n0 = t0 * t0 * (permGrad2x[gi0] * x0 + permGrad2y[gi0] * y0);
  }
  double t1 = 0.5 - x1 * x1 - y1 * y1;
  if (t1 >= 0) {
    int gi1 = ii + i1 + perm[jj + j1];
    t1 *= t1;
    n1 = t1 * t1 * (permGrad2x[gi1] * x1 + permGrad2y[gi1] * y1);
  }
  double t2 = 0.5 - x2 * x2 - y2 * y2;
  if (t2 >= 0) {
    int gi2 = ii + 1 + perm[jj + 1];
    t2 *= t2;
    n2 = t2 * t2 * (permGrad2x[gi2] * x2 + permGrad2y[gi2] * y2);
  }

  return 70.0 * (n0 + n1 + n2);
}
