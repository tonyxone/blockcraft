// GENERATED FILE - overwritten by tools\gen_sprite.ps1. Do not hand-edit.
//
// Source images: art\*.png, converted at build-authoring time so the game
// ships as a single exe with no asset files to load at runtime.
#include "sprites_generated.h"
#include <cstring>

const GeneratedSprite GENERATED_SPRITES[] = {
  { nullptr, nullptr }, // placeholder: C++ has no zero-length arrays
};
const int GENERATED_SPRITE_COUNT = 0;

const GeneratedSprite* generatedSpriteNamed(const char* name) {
  if (!name) return nullptr;
  for (int i = 0; i < GENERATED_SPRITE_COUNT; i++) {
    if (GENERATED_SPRITES[i].name && std::strcmp(GENERATED_SPRITES[i].name, name) == 0) {
      return &GENERATED_SPRITES[i];
    }
  }
  return nullptr;
}

