#include "menu.h"
#include "gfx.h"

// Layout constants mirroring style.css (.menu-panel and friends).
static const double BUTTON_W = 220;
static const double BUTTON_H = 41; // 10px padding + 16px text + 10px + border
static const double SAVE_BUTTON_W = 340; // wider: save name + timestamp + delete
static const double GAP = 10;
static const double TITLE_MARGIN = 8;
static const double ROW_W = 280;
static const double ROW_H = 24;
static const double SLIDER_W = 150;
static const double INPUT_W = 280;
static const double INPUT_H = 34;
static const double MESSAGE_TIMEOUT = 2.2;
static const int MAX_SAVES_SHOWN = 8;
static const size_t MAX_NAME_LEN = 24;

static const double SENS_MIN = 0.2, SENS_MAX = 3.0, SENS_STEP = 0.1;
static const double RD_MIN = 2, RD_MAX = 8;

void Menu::showPanel(MenuPanel p) {
  panel = p;
  visible = true;
  message.clear();
  messageTimer = 0;
}

void Menu::openSavePanel(const std::string& defaultName, std::vector<SaveInfo> saves) {
  previousPanel = panel;
  saveNameInput = defaultName;
  saveList = std::move(saves);
  showPanel(MenuPanel::SaveGamePanel);
}

void Menu::openLoadPanel(std::vector<SaveInfo> saves) {
  previousPanel = panel;
  saveList = std::move(saves);
  showPanel(MenuPanel::LoadGamePanel);
}

void Menu::refreshLoadPanel(std::vector<SaveInfo> saves) {
  saveList = std::move(saves);
  showPanel(MenuPanel::LoadGamePanel);
}

void Menu::openConfirm(const std::string& text, const std::string& buttonLabel,
                       MenuAction action, const std::string& data) {
  confirmText = text;
  confirmButtonLabel = buttonLabel;
  confirmAction = action;
  confirmData = data;
  confirmReturnPanel = panel;
  showPanel(MenuPanel::ConfirmPanel);
}

void Menu::closeSubPanel() {
  if (panel == MenuPanel::ConfirmPanel) {
    showPanel(confirmReturnPanel);
  } else {
    showPanel(previousPanel);
  }
}

void Menu::showMessage(const std::string& text) {
  message = text;
  messageTimer = MESSAGE_TIMEOUT;
}

void Menu::update(double dt) {
  timeSec += dt;
  if (messageTimer > 0) {
    messageTimer -= dt;
    if (messageTimer <= 0) message.clear();
  }
}

MenuAction Menu::onMouseDown(double x, double y) {
  if (!visible) return MenuAction::None;
  if (panel == MenuPanel::SettingsPanel) {
    if (sensSlider.contains(x, y)) {
      draggingSlider = 0;
      return applySliderDrag(x);
    }
    if (rdSlider.contains(x, y)) {
      draggingSlider = 1;
      return applySliderDrag(x);
    }
  }
  for (const ButtonHit& b : buttons) {
    if (b.rect.contains(x, y)) {
      if (b.action == MenuAction::OpenSettings) {
        previousPanel = panel;
        showPanel(MenuPanel::SettingsPanel);
        return MenuAction::None;
      }
      if (b.action == MenuAction::Back) {
        showPanel(previousPanel);
        return MenuAction::None;
      }
      if (b.action == MenuAction::CancelConfirm) {
        showPanel(confirmReturnPanel);
        return MenuAction::None;
      }
      if (b.action == MenuAction::LoadNamed || b.action == MenuAction::DeleteSave) {
        selectedSave = b.data;
      }
      if (b.action == MenuAction::PickSaveName) {
        saveNameInput = b.data;
        return MenuAction::None;
      }
      if (b.action == MenuAction::ResolutionChanged) {
        resolutionIndex = (resolutionIndex + 1) % RESOLUTION_COUNT;
      }
      if (b.action == MenuAction::DisplayModeChanged) {
        displayMode = (displayMode + 1) % DISPLAY_MODE_COUNT;
      }
      if (b.action == MenuAction::CharacterChanged) {
        characterType = (characterType + 1) % CHARACTER_COUNT;
      }
      return b.action;
    }
  }
  return MenuAction::None;
}

MenuAction Menu::onMouseMove(double x, double) {
  if (draggingSlider < 0) return MenuAction::None;
  return applySliderDrag(x);
}

void Menu::onMouseUp() { draggingSlider = -1; }

MenuAction Menu::onChar(unsigned char c) {
  if (!visible || panel != MenuPanel::SaveGamePanel) return MenuAction::None;
  if (c == '\r') return MenuAction::ConfirmSave;
  if (c == '\b') {
    if (!saveNameInput.empty()) saveNameInput.pop_back();
    return MenuAction::None;
  }
  if (c >= 32 && c < 127 && saveNameInput.size() < MAX_NAME_LEN) {
    if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') {
      saveNameInput += (char)c;
    }
  }
  return MenuAction::None;
}

MenuAction Menu::applySliderDrag(double x) {
  const Rect& r = draggingSlider == 0 ? sensSlider : rdSlider;
  double t = clampd((x - r.x) / r.w, 0, 1);
  if (draggingSlider == 0) {
    double v = SENS_MIN + t * (SENS_MAX - SENS_MIN);
    v = std::round(v / SENS_STEP) * SENS_STEP;
    v = clampd(v, SENS_MIN, SENS_MAX);
    if (v != sensitivity) {
      sensitivity = v;
      return MenuAction::SensitivityChanged;
    }
  } else {
    int v = (int)std::lround(RD_MIN + t * (RD_MAX - RD_MIN));
    v = (int)clampd(v, RD_MIN, RD_MAX);
    if (v != renderDistance) {
      renderDistance = v;
      return MenuAction::RenderDistanceChanged;
    }
  }
  return MenuAction::None;
}

void Menu::drawSlider(const Rect& r, double t) {
  // track
  drawRect(r.x, r.y + r.h / 2 - 2, r.w, 4, 1, 1, 1, 0.3);
  drawRect(r.x, r.y + r.h / 2 - 2, r.w * t, 4, 1, 1, 1, 0.7);
  // handle
  double hx = r.x + r.w * t - 5;
  drawRect(hx, r.y + r.h / 2 - 9, 10, 18, 0.95, 0.95, 0.95, 1);
}

void Menu::draw(int winW, int winH, double mouseX, double mouseY) {
  buttons.clear();
  if (!visible) return;

  struct Item {
    const char* label;
    MenuAction action;
  };
  // Panel content mirrors index.html (+ the save/load slot panels).
  std::vector<Item> items;
  const char* title = "";
  bool isSettings = panel == MenuPanel::SettingsPanel;
  bool isSave = panel == MenuPanel::SaveGamePanel;
  bool isLoad = panel == MenuPanel::LoadGamePanel;
  bool showHint = panel == MenuPanel::Main;

  switch (panel) {
    case MenuPanel::Main:
      title = "BlockCraft";
      items = { { "Start", MenuAction::Start },
                { "Load", MenuAction::Load },
                { "Settings", MenuAction::OpenSettings },
                { "Quit", MenuAction::Quit } };
      break;
    case MenuPanel::Pause:
      title = "Paused";
      items = { { "Resume", MenuAction::Resume },
                { "Restart", MenuAction::Restart },
                { "Save", MenuAction::Save },
                { "Load", MenuAction::Load },
                { "Settings", MenuAction::OpenSettings },
                { "Main Menu", MenuAction::QuitToMenu } ,
                { "Quit", MenuAction::Quit }};;
      break;
    case MenuPanel::Dead:
      // No Resume (there's no game state worth resuming into) and no Save
      // (nothing worth saving) — just the ways forward: a fresh attempt,
      // an earlier save, or back out entirely.
      title = "Dead";
      items = { { "Restart", MenuAction::Restart },
                { "Load", MenuAction::Load },
                { "Settings", MenuAction::OpenSettings },
                { "Main Menu", MenuAction::QuitToMenu },
                { "Quit", MenuAction::Quit } };
      break;
    case MenuPanel::SettingsPanel:
      title = "Settings";
      items = { { "Back", MenuAction::Back } };
      break;
    case MenuPanel::SaveGamePanel:
      title = "Save Game";
      items = { { "Save", MenuAction::ConfirmSave },
                { "Cancel", MenuAction::Back } };
      break;
    case MenuPanel::LoadGamePanel:
      title = "Load Game";
      items = { { "Back", MenuAction::Back } };
      break;
    case MenuPanel::ConfirmPanel:
      title = "Confirm";
      items = { { confirmButtonLabel.c_str(), confirmAction },
                { "Cancel", MenuAction::CancelConfirm } };
      break;
  }
  bool isConfirm = panel == MenuPanel::ConfirmPanel;

  int savesShown = (isLoad || isSave) ? std::min((int)saveList.size(), MAX_SAVES_SHOWN) : 0;
  bool savesTruncated = (isLoad || isSave) && (int)saveList.size() > savesShown;

  // --- measure panel height ---
  double titleH = g_fontTitle.height;
  double h = titleH + TITLE_MARGIN;
  if (isSettings) {
    h += GAP + ROW_H + GAP + ROW_H + GAP + ROW_H + GAP + ROW_H + GAP + ROW_H;
  } else if (isSave) {
    h += GAP + INPUT_H + GAP + 18; // input + hint line
    if (savesShown > 0) {
      h += GAP + 14; // "or overwrite an existing save:" label
      h += savesShown * (GAP + BUTTON_H);
      if (savesTruncated) h += GAP + 18;
    }
  } else if (isLoad) {
    h += savesShown * (GAP + BUTTON_H);
    if (savesTruncated) h += GAP + 18;
  } else if (isConfirm) {
    h += GAP + 18; // confirmation question line
  }
  for (size_t i = 0; i < items.size(); i++) h += GAP + BUTTON_H;
  if (showHint) h += GAP + 14 + 3 * 21; // hint margin + three 13px lines at 1.6 line-height
  double y = (winH - h) / 2;
  double cx = winW / 2.0;

  // --- title --- (red on the death screen, white everywhere else)
  bool isDead = panel == MenuPanel::Dead;
  double titleR = isDead ? 0.85 : 1, titleG = isDead ? 0.12 : 1, titleB = isDead ? 0.12 : 1;
  drawText(g_fontTitle, cx - textWidth(g_fontTitle, title) / 2 + 2, y + 2, title, 0, 0, 0, 1);
  drawText(g_fontTitle, cx - textWidth(g_fontTitle, title) / 2, y, title, titleR, titleG, titleB, 1);
  y += titleH + TITLE_MARGIN;

  auto drawButton = [&](const char* label, MenuAction action) {
    y += GAP;
    Rect r = { cx - BUTTON_W / 2, y, BUTTON_W, BUTTON_H };
    bool hover = r.contains(mouseX, mouseY);
    drawRect(r.x, r.y, r.w, r.h, 1, 1, 1, hover ? 0.25 : 0.12);
    drawRectOutline(r.x, r.y, r.w, r.h, 2, 1, 1, 1, hover ? 1.0 : 0.4);
    drawText(g_fontButton, cx - textWidth(g_fontButton, label) / 2,
             r.y + (BUTTON_H - g_fontButton.height) / 2, label, 1, 1, 1, 1);
    buttons.push_back({ r, action, "" });
    y += BUTTON_H;
  };

  if (isSettings) {
    auto drawRow = [&](const char* label, Rect& sliderOut, double t, char* valueText) {
      y += GAP;
      double rowX = cx - ROW_W / 2;
      drawText(g_fontMsg, rowX, y + (ROW_H - g_fontMsg.height) / 2, label, 1, 1, 1, 1);
      sliderOut = { rowX + ROW_W - SLIDER_W, y, SLIDER_W, ROW_H };
      drawSlider(sliderOut, t);
      if (valueText) {
        drawText(g_fontMsg, rowX + ROW_W + 10, y + (ROW_H - g_fontMsg.height) / 2,
                 valueText, 1, 1, 1, 0.8);
      }
      y += ROW_H;
    };

    char sensText[16], rdText[16];
    std::snprintf(sensText, sizeof(sensText), "%.1f", sensitivity);
    std::snprintf(rdText, sizeof(rdText), "%d", renderDistance);
    drawRow("Mouse sensitivity", sensSlider,
            (sensitivity - SENS_MIN) / (SENS_MAX - SENS_MIN), sensText);
    drawRow("Render distance", rdSlider,
            (renderDistance - RD_MIN) / (RD_MAX - RD_MIN), rdText);

    // Resolution: click the value to cycle through the options.
    y += GAP;
    double rowX = cx - ROW_W / 2;
    drawText(g_fontMsg, rowX, y + (ROW_H - g_fontMsg.height) / 2, "Resolution", 1, 1, 1, 1);
    Rect res = { rowX + ROW_W - SLIDER_W, y, SLIDER_W, ROW_H };
    bool hover = res.contains(mouseX, mouseY);
    drawRect(res.x, res.y, res.w, res.h, 1, 1, 1, hover ? 0.25 : 0.12);
    drawRectOutline(res.x, res.y, res.w, res.h, 2, 1, 1, 1, hover ? 1.0 : 0.4);
    const char* resLabel = RESOLUTIONS[resolutionIndex].label;
    drawText(g_fontMsg, res.x + (res.w - textWidth(g_fontMsg, resLabel)) / 2,
             res.y + (ROW_H - g_fontMsg.height) / 2, resLabel, 1, 1, 1, 1);
    buttons.push_back({ res, MenuAction::ResolutionChanged, "" });
    y += ROW_H;

    // Display mode: click the value to toggle Fullscreen / Window.
    y += GAP;
    drawText(g_fontMsg, rowX, y + (ROW_H - g_fontMsg.height) / 2, "Display", 1, 1, 1, 1);
    Rect dm = { rowX + ROW_W - SLIDER_W, y, SLIDER_W, ROW_H };
    bool dmHover = dm.contains(mouseX, mouseY);
    drawRect(dm.x, dm.y, dm.w, dm.h, 1, 1, 1, dmHover ? 0.25 : 0.12);
    drawRectOutline(dm.x, dm.y, dm.w, dm.h, 2, 1, 1, 1, dmHover ? 1.0 : 0.4);
    const char* dmLabel = DISPLAY_MODE_LABELS[displayMode];
    drawText(g_fontMsg, dm.x + (dm.w - textWidth(g_fontMsg, dmLabel)) / 2,
             dm.y + (ROW_H - g_fontMsg.height) / 2, dmLabel, 1, 1, 1, 1);
    buttons.push_back({ dm, MenuAction::DisplayModeChanged, "" });
    y += ROW_H;

    // Character: click the value to cycle Steve / Alex (girl).
    y += GAP;
    drawText(g_fontMsg, rowX, y + (ROW_H - g_fontMsg.height) / 2, "Character", 1, 1, 1, 1);
    Rect ch = { rowX + ROW_W - SLIDER_W, y, SLIDER_W, ROW_H };
    bool chHover = ch.contains(mouseX, mouseY);
    drawRect(ch.x, ch.y, ch.w, ch.h, 1, 1, 1, chHover ? 0.25 : 0.12);
    drawRectOutline(ch.x, ch.y, ch.w, ch.h, 2, 1, 1, 1, chHover ? 1.0 : 0.4);
    const char* chLabel = CHARACTER_LABELS[characterType];
    drawText(g_fontMsg, ch.x + (ch.w - textWidth(g_fontMsg, chLabel)) / 2,
             ch.y + (ROW_H - g_fontMsg.height) / 2, chLabel, 1, 1, 1, 1);
    buttons.push_back({ ch, MenuAction::CharacterChanged, "" });
    y += ROW_H;
  }

  if (isSave) {
    // text input box
    y += GAP;
    Rect box = { cx - INPUT_W / 2, y, INPUT_W, INPUT_H };
    drawRect(box.x, box.y, box.w, box.h, 0, 0, 0, 0.5);
    drawRectOutline(box.x, box.y, box.w, box.h, 2, 1, 1, 1, 0.6);
    std::string shown = saveNameInput;
    if (std::fmod(timeSec, 1.0) < 0.6) shown += "|"; // caret blink
    drawText(g_fontButton, box.x + 8, box.y + (INPUT_H - g_fontButton.height) / 2,
             shown.c_str(), 1, 1, 1, 1);
    y += INPUT_H;

    y += GAP;
    const char* hint = "Type a name - Enter to save";
    drawText(g_fontHint, cx - textWidth(g_fontHint, hint) / 2, y, hint, 1, 1, 1, 0.6);
    y += 18;
  }

  if (isConfirm) {
    y += GAP;
    double tw = textWidth(g_fontMsg, confirmText.c_str());
    drawText(g_fontMsg, cx - tw / 2, y, confirmText.c_str(), 1, 1, 1, 0.9);
    y += 18;
  }

  if (isSave && savesShown > 0) {
    y += GAP;
    const char* label = "Or overwrite an existing save:";
    drawText(g_fontHint, cx - textWidth(g_fontHint, label) / 2, y, label, 1, 1, 1, 0.6);
    y += 14;
  }
  if (isLoad || (isSave && savesShown > 0)) {
    // The load panel pairs each row with a delete square; the save panel's
    // list is a picker only (clicking a row just fills the name box above),
    // so it skips that column and uses PickSaveName instead of LoadNamed.
    const double DEL_W = isLoad ? 38 : 0;
    const double DEL_GAP = isLoad ? 6 : 0;
    MenuAction rowAction = isLoad ? MenuAction::LoadNamed : MenuAction::PickSaveName;
    for (int i = 0; i < savesShown; i++) {
      const SaveInfo& s = saveList[i];
      y += GAP;
      Rect r = { cx - SAVE_BUTTON_W / 2, y, SAVE_BUTTON_W - DEL_W - DEL_GAP, BUTTON_H };
      bool hover = r.contains(mouseX, mouseY);
      drawRect(r.x, r.y, r.w, r.h, 1, 1, 1, hover ? 0.25 : 0.12);
      drawRectOutline(r.x, r.y, r.w, r.h, 2, 1, 1, 1, hover ? 1.0 : 0.4);
      drawText(g_fontButton, r.x + 12, r.y + (BUTTON_H - g_fontButton.height) / 2,
               s.name.c_str(), 1, 1, 1, 1);
      double dw = textWidth(g_fontHint, s.dateText.c_str());
      drawText(g_fontHint, r.x + r.w - 10 - dw, r.y + (BUTTON_H - g_fontHint.height) / 2,
               s.dateText.c_str(), 1, 1, 1, 0.6);
      buttons.push_back({ r, rowAction, s.name });

      if (isLoad) {
        Rect del = { r.x + r.w + DEL_GAP, y, DEL_W, BUTTON_H };
        bool delHover = del.contains(mouseX, mouseY);
        drawRect(del.x, del.y, del.w, del.h,
                delHover ? 0.8 : 1, delHover ? 0.2 : 1, delHover ? 0.2 : 1,
                delHover ? 0.45 : 0.12);
        drawRectOutline(del.x, del.y, del.w, del.h, 2,
                        1, delHover ? 0.5 : 1, delHover ? 0.5 : 1, delHover ? 1.0 : 0.4);
        drawText(g_fontButton, del.x + (DEL_W - textWidth(g_fontButton, "X")) / 2,
                del.y + (BUTTON_H - g_fontButton.height) / 2, "X", 1, 1, 1, 0.9);
        buttons.push_back({ del, MenuAction::DeleteSave, s.name });
      }

      y += BUTTON_H;
    }
    if (savesTruncated) {
      y += GAP;
      char note[64];
      std::snprintf(note, sizeof(note), "(showing %d most recent of %d)",
                    savesShown, (int)saveList.size());
      drawText(g_fontHint, cx - textWidth(g_fontHint, note) / 2, y, note, 1, 1, 1, 0.6);
      y += 18;
    }
  }

  for (const Item& item : items) {
    drawButton(item.label, item.action);
  }

  if (showHint) {
    y += GAP + 14;
    const char* line1 = "WASD move - Mouse look - Space jump";
    const char* line2 = "Left-click mine - Right-click place - 1-9,0 select block";
    const char* line3 = "V toggle first/third person view";
    drawText(g_fontHint, cx - textWidth(g_fontHint, line1) / 2, y, line1, 1, 1, 1, 0.75);
    y += 21;
    drawText(g_fontHint, cx - textWidth(g_fontHint, line2) / 2, y, line2, 1, 1, 1, 0.75);
    y += 21;
    drawText(g_fontHint, cx - textWidth(g_fontHint, line3) / 2, y, line3, 1, 1, 1, 0.75);
    y += 21;
  }

  if (!message.empty()) {
    y += 18;
    double tw = textWidth(g_fontMsg, message.c_str());
    drawText(g_fontMsg, cx - tw / 2 + 1, y + 1, message.c_str(), 0, 0, 0, 1);
    drawText(g_fontMsg, cx - tw / 2, y, message.c_str(),
             255 / 255.0, 227 / 255.0, 138 / 255.0, 1);
  }
}
