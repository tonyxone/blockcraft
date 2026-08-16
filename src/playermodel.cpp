#include "playermodel.h"
#include "noise.h"
#include "tools.h"
#include "win_gl.h"

// Classic Steve palette (see colorswall.com/palette/530423 and friends):
// tan skin, dark brown hair, teal shirt, indigo pants, gray shoes.
namespace {

const int SKIN_PX = 64; // 64x64 skin atlas, standard cross layout per part
const double MODEL_PX = 32.0;   // model height in skin pixels
const double MODEL_HEIGHT = 1.8; // matches the physics hitbox
const double S = MODEL_HEIGHT / MODEL_PX;

struct Color { int r, g, b; };
const Color SKIN = { 0xB6, 0x89, 0x6C };
const Color SKIN_DARK = { 0xA9, 0x7D, 0x64 };
const Color SKIN_LIGHT = { 0xC6, 0x99, 0x7C };
const Color SKIN_LIGHT_DARK = { 0xB9, 0x8D, 0x70 };
const Color HAIR = { 0x4A, 0x30, 0x1A };
const Color HAIR_DARK = { 0x38, 0x24, 0x12 };
const Color HAIR_GINGER = { 0xD4, 0x74, 0x2A };
const Color HAIR_GINGER_DARK = { 0xB5, 0x5F, 0x20 };
const Color SHIRT = { 0x00, 0x8B, 0x8B };
const Color SHIRT_DARK = { 0x00, 0x78, 0x78 };
const Color SHIRT_PINK = { 0xD4, 0x69, 0x8C };
const Color SHIRT_PINK_DARK = { 0xB8, 0x56, 0x76 };
const Color PANTS = { 0x3D, 0x3C, 0x8E };
const Color PANTS_DARK = { 0x2F, 0x2E, 0x74 };
const Color PANTS_JEANS = { 0x3A, 0x5F, 0xA8 };
const Color PANTS_JEANS_DARK = { 0x2D, 0x4C, 0x8A };
const Color SHOE = { 0x6B, 0x6B, 0x6B };
const Color SHOE_DARK = { 0x4F, 0x4F, 0x4F };
const Color SHOE_LIGHT = { 0x8A, 0x8A, 0x8A };
const Color SHOE_LIGHT_DARK = { 0x6E, 0x6E, 0x6E };
const Color EYE_WHITE = { 0xFF, 0xFF, 0xFF };
const Color EYE_PUPIL = { 0x4A, 0x3C, 0xA3 };
const Color EYE_GREEN = { 0x3D, 0x8B, 0x3D };
const Color NOSE = { 0x8A, 0x5A, 0x44 };
const Color MOUTH = { 0x6B, 0x44, 0x33 };
const Color BLUSH = { 0xD6, 0x7A, 0x7A };

uint8_t g_pixels[SKIN_PX * SKIN_PX * 4]; // canvas order: row 0 = top
GLuint g_texSteve = 0;
GLuint g_texAlex = 0;
PlayerCharacter g_currentChar = PlayerCharacter::Steve;

void put(int x, int y, Color c) {
  if (x < 0 || x >= SKIN_PX || y < 0 || y >= SKIN_PX) return;
  uint8_t* p = &g_pixels[(size_t)(y * SKIN_PX + x) * 4];
  p[0] = (uint8_t)c.r;
  p[1] = (uint8_t)c.g;
  p[2] = (uint8_t)c.b;
  p[3] = 255;
}

// Fills a rect with `base`, jittered toward `dark` for a cloth/skin feel.
void fill(int x, int y, int w, int h, Color base, Color dark, Mulberry32& rng) {
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) {
      double t = rng.next() * 0.35;
      Color c = { (int)(base.r + (dark.r - base.r) * t),
                  (int)(base.g + (dark.g - base.g) * t),
                  (int)(base.b + (dark.b - base.b) * t) };
      put(xx, yy, c);
    }
  }
}

// Standard skin cross layout for a w*h*d part whose region starts at (px,py):
//   (px+d,   py)     w x d  top        (px+d+w, py)      w x d  bottom
//   (px,     py+d)   d x h  right      (px+d,   py+d)    w x h  front
//   (px+d+w, py+d)   d x h  left       (px+2d+w,py+d)    w x h  back
struct Part {
  int px, py; // region origin in the atlas
  int w, h, d;
};
const Part HEAD = { 0, 0, 8, 8, 8 };
const Part TORSO = { 16, 16, 8, 12, 4 };
const Part ARM = { 40, 16, 4, 12, 4 };
const Part SLIM_ARM = { 40, 32, 3, 12, 4 }; // Alex-style 3px-wide arm
const Part LEG = { 0, 16, 4, 12, 4 };

// face: 0 top, 1 bottom, 2 right(+x), 3 front(-z), 4 left(-x), 5 back(+z)
void faceRect(const Part& p, int face, int& x, int& y, int& w, int& h) {
  switch (face) {
    case 0: x = p.px + p.d; y = p.py; w = p.w; h = p.d; break;
    case 1: x = p.px + p.d + p.w; y = p.py; w = p.w; h = p.d; break;
    case 2: x = p.px; y = p.py + p.d; w = p.d; h = p.h; break;
    case 3: x = p.px + p.d; y = p.py + p.d; w = p.w; h = p.h; break;
    case 4: x = p.px + p.d + p.w; y = p.py + p.d; w = p.d; h = p.h; break;
    default: x = p.px + 2 * p.d + p.w; y = p.py + p.d; w = p.w; h = p.h; break;
  }
}

void fillFace(const Part& p, int face, Color base, Color dark, Mulberry32& rng) {
  int x, y, w, h;
  faceRect(p, face, x, y, w, h);
  fill(x, y, w, h, base, dark, rng);
}

void paintSkinSteve() {
  Mulberry32 rng(0x57E7E);

  // --- head: hair everywhere, then the face and side/underside details ---
  for (int f = 0; f < 6; f++) fillFace(HEAD, f, HAIR, HAIR_DARK, rng);
  int fx, fy, fw, fh;
  faceRect(HEAD, 3, fx, fy, fw, fh); // front (8x8)
  fill(fx, fy + 2, 8, 6, SKIN, SKIN_DARK, rng); // face below a 2px fringe
  put(fx + 1, fy + 4, EYE_WHITE);
  put(fx + 2, fy + 4, EYE_PUPIL);
  put(fx + 5, fy + 4, EYE_PUPIL);
  put(fx + 6, fy + 4, EYE_WHITE);
  put(fx + 3, fy + 5, NOSE);
  put(fx + 4, fy + 5, NOSE);
  put(fx + 3, fy + 6, MOUTH);
  put(fx + 4, fy + 6, MOUTH);
  for (int f = 2; f <= 4; f += 2) { // right/left sides: skin below the hairline
    faceRect(HEAD, f, fx, fy, fw, fh);
    fill(fx, fy + 3, fw, fh - 3, SKIN, SKIN_DARK, rng);
  }
  faceRect(HEAD, 1, fx, fy, fw, fh); // underside of the head: skin
  fill(fx, fy, fw, fh, SKIN, SKIN_DARK, rng);

  // --- torso: teal shirt ---
  for (int f = 0; f < 6; f++) fillFace(TORSO, f, SHIRT, SHIRT_DARK, rng);

  // --- arms: bare skin (classic Steve), shirt covering the shoulder ---
  for (int f = 0; f < 6; f++) fillFace(ARM, f, SKIN, SKIN_DARK, rng);
  for (int f = 2; f <= 5; f++) { // 1px sleeve at the top of each side face
    faceRect(ARM, f, fx, fy, fw, fh);
    fill(fx, fy, fw, 1, SHIRT, SHIRT_DARK, rng);
  }
  faceRect(ARM, 0, fx, fy, fw, fh); // shoulder top
  fill(fx, fy, fw, fh, SHIRT, SHIRT_DARK, rng);

  // --- legs: indigo pants with gray shoes (bottom 2 rows) ---
  for (int f = 0; f < 6; f++) fillFace(LEG, f, PANTS, PANTS_DARK, rng);
  for (int f = 2; f <= 5; f++) {
    faceRect(LEG, f, fx, fy, fw, fh);
    fill(fx, fy + fh - 2, fw, 2, SHOE, SHOE_DARK, rng);
  }
  faceRect(LEG, 1, fx, fy, fw, fh); // sole
  fill(fx, fy, fw, fh, SHOE_DARK, SHOE_DARK, rng);
}

void paintSkinAlex() {
  Mulberry32 rng(0xA7B3E);

  // --- head: longer ginger hair, lighter skin, green eyes, smile, blush ---
  for (int f = 0; f < 6; f++) fillFace(HEAD, f, HAIR_GINGER, HAIR_GINGER_DARK, rng);
  int fx, fy, fw, fh;
  faceRect(HEAD, 3, fx, fy, fw, fh); // front face
  fill(fx, fy + 2, 8, 6, SKIN_LIGHT, SKIN_LIGHT_DARK, rng); // face below 2px fringe
  put(fx + 1, fy + 4, EYE_WHITE);
  put(fx + 2, fy + 4, EYE_GREEN);
  put(fx + 5, fy + 4, EYE_GREEN);
  put(fx + 6, fy + 4, EYE_WHITE);
  put(fx + 3, fy + 5, NOSE);
  put(fx + 4, fy + 5, NOSE);
  put(fx + 2, fy + 6, MOUTH);
  put(fx + 3, fy + 6, MOUTH);
  put(fx + 4, fy + 6, MOUTH);
  put(fx + 5, fy + 6, MOUTH);
  put(fx + 1, fy + 5, BLUSH); // cheek blush
  put(fx + 6, fy + 5, BLUSH);

  // Back of the head is covered by long hair down to the shoulders.
  faceRect(HEAD, 5, fx, fy, fw, fh);
  fill(fx, fy, fw, fh, HAIR_GINGER, HAIR_GINGER_DARK, rng);
  for (int f = 2; f <= 4; f += 2) { // right/left sides: skin below the hairline
    faceRect(HEAD, f, fx, fy, fw, fh);
    fill(fx, fy + 3, fw, fh - 3, SKIN_LIGHT, SKIN_LIGHT_DARK, rng);
  }
  faceRect(HEAD, 1, fx, fy, fw, fh); // underside of the head: skin
  fill(fx, fy, fw, fh, SKIN_LIGHT, SKIN_LIGHT_DARK, rng);

  // --- torso: pink shirt ---
  for (int f = 0; f < 6; f++) fillFace(TORSO, f, SHIRT_PINK, SHIRT_PINK_DARK, rng);

  // --- slim arms: bare skin with pink sleeves ---
  for (int f = 0; f < 6; f++) fillFace(SLIM_ARM, f, SKIN_LIGHT, SKIN_LIGHT_DARK, rng);
  for (int f = 2; f <= 5; f++) { // 1px sleeve at the top of each side face
    faceRect(SLIM_ARM, f, fx, fy, fw, fh);
    fill(fx, fy, fw, 1, SHIRT_PINK, SHIRT_PINK_DARK, rng);
  }
  faceRect(SLIM_ARM, 0, fx, fy, fw, fh); // shoulder top
  fill(fx, fy, fw, fh, SHIRT_PINK, SHIRT_PINK_DARK, rng);

  // --- legs: blue jeans with light gray shoes ---
  for (int f = 0; f < 6; f++) fillFace(LEG, f, PANTS_JEANS, PANTS_JEANS_DARK, rng);
  for (int f = 2; f <= 5; f++) {
    faceRect(LEG, f, fx, fy, fw, fh);
    fill(fx, fy + fh - 2, fw, 2, SHOE_LIGHT, SHOE_LIGHT_DARK, rng);
  }
  faceRect(LEG, 1, fx, fy, fw, fh); // sole
  fill(fx, fy, fw, fh, SHOE_LIGHT_DARK, SHOE_LIGHT_DARK, rng);
}

// Box geometry: same CCW-outward face corner sets as the chunk mesher, with
// the same baked directional shading so the character matches world lighting.
struct FaceDef {
  int corners[4][3];
  int uvS[4], uvT[4];
  uint8_t shade;
};
const FaceDef FACES[6] = {
  /* top    */ { { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } }, { 0, 0, 1, 1 }, { 0, 1, 1, 0 }, 255 },
  /* bottom */ { { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } }, { 0, 0, 1, 1 }, { 0, 1, 1, 0 }, 154 },
  /* +x     */ { { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, { 0, 0, 1, 1 }, { 0, 1, 1, 0 }, 226 },
  /* -z     */ { { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } }, { 0, 0, 1, 1 }, { 0, 1, 1, 0 }, 177 },
  /* -x     */ { { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } }, { 0, 0, 1, 1 }, { 0, 1, 1, 0 }, 188 },
  /* +z     */ { { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } }, { 0, 0, 1, 1 }, { 0, 1, 1, 0 }, 202 },
};
// FACES index -> faceRect face id (top, bottom, +x=right, -z=front, -x=left, +z=back)
const int FACE_REGION[6] = { 0, 1, 2, 3, 4, 5 };

// Draws one w*h*d px box with its min corner at (x0,y0,z0) px, textured from
// the given part's regions. Coordinates are in model pixels; the caller has
// already applied the px->world scale. `angleDeg` rotates the box about the
// X axis through height `pivotY` (limb joint: positive swings the lower end
// forward, since the model faces -Z).
void drawBox(const Part& part, double x0, double y0, double z0,
             double pivotY = 0, double angleDeg = 0) {
  double size[3] = { (double)part.w, (double)part.h, (double)part.d };
  glPushMatrix();
  if (angleDeg != 0) {
    glTranslated(0, pivotY, 0);
    glRotated(angleDeg, 1, 0, 0);
    glTranslated(0, -pivotY, 0);
  }
  for (int f = 0; f < 6; f++) {
    const FaceDef& face = FACES[f];
    int rx, ry, rw, rh;
    faceRect(part, FACE_REGION[f], rx, ry, rw, rh);
    // atlas is uploaded flipped (row 0 of the canvas = v 1.0)
    double u0 = rx / (double)SKIN_PX;
    double u1 = (rx + rw) / (double)SKIN_PX;
    double vTop = 1.0 - ry / (double)SKIN_PX;
    double vBottom = 1.0 - (ry + rh) / (double)SKIN_PX;

    glColor3ub(face.shade, face.shade, face.shade);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
      double u = face.uvS[i] ? u1 : u0;
      double v = face.uvT[i] ? vTop : vBottom;
      glTexCoord2d(u, v);
      glVertex3d(x0 + face.corners[i][0] * size[0],
                 y0 + face.corners[i][1] * size[1],
                 z0 + face.corners[i][2] * size[2]);
    }
    glEnd();
  }
  glPopMatrix();
}

double mixAngle(double a, double b, double t) { return a + (b - a) * t; }

// A rowboat's oar (PlayerAnim::boating): plain wood, flat-shaded rather than
// skin-textured like the body parts above, since it isn't part of the
// character's own skin — same per-face shading convention every hand-rolled
// prop elsewhere in this game uses (animal.cpp, tools.cpp, boat.cpp).
struct FlatFace {
  int corners[4][3];
  uint8_t shade;
};
const FlatFace PADDLE_FACES[6] = {
  { { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } }, 255 },
  { { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } }, 154 },
  { { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, 226 },
  { { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } }, 177 },
  { { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } }, 188 },
  { { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } }, 202 },
};
void drawPaddleBox(double x0, double y0, double z0, double w, double h, double d, double r,
                   double g, double b) {
  for (const FlatFace& face : PADDLE_FACES) {
    double shade = face.shade / 255.0;
    glColor3d(r * shade, g * shade, b * shade);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
      glVertex3d(x0 + face.corners[i][0] * w, y0 + face.corners[i][1] * h,
                z0 + face.corners[i][2] * d);
    }
    glEnd();
  }
}

// A thin shaft hanging down from the grip with a flat blade at the bottom
// end — held in model-pixel units, same scale as the arm it's attached to.
void drawPaddle() {
  const double R = 0.55, G = 0.38, B = 0.22;
  drawPaddleBox(-0.75, -16, -0.75, 1.5, 16, 1.5, R, G, B); // shaft
  drawPaddleBox(-2.5, -20, -0.5, 5, 5, 1, R, G, B);        // blade
}

} // namespace

GLuint uploadSkin() {
  // flip rows so canvas row 0 (top) lands at v = 1
  std::vector<uint8_t> flipped(sizeof(g_pixels));
  for (int y = 0; y < SKIN_PX; y++) {
    std::memcpy(&flipped[(size_t)(SKIN_PX - 1 - y) * SKIN_PX * 4],
                &g_pixels[(size_t)y * SKIN_PX * 4], SKIN_PX * 4);
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SKIN_PX, SKIN_PX, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
}

void playerModelInit() {
  if (g_texSteve) return;
  std::memset(g_pixels, 0, sizeof(g_pixels));
  paintSkinSteve();
  g_texSteve = uploadSkin();

  std::memset(g_pixels, 0, sizeof(g_pixels));
  paintSkinAlex();
  g_texAlex = uploadSkin();
}

void playerModelSetCharacter(PlayerCharacter c) {
  g_currentChar = c;
}

void drawFirstPersonArm(double swing, bool leftHand, int heldTool, int winW, int winH) {
  bool swinging = swing > 0.0 && swing < 1.0;
  // A gripped tool stays in view the whole time — you are carrying it, so it
  // rides in the corner of the screen the way Minecraft holds an item. A
  // BARE hand keeps the original behaviour of appearing only for a swing.
  if (!swinging && heldTool < 0) return;
  const double PI = 3.14159265358979323846;
  double arc = swinging ? std::sin(clampd(swing, 0, 1) * PI) : 0.0; // 0 -> 1 -> 0

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  double aspect = (double)winW / (winH > 0 ? winH : 1);
  double fovY = 70.0 * PI / 180.0;
  double zNear = 0.05, zFar = 10.0;
  double top = zNear * std::tan(fovY / 2);
  glFrustum(-top * aspect, top * aspect, -top, top, zNear, zFar);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  // Own depth range: the arm is drawn over the finished world.
  glClear(GL_DEPTH_BUFFER_BIT);
  glDisable(GL_FOG);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_currentChar == PlayerCharacter::Alex ? g_texAlex : g_texSteve);
  glColor4d(1, 1, 1, 1);

  // The shoulder sits just off the bottom corner of the screen and the limb
  // reaches forward into view, so the fist leads the swing. Tipping further
  // about X drives the punch out and back again. The left hand is the same
  // pose mirrored across the screen — the sideways offset and the two
  // off-axis tilts flip sign, while the forward tip does not.
  // Carrying a tool at rest is a different pose from punching: the swing
  // brings the fist up close to the eye, which is right for a punch but
  // leaves a long tool filling half the screen. So at rest the whole
  // viewmodel drops toward the corner and sits further from the eye, where
  // perspective shrinks it — only the head and part of the haft stay in
  // frame. `idle` fades this out as the swing starts, so the punch keeps its
  // original framing.
  bool restingWithTool = heldTool >= 0;
  double idle = restingWithTool ? 1.0 - arc : 0.0;
  const double IDLE_DROP = 0.20;  // down, out of the middle of the view
  const double IDLE_OUT = 0.03;   // further into the corner
  const double IDLE_BACK = 0.14;  // away from the eye, so it reads smaller

  // Swinging a TOOL is a smash, not a punch: the arm lifts, drives down
  // through the block and recovers, following the same phase curve the tool
  // does so the two move as one piece. A bare fist keeps the old even
  // out-and-back jab. A poking weapon (toolPokes — the spear) does neither:
  // the arm stays at its rest tip and the whole viewmodel slides straight
  // forward into the scene and back, a jab rather than a chop.
  bool smashing = heldTool >= 0 && !leftHand;
  bool poking = smashing && toolPokes((uint8_t)heldTool);
  double phase = smashing ? toolSwingPhase(swing) : 0.0;
  // Matches the 80 degrees the third-person arm swings through, so the blow
  // has the same weight from inside the head as it does watching yourself.
  // At 34 the viewmodel barely twitched next to it.
  const double ARM_SMASH_DEG = 76.0;  // how far the arm itself rises and falls
  const double ARM_SMASH_LIFT = 0.11; // and how far the fist travels with it
  const double FP_POKE_REACH = 0.22;  // how far the spear jabs into the scene

  double side = leftHand ? -1.0 : 1.0;
  // MINUS phase: tipping further about X drives the fist up and out, so the
  // windup (phase -1) raises the arm and the strike (phase +1) brings it
  // down. Adding it instead made the blow travel upwards.
  double armTip = 72.0 + arc * 26.0 - (poking ? 0.0 : phase * ARM_SMASH_DEG);
  glTranslated((0.42 + IDLE_OUT * idle) * side,
               -0.42 - arc * 0.04 - IDLE_DROP * idle - (poking ? 0.0 : phase * ARM_SMASH_LIFT),
               -0.30 - IDLE_BACK * idle - (poking ? phase * FP_POKE_REACH : 0.0));
  glRotated(-18.0 * side, 0, 0, 1);
  glRotated(armTip, 1, 0, 0);
  glRotated(6.0 * side, 0, 1, 0);

  const double VIEW_SCALE = 0.05;
  glScaled(VIEW_SCALE, VIEW_SCALE, VIEW_SCALE);
  // Alex uses the slimmer 3px arm; center it so the fist stays in the same
  // screen position.
  bool alex = g_currentChar == PlayerCharacter::Alex;
  drawBox(alex ? SLIM_ARM : ARM, alex ? -1.5 : -2, -12, -2);

  // The gripped tool is welded to the fist (0,-12,0 in this rig) and rides
  // the arm rigidly, so the smash carries it bodily rather than leaving it
  // hanging in the air.
  //
  // The lean is a ROLL, not a forward tilt. Tipping the tool about X moves it
  // in the depth plane, which projects to a vertical line on screen — it only
  // foreshortens, never changes the angle you see, which is why raising the
  // grip tilt did nothing here while working in third person. Rolling it lays
  // the tool diagonally across the view the way a held item actually reads.
  // Negative leans the head INWARD, toward the middle of the view, rather
  // than out past the edge of the screen — same angle, mirrored.
  const double FP_TOOL_LEAN_DEG = -45.0;
  // A sprite tool (isSpriteTool: currently the sword, the power axe and the
  // spear — any item held as its own hand-drawn art rather than procedural
  // geometry — see tools.h) gets its own lean: at -45 it points hard to the right (the
  // lean plus the arm's own -18° roll compound). 45 with the tip below nets
  // out to about 10° to the LEFT — measured by projecting the haft axis
  // through this exact transform chain. Other tools keep the shared lean.
  const double FP_SPRITE_LEAN_DEG = 45.0;
  // ...and its own forward TIP, applied in the arm frame BEFORE the lean.
  // -80° pitches the art 45° forward into the scene (away from the eye)
  // instead of straight up. This slot is the one that works: an X-rotation
  // after the lean only moves it toward or past vertical, never forward —
  // the lean turns that axis sideways, so it has to come first.
  const double FP_SPRITE_TIP_DEG = -80.0;
  // Full size, matching the third-person model. This was shrunk to 0.6 while
  // the tool was an extruded slab that filled the frame; against the box
  // model it just made the viewmodel look like it was holding a toy.
  const double FP_TOOL_SCALE = 1.0;
  if (!leftHand && heldTool >= 0) {
    bool spriteHeld = isSpriteTool((uint8_t)heldTool);
    double lean = spriteHeld ? FP_SPRITE_LEAN_DEG : FP_TOOL_LEAN_DEG;
    glPushMatrix();
    glTranslated(0, -12, 0);
    if (spriteHeld) glRotated(FP_SPRITE_TIP_DEG, 1, 0, 0);
    glRotated(lean, 0, 0, 1);
    glScaled(FP_TOOL_SCALE, FP_TOOL_SCALE, FP_TOOL_SCALE);
    drawGrippedTool((uint8_t)heldTool, 0.0); // the arm supplies the swing
    glPopMatrix();
  }

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}

void drawInventoryPlayerPreview(int winW, int winH, double x, double y, double w, double h) {
  const double PI = 3.14159265358979323846;

  // Confine both the viewport and the depth clear to the preview box.
  GLint vx = (GLint)x, vy = (GLint)(winH - y - h);
  GLsizei vw = (GLsizei)w, vh = (GLsizei)h;
  glViewport(vx, vy, vw, vh);
  glEnable(GL_SCISSOR_TEST);
  glScissor(vx, vy, vw, vh);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  double aspect = w / (h > 0 ? h : 1);
  double fovY = 40.0 * PI / 180.0;
  double zNear = 0.1, zFar = 10.0;
  double top = zNear * std::tan(fovY / 2);
  glFrustum(-top * aspect, top * aspect, -top, top, zNear, zFar);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glClear(GL_DEPTH_BUFFER_BIT);
  glDisable(GL_FOG);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);

  // Camera sits at the origin looking down -Z; the dummy player stands in
  // front of it, feet centered vertically, turned to face the camera so the
  // painted face (eyes, nose, mouth) is visible. Standing still: all
  // animation inputs zero.
  Player dummy(Vec3(0, -0.9, -3.0));
  dummy.yaw = PI; // model faces -Z by default; PI turns it toward the camera
  PlayerAnim anim;
  drawPlayerModel(dummy, anim);

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);

  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, winW, winH);
}

void drawPlayerModel(const Player& player, const PlayerAnim& anim) {
  const double PI = 3.14159265358979323846;

  // Human gait: legs alternate, and each arm swings opposite the leg on its
  // own side (contralateral). Amplitude scales with how fast we're moving.
  // The model faces -Z, so the character's RIGHT side is +X.
  double legSwing = std::sin(anim.walkPhase) * 40.0 * anim.walkAmount;
  double armSwing = -std::sin(anim.walkPhase) * 35.0 * anim.walkAmount;

  // Airborne: the gait freezes into a split-leg jump pose, arms raised a bit.
  double legR = mixAngle(legSwing, 22.0, anim.air);
  double legL = mixAngle(-legSwing, -14.0, anim.air);
  double armR = mixAngle(armSwing, 30.0, anim.air);
  double armL = mixAngle(-armSwing, 24.0, anim.air);

  // Collect/build/interact: one arm arcs forward-up and back — the right
  // hand collects, the left hand builds.
  if (anim.swing > 0) {
    if (anim.heldTool >= 0 && !anim.swingLeft && toolPokes((uint8_t)anim.heldTool)) {
      // A spear jabs straight forward, no overhead chop: the windup leaves
      // the arm near rest, the thrust drives it out horizontal. The
      // sin envelope fades the pose in and out so it lands back at rest
      // exactly when the swing ends (a bare phase+1 would pop from 45°).
      armR += std::sin(anim.swing * PI) * (toolSwingPhase(anim.swing) + 1.0) * 45.0;
    } else if (anim.heldTool >= 0 && !anim.swingLeft) {
      // Mining with a tool is a smash: the arm winds back and up, then comes
      // down through the blow. Same phase curve the tool itself uses.
      armR += toolSwingPhase(anim.swing) * 80.0;
    } else {
      double reach = std::sin(anim.swing * PI) * 80.0;
      if (anim.swingLeft) armL += reach;
      else armR += reach;
    }
  }

  // Riding a boat overrides all of the above: sitting (both legs bent
  // forward at the hip — there's no knee joint to bend instead) and rowing
  // (both oars stroke together, in phase, the way a rowboat's two oars pull
  // in sync — unlike a canoe's single alternating paddle).
  if (anim.boating) {
    legL = 80.0;
    legR = 80.0;
    double row = std::sin(anim.rowPhase) * 32.0;
    armL = -18.0 + row;
    armR = -18.0 + row;
  }

  // Swimming (touching water, not riding a boat): legs trail together and
  // arms sweep back, the pose a near-horizontal float actually looks like
  // rather than the standing walk pose tipped on its side. A slow alternating
  // kick/stroke (swimPhase, always advancing — see PlayerAnim) keeps the
  // limbs moving instead of holding a single frozen pose the whole time.
  bool swimPose = player.swimming && !anim.boating;
  if (swimPose) {
    double kick = std::sin(anim.swimPhase) * 18.0;
    legL = 8.0 + kick;
    legR = 8.0 - kick;
    double stroke = std::sin(anim.swimPhase + PI) * 12.0;
    armL = -12.0 + stroke;
    armR = -12.0 - stroke;
  }

  glPushMatrix();
  glTranslated(player.position.x, player.position.y, player.position.z);
  glRotated(player.yaw * 180.0 / PI, 0, 1, 0);
  glScaled(S, S, S);
  // player.position (feet) tracks the boat's own position, which is the
  // water surface the hull floats on — well below the low, shallow hull's
  // seat. Drop the WHOLE rigid model (head/torso/arms/legs all move
  // together, so nothing but this offset needs to change) so the hips
  // settle onto the seat instead of floating above the gunwale with the
  // boat's walls hanging in mid-air below the character.
  if (anim.boating) {
    glTranslated(0, -9.33, 0);
  } else if (swimPose) {
    // Same rigid-rotation trick as the boat's seat offset, just tipping the
    // whole body forward around a waist-height pivot instead of dropping it
    // — belly toward the water, head leading forward (a NEGATIVE angle here:
    // positive tipped the chest up instead, a face-up float).
    glTranslated(0, 12.0, 0);
    glRotated(-75.0, 1, 0, 0);
    glTranslated(0, -12.0, 0);
  }

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_currentChar == PlayerCharacter::Alex ? g_texAlex : g_texSteve);

  bool alex = g_currentChar == PlayerCharacter::Alex;
  const Part& armPart = alex ? SLIM_ARM : ARM;
  double armLX = alex ? -7 : -8; // slim arm starts 1px closer to the torso
  double armRX = 4;              // right arm starts at the same torso edge

  drawBox(HEAD, -4, 24, -4);                 // 8x8x8
  drawBox(TORSO, -4, 12, -2);                // 8x12x4
  drawBox(armPart, armLX, 12, -2, 22, armL); // left arm (-X), shoulder pivot
  drawBox(armPart, armRX, 12, -2, 22, armR); // right arm (+X)

  // Gripped tool: follow the hand (bottom-center of the right arm box) by
  // replaying the arm's own pivot rotation, then cancel that rotation's
  // contribution to ORIENTATION so the tool keeps its own independent grip
  // tilt and swing regardless of the arm's current swing angle.
  if (anim.heldTool >= 0 && !anim.boating) {
    glPushMatrix();
    glTranslated(0, 22, 0);
    glRotated(armR, 1, 0, 0);
    glTranslated(0, -22, 0);
    // Held at the OUTSIDE of the fist, not down its centre line. Centred
    // (armRX + w/2) the tool sits inside the arm's own volume and the limb
    // hides it from the chase camera — which used to be masked by yawing the
    // tool outwards, at the cost of it no longer facing front.
    glTranslated(armRX + armPart.w - 0.5, 12, 0);
    glRotated(-armR, 1, 0, 0);
    drawGrippedTool((uint8_t)anim.heldTool, anim.swingLeft ? 0.0 : anim.swing);
    glPopMatrix();
    glBindTexture(GL_TEXTURE_2D, g_currentChar == PlayerCharacter::Alex ? g_texAlex : g_texSteve);
  }

  // Paddles: unlike the gripped tool above, these DO follow the arm's own
  // rotation for orientation, not just position — the whole point is that
  // the oar visibly swings through the stroke with the arm, not that it
  // holds some independent tilt of its own. The grip starts a couple of
  // pixels outside the hand (not centred on it like the tool grip is) —
  // flush against the hand, the paddle's thin shaft is mostly nested inside
  // the arm's own (wider) box and the hand hides it entirely; starting just
  // outside keeps it visibly touching the hand while giving the outward
  // tilt below room to swing the shaft clear of the boat's own side wall
  // (same wood color as the hull, so overlapping it would just read as more
  // hull) with the blade dipping into the water beside the boat.
  if (anim.boating) {
    glDisable(GL_TEXTURE_2D);
    // The 3D pass renders with back-face culling on, and mirroring the tilt
    // (-28 vs +28, for the left vs right paddle) mirrors which of each box's
    // faces end up back-facing too — the right paddle's outward faces stay
    // front-facing from behind, but the left paddle's equivalent faces don't,
    // so half of it gets culled away to almost nothing. The paddle is thin
    // decorative geometry with no interior ever exposed, so there's no
    // downside to just rendering both sides of every face here.
    glDisable(GL_CULL_FACE);
    glPushMatrix();
    glTranslated(0, 22, 0);
    glRotated(armL, 1, 0, 0);
    glTranslated(0, -22, 0);
    glTranslated(armLX - 2, 12, 0);
    glRotated(-28, 0, 0, 1);
    drawPaddle();
    glPopMatrix();

    glPushMatrix();
    glTranslated(0, 22, 0);
    glRotated(armR, 1, 0, 0);
    glTranslated(0, -22, 0);
    glTranslated(armRX + armPart.w + 2, 12, 0);
    glRotated(28, 0, 0, 1);
    drawPaddle();
    glPopMatrix();
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
  }

  drawBox(LEG, -4, 0, -2, 12, legL);         // left leg, hip pivot
  drawBox(LEG, 0, 0, -2, 12, legR);          // right leg

  glPopMatrix();
}
