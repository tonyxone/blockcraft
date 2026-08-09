#include "sound.h"
#include "noise.h"
#include "win_gl.h"
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

static const int SAMPLE_RATE = 22050;

static std::vector<uint8_t> g_mineWav, g_placeWav;
static std::vector<uint8_t> g_chestOpenWav, g_chestCloseWav;
static std::vector<uint8_t> g_doorOpenWav, g_doorCloseWav;
static std::vector<uint8_t> g_furnaceIgniteWav, g_furnaceExtinguishWav;
static std::vector<uint8_t> g_trapdoorWav, g_swingWav, g_hitWav, g_sleepWav;

static void appendBytes(std::vector<uint8_t>& v, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  v.insert(v.end(), b, b + n);
}

static void appendU32(std::vector<uint8_t>& v, uint32_t x) { appendBytes(v, &x, 4); }
static void appendU16(std::vector<uint8_t>& v, uint16_t x) { appendBytes(v, &x, 2); }

// Wraps [-1,1] float samples into a 16-bit mono PCM WAV file image.
static std::vector<uint8_t> buildWav(const std::vector<double>& samples) {
  std::vector<uint8_t> v;
  uint32_t dataSize = (uint32_t)(samples.size() * 2);
  appendBytes(v, "RIFF", 4);
  appendU32(v, 36 + dataSize);
  appendBytes(v, "WAVE", 4);
  appendBytes(v, "fmt ", 4);
  appendU32(v, 16);
  appendU16(v, 1); // PCM
  appendU16(v, 1); // mono
  appendU32(v, SAMPLE_RATE);
  appendU32(v, SAMPLE_RATE * 2); // byte rate
  appendU16(v, 2);  // block align
  appendU16(v, 16); // bits per sample
  appendBytes(v, "data", 4);
  appendU32(v, dataSize);
  for (double s : samples) {
    int16_t i = (int16_t)std::lround(clampd(s, -1, 1) * 32767);
    appendU16(v, (uint16_t)i);
  }
  return v;
}

// Collect: a short scratchy "dig" — lowpassed noise with a fast decay.
static std::vector<uint8_t> synthMine() {
  int n = (int)(0.12 * SAMPLE_RATE);
  std::vector<double> s((size_t)n);
  Mulberry32 rng(12345);
  double lp = 0;
  for (int i = 0; i < n; i++) {
    double t = (double)i / SAMPLE_RATE;
    double white = rng.next() * 2 - 1;
    lp += 0.3 * (white - lp); // one-pole lowpass keeps it thuddy, not hissy
    double env = std::exp(-t * 28);
    s[i] = clampd(lp * 2.2, -1, 1) * env * 0.5;
  }
  return buildWav(s);
}

// Build: a wooden "thock" — a falling-pitch tone plus a tiny noise attack.
static std::vector<uint8_t> synthPlace() {
  int n = (int)(0.10 * SAMPLE_RATE);
  std::vector<double> s((size_t)n);
  Mulberry32 rng(67890);
  double phase = 0;
  const double TWO_PI = 6.28318530717958647692;
  for (int i = 0; i < n; i++) {
    double t = (double)i / SAMPLE_RATE;
    double freq = 200 * std::exp(-t * 8);
    phase += TWO_PI * freq / SAMPLE_RATE;
    double tone = std::sin(phase) + 0.4 * std::sin(2 * phase);
    double attack = (rng.next() * 2 - 1) * 0.5 * std::exp(-t * 120);
    double env = std::exp(-t * 35);
    s[i] = clampd(tone * 0.8 + attack, -1, 1) * env * 0.5;
  }
  return buildWav(s);
}

// A wooden hinge creak — a pitch-swept tone with grainy noise riding on top,
// shared shape for both the chest lid and the door (different frequency
// range and duration give each its own character). Sweeping the pitch UP
// reads as opening, DOWN as closing, the same convention synthPlace already
// uses for its falling tone.
static std::vector<uint8_t> synthCreak(double startFreq, double endFreq, double dur, unsigned seed) {
  int n = (int)(dur * SAMPLE_RATE);
  std::vector<double> s((size_t)n);
  Mulberry32 rng(seed);
  double phase = 0;
  const double TWO_PI = 6.28318530717958647692;
  for (int i = 0; i < n; i++) {
    double t = (double)i / SAMPLE_RATE;
    double u = t / dur;
    double freq = startFreq + (endFreq - startFreq) * u;
    phase += TWO_PI * freq / SAMPLE_RATE;
    double tone = std::sin(phase) + 0.25 * std::sin(2 * phase);
    double grain = (rng.next() * 2 - 1) * 0.3;
    double env = std::exp(-t * (3.2 / dur));
    s[i] = clampd(tone * 0.65 + grain, -1, 1) * env * 0.45;
  }
  return buildWav(s);
}

// Furnace catching: a noise burst that swells quickly then crackles as it
// dies down, like kindling catching.
static std::vector<uint8_t> synthIgnite() {
  int n = (int)(0.35 * SAMPLE_RATE);
  std::vector<double> s((size_t)n);
  Mulberry32 rng(24680);
  double lp = 0;
  for (int i = 0; i < n; i++) {
    double t = (double)i / SAMPLE_RATE;
    double white = rng.next() * 2 - 1;
    lp += 0.5 * (white - lp);
    double swell = 1.0 - std::exp(-t * 18); // quick rise
    double decay = std::exp(-t * 5);
    double crackle = rng.next() < 0.06 ? (rng.next() * 2 - 1) * 0.6 : 0.0;
    s[i] = clampd((white - lp) * 1.6 + crackle, -1, 1) * swell * decay * 0.5;
  }
  return buildWav(s);
}

// Furnace going out: a short, low, damped thud with a fading hiss tail.
static std::vector<uint8_t> synthExtinguish() {
  int n = (int)(0.3 * SAMPLE_RATE);
  std::vector<double> s((size_t)n);
  Mulberry32 rng(13579);
  double phase = 0;
  const double TWO_PI = 6.28318530717958647692;
  double lp = 0;
  for (int i = 0; i < n; i++) {
    double t = (double)i / SAMPLE_RATE;
    phase += TWO_PI * 70 / SAMPLE_RATE;
    double thud = std::sin(phase) * std::exp(-t * 22);
    double white = rng.next() * 2 - 1;
    lp += 0.15 * (white - lp);
    double hiss = lp * std::exp(-t * 6);
    s[i] = clampd(thud * 0.6 + hiss * 0.5, -1, 1) * 0.5;
  }
  return buildWav(s);
}

// A weapon/tool swing: bandpass-filtered noise with a swept centre
// frequency — the standard technique real sword-swing sound design uses
// (an "Aeolian tone" model — see published sword-swing-synthesis research).
// Bandpass is approximated the classic way, as the difference of a fast and
// a slow one-pole lowpass; sweeping the fast filter's own coefficient up
// through the swing's first half and back down through its second sweeps
// the band's centre frequency the way a blade's speed rises then falls
// through an arc, rather than a one-way slide.
static std::vector<uint8_t> synthWhoosh() {
  const double DUR = 0.22;
  int n = (int)(DUR * SAMPLE_RATE);
  std::vector<double> s((size_t)n);
  Mulberry32 rng(98765);
  double lpFast = 0, lpSlow = 0;
  const double PI = 3.14159265358979323846;
  for (int i = 0; i < n; i++) {
    double t = (double)i / SAMPLE_RATE;
    double u = t / DUR;
    double arc = std::sin(u * PI);       // 0 at both ends, 1 mid-swing
    double sweepCoeff = 0.15 + arc * 0.35;
    double white = rng.next() * 2 - 1;
    lpFast += sweepCoeff * (white - lpFast);
    lpSlow += 0.05 * (white - lpSlow);
    double band = lpFast - lpSlow;
    s[i] = clampd(band * 2.2, -1, 1) * arc * 0.55 * 0.4; // 60% quieter
  }
  return buildWav(s);
}

// A swing landing on an animal: a sharp noise "smack" transient over a
// short low thump, distinct from the scratchy dig (synthMine) and the
// airy bandpass sweep (synthWhoosh) — punchier and lower than either, with
// a much faster decay so a flurry of hits stays crisp instead of smearing.
static std::vector<uint8_t> synthHit() {
  const double DUR = 0.1;
  int n = (int)(DUR * SAMPLE_RATE);
  std::vector<double> s((size_t)n);
  Mulberry32 rng(24601);
  double lp = 0, phase = 0;
  const double TWO_PI = 6.28318530717958647692;
  for (int i = 0; i < n; i++) {
    double t = (double)i / SAMPLE_RATE;
    double white = rng.next() * 2 - 1;
    lp += 0.6 * (white - lp); // thuddy, not hissy
    double smack = lp * std::exp(-t * 55); // sharp transient, gone fast
    phase += TWO_PI * 130 / SAMPLE_RATE;
    double thump = std::sin(phase) * std::exp(-t * 30);
    s[i] = clampd(smack * 1.4 + thump * 0.7, -1, 1) * 0.55;
  }
  return buildWav(s);
}

// Settling into bed: a soft two-note rising chime, unlike every other sound
// here — no noise/attack component at all, just a gentle sine swell — so it
// reads as restful rather than mechanical.
static std::vector<uint8_t> synthSleep() {
  const double DUR = 0.5;
  int n = (int)(DUR * SAMPLE_RATE);
  std::vector<double> s((size_t)n);
  const double TWO_PI = 6.28318530717958647692;
  double phase1 = 0, phase2 = 0;
  for (int i = 0; i < n; i++) {
    double t = (double)i / SAMPLE_RATE;
    double u = t / DUR;
    // Second note comes in a beat after the first, a soft third above it.
    phase1 += TWO_PI * 330 / SAMPLE_RATE;
    phase2 += TWO_PI * 415 / SAMPLE_RATE;
    double note1 = std::sin(phase1) * std::exp(-t * 3.5);
    double note2 = u > 0.15 ? std::sin(phase2) * std::exp(-(t - 0.15) * 3.5) : 0.0;
    double swell = 1.0 - std::exp(-t * 14); // gentle fade-in, no hard attack
    s[i] = clampd((note1 * 0.6 + note2 * 0.5) * swell, -1, 1) * 0.45;
  }
  return buildWav(s);
}

void soundInit() {
  if (!g_mineWav.empty()) return;
  g_mineWav = synthMine();
  g_placeWav = synthPlace();
  g_chestOpenWav = synthCreak(140, 320, 0.22, 11111);
  g_chestCloseWav = synthCreak(320, 140, 0.18, 22222);
  g_doorOpenWav = synthCreak(90, 180, 0.30, 33333);
  g_doorCloseWav = synthCreak(180, 90, 0.24, 44444);
  g_furnaceIgniteWav = synthIgnite();
  g_furnaceExtinguishWav = synthExtinguish();
  g_trapdoorWav = synthCreak(420, 700, 0.12, 55555); // quick wooden flap
  g_swingWav = synthWhoosh();
  g_hitWav = synthHit();
  g_sleepWav = synthSleep();
}

static void play(const std::vector<uint8_t>& wav) {
  if (wav.empty()) return;
  // SND_ASYNC returns immediately; a new effect replaces the current one,
  // which is fine for sub-150ms clicks.
  PlaySoundA((LPCSTR)wav.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void playMineSound() { play(g_mineWav); }
void playPlaceSound() { play(g_placeWav); }
void playChestSound(bool opening) { play(opening ? g_chestOpenWav : g_chestCloseWav); }
void playDoorSound(bool opening) { play(opening ? g_doorOpenWav : g_doorCloseWav); }
void playFurnaceSound(bool igniting) { play(igniting ? g_furnaceIgniteWav : g_furnaceExtinguishWav); }
void playTrapdoorSound() { play(g_trapdoorWav); }
void playSwingSound() { play(g_swingWav); }
void playHitSound() { play(g_hitWav); }
void playSleepSound() { play(g_sleepWav); }

const std::vector<uint8_t>& mineWavData() { return g_mineWav; }
const std::vector<uint8_t>& placeWavData() { return g_placeWav; }
