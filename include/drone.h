#pragma once
#include "raylib.h"
#include "raymath.h"

// Physics constants (SI units: meters, kg, seconds) — runtime-mutable for settings page
constexpr float DRONE_MASS_DEFAULT       = 0.5f;
constexpr float GRAVITY_DEFAULT          = 9.81f;
constexpr float MAX_THRUST_DEFAULT       = 5.0f;
constexpr float THRUST_RAMP_UP_DEFAULT   = 10.0f;
constexpr float THRUST_RAMP_DOWN_DEFAULT = 10.0f;
constexpr float ARM_LENGTH_DEFAULT       = 0.25f;
constexpr float I_PITCH_DEFAULT          = 0.4f;
constexpr float I_YAW_DEFAULT            = 0.7f;
constexpr float I_ROLL_DEFAULT           = 0.4f;
constexpr float LIN_DRAG_DEFAULT         = 1.0f;
constexpr float ANG_DRAG_DEFAULT         = 0.5f;
constexpr float K_YAW_DEFAULT            = 0.02f;

inline float DRONE_MASS       = DRONE_MASS_DEFAULT;
inline float GRAVITY          = GRAVITY_DEFAULT;
inline float MAX_THRUST       = MAX_THRUST_DEFAULT;   // N per motor; hover needs ~1.23N each
inline float THRUST_RAMP_UP   = THRUST_RAMP_UP_DEFAULT;   // N/s (0 → max in 0.5s)
inline float THRUST_RAMP_DOWN = THRUST_RAMP_DOWN_DEFAULT;  // N/s (max → 0 in 0.3s)
inline float ARM_LENGTH       = ARM_LENGTH_DEFAULT;  // m (center to motor)
inline float I_PITCH          = I_PITCH_DEFAULT; // kg·m² (around body-X, right-wing axis)
inline float I_YAW            = I_YAW_DEFAULT;   // kg·m² (around body-Y, up axis)
inline float I_ROLL           = I_ROLL_DEFAULT;  // kg·m² (around body-Z, tail axis)
inline float LIN_DRAG         = LIN_DRAG_DEFAULT;   // /s
inline float ANG_DRAG         = ANG_DRAG_DEFAULT;   // /s
inline float K_YAW            = K_YAW_DEFAULT;  // reactive yaw coefficient

// Easy-mode flight assists (fixed; not on the settings page). Thrust limits are
// ratios of hover thrust (m·g/4 per rotor) so they track the mass/gravity settings.
constexpr float EASY_IDLE_THRUST_RATIO = 0.5f;  // released rotor settles here: hands-off descent ~4.9 m/s crashes from above ~2 m
constexpr float EASY_MAX_THRUST_RATIO  = 1.8f;  // held rotor tops out here (capped at MAX_THRUST)
constexpr float EASY_LEVEL_FREQ        = 0.9f;  // rad/s, natural frequency of the auto-level spring
constexpr float EASY_LEVEL_DAMPING     = 1.0f;  // damping ratio of the auto-level spring (1 = critical)

// Body frame convention:
//   body-X = right wing
//   body-Y = up (thrust direction)
//   body-Z = tail (backward; forward = -Z)
//
// Motor layout matching keyboard:
//   Q W    <- front (-Z side)
//   A S    <- rear (+Z side)

constexpr int ROTOR_COUNT = 4;
enum RotorID { ROTOR_FRONT_LEFT = 0, ROTOR_FRONT_RIGHT = 1, ROTOR_REAR_LEFT = 2, ROTOR_REAR_RIGHT = 3 };

struct Rotor {
    float   thrust;     // current thrust [GetIdleThrust(), GetMaxThrust()] N
    float   spinAngle;  // accumulated radians (animation only)
    Vector3 localPos;   // offset from drone center in body frame
    Color   color;      // Q=RED, W=BLUE, A=GREEN, S=YELLOW
};

struct Drone {
    Vector3    position;
    Vector3    velocity;
    Quaternion orientation;  // body-from-world; init = QuaternionIdentity
    Vector3    angularVel;   // body frame rad/s
    Rotor      rotors[ROTOR_COUNT];
    bool       alive;
    float      distanceTraveled;  // max forward (-Z) distance reached
    bool       assisted;          // easy mode: hover idle, softer max thrust, auto-level

    void Init(Vector3 spawnPos, bool assistedFlight);
    void SetRotorInput(RotorID id, bool keyDown, float dt);
    void Update(float dt);

    // 3D rendering — call inside BeginMode3D/EndMode3D
    void Draw() const;
    // 2D HUD thrust bars — call after EndMode3D
    void DrawHUDBars(int screenW, int screenH) const;

    float   GetIdleThrust() const;   // per-rotor thrust with the key released
    float   GetMaxThrust() const;    // per-rotor thrust with the key held
    Vector3 GetRotorWorldPos(RotorID id) const;
    Vector3 GetForwardDir() const;   // yaw-only XZ forward direction
    float   GetAltitude() const;
    float   GetTiltAngle() const;    // degrees from upright (0 = level)
};
