#pragma once
#include "common.h"

// Procedurally synthesized sound effects (no audio assets), played through
// the Windows PlaySound API. soundInit() builds the WAV buffers once.
void soundInit();
void playMineSound();  // collecting a block
void playPlaceSound(); // building a block
void playChestSound(bool opening);    // lid easing open/closed
void playDoorSound(bool opening);     // swinging open/closed
void playFurnaceSound(bool igniting); // catching / going out
void playTrapdoorSound();             // sprung open underfoot
void playSwingSound();                // a gripped tool/weapon swinging
void playHitSound();                  // a swing landing on an animal
void playSleepSound();                // settling into a bed

// Raw WAV buffers, exposed for the selftest.
const std::vector<uint8_t>& mineWavData();
const std::vector<uint8_t>& placeWavData();
