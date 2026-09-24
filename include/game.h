#pragma once
#include "raylib.h"
#include "drone.h"

constexpr float CHASE_DIST    = 4.0f;
constexpr float CHASE_HEIGHT  = 2.0f;
constexpr float CAMERA_FOV    = 60.0f;
constexpr float CAMERA_SMOOTH = 3.0f;

constexpr float PAD_WORLD_Z   = -25.0f;  // landing pad distance (meters forward)
constexpr float PAD_TOP_Y    = 0.10f;   // top surface of any pad (cube: center 0.05, height 0.10)
constexpr float DRONE_REST_Y = 0.13f;   // drone center when resting on a pad (PAD_TOP_Y + body half-height 0.03)
constexpr float GROUND_REST_Y = 0.03f;  // drone center when resting level on the grass (body half-height)

// Easy mode: touching the grass below both limits is a landing, not a crash
constexpr float EASY_SAFE_TOUCHDOWN_SPEED = 3.0f;   // m/s
constexpr float EASY_SAFE_TOUCHDOWN_TILT  = 20.0f;  // degrees (keeps rotors clear of the grass at default arm length)

enum class GameState { MENU, SETTINGS, INSTRUCTIONS, PLAYING, DEAD, WIN };
enum class Difficulty { HARD, EASY };
constexpr int DIFFICULTY_COUNT = 2;
enum class CrashReason { NONE, ROTOR_STRIKE, GROUND_IMPACT, TOO_HIGH, OUT_OF_BOUNDS };

struct LandingPad {
    Vector3 position;  // center
    float   halfSize;  // half-width of square pad
};

struct Game {
    int        screenWidth;
    int        screenHeight;
    GameState  state;
    Difficulty difficulty;
    CrashReason crashReason;
    Drone      drone;
    LandingPad startPad;  // spawn pad; ground contact here is never a crash
    LandingPad pad;       // destination pad
    Camera3D   camera;
    float      deadTimer;        // counts down; press R only when <= 0
    float      winTimer;         // counts up from 0 on WIN entry (drives celebration)
    float      bestScores[DIFFICULTY_COUNT];  // best progress % this session, per difficulty
    bool       perfectLanding;   // last WIN was slow and level
    int        settingsSelectedIdx;
    bool       draggingSlider;
    int        draggedSettingsIdx;
    int        menuSelectedIdx;
    int        endScreenSelectedIdx;  // 0=Retry, 1=Menu on DEAD/WIN screens
    float      touchGuideAlpha;  // fades after the first motor input
    bool       touchGuideDismissed;
    bool       tapWasDown;
    bool       tapCandidate;
    bool       tapCompleted;     // a tap (mouse click or touch) was released this frame
    GameState  tapStartState;    // screen the current tap began on
    Vector2    tapStart;
    float      modeSwitchT;      // difficulty switch knob position, 0 = EASY, 1 = HARD (animated)

    void Init();
    void Reset();
    void Update(float dt);
    void Draw() const;

private:
    void UpdateMenu();
    void UpdateSettings();
    void UpdateInstructions();
    void UpdatePlaying(float dt);
    void UpdateDead(float dt);
    void UpdateWin(float dt);

    void DrawMenu() const;
    void DrawSettings() const;
    void DrawInstructions() const;
    void DrawPlaying() const;
    void DrawDead() const;
    void DrawWin() const;
    void ActivateMenuButton(int idx);

    void UpdateCamera(float dt);
    void CheckGameStatus();
    void DrawWorld() const;
    void DrawOverlay() const;
    void UpdateTap();
    bool TappedIn(Rectangle rect) const;
    float& BestScore()       { return bestScores[(int)difficulty]; }
    float  BestScore() const { return bestScores[(int)difficulty]; }
};
