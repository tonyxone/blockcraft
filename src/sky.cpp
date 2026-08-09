#include "sky.h"
#include "win_gl.h"

namespace {

unsigned int g_cloudList = 0;

// Wrap into [0, CLOUD_GRID) so the pattern is a torus and tiles seamlessly.
int wrapCell(int v) {
  int m = v % CLOUD_GRID;
  return m < 0 ? m + CLOUD_GRID : m;
}

uint32_t cellHash(int i, int j) {
  uint32_t h = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u + 0xC10D5EEDu;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// Blobby coverage: start from per-cell noise, then blur it a few times with
// wraparound. Thresholding raw noise alone would give single-cell specks
// rather than clouds.
const std::vector<uint8_t>& pattern() {
  static std::vector<uint8_t> cells;
  if (!cells.empty()) return cells;

  std::vector<double> field((size_t)CLOUD_GRID * CLOUD_GRID);
  for (int j = 0; j < CLOUD_GRID; j++) {
    for (int i = 0; i < CLOUD_GRID; i++) {
      field[(size_t)j * CLOUD_GRID + i] = (double)cellHash(i, j) / 4294967296.0;
    }
  }
  std::vector<double> blurred = field;
  for (int pass = 0; pass < 3; pass++) {
    for (int j = 0; j < CLOUD_GRID; j++) {
      for (int i = 0; i < CLOUD_GRID; i++) {
        double sum = 0;
        for (int dj = -1; dj <= 1; dj++) {
          for (int di = -1; di <= 1; di++) {
            sum += field[(size_t)wrapCell(j + dj) * CLOUD_GRID + wrapCell(i + di)];
          }
        }
        blurred[(size_t)j * CLOUD_GRID + i] = sum / 9.0;
      }
    }
    field = blurred;
  }

  // Pick the cut-off by percentile so coverage lands exactly on
  // CLOUD_COVERAGE. Blurring narrows the value spread unpredictably, so a
  // fixed threshold gives wildly different amounts of sky.
  std::vector<double> sorted = field;
  std::sort(sorted.begin(), sorted.end());
  size_t idx = (size_t)((1.0 - CLOUD_COVERAGE) * (sorted.size() - 1));
  double cutoff = sorted[idx];

  cells.assign((size_t)CLOUD_GRID * CLOUD_GRID, 0);
  for (size_t k = 0; k < cells.size(); k++) {
    cells[k] = field[k] > cutoff ? 1 : 0;
  }
  return cells;
}

} // namespace

bool cloudAt(int i, int j) {
  return pattern()[(size_t)wrapCell(j) * CLOUD_GRID + wrapCell(i)] != 0;
}

double cloudCoverage() {
  const std::vector<uint8_t>& cells = pattern();
  int on = 0;
  for (uint8_t c : cells) on += c;
  return (double)on / cells.size();
}

void skyInit() {
  if (g_cloudList) return;
  g_cloudList = glGenLists(1);
  glNewList(g_cloudList, GL_COMPILE);

  const double S = CLOUD_CELL;
  const double top = CLOUD_HEIGHT;
  const double bot = CLOUD_HEIGHT - CLOUD_THICKNESS;

  glBegin(GL_QUADS);
  for (int j = 0; j < CLOUD_GRID; j++) {
    for (int i = 0; i < CLOUD_GRID; i++) {
      if (!cloudAt(i, j)) continue;
      double x0 = i * S, x1 = x0 + S;
      double z0 = j * S, z1 = z0 + S;

      // underside — the face players actually look at
      glColor4d(0.94, 0.96, 1.0, CLOUD_ALPHA);
      glVertex3d(x0, bot, z1);
      glVertex3d(x1, bot, z1);
      glVertex3d(x1, bot, z0);
      glVertex3d(x0, bot, z0);

      // top, in case the view ever gets above the layer
      glColor4d(1.0, 1.0, 1.0, CLOUD_ALPHA);
      glVertex3d(x0, top, z0);
      glVertex3d(x1, top, z0);
      glVertex3d(x1, top, z1);
      glVertex3d(x0, top, z1);

      // sides, only where the neighbouring cell is open sky
      glColor4d(0.90, 0.93, 0.99, CLOUD_ALPHA);
      if (!cloudAt(i - 1, j)) {
        glVertex3d(x0, bot, z0);
        glVertex3d(x0, top, z0);
        glVertex3d(x0, top, z1);
        glVertex3d(x0, bot, z1);
      }
      if (!cloudAt(i + 1, j)) {
        glVertex3d(x1, bot, z1);
        glVertex3d(x1, top, z1);
        glVertex3d(x1, top, z0);
        glVertex3d(x1, bot, z0);
      }
      if (!cloudAt(i, j - 1)) {
        glVertex3d(x1, bot, z0);
        glVertex3d(x1, top, z0);
        glVertex3d(x0, top, z0);
        glVertex3d(x0, bot, z0);
      }
      if (!cloudAt(i, j + 1)) {
        glVertex3d(x0, bot, z1);
        glVertex3d(x0, top, z1);
        glVertex3d(x1, top, z1);
        glVertex3d(x1, bot, z1);
      }
    }
  }
  glEnd();
  glEndList();
}

void drawClouds(double timeSeconds, double camX, double camZ) {
  if (!g_cloudList) return;

  const double span = CLOUD_GRID * CLOUD_CELL;
  double drift = -std::fmod(timeSeconds * CLOUD_DRIFT_SPEED, span); // westward

  // Snap the tiling to the camera so the layer always covers the view, and
  // draw the neighbouring copies too — the pattern wraps, so the seams line
  // up and the sky reads as endless.
  double baseX = std::floor((camX - span) / span) * span - drift;
  double baseZ = std::floor((camZ - span) / span) * span;

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_FOG); // clouds sit against the sky, not in the terrain haze
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);

  for (int tz = 0; tz <= 2; tz++) {
    for (int tx = 0; tx <= 2; tx++) {
      glPushMatrix();
      glTranslated(baseX + tx * span, 0, baseZ + tz * span);
      glCallList(g_cloudList);
      glPopMatrix();
    }
  }

  // Restore everything this pass changed — the terrain and water draws that
  // follow expect texturing on, and losing it renders the ocean untextured.
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_FOG);
  glEnable(GL_TEXTURE_2D);
  glColor4d(1, 1, 1, 1);
}
