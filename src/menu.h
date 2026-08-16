#pragma once
#include "common.h"
#include "save.h"

// Menu overlay: panel switching (main / pause / settings / save / load),
// buttons, settings sliders, a save-name text input, the saved-game picker
// and a timed message line — the native port (plus save slots) of ui/menu.js.

enum class MenuAction {
  None,
  Start,
  Load,       // open the load panel (main.cpp supplies the save list)
  Resume,
  Restart,
  Save,       // open the save panel (main.cpp supplies the default name)
  Quit,       // main menu Quit (exit app)
  QuitToMenu, // pause menu Quit
  OpenSettings, // handled internally (panel switch), never returned
  Back,         // handled internally (panel switch), never returned
  ConfirmSave,  // save panel confirmed; name is in saveNameInput
  LoadNamed,    // a save was picked in the load panel; name is in selectedSave
  PickSaveName, // a row was picked in the SAVE panel's list; handled internally
                // (copies the name into saveNameInput rather than loading it)
  DeleteSave,   // a row's X was clicked; name is in selectedSave
  DeleteConfirmed,    // confirm dialog accepted; name is in confirmData
  OverwriteConfirmed, // confirm dialog accepted; name is in confirmData
  CancelConfirm,      // handled internally (back to confirmReturnPanel)
  SensitivityChanged,
  RenderDistanceChanged,
  ResolutionChanged, // resolutionIndex was cycled; read RESOLUTIONS[index]
  DisplayModeChanged, // displayMode was cycled; read DISPLAY_MODE_LABELS[displayMode]
  CharacterChanged,   // characterType was cycled; read CHARACTER_LABELS[characterType]
};

struct Resolution { int w, h; const char* label; };
inline constexpr Resolution RESOLUTIONS[] = {
  { 1280, 720, "1280x720" },
  { 2560, 1440, "2560x1440" },
  { 3440, 1440, "3440x1440" },
};
inline constexpr int RESOLUTION_COUNT = 3;

// 0 = Fullscreen (auto-borderless when the resolution doesn't fit the work
// area), 1 = Window (always a framed, movable window).
inline constexpr const char* DISPLAY_MODE_LABELS[] = { "Fullscreen", "Window" };
inline constexpr int DISPLAY_MODE_COUNT = 2;
inline constexpr const char* CHARACTER_LABELS[] = { "Steve", "Alex" };
inline constexpr int CHARACTER_COUNT = 2;

enum class MenuPanel { Main, Pause, Dead, SettingsPanel, SaveGamePanel, LoadGamePanel, ConfirmPanel };

class Menu {
public:
  MenuPanel panel = MenuPanel::Main;
  MenuPanel previousPanel = MenuPanel::Main;
  bool visible = true;

  // Live-updating slider values (main.cpp copies them into Settings on
  // SensitivityChanged / RenderDistanceChanged actions).
  double sensitivity = 1;
  int renderDistance = 4;
  int resolutionIndex = 0; // index into RESOLUTIONS
  int displayMode = 0;     // index into DISPLAY_MODE_LABELS
  int characterType = 0;   // index into CHARACTER_LABELS

  std::string saveNameInput; // save panel text field
  std::string selectedSave;  // set when a LoadNamed/DeleteSave action is returned

  // Confirm dialog state (delete / overwrite prompts).
  std::string confirmText;        // e.g. "Delete \"save\"?"
  std::string confirmButtonLabel; // e.g. "Delete"
  MenuAction confirmAction = MenuAction::None; // returned on confirm click
  std::string confirmData;        // save name the confirmation refers to

  void showPanel(MenuPanel p);
  void hide() { visible = false; }
  // Sub-panels (settings/save/load) close back to previousPanel on ESC.
  // Dead behaves like Pause here — a top-level screen with nothing to close
  // back to, not something ESC should back out of.
  bool isSubPanel() const {
    return visible && panel != MenuPanel::Main && panel != MenuPanel::Pause && panel != MenuPanel::Dead;
  }
  // `saves` lets the panel list existing slots so one can be picked to
  // overwrite, same list the load panel already shows.
  void openSavePanel(const std::string& defaultName, std::vector<SaveInfo> saves);
  void openLoadPanel(std::vector<SaveInfo> saves);
  // Re-shows the load panel with a fresh list without touching previousPanel
  // (used after deleting a save).
  void refreshLoadPanel(std::vector<SaveInfo> saves);
  void openConfirm(const std::string& text, const std::string& buttonLabel,
                   MenuAction action, const std::string& data);
  // ESC from a sub-panel: confirm dialogs return to the panel that opened
  // them, other sub-panels return to previousPanel.
  void closeSubPanel();
  void showMessage(const std::string& text);
  void update(double dt);

  // Draws the overlay and records this frame's hit-test rects.
  void draw(int winW, int winH, double mouseX, double mouseY);

  MenuAction onMouseDown(double x, double y);
  MenuAction onMouseMove(double x, double y); // slider dragging
  void onMouseUp();
  // Typed character (WM_CHAR) for the save-name field; Enter confirms.
  MenuAction onChar(unsigned char c);

private:
  struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const {
      return px >= x && px < x + w && py >= y && py < y + h;
    }
  };
  struct ButtonHit {
    Rect rect;
    MenuAction action;
    std::string data; // save name for LoadNamed buttons
  };

  std::vector<ButtonHit> buttons; // rebuilt every draw
  Rect sensSlider, rdSlider;      // slider track hit areas (settings panel)
  int draggingSlider = -1;        // -1 none, 0 sensitivity, 1 render distance

  std::vector<SaveInfo> saveList; // shown in the load panel
  MenuPanel confirmReturnPanel = MenuPanel::Main; // where Cancel/ESC goes

  std::string message;
  double messageTimer = 0;
  double timeSec = 0; // for the text caret blink

  MenuAction applySliderDrag(double x);
  void drawSlider(const Rect& r, double t);
};
