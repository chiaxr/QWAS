#include "game.h"
#include "raymath.h"
#include "save.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

// Browsers can deliver a press and its release between two frames (quick
// touch taps, trackpad tap-to-click). raylib only samples the current button
// state once per frame, so such a press would never be seen. Latch every
// canvas pointerdown (mouse, touch or pen) in screen coordinates so the next
// frame can treat the pointer as pressed at least once.
EM_JS(int, qwas_web_take_pointer_press, (float* x, float* y), {
    var canvas = Module.canvas;
    if (!canvas) return 0;
    if (!Module.__qwasPressListener) {
        Module.__qwasPressListener = true;
        canvas.addEventListener('pointerdown', function(e) {
            var r = canvas.getBoundingClientRect();
            if (r.width <= 0 || r.height <= 0) return;
            Module.__qwasPress = {
                x: (e.clientX - r.left) * canvas.width  / r.width,
                y: (e.clientY - r.top)  * canvas.height / r.height
            };
        }, true);
    }
    var p = Module.__qwasPress;
    if (!p) return 0;
    Module.__qwasPress = null;
    HEAPF32[x >> 2] = p.x;
    HEAPF32[y >> 2] = p.y;
    return 1;
});
#endif

namespace {
constexpr float TOUCH_GUIDE_FADE_SPEED = 1.8f;
constexpr float TAP_MAX_MOVE = 28.0f;

enum MenuItem { MENU_START, MENU_MODE, MENU_SETTINGS, MENU_INSTRUCTIONS, MENU_COUNT };

// Ground reaction: rest at restY, kill downward velocity, and damp tilt
// (simulates contact friction). Called once per physics step; the damping
// matches the original 0.85 per frame at 60 fps.
void ApplyGroundContact(Drone& drone, float restY) {
    static const float contactDamp = powf(0.85f, PHYSICS_DT * 60.0f);
    drone.position.y = restY;
    if (drone.velocity.y < 0.0f) drone.velocity.y = 0.0f;
    drone.angularVel.x *= contactDamp;
    drone.angularVel.z *= contactDamp;
}

Rectangle GetRotorTouchZone(RotorID id, int screenW, int screenH) {
    float zoneW = screenW * 0.5f;
    float zoneH = screenH * 0.5f;

    switch (id) {
        case ROTOR_FRONT_LEFT:
            return {0.0f, 0.0f, zoneW, zoneH};
        case ROTOR_FRONT_RIGHT:
            return {screenW - zoneW, 0.0f, zoneW, zoneH};
        case ROTOR_REAR_LEFT:
            return {0.0f, screenH - zoneH, zoneW, zoneH};
        case ROTOR_REAR_RIGHT:
            return {screenW - zoneW, screenH - zoneH, zoneW, zoneH};
    }

    return {0.0f, 0.0f, 0.0f, 0.0f};
}

bool IsRotorTouchDown(RotorID id, int screenW, int screenH) {
    Rectangle zone = GetRotorTouchZone(id, screenW, screenH);

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), zone))
        return true;

    int touchCount = GetTouchPointCount();
    for (int i = 0; i < touchCount; i++) {
        if (CheckCollisionPointRec(GetTouchPosition(i), zone))
            return true;
    }

    return false;
}

bool IsPrimaryPointerDown() {
    return IsMouseButtonDown(MOUSE_BUTTON_LEFT) || GetTouchPointCount() > 0;
}

// True if a press happened since the last call that raylib may have missed
// (web only; desktop relies on raylib's per-frame button state).
bool TakeLatchedPointerPress(Vector2* pos) {
#if defined(__EMSCRIPTEN__)
    return qwas_web_take_pointer_press(&pos->x, &pos->y) != 0;
#else
    (void)pos;
    return false;
#endif
}

Vector2 GetPrimaryPointerPosition() {
    if (GetTouchPointCount() > 0)
        return GetTouchPosition(0);

    return GetMousePosition();
}

void DrawRotorTouchZone(RotorID id, const Drone& drone, int screenW, int screenH, const char* label, float alpha) {
    Rectangle zone = GetRotorTouchZone(id, screenW, screenH);
    Color color = drone.rotors[id].color;
    bool active = drone.rotors[id].thrust > 0.01f;

    DrawRectangleRec(zone, Fade(color, alpha * (active ? 0.20f : 0.06f)));
    DrawRectangleLinesEx(zone, active ? 4.0f : 2.0f, Fade(color, alpha * (active ? 0.80f : 0.35f)));

    int fontSize = 26;
    int labelW = MeasureText(label, fontSize);
    DrawText(label,
             (int)(zone.x + (zone.width - labelW) * 0.5f),
             (int)(zone.y + (zone.height - fontSize) * 0.5f),
             fontSize,
             Fade(color, alpha * 0.90f));
}

const char* GetCrashTitle(CrashReason reason) {
    switch (reason) {
        case CrashReason::ROTOR_STRIKE:   return "ROTOR STRIKE!";
        case CrashReason::GROUND_IMPACT:  return "GROUND IMPACT!";
        case CrashReason::TOO_HIGH:       return "TOO HIGH!";
        case CrashReason::OUT_OF_BOUNDS:  return "OUT OF BOUNDS!";
        case CrashReason::NONE:           return "CRASHED!";
    }

    return "CRASHED!";
}

const char* GetCrashHint(CrashReason reason) {
    switch (reason) {
        case CrashReason::ROTOR_STRIKE:   return "A rotor hit the ground.";
        case CrashReason::GROUND_IMPACT:  return "The drone body hit the ground.";
        case CrashReason::TOO_HIGH:       return "You flew above the safe altitude.";
        case CrashReason::OUT_OF_BOUNDS:  return "You left the flight area.";
        case CrashReason::NONE:           return "";
    }

    return "";
}
static Rectangle GetEndScreenButtonRect(int idx, int screenW, int btnTopY) {
    const int btnW = 280, btnH = 52, gap = 24;
    int bx = (screenW - (btnW * 2 + gap)) / 2 + idx * (btnW + gap);
    return {(float)bx, (float)btnTopY, (float)btnW, (float)btnH};
}

static Rectangle GetMenuButtonRect(int idx, int screenW, int screenH) {
    const int btnW = 400, btnH = 52;
    int btnX = (screenW - btnW) / 2;
    int btnY = (int)(screenH * 0.43f) + idx * 66;
    return {(float)btnX, (float)btnY, (float)btnW, (float)btnH};
}

// Shared look for menu buttons and the difficulty switch
static void DrawMenuButtonFrame(Rectangle rect, bool selected) {
    Color bg     = selected ? Color{45, 75, 45, 230} : Color{15, 15, 15, 210};
    Color border = selected ? Color{100, 200, 100, 255} : Color{55, 55, 55, 200};
    DrawRectangleRec(rect, bg);
    DrawRectangleLinesEx(rect, 2.0f, border);
}

static Color MenuButtonTextColor(bool selected) {
    return selected ? WHITE : Color{200, 200, 200, 255};
}

static void DrawMenuButton(const char* label, Rectangle rect, bool selected) {
    DrawMenuButtonFrame(rect, selected);
    const int fs = 26;
    int tw = MeasureText(label, fs);
    DrawText(label,
             (int)(rect.x + (rect.width  - tw) * 0.5f),
             (int)(rect.y + (rect.height - fs) * 0.5f),
             fs, MenuButtonTextColor(selected));
}

// Difficulty switch on the menu: a slide switch filling the whole menu-button
// rect, styled like the other buttons. The active label shows in the uncovered
// half and a flat grey block covers the other half. t is the block position
// (0 = EASY: "EASY" on the left, block on the right;
//  1 = HARD: block on the left, "HARD" on the right).
// The whole rect is the click/tap target.
static void DrawDifficultySwitch(Rectangle rect, bool selected, float t) {
    DrawMenuButtonFrame(rect, selected);

    // Labels centered in each half; the block slides over the inactive one
    const int fs = 26;
    float halfW = rect.width * 0.5f;
    float textY = rect.y + (rect.height - fs) * 0.5f;
    Color textCol = MenuButtonTextColor(selected);
    const char* left  = "EASY";
    const char* right = "HARD";
    DrawText(left,  (int)(rect.x + (halfW - MeasureText(left, fs)) * 0.5f), (int)textY, fs, textCol);
    DrawText(right, (int)(rect.x + halfW + (halfW - MeasureText(right, fs)) * 0.5f), (int)textY, fs, textCol);

    const float inset = 6.0f;
    Rectangle block = {rect.x + inset + (1.0f - t) * halfW, rect.y + inset,
                       halfW - 2.0f * inset, rect.height - 2.0f * inset};
    DrawRectangleRec(block, {110, 110, 110, 255});
}

struct SettingsEntry {
    const char* label;
    const char* unit;
    float*      val;
    float       minV;
    float       maxV;
    float       step;
    float       defaultVal;
};

constexpr int SETTINGS_COUNT = 12;
constexpr int SETTINGS_ROW_H = 34;

const SettingsEntry kSettingsEntries[SETTINGS_COUNT] = {
    { "Mass",             "kg",          &DRONE_MASS,       0.1f,  5.0f,  0.05f, DRONE_MASS_DEFAULT       },
    { "Gravity",          "m/s\xc2\xb2",     &GRAVITY,          0.0f,  20.0f, 0.1f,  GRAVITY_DEFAULT          },
    { "Max Thrust",       "N",           &MAX_THRUST,       0.5f,  10.0f, 0.1f,  MAX_THRUST_DEFAULT       },
    { "Thrust Ramp Up",   "N/s",         &THRUST_RAMP_UP,   1.0f,  20.0f, 0.5f,  THRUST_RAMP_UP_DEFAULT   },
    { "Thrust Ramp Down", "N/s",         &THRUST_RAMP_DOWN, 1.0f,  30.0f, 0.5f,  THRUST_RAMP_DOWN_DEFAULT },
    { "Arm Length",       "m",           &ARM_LENGTH,       0.05f, 1.0f,  0.01f, ARM_LENGTH_DEFAULT       },
    { "Inertia Pitch",    "kg\xc2\xb7m\xc2\xb2", &I_PITCH,          0.01f, 1.0f,  0.05f, I_PITCH_DEFAULT          },
    { "Inertia Yaw",      "kg\xc2\xb7m\xc2\xb2", &I_YAW,            0.01f, 1.0f,  0.05f, I_YAW_DEFAULT            },
    { "Inertia Roll",     "kg\xc2\xb7m\xc2\xb2", &I_ROLL,           0.01f, 1.0f,  0.05f, I_ROLL_DEFAULT           },
    { "Linear Drag",      "/s",          &LIN_DRAG,         0.0f,  3.0f,  0.5f,  LIN_DRAG_DEFAULT         },
    { "Angular Drag",     "/s",          &ANG_DRAG,         0.0f,  10.0f, 0.1f,  ANG_DRAG_DEFAULT         },
    { "Yaw Coeff",        "",            &K_YAW,            0.01f, 0.2f,  0.01f, K_YAW_DEFAULT            },
};

// Shared modal-dialog chrome: full-screen dim + panel fill/border, and a
// BACK button + footer hint reserved in a fixed-height block at the bottom
// of every panel, so Settings/Instructions/any future dialog look and
// behave identically.
constexpr int DIALOG_BACK_BTN_W            = 220;
constexpr int DIALOG_BACK_BTN_H            = 52;
constexpr int DIALOG_BACK_BTN_BOTTOM_MARGIN = 20;
constexpr int DIALOG_FOOTER_GAP            = 24;  // gap between footer hint and button top
constexpr int DIALOG_FOOTER_BLOCK_H        = DIALOG_BACK_BTN_BOTTOM_MARGIN + DIALOG_BACK_BTN_H + DIALOG_FOOTER_GAP;
constexpr int DIALOG_TITLE_SIZE            = 28;
constexpr int DIALOG_TITLE_TOP_PAD         = 14;

Rectangle GetDialogBackButtonRect(int bx, int by, int bw, int bh) {
    int bx2 = bx + (bw - DIALOG_BACK_BTN_W) / 2;
    int by2 = by + bh - DIALOG_BACK_BTN_BOTTOM_MARGIN - DIALOG_BACK_BTN_H;
    return { (float)bx2, (float)by2, (float)DIALOG_BACK_BTN_W, (float)DIALOG_BACK_BTN_H };
}

int GetDialogFooterY(int by, int bh) {
    return by + bh - DIALOG_FOOTER_BLOCK_H;
}

struct SettingsLayout { int bx, by, bw, bh, sliderX, sliderW; };

SettingsLayout GetSettingsLayout(int screenW, int screenH) {
    int bw = 820, bh = 60 + SETTINGS_COUNT * SETTINGS_ROW_H + DIALOG_FOOTER_BLOCK_H + 20;
    int bx = (screenW - bw) / 2;
    int by = (screenH - bh) / 2;
    return { bx, by, bw, bh, bx + 290, 300 };
}

Rectangle GetSettingsRowRect(const SettingsLayout& L, int idx) {
    int ry = L.by + 54 + idx * SETTINGS_ROW_H;
    return { (float)(L.bx + 2), (float)ry, (float)(L.bw - 4), (float)(SETTINGS_ROW_H - 2) };
}

Rectangle GetSettingsSliderHitRect(const SettingsLayout& L, int idx) {
    int ry = L.by + 54 + idx * SETTINGS_ROW_H;
    return { (float)L.sliderX, (float)ry, (float)L.sliderW, (float)SETTINGS_ROW_H };
}

struct InstructionsLayout { int bx, by, bw, bh; };

InstructionsLayout GetInstructionsLayout(int screenW, int screenH) {
    const int bw = 760, bh = 680;
    int bx = (screenW - bw) / 2;
    int by = (screenH - bh) / 2;
    return { bx, by, bw, bh };
}
}  // namespace

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------

void Game::Init() {
    startPad.position = {0, 0, 0};
    startPad.halfSize = 1.0f;
    pad.position      = {0, 0, PAD_WORLD_Z};
    pad.halfSize      = 1.0f;
    for (int i = 0; i < DIFFICULTY_COUNT; i++) {
        bestScores[i] = 0;
        bestTimes[i]  = 0;
    }
    perfectLanding    = false;
    runProgress       = 0;
    runTime           = 0;
    runTimerStarted   = false;
    newBestTime       = false;

    camera.fovy       = CAMERA_FOV;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.up         = {0, 1, 0};

    // Camera starts looking at spawn from behind/above
    camera.position = {0, 4, 8};
    camera.target   = {0, DRONE_REST_Y, 0};

    state = GameState::MENU;
    difficulty = Difficulty::EASY;
    LoadProgress();  // may override difficulty, bests and physics settings
    crashReason = CrashReason::NONE;
    drone.Init({0, DRONE_REST_Y, 0}, difficulty == Difficulty::EASY);
    physicsAccumulator = 0;
    deadTimer = 0;
    winTimer  = 0;
    settingsSelectedIdx = 0;
    draggingSlider = false;
    draggedSettingsIdx = 0;
    menuSelectedIdx = 0;
    endScreenSelectedIdx = 0;
    touchGuideAlpha = 1.0f;
    touchGuideDismissed = false;
    tapWasDown = false;
    tapCandidate = false;
    tapCompleted = false;
    tapStartState = state;
    tapStart = {0, 0};
    modeSwitchT = (difficulty == Difficulty::HARD) ? 1.0f : 0.0f;
}

void Game::Reset() {
    drone.Init({0, DRONE_REST_Y, 0}, difficulty == Difficulty::EASY);
    crashReason = CrashReason::NONE;
    perfectLanding = false;
    runProgress = 0;
    runTime = 0;
    runTimerStarted = false;
    newBestTime = false;
    runInputs.clear();
    ghostActive = false;
    ghostStep = 0;
    physicsAccumulator = 0;
    deadTimer = 0;
    winTimer  = 0;
    touchGuideAlpha = 1.0f;
    touchGuideDismissed = false;

    camera.position = {0, 4, 8};
    camera.target   = {0, DRONE_REST_Y, 0};
    camera.up       = {0, 1, 0};
    camera.fovy     = CAMERA_FOV;
}

// ---------------------------------------------------------------------------
//  Saved progress: key=value lines (difficulty, per-difficulty bests, settings)
// ---------------------------------------------------------------------------

static const char* DifficultyKey(int i) {
    return i == (int)Difficulty::EASY ? "easy" : "hard";
}

// "Thrust Ramp Up" -> "thrust_ramp_up"
static std::string SettingKey(const char* label) {
    std::string key = "setting.";
    for (const char* c = label; *c; c++)
        key += (*c == ' ') ? '_' : (char)tolower((unsigned char)*c);
    return key;
}

void Game::LoadProgress() {
    std::istringstream in(LoadSaveData());
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        float v = strtof(value.c_str(), nullptr);

        if (key == "difficulty") {
            if (value == "easy") difficulty = Difficulty::EASY;
            if (value == "hard") difficulty = Difficulty::HARD;
            continue;
        }
        for (int i = 0; i < DIFFICULTY_COUNT; i++) {
            if (key == std::string("best_progress_") + DifficultyKey(i)) bestScores[i] = Clamp(v, 0.0f, 100.0f);
            if (key == std::string("best_time_") + DifficultyKey(i))     bestTimes[i]  = fmaxf(v, 0.0f);
        }
        for (const SettingsEntry& e : kSettingsEntries)
            if (key == SettingKey(e.label)) *e.val = Clamp(v, e.minV, e.maxV);
    }
}

void Game::SaveProgress() const {
    std::ostringstream out;
    out << "difficulty=" << DifficultyKey((int)difficulty) << "\n";
    for (int i = 0; i < DIFFICULTY_COUNT; i++) {
        out << "best_progress_" << DifficultyKey(i) << "=" << bestScores[i] << "\n";
        out << "best_time_"     << DifficultyKey(i) << "=" << bestTimes[i]  << "\n";
    }
    for (const SettingsEntry& e : kSettingsEntries)
        out << SettingKey(e.label) << "=" << *e.val << "\n";
    StoreSaveData(out.str());
}

// ---------------------------------------------------------------------------
//  Update
// ---------------------------------------------------------------------------

void Game::Update(float dt) {
    UpdateTap();

    float modeTarget = (difficulty == Difficulty::HARD) ? 1.0f : 0.0f;
    modeSwitchT += (modeTarget - modeSwitchT) * fminf(1.0f, 14.0f * dt);

    switch (state) {
        case GameState::MENU:           UpdateMenu();         break;
        case GameState::SETTINGS:       UpdateSettings();     break;
        case GameState::INSTRUCTIONS:   UpdateInstructions(); break;
        case GameState::PLAYING:        UpdatePlaying(dt);      break;
        case GameState::DEAD:           UpdateDead(dt);         break;
        case GameState::WIN:            UpdateWin(dt);          break;
    }
}

void Game::UpdateMenu() {
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    // Keyboard navigation
    if (IsKeyPressed(KEY_UP))   menuSelectedIdx = (menuSelectedIdx - 1 + MENU_COUNT) % MENU_COUNT;
    if (IsKeyPressed(KEY_DOWN)) menuSelectedIdx = (menuSelectedIdx + 1) % MENU_COUNT;

    // Mouse hover → update selection
    Vector2 mp = GetMousePosition();
    for (int i = 0; i < MENU_COUNT; i++) {
        if (CheckCollisionPointRec(mp, GetMenuButtonRect(i, sw, sh)))
            menuSelectedIdx = i;
    }
    // Touch hover → update selection
    for (int t = 0; t < GetTouchPointCount(); t++) {
        Vector2 tp = GetTouchPosition(t);
        for (int i = 0; i < MENU_COUNT; i++) {
            if (CheckCollisionPointRec(tp, GetMenuButtonRect(i, sw, sh)))
                menuSelectedIdx = i;
        }
    }

    // Left/Right set the difficulty switch directly when it is selected
    if (menuSelectedIdx == MENU_MODE) {
        if (IsKeyPressed(KEY_LEFT))  { difficulty = Difficulty::EASY; SaveProgress(); }
        if (IsKeyPressed(KEY_RIGHT)) { difficulty = Difficulty::HARD; SaveProgress(); }
    }

    // Activate via keyboard Enter / Space
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        ActivateMenuButton(menuSelectedIdx);
        return;
    }

    // Activate via mouse click or touch tap
    for (int i = 0; i < MENU_COUNT; i++) {
        if (TappedIn(GetMenuButtonRect(i, sw, sh))) {
            menuSelectedIdx = i;
            ActivateMenuButton(i);
            return;
        }
    }
}

void Game::ActivateMenuButton(int idx) {
    switch (idx) {
        case MENU_START:        Reset(); endScreenSelectedIdx = 0; state = GameState::PLAYING; break;
        case MENU_MODE:
            difficulty = (difficulty == Difficulty::EASY) ? Difficulty::HARD : Difficulty::EASY;
            SaveProgress();
            break;
        case MENU_SETTINGS:     settingsSelectedIdx = 0; draggingSlider = false; state = GameState::SETTINGS; break;
        case MENU_INSTRUCTIONS: state = GameState::INSTRUCTIONS; break;
    }
}

void Game::UpdateSettings() {
    auto exitToMenu = [&] {
        SaveProgress();
        draggingSlider = false;
        menuSelectedIdx = MENU_SETTINGS;
        state = GameState::MENU;
    };

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) { exitToMenu(); return; }

    if (IsKeyPressed(KEY_R)) {
        for (const SettingsEntry& e : kSettingsEntries) *e.val = e.defaultVal;
        return;
    }

    if (IsKeyPressed(KEY_UP))   settingsSelectedIdx = (settingsSelectedIdx - 1 + SETTINGS_COUNT) % SETTINGS_COUNT;
    if (IsKeyPressed(KEY_DOWN)) settingsSelectedIdx = (settingsSelectedIdx + 1) % SETTINGS_COUNT;

    const SettingsEntry& ke = kSettingsEntries[settingsSelectedIdx];
    if (IsKeyDown(KEY_LEFT))  *ke.val = fmaxf(ke.minV, *ke.val - ke.step);
    if (IsKeyDown(KEY_RIGHT)) *ke.val = fminf(ke.maxV, *ke.val + ke.step);

    // Pointer (mouse/touch) handling for sliders
    SettingsLayout L = GetSettingsLayout(GetScreenWidth(), GetScreenHeight());
    bool pointerDown = IsPrimaryPointerDown();

    if (draggingSlider) {
        if (pointerDown) {
            float px = GetPrimaryPointerPosition().x;
            const SettingsEntry& e = kSettingsEntries[draggedSettingsIdx];
            float t = Clamp((px - L.sliderX) / (float)L.sliderW, 0.0f, 1.0f);
            *e.val = e.minV + t * (e.maxV - e.minV);
        } else {
            draggingSlider = false;
        }
    } else if (pointerDown) {
        Vector2 pp = GetPrimaryPointerPosition();
        for (int i = 0; i < SETTINGS_COUNT; i++) {
            if (CheckCollisionPointRec(pp, GetSettingsRowRect(L, i))) {
                settingsSelectedIdx = i;
                if (CheckCollisionPointRec(pp, GetSettingsSliderHitRect(L, i))) {
                    draggingSlider = true;
                    draggedSettingsIdx = i;
                    const SettingsEntry& e = kSettingsEntries[i];
                    float t = Clamp((pp.x - L.sliderX) / (float)L.sliderW, 0.0f, 1.0f);
                    *e.val = e.minV + t * (e.maxV - e.minV);
                }
                break;
            }
        }
    }

    // Back button (discrete — skip while a slider drag is in progress)
    if (!draggingSlider) {
        Rectangle backRect = GetDialogBackButtonRect(L.bx, L.by, L.bw, L.bh);
        if (TappedIn(backRect)) { exitToMenu(); return; }
    }
}

void Game::UpdateInstructions() {
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) {
        menuSelectedIdx = MENU_INSTRUCTIONS;
        state = GameState::MENU;
        return;
    }

    InstructionsLayout L = GetInstructionsLayout(GetScreenWidth(), GetScreenHeight());
    Rectangle backRect = GetDialogBackButtonRect(L.bx, L.by, L.bw, L.bh);

    if (TappedIn(backRect)) {
        menuSelectedIdx = MENU_INSTRUCTIONS;
        state = GameState::MENU;
    }
}

void Game::UpdatePlaying(float dt) {
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) { SaveProgress(); state = GameState::MENU; return; }
    if (IsKeyPressed(KEY_R)) { SaveProgress(); Reset(); return; }

    int w = GetScreenWidth();
    int h = GetScreenHeight();

    bool frontLeftInput  = IsKeyDown(KEY_Q) || IsRotorTouchDown(ROTOR_FRONT_LEFT, w, h);
    bool frontRightInput = IsKeyDown(KEY_W) || IsRotorTouchDown(ROTOR_FRONT_RIGHT, w, h);
    bool rearLeftInput   = IsKeyDown(KEY_A) || IsRotorTouchDown(ROTOR_REAR_LEFT, w, h);
    bool rearRightInput  = IsKeyDown(KEY_S) || IsRotorTouchDown(ROTOR_REAR_RIGHT, w, h);

    if (frontLeftInput || frontRightInput || rearLeftInput || rearRightInput) {
        touchGuideDismissed = true;
        if (!runTimerStarted) StartRun();  // time on the start pad before the first input doesn't count
    }

    if (touchGuideDismissed)
        touchGuideAlpha = fmaxf(0.0f, touchGuideAlpha - TOUCH_GUIDE_FADE_SPEED * dt);

    // Fixed-step physics: inputs are sampled once per frame and the simulation
    // advances in PHYSICS_DT steps, so flight behaves the same at any frame rate
    const bool rotorInputs[ROTOR_COUNT] = {frontLeftInput, frontRightInput, rearLeftInput, rearRightInput};
    physicsAccumulator += dt;
    while (physicsAccumulator >= PHYSICS_DT && state == GameState::PLAYING) {
        physicsAccumulator -= PHYSICS_DT;
        StepPhysics(rotorInputs);
    }
    UpdateCamera(dt);

    if (runProgress > BestScore()) BestScore() = runProgress;

    if (state != GameState::PLAYING) SaveProgress();  // run ended (WIN or DEAD)
}

void Game::UpdateDead(float dt) {
    deadTimer -= dt;

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) { state = GameState::MENU; return; }

    if (deadTimer > 0.0f) return;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int btnY = sh / 2 + 90;

    // Keyboard navigation between Retry (0) and Menu (1)
    if (IsKeyPressed(KEY_LEFT))  endScreenSelectedIdx = 0;
    if (IsKeyPressed(KEY_RIGHT)) endScreenSelectedIdx = 1;

    // Hover: mouse
    Vector2 mp = GetMousePosition();
    for (int i = 0; i < 2; i++)
        if (CheckCollisionPointRec(mp, GetEndScreenButtonRect(i, sw, btnY)))
            endScreenSelectedIdx = i;
    // Hover: touch
    for (int t = 0; t < GetTouchPointCount(); t++) {
        Vector2 tp = GetTouchPosition(t);
        for (int i = 0; i < 2; i++)
            if (CheckCollisionPointRec(tp, GetEndScreenButtonRect(i, sw, btnY)))
                endScreenSelectedIdx = i;
    }

    auto activateEnd = [&](int idx) {
        if (idx == 0) { Reset(); state = GameState::PLAYING; }
        else          { state = GameState::MENU; }
    };

    if (IsKeyPressed(KEY_R))                                    { activateEnd(0); return; }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))     { activateEnd(endScreenSelectedIdx); return; }

    for (int i = 0; i < 2; i++)
        if (TappedIn(GetEndScreenButtonRect(i, sw, btnY))) { activateEnd(i); return; }
}

void Game::UpdateWin(float dt) {
    winTimer += dt;

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) { state = GameState::MENU; return; }

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int btnY = sh / 2 + 90;

    // Keyboard navigation between Retry (0) and Menu (1)
    if (IsKeyPressed(KEY_LEFT))  endScreenSelectedIdx = 0;
    if (IsKeyPressed(KEY_RIGHT)) endScreenSelectedIdx = 1;

    // Hover: mouse
    Vector2 mp = GetMousePosition();
    for (int i = 0; i < 2; i++)
        if (CheckCollisionPointRec(mp, GetEndScreenButtonRect(i, sw, btnY)))
            endScreenSelectedIdx = i;
    // Hover: touch
    for (int t = 0; t < GetTouchPointCount(); t++) {
        Vector2 tp = GetTouchPosition(t);
        for (int i = 0; i < 2; i++)
            if (CheckCollisionPointRec(tp, GetEndScreenButtonRect(i, sw, btnY)))
                endScreenSelectedIdx = i;
    }

    auto activateEnd = [&](int idx) {
        if (idx == 0) { Reset(); state = GameState::PLAYING; }
        else          { state = GameState::MENU; }
    };

    if (IsKeyPressed(KEY_R))                                    { activateEnd(0); return; }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))     { activateEnd(endScreenSelectedIdx); return; }

    for (int i = 0; i < 2; i++)
        if (TappedIn(GetEndScreenButtonRect(i, sw, btnY))) { activateEnd(i); return; }
}

// Tracks one press/release of the primary pointer (mouse or first touch) per
// frame. Called once at the top of Update so every screen sees the same tap,
// and each tap activates exactly one button: on release, where it started,
// and only on the screen it started on (a press on one screen can't click a
// button on the next).
void Game::UpdateTap() {
    Vector2 pressPosition;
    bool latchedPress = TakeLatchedPointerPress(&pressPosition);
    bool pointerDown = IsPrimaryPointerDown() || latchedPress;
    tapCompleted = false;

    if (pointerDown) {
        Vector2 pointerPosition = latchedPress ? pressPosition : GetPrimaryPointerPosition();
        if (!tapWasDown) {
            tapCandidate = true;
            tapStart = pointerPosition;
            tapStartState = state;
        } else if (tapCandidate && Vector2Distance(tapStart, pointerPosition) > TAP_MAX_MOVE) {
            tapCandidate = false;
        }
    } else if (tapWasDown && tapCandidate) {
        tapCompleted = (tapStartState == state);
        tapCandidate = false;
    }

    tapWasDown = pointerDown;
}

bool Game::TappedIn(Rectangle rect) const {
    return tapCompleted && CheckCollisionPointRec(tapStart, rect);
}

void Game::UpdateCamera(float dt) {
    Vector3 fwd = drone.GetForwardDir();

    Vector3 targetPos = {
        drone.position.x - fwd.x * CHASE_DIST,
        drone.position.y + CHASE_HEIGHT,
        drone.position.z - fwd.z * CHASE_DIST
    };

    float s = CAMERA_SMOOTH * dt;
    camera.position.x += (targetPos.x - camera.position.x) * s;
    camera.position.y += (targetPos.y - camera.position.y) * s;
    camera.position.z += (targetPos.z - camera.position.z) * s;

    camera.target = {
        drone.position.x + fwd.x * 0.5f,
        drone.position.y,
        drone.position.z + fwd.z * 0.5f
    };
    camera.up = {0, 1, 0};
}

// Progress toward the landing pad from straight-line distance: 0% at the spawn
// point, rising as the drone closes in on the pad's landing spot. Capped at
// MAX_FLIGHT_PROGRESS so only an actual landing scores 99.999% or 100%.
float Game::ProgressAt(Vector3 position) const {
    Vector3 goal  = {pad.position.x, DRONE_REST_Y, pad.position.z};
    Vector3 spawn = {startPad.position.x, DRONE_REST_Y, startPad.position.z};
    float pct = (1.0f - Vector3Distance(position, goal) / Vector3Distance(spawn, goal)) * 100.0f;
    return Clamp(pct, 0.0f, MAX_FLIGHT_PROGRESS);
}

// Applies pad/ground contact to any drone (the player or the ghost) and reports
// whether it is still flying, has landed on the destination pad, or crashed.
Game::ContactResult Game::ResolveContact(Drone& d) const {
    // --- Starting pad: ground contact here is never a crash ---
    bool onStartPad = fabsf(d.position.x - startPad.position.x) < startPad.halfSize &&
                      fabsf(d.position.z - startPad.position.z) < startPad.halfSize;

    if (onStartPad) {
        // Detect contact: drone sinking into the pad surface
        if (d.position.y < DRONE_REST_Y)
            ApplyGroundContact(d, DRONE_REST_Y);
        return {GameState::PLAYING, CrashReason::NONE};  // never crash while on the starting pad
    }

    // --- Destination pad: win check (before crash checks) ---
    bool reachedPad = fabsf(d.position.x - pad.position.x) < pad.halfSize &&
                      fabsf(d.position.z - pad.position.z) < pad.halfSize &&
                      d.position.y < DRONE_REST_Y + 0.5f;
    if (reachedPad)
        return {GameState::WIN, CrashReason::NONE};

    // --- Easy mode: gentle touchdowns on the grass are safe ---
    if (difficulty == Difficulty::EASY) {
        // Lowest center height that keeps the body and every rotor above the grass
        float restY = GROUND_REST_Y;
        for (int i = 0; i < ROTOR_COUNT; i++)
            restY = fmaxf(restY, d.position.y - d.GetRotorWorldPos((RotorID)i).y);

        bool gentle = Vector3Length(d.velocity) < EASY_SAFE_TOUCHDOWN_SPEED &&
                      d.GetTiltAngle() < EASY_SAFE_TOUCHDOWN_TILT;
        if (d.position.y < restY && gentle) {
            ApplyGroundContact(d, restY);
            return {GameState::PLAYING, CrashReason::NONE};
        }
    }

    // --- Normal crash checks (only when away from both pads) ---
    for (int i = 0; i < ROTOR_COUNT; i++)
        if (d.GetRotorWorldPos((RotorID)i).y < 0.0f)
            return {GameState::DEAD, CrashReason::ROTOR_STRIKE};
    if (d.position.y < 0.0f)            return {GameState::DEAD, CrashReason::GROUND_IMPACT};
    if (d.position.y > MAX_ALTITUDE)    return {GameState::DEAD, CrashReason::TOO_HIGH};
    if (fabsf(d.position.x) > BOUNDS_HALF_WIDTH ||
        d.position.z > startPad.position.z + BOUNDS_BEHIND ||
        d.position.z < pad.position.z - BOUNDS_PAST_PAD)
        return {GameState::DEAD, CrashReason::OUT_OF_BOUNDS};

    return {GameState::PLAYING, CrashReason::NONE};
}

void Game::CheckGameStatus() {
    ContactResult result = ResolveContact(drone);

    if (result.outcome == GameState::WIN) {
        state = GameState::WIN;

        // Perfect win - only if drone lands gently and level
        constexpr float perfectLandSpeed = 2.0f;
        constexpr float perfectLandAngle = 20.0f;
        float speed = Vector3Length(drone.velocity);
        perfectLanding = speed < perfectLandSpeed && drone.GetTiltAngle() < perfectLandAngle;
        BestScore() = fmaxf(BestScore(), perfectLanding ? 100.0f : 99.999f);

        float& bestTime = bestTimes[(int)difficulty];
        newBestTime = bestTime <= 0.0f || runTime < bestTime;
        if (newBestTime) bestTime = runTime;

        // Fastest landing this session becomes the ghost for this difficulty
        GhostRun& g = ghostRuns[(int)difficulty];
        if (!g.valid || runTime < g.time)
            g = {true, runTime, runStart, runInputs, CapturePhysicsSettings()};
    } else if (result.outcome == GameState::DEAD) {
        crashReason = result.reason;
        state       = GameState::DEAD;
        deadTimer   = 1.5f;
    }
}

// ---------------------------------------------------------------------------
//  Run recording and ghost replay
// ---------------------------------------------------------------------------

std::vector<float> Game::CapturePhysicsSettings() {
    std::vector<float> values;
    for (const SettingsEntry& e : kSettingsEntries) values.push_back(*e.val);
    return values;
}

// First rotor input of a run: start the timer and recording, and launch the
// ghost from the same starting state if one was recorded with these settings.
void Game::StartRun() {
    runTimerStarted = true;
    runStart = drone;
    runInputs.clear();

    const GhostRun& g = ghostRuns[(int)difficulty];
    ghostActive = g.valid && g.settings == CapturePhysicsSettings();
    if (ghostActive) {
        ghost = g.start;
        ghostStep = 0;
    }
}

// One fixed physics step of a run: player drone, recording/timer, ghost, status
void Game::StepPhysics(const bool rotorInputs[ROTOR_COUNT]) {
    uint8_t mask = 0;
    for (int i = 0; i < ROTOR_COUNT; i++) {
        drone.SetRotorInput((RotorID)i, rotorInputs[i], PHYSICS_DT);
        if (rotorInputs[i]) mask |= (uint8_t)(1 << i);
    }
    if (runTimerStarted) {
        runInputs.push_back(mask);
        runTime += PHYSICS_DT;
    }
    drone.Update(PHYSICS_DT);
    runProgress = fmaxf(runProgress, ProgressAt(drone.position));

    if (ghostActive) StepGhost();
    CheckGameStatus();
}

// Replays the ghost's recorded inputs one step at a time. Physics is
// deterministic, so it retraces the recorded run exactly; once the recording
// ends (its landing) or it stops flying, it stays where it is.
void Game::StepGhost() {
    const GhostRun& g = ghostRuns[(int)difficulty];
    if (ghostStep >= g.inputs.size()) return;

    uint8_t mask = g.inputs[ghostStep++];
    for (int i = 0; i < ROTOR_COUNT; i++)
        ghost.SetRotorInput((RotorID)i, (mask >> i) & 1, PHYSICS_DT);
    ghost.Update(PHYSICS_DT);

    if (ResolveContact(ghost).outcome != GameState::PLAYING)
        ghostStep = g.inputs.size();
}

// ---------------------------------------------------------------------------
//  Draw
// ---------------------------------------------------------------------------

void Game::Draw() const {
    BeginDrawing();
    ClearBackground(SKYBLUE);

    BeginMode3D(camera);
    DrawWorld();
    if (state == GameState::PLAYING) DrawDepthCues();
    if (state != GameState::MENU) drone.Draw();
    if (state != GameState::MENU && ghostActive) ghost.Draw(GHOST_ALPHA);
    EndMode3D();

    DrawOverlay();
    EndDrawing();
}

void Game::DrawOverlay() const {
    switch (state) {
        case GameState::MENU:     DrawMenu();     break;
        case GameState::PLAYING:  DrawPlaying();  break;
        case GameState::DEAD:     DrawDead();     break;
        case GameState::WIN:      DrawWin();      break;
        case GameState::SETTINGS:      DrawSettings();      break;
        case GameState::INSTRUCTIONS:  DrawInstructions();  break;
    }
}

void Game::DrawWorld() const {
    // Ground plane slightly below grid to avoid Z-fighting
    DrawPlane({0, -0.01f, 0}, {100.0f, 100.0f}, DARKGREEN);
    DrawGrid(60, 1.0f);

    // Landing pad
    float padX = pad.position.x, padZ = pad.position.z, half = pad.halfSize;
    DrawCube({padX, 0.05f, padZ}, half * 2, 0.10f, half * 2, ORANGE);
    DrawCubeWires({padX, 0.05f, padZ}, half * 2, 0.10f, half * 2, DARKBROWN);

    // "H" marker on pad top using lines
    float top = 0.12f;
    float hw  = half * 0.55f;  // horizontal bar half-width
    float hh  = half * 0.70f;  // vertical bar half-height
    DrawLine3D({padX - hw, top, padZ - hh}, {padX - hw, top, padZ + hh}, WHITE);
    DrawLine3D({padX + hw, top, padZ - hh}, {padX + hw, top, padZ + hh}, WHITE);
    DrawLine3D({padX - hw, top, padZ      }, {padX + hw, top, padZ      }, WHITE);

    // Starting pad (lime green, same size as destination pad)
    float sX = startPad.position.x, sZ = startPad.position.z, sH = startPad.halfSize;
    DrawCube({sX, 0.05f, sZ}, sH * 2, 0.10f, sH * 2, LIME);
    DrawCubeWires({sX, 0.05f, sZ}, sH * 2, 0.10f, sH * 2, DARKGREEN);
    // Arrow pointing toward destination pad
    DrawLine3D({sX,       0.12f, sZ - sH * 0.3f}, {sX,       0.12f, sZ - sH * 0.7f}, WHITE);
    DrawLine3D({sX - 0.2f, 0.12f, sZ - sH * 0.4f}, {sX, 0.12f, sZ - sH * 0.7f}, WHITE);
    DrawLine3D({sX + 0.2f, 0.12f, sZ - sH * 0.4f}, {sX, 0.12f, sZ - sH * 0.7f}, WHITE);

    // Reference trees along path at x=±4, every 5m forward
    for (int i = 1; i <= 5; i++) {
        float z = -i * 4.5f;
        const float treeXs[] = {-4.0f, 4.0f};
        for (float x : treeXs) {
            DrawCylinder({x, 0, z}, 0.15f, 0.0f, 2.0f, 6, BROWN);
            DrawSphere({x, 2.2f, z}, 0.65f, DARKGREEN);
        }
    }
}

// Height/position cue for the chase camera: a shadow on the surface directly
// below the drone that shrinks and fades with altitude.
void Game::DrawDepthCues() const {
    auto over = [&](const LandingPad& p) {
        return fabsf(drone.position.x - p.position.x) < p.halfSize &&
               fabsf(drone.position.z - p.position.z) < p.halfSize;
    };
    float surfaceY = (over(pad) || over(startPad)) ? PAD_TOP_Y : 0.0f;
    float height = fmaxf(0.0f, drone.position.y - surfaceY);

    constexpr float fadeHeight = 12.0f;  // shadow is smallest/faintest at this height
    float k = fminf(height / fadeHeight, 1.0f);
    float radius = 0.35f - 0.20f * k;
    float alpha  = 0.55f - 0.35f * k;

    Vector3 ground = {drone.position.x, surfaceY + 0.012f, drone.position.z};
    DrawCylinder(ground, radius, radius, 0.004f, 24, Fade(BLACK, alpha));
}

// ---------------------------------------------------------------------------
//  State overlays (2D HUD, all drawn after EndMode3D)
// ---------------------------------------------------------------------------

static void DrawCenteredText(const char* text, int y, int fontSize, Color color) {
    int w = MeasureText(text, fontSize);
    DrawText(text, (GetScreenWidth() - w) / 2, y, fontSize, color);
}

// Shared chrome for boxed modal dialogs (Settings, Instructions): dimmed
// backdrop, filled/bordered panel, and title, so both screens look alike.
static void DrawDialogPanel(int bx, int by, int bw, int bh, int screenW, int screenH, const char* title) {
    DrawRectangle(0, 0, screenW, screenH, {0, 0, 0, 180});
    DrawRectangle(bx, by, bw, bh, {10, 10, 10, 220});
    DrawRectangleLinesEx({(float)bx, (float)by, (float)bw, (float)bh}, 2.0f, {70, 70, 70, 255});
    DrawCenteredText(title, by + DIALOG_TITLE_TOP_PAD, DIALOG_TITLE_SIZE, WHITE);
}

// Shared footer for boxed modal dialogs: hint text + BACK button, both
// positioned in the fixed-height block reserved by DIALOG_FOOTER_BLOCK_H.
static void DrawDialogFooter(int bx, int by, int bw, int bh, const char* hint) {
    DrawCenteredText(hint, GetDialogFooterY(by, bh), 16, {180, 180, 180, 255});
    Rectangle backRect = GetDialogBackButtonRect(bx, by, bw, bh);
    bool backHovered = CheckCollisionPointRec(GetMousePosition(), backRect);
    DrawMenuButton("BACK", backRect, backHovered);
}

void Game::DrawMenu() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    // Subtle darkening so world is still visible as backdrop
    DrawRectangle(0, 0, sw, sh, {0, 0, 0, 110});

    // Title with drop shadow
    const char* title = "QWAS";
    const int titleSize = 104;
    int tw = MeasureText(title, titleSize);
    int tx = (sw - tw) / 2;
    int ty = (int)(sh * 0.12f);
    DrawText(title, tx + 4, ty + 4, titleSize, {0, 0, 0, 150});
    DrawText(title, tx, ty, titleSize, WHITE);

    // Subtitle
    const char* sub = "Quadrotor With Awkward Strokes";
    const int subSize = 32;
    int stw = MeasureText(sub, subSize);
    DrawText(sub, (sw - stw) / 2, ty + titleSize + 12, subSize, {180, 180, 180, 255});

    // Menu buttons
    const char* labels[MENU_COUNT] = {"START", nullptr, "SETTINGS", "INSTRUCTIONS"};
    for (int i = 0; i < MENU_COUNT; i++) {
        if (i == MENU_MODE)
            DrawDifficultySwitch(GetMenuButtonRect(i, sw, sh), i == menuSelectedIdx, modeSwitchT);
        else
            DrawMenuButton(labels[i], GetMenuButtonRect(i, sw, sh), i == menuSelectedIdx);
    }

    DrawCenteredText("Arrow keys to navigate  |  Enter or click to select",
                     sh - 28, 14, {100, 100, 100, 255});
}

void Game::DrawPlaying() const {
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    if (touchGuideAlpha > 0.01f) {
        DrawRotorTouchZone(ROTOR_FRONT_LEFT, drone, w, h, "Q", touchGuideAlpha);
        DrawRotorTouchZone(ROTOR_FRONT_RIGHT, drone, w, h, "W", touchGuideAlpha);
        DrawRotorTouchZone(ROTOR_REAR_LEFT, drone, w, h, "A", touchGuideAlpha);
        DrawRotorTouchZone(ROTOR_REAR_RIGHT, drone, w, h, "S", touchGuideAlpha);
    }

    drone.DrawHUDBars(w, h);

    // Stats (top-center, clear of the corner touch zones)
    int sx = (w - 210) / 2, sy = 16;
    DrawText(TextFormat("Alt:      %.1f m",  drone.GetAltitude()),              sx, sy,      20, WHITE);
    DrawText(TextFormat("Speed:  %.1f m/s",  Vector3Length(drone.velocity)),    sx, sy + 26, 20, WHITE);
    DrawText(TextFormat("Progress: %.0f%%", ProgressAt(drone.position)), sx, sy + 52, 20, YELLOW);
    DrawText(TextFormat("Time:    %.2f s", runTime), sx, sy + 78, 20, WHITE);

    // Best time once the level has been completed, otherwise best progress
    float bestTime = bestTimes[(int)difficulty];
    if (bestTime > 0)
        DrawText(TextFormat("Best:    %.2f s", bestTime), sx, sy + 104, 20, GREEN);
    else if (BestScore() > 0)
        DrawText(TextFormat("Best:   %.0f%%", BestScore()), sx, sy + 104, 20, GREEN);

    float tilt = drone.GetTiltAngle();
    Color tc   = tilt < 15 ? GREEN : (tilt < 35 ? YELLOW : RED);
    DrawText(TextFormat("Tilt:     %.0f°",  tilt), sx, sy + 130, 20, tc);

    if (difficulty == Difficulty::EASY)
        DrawText("Easy mode", sx, sy + 156, 20, WHITE);
}

void Game::DrawDead() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    drone.DrawHUDBars(sw, sh);

    if (deadTimer <= 0.0f) {
        DrawRectangle(0, 0, sw, sh, {0, 0, 0, 120});

        DrawCenteredText(GetCrashTitle(crashReason), sh / 2 - 100, 60, RED);
        DrawCenteredText(GetCrashHint(crashReason),  sh / 2 - 38,  24, LIGHTGRAY);

        DrawCenteredText(TextFormat("Progress: %.0f%%", runProgress), sh / 2, 30, WHITE);

        int btnY = sh / 2 + 90;
        DrawMenuButton("RETRY",           GetEndScreenButtonRect(0, sw, btnY), endScreenSelectedIdx == 0);
        DrawMenuButton("RETURN TO MENU",  GetEndScreenButtonRect(1, sw, btnY), endScreenSelectedIdx == 1);
        DrawCenteredText("Left/Right to navigate  |  Enter or click to select",
                         btnY + 66, 14, {100, 100, 100, 255});
    }
}

void Game::DrawWin() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, {0, 60, 0, 150});

    // Expanding celebration rings in motor colors, staggered 0.4s apart
    const Color ringColors[4] = {RED, BLUE, GREEN, YELLOW};
    const float period = 1.6f;
    int cx = sw / 2, cy = sh / 2;
    for (int w = 0; w < 4; w++) {
        float t      = fmod(winTimer + w * (period / 4.0f), period);
        float radius = t * 420.0f;
        float alpha  = fmaxf(0.0f, 1.0f - t / period);
        Color c      = Fade(ringColors[w], alpha * 0.85f);
        DrawCircleLines(cx, cy, radius,        c);
        DrawCircleLines(cx, cy, radius - 5.0f, Fade(ringColors[w], alpha * 0.4f));
    }

    // Pulsing title
    int titleSize = (int)(80 * (1.0f + 0.07f * sinf(winTimer * 5.0f)));
    if (!perfectLanding)
    {
        DrawCenteredText("\"LANDED\"",              sh / 2 - 100, titleSize, GREEN);
        DrawCenteredText("Progress:  99.999%",  sh / 2 - 10,  30, WHITE);
        DrawCenteredText("You made it to the end.",    sh / 2 + 30,  24, GOLD);
    }
    else
    {
        DrawCenteredText("LANDED",              sh / 2 - 100, titleSize, GREEN);
        DrawCenteredText("Progress:  100%",  sh / 2 - 10,  30, WHITE);
        DrawCenteredText("Perfect landing!",    sh / 2 + 30,  24, GOLD);
    }
    const char* timeLine = newBestTime
        ? TextFormat("Time: %.2f s  -  New best!", runTime)
        : TextFormat("Time: %.2f s  (best %.2f s)", runTime, bestTimes[(int)difficulty]);
    if (difficulty == Difficulty::EASY) timeLine = TextFormat("%s  -  Easy mode", timeLine);
    DrawCenteredText(timeLine, sh / 2 + 58, 20, newBestTime ? GOLD : LIGHTGRAY);

    int btnY = sh / 2 + 90;
    DrawMenuButton("FLY AGAIN",       GetEndScreenButtonRect(0, sw, btnY), endScreenSelectedIdx == 0);
    DrawMenuButton("RETURN TO MENU",  GetEndScreenButtonRect(1, sw, btnY), endScreenSelectedIdx == 1);
    DrawCenteredText("Left/Right to navigate  |  Enter or click to select",
                     btnY + 66, 14, {100, 100, 100, 255});

    drone.DrawHUDBars(sw, sh);
}

void Game::DrawSettings() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    SettingsLayout L = GetSettingsLayout(sw, sh);
    DrawDialogPanel(L.bx, L.by, L.bw, L.bh, sw, sh, "PHYSICS SETTINGS");

    int labelX = L.bx + 20;
    int valueX = L.bx + L.bw - 120;

    for (int i = 0; i < SETTINGS_COUNT; i++) {
        int ry = L.by + 54 + i * SETTINGS_ROW_H;
        const SettingsEntry& e = kSettingsEntries[i];

        if (i == settingsSelectedIdx)
            DrawRectangle(L.bx + 2, ry, L.bw - 4, SETTINGS_ROW_H - 2, {255, 255, 255, 30});

        Color textCol = (i == settingsSelectedIdx) ? WHITE : LIGHTGRAY;

        // Label
        DrawText(e.label, labelX, ry + 8, 18, textCol);

        // Slider track
        DrawRectangle(L.sliderX, ry + 12, L.sliderW, 10, {80, 80, 80, 255});

        // Slider fill
        float t = (*e.val - e.minV) / (e.maxV - e.minV);
        int fillW = (int)(t * L.sliderW);
        Color fillCol = (i == settingsSelectedIdx) ? WHITE : GRAY;
        DrawRectangle(L.sliderX, ry + 12, fillW, 10, fillCol);

        // Slider thumb
        DrawRectangle(L.sliderX + fillW - 3, ry + 8, 6, 18,
                      (i == settingsSelectedIdx) ? YELLOW : DARKGRAY);

        // Value + unit
        DrawText(TextFormat("%.3g %s", *e.val, e.unit), valueX, ry + 8, 17, textCol);
    }

    DrawDialogFooter(L.bx, L.by, L.bw, L.bh,
                     "Drag sliders or Up/Down + Left/Right to adjust   R Reset defaults   Back or Backspace/Escape to exit");
}

void Game::DrawInstructions() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    InstructionsLayout L = GetInstructionsLayout(sw, sh);
    const int bx = L.bx, by = L.by, bw = L.bw, bh = L.bh;
    DrawDialogPanel(bx, by, bw, bh, sw, sh, "INSTRUCTIONS");

    int lx = bx + 40;
    int y  = by + 65;

    DrawText("Objective", lx, y, 20, YELLOW);
    y += 26;
    DrawText("Fly from the green start pad to the orange landing pad.", lx, y, 17, WHITE);
    y += 20;
    DrawText("Land gently (low speed, nearly level) to win.", lx, y, 17, WHITE);
    y += 34;

    DrawText("Keyboard Controls", lx, y, 20, YELLOW);
    y += 26;
    struct MotorKey { const char* key; Color col; const char* desc; };
    const MotorKey motors[4] = {
        {"Q", RED,    "Front-Left rotor"},
        {"W", BLUE,   "Front-Right rotor"},
        {"A", GREEN,  "Rear-Left rotor"},
        {"S", YELLOW, "Rear-Right rotor"},
    };
    for (auto& m : motors) {
        DrawRectangle(lx, y + 2, 14, 14, m.col);
        DrawText(m.key,  lx + 18, y, 17, WHITE);
        DrawText(m.desc, lx + 38, y, 17, LIGHTGRAY);
        y += 22;
    }
    y += 4;
    DrawText("R  -  Restart", lx, y, 17, WHITE);
    y += 34;

    DrawText("Touch / Click Controls", lx, y, 20, YELLOW);
    y += 26;
    DrawText("Hold a screen corner to spin the matching rotor:", lx, y, 17, WHITE);
    y += 20;
    DrawText("Top-Left = Q          Top-Right = W", lx, y, 17, WHITE);
    y += 20;
    DrawText("Bottom-Left = A       Bottom-Right = S", lx, y, 17, WHITE);
    y += 34;

    DrawText("Flight Tips", lx, y, 20, YELLOW);
    y += 26;
    DrawText("Longer hold = more thrust.  Release = thrust drops quickly.", lx, y, 17, WHITE);
    y += 20;
    DrawText("The drone tilts toward whichever rotors are spinning harder.", lx, y, 17, WHITE);
    y += 20;
    DrawText("Use Settings to adjust physics constants.", lx, y, 17, WHITE);
    y += 34;

    DrawText("Easy Mode  (switch on the menu)", lx, y, 20, YELLOW);
    y += 26;
    DrawText("The drone levels itself. Releasing all rotors drops it fast.", lx, y, 17, WHITE);
    y += 20;
    DrawText("Slow, level touchdowns on the grass are safe.", lx, y, 17, WHITE);

    DrawDialogFooter(bx, by, bw, bh, "Back or Backspace/Escape to exit");
}
