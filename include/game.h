#pragma once
#include "raylib.h"
#include "drone.h"
#include <cstdint>
#include <vector>

constexpr float CHASE_DIST    = 4.0f;
constexpr float CHASE_HEIGHT  = 2.0f;
constexpr float CAMERA_FOV    = 60.0f;
constexpr float CAMERA_SMOOTH = 3.0f;

constexpr float PHYSICS_DT    = 1.0f / 240.0f;  // fixed physics step, independent of frame rate
constexpr float GHOST_ALPHA   = 0.45f;          // opacity of the best-run ghost drone

constexpr float PAD_WORLD_Z   = -25.0f;  // landing pad distance (meters forward)
constexpr float PAD_TOP_Y    = 0.10f;   // top surface of any pad (cube: center 0.05, height 0.10)
constexpr float DRONE_REST_Y = 0.13f;   // drone center when resting on a pad (PAD_TOP_Y + body half-height 0.03)
constexpr float GROUND_REST_Y = 0.03f;  // drone center when resting level on the grass (body half-height)

// Progress while flying is capped here; 99.999% (landed) and 100% (perfect landing) come only from a WIN
constexpr float MAX_FLIGHT_PROGRESS = 99.0f;

// Flight area: leaving it crashes the run (TOO_HIGH / OUT_OF_BOUNDS)
constexpr float MAX_ALTITUDE      = 15.0f;  // m
constexpr float BOUNDS_HALF_WIDTH = 20.0f;  // max sideways distance |x|
constexpr float BOUNDS_BEHIND     = 10.0f;  // max distance behind the start pad (+z)
constexpr float BOUNDS_PAST_PAD   = 10.0f;  // max distance past the landing pad (-z)

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

// A recorded landing that can be replayed exactly (physics is deterministic)
struct GhostRun {
    bool                 valid = false;
    float                time = 0;        // run time of the recorded landing
    Drone                start = {};      // drone state at the run's first input
    std::vector<uint8_t> inputs;          // rotor key bitmask (bit i = RotorID i) per physics step
    std::vector<float>   settings;        // physics settings it was recorded with
};

struct Game {
    GameState  state;
    Difficulty difficulty;
    CrashReason crashReason;
    Drone      drone;
    LandingPad startPad;  // spawn pad; ground contact here is never a crash
    LandingPad pad;       // destination pad
    Camera3D   camera;
    float      physicsAccumulator;  // frame time not yet simulated (< PHYSICS_DT)
    float      deadTimer;        // counts down; press R only when <= 0
    float      winTimer;         // counts up from 0 on WIN entry (drives celebration)
    float      bestScores[DIFFICULTY_COUNT];  // best progress %, per difficulty (saved)
    float      bestTimes[DIFFICULTY_COUNT];   // fastest landing in seconds, 0 = none yet (saved)
    bool       perfectLanding;   // last WIN was slow and level
    float      runProgress;      // closest approach to the pad this run, as progress %
    float      runTime;          // seconds of flight this run (starts on first rotor input)
    bool       runTimerStarted;
    bool       newBestTime;      // last WIN set a new best time
    Drone      runStart;         // drone state when this run's timer started
    std::vector<uint8_t> runInputs;          // this run's rotor inputs per physics step
    GhostRun   ghostRuns[DIFFICULTY_COUNT];  // fastest landing this session, per difficulty
    Drone      ghost;            // replay of ghostRuns[difficulty]
    size_t     ghostStep;
    bool       ghostActive;      // ghost is flying alongside this run
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
    float ProgressAt(Vector3 position) const;
    struct ContactResult { GameState outcome; CrashReason reason; };  // outcome PLAYING = still flying
    ContactResult ResolveContact(Drone& d) const;
    void CheckGameStatus();
    void StartRun();
    void StepPhysics(const bool rotorInputs[ROTOR_COUNT]);
    void StepGhost();
    static std::vector<float> CapturePhysicsSettings();
    void DrawWorld() const;
    void DrawDepthCues() const;
    void DrawOverlay() const;
    void UpdateTap();
    bool TappedIn(Rectangle rect) const;
    void LoadProgress();
    void SaveProgress() const;
    float& BestScore()       { return bestScores[(int)difficulty]; }
    float  BestScore() const { return bestScores[(int)difficulty]; }
};
