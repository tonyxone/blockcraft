#include "settings.h"
#include "save.h"

static std::string settingsPath() { return exeDir() + "settings.txt"; }

Settings loadSettings() {
  Settings s;
  FILE* f = std::fopen(settingsPath().c_str(), "r");
  if (!f) return s;
  char word[32];
  while (std::fscanf(f, "%31s", word) == 1) {
    if (std::strcmp(word, "sensitivity") == 0) {
      std::fscanf(f, "%lf", &s.sensitivity);
    } else if (std::strcmp(word, "renderDistance") == 0) {
      std::fscanf(f, "%d", &s.renderDistance);
    } else if (std::strcmp(word, "resolution") == 0) {
      int w = 0, h = 0;
      if (std::fscanf(f, "%d %d", &w, &h) == 2 && w >= 320 && h >= 240) {
        s.resolutionW = w;
        s.resolutionH = h;
      }
    } else if (std::strcmp(word, "thirdPerson") == 0) {
      int v = 0;
      if (std::fscanf(f, "%d", &v) == 1) s.thirdPerson = v != 0;
    } else if (std::strcmp(word, "displayMode") == 0) {
      int m = 0;
      if (std::fscanf(f, "%d", &m) == 1 && (m == 0 || m == 1)) {
        s.displayMode = m;
      }
    } else if (std::strcmp(word, "character") == 0) {
      int c = 0;
      if (std::fscanf(f, "%d", &c) == 1 && (c == 0 || c == 1)) {
        s.characterType = c;
      }
    }
  }
  std::fclose(f);
  return s;
}

void saveSettings(const Settings& settings) {
  FILE* f = std::fopen(settingsPath().c_str(), "w");
  if (!f) return;
  std::fprintf(f, "sensitivity %.17g\n", settings.sensitivity);
  std::fprintf(f, "renderDistance %d\n", settings.renderDistance);
  std::fprintf(f, "resolution %d %d\n", settings.resolutionW, settings.resolutionH);
  std::fprintf(f, "thirdPerson %d\n", settings.thirdPerson ? 1 : 0);
  std::fprintf(f, "displayMode %d\n", settings.displayMode);
  std::fprintf(f, "character %d\n", settings.characterType);
  std::fclose(f);
}
