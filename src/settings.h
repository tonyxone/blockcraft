#pragma once
#include "common.h"
#include "constants.h"

// Persisted to settings.txt next to the executable (replaces localStorage).
struct Settings {
  double sensitivity = 1;
  int renderDistance = RENDER_DISTANCE;
  int resolutionW = 1280; // window client size
  int resolutionH = 720;
  int displayMode = 0; // 0 = Fullscreen (auto-borderless when needed), 1 = Window
  bool thirdPerson = false; // camera mode, toggled in-game with V
  int characterType = 0;    // 0 = Steve (boy), 1 = Alex (girl)
};

Settings loadSettings();
void saveSettings(const Settings& settings);
