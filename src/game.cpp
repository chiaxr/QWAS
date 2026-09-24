#include "game.h"
#include "game_layout.h"
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

    if (deadTimer > 0.0f) return;  // let the crash play out before offering the buttons

    UpdateEndScreen();
}

void Game::UpdateWin(float dt) {
    winTimer += dt;

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) { state = GameState::MENU; return; }

    UpdateEndScreen();
}

// Retry (0) / Return to menu (1) buttons shared by the DEAD and WIN screens
void Game::UpdateEndScreen() {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int btnY = GetEndScreenButtonsY(sh);

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
    if (startPad.Covers(d.position)) {
        // Detect contact: drone sinking into the pad surface
        if (d.position.y < DRONE_REST_Y)
            ApplyGroundContact(d, DRONE_REST_Y);
        return {GameState::PLAYING, CrashReason::NONE};  // never crash while on the starting pad
    }

    // --- Destination pad: win check (before crash checks) ---
    if (pad.Covers(d.position) && d.position.y < DRONE_REST_Y + 0.5f)
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
