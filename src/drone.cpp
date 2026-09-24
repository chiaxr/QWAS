#include "drone.h"
#include "rlgl.h"
#include <cmath>

void Drone::Init(Vector3 spawnPos, bool assistedFlight) {
    position     = spawnPos;
    velocity     = {0, 0, 0};
    orientation  = QuaternionIdentity();
    angularVel   = {0, 0, 0};
    alive        = true;
    distanceTraveled = 0;
    assisted     = assistedFlight;

    const Color colors[ROTOR_COUNT] = {RED, BLUE, GREEN, YELLOW};
    const Vector3 localPositions[ROTOR_COUNT] = {
        {-ARM_LENGTH, 0, -ARM_LENGTH},  // Q: front-left
        {+ARM_LENGTH, 0, -ARM_LENGTH},  // W: front-right
        {-ARM_LENGTH, 0, +ARM_LENGTH},  // A: rear-left
        {+ARM_LENGTH, 0, +ARM_LENGTH},  // S: rear-right
    };

    for (int i = 0; i < ROTOR_COUNT; i++) {
        rotors[i].thrust    = 0;
        rotors[i].spinAngle = 0;
        rotors[i].localPos  = localPositions[i];
        rotors[i].color     = colors[i];
    }
}

float Drone::GetMaxThrust() const {
    float hover = DRONE_MASS * GRAVITY / ROTOR_COUNT;
    if (!assisted || hover <= 0.0f) return MAX_THRUST;  // ratios are meaningless in zero-g
    return fminf(EASY_MAX_THRUST_RATIO * hover, MAX_THRUST);
}

float Drone::GetIdleThrust() const {
    if (!assisted) return 0.0f;
    float hover = DRONE_MASS * GRAVITY / ROTOR_COUNT;
    return fminf(EASY_IDLE_THRUST_RATIO * hover, GetMaxThrust());
}

void Drone::SetRotorInput(RotorID id, bool keyDown, float dt) {
    float& T = rotors[id].thrust;
    float target = keyDown ? GetMaxThrust() : GetIdleThrust();
    if (T < target)
        T = fminf(T + THRUST_RAMP_UP * dt, target);
    else
        T = fmaxf(T - THRUST_RAMP_DOWN * dt, target);
}

void Drone::Update(float dt) {
    // --- Body-frame torques ---
    // Each motor at r=(rx,0,rz) with force F=(0,T,0):
    //   τ = r × F = (-rz*T, 0, rx*T)
    float tauX = 0, tauZ = 0;
    float totalThrust = 0;

    for (int i = 0; i < ROTOR_COUNT; i++) {
        const float T  = rotors[i].thrust;
        const float rx = rotors[i].localPos.x;
        const float rz = rotors[i].localPos.z;
        tauX += -rz * T;
        tauZ +=  rx * T;
        totalThrust += T;
    }

    // Reactive yaw: Q,S spin CW; W,A spin CCW (opposite diagonals)
    // Easy mode's narrower idle..max thrust range would leave yaw ~4x weaker, so
    // scale it so a held diagonal pair yaws as fast as it does in hard mode.
    float kYaw = K_YAW;
    float thrustRange = GetMaxThrust() - GetIdleThrust();
    if (assisted && thrustRange > 0.0f) kYaw *= MAX_THRUST / thrustRange;
    float tauY = kYaw * (rotors[ROTOR_FRONT_LEFT].thrust + rotors[ROTOR_REAR_RIGHT].thrust
                        - rotors[ROTOR_FRONT_RIGHT].thrust - rotors[ROTOR_REAR_LEFT].thrust);

    // --- Easy-mode auto-level (body-frame angular acceleration) ---
    // Damped spring that rotates body-up toward world-up. Yaw is left alone.
    // Specified as acceleration rather than torque so it behaves the same for
    // any inertia setting; the player's rotor torques can still overpower it.
    Vector3 assistAcc = {0, 0, 0};
    if (assisted) {
        Vector3 up = Vector3RotateByQuaternion({0, 1, 0}, QuaternionInvert(orientation));
        // Axis = body-Y × up = (up.z, 0, -up.x); its length is sin(tilt)
        Vector3 axis = {up.z, 0.0f, -up.x};
        float sinTilt = Vector3Length(axis);
        float tilt    = acosf(fmaxf(-1.0f, fminf(1.0f, up.y)));
        if (sinTilt > 1e-4f) axis = Vector3Scale(axis, tilt / sinTilt);  // axis * angle

        const float kp = EASY_LEVEL_FREQ * EASY_LEVEL_FREQ;
        const float kd = 2.0f * EASY_LEVEL_DAMPING * EASY_LEVEL_FREQ;
        assistAcc.x = kp * axis.x - kd * angularVel.x;
        assistAcc.z = kp * axis.z - kd * angularVel.z;
    }

    // --- Angular acceleration and velocity (body frame) ---
    angularVel.x += (tauX / I_PITCH + assistAcc.x) * dt;
    angularVel.y += (tauY / I_YAW   + assistAcc.y) * dt;
    angularVel.z += (tauZ / I_ROLL  + assistAcc.z) * dt;

    float angDrag = 1.0f - ANG_DRAG * dt;
    angularVel.x *= angDrag;
    angularVel.y *= angDrag;
    angularVel.z *= angDrag;

    // --- Quaternion integration (body-frame ω, left-multiply) ---
    // dq/dt = 0.5 * q * Ω, where Ω = (ωx,ωy,ωz,0)
    Quaternion omegaQ = {angularVel.x, angularVel.y, angularVel.z, 0.0f};
    Quaternion dq = QuaternionMultiply(orientation, omegaQ);
    orientation.x += 0.5f * dq.x * dt;
    orientation.y += 0.5f * dq.y * dt;
    orientation.z += 0.5f * dq.z * dt;
    orientation.w += 0.5f * dq.w * dt;
    orientation = QuaternionNormalize(orientation);

    // --- Linear forces ---
    Vector3 bodyThrust  = {0, totalThrust, 0};
    Vector3 worldThrust = Vector3RotateByQuaternion(bodyThrust, orientation);

    Vector3 worldForce = {
        worldThrust.x,
        worldThrust.y - DRONE_MASS * GRAVITY,
        worldThrust.z
    };

    float invMass = 1.0f / DRONE_MASS;
    velocity.x += worldForce.x * invMass * dt;
    velocity.y += worldForce.y * invMass * dt;
    velocity.z += worldForce.z * invMass * dt;

    float linDrag = 1.0f - LIN_DRAG * dt;
    velocity.x *= linDrag;
    velocity.y *= linDrag;
    velocity.z *= linDrag;

    position.x += velocity.x * dt;
    position.y += velocity.y * dt;
    position.z += velocity.z * dt;

    // --- Spin animation & distance tracking ---
    for (int i = 0; i < ROTOR_COUNT; i++)
        rotors[i].spinAngle += (rotors[i].thrust / GetMaxThrust()) * 30.0f * dt;

    float fwdDist = -position.z;
    if (fwdDist > distanceTraveled)
        distanceTraveled = fwdDist;
}

Vector3 Drone::GetRotorWorldPos(RotorID id) const {
    return Vector3Add(position, Vector3RotateByQuaternion(rotors[id].localPos, orientation));
}

Vector3 Drone::GetForwardDir() const {
    Vector3 bodyFwd  = {0, 0, -1};
    Vector3 worldFwd = Vector3RotateByQuaternion(bodyFwd, orientation);
    worldFwd.y = 0;  // project to XZ plane so camera never rolls with drone
    float len = sqrtf(worldFwd.x * worldFwd.x + worldFwd.z * worldFwd.z);
    if (len > 0.001f) { worldFwd.x /= len; worldFwd.z /= len; }
    else              { worldFwd = {0, 0, -1}; }
    return worldFwd;
}

float Drone::GetAltitude() const { return position.y; }

float Drone::GetTiltAngle() const {
    Vector3 worldUp = Vector3RotateByQuaternion({0, 1, 0}, orientation);
    float dot = fmaxf(-1.0f, fminf(1.0f, worldUp.y));
    return acosf(dot) * RAD2DEG;
}

// ---------------------------------------------------------------------------
//  Rendering
// ---------------------------------------------------------------------------

void Drone::Draw() const {
    Matrix rotMat = QuaternionToMatrix(orientation);
    const float maxThrust = GetMaxThrust();

    rlPushMatrix();
    rlTranslatef(position.x, position.y, position.z);
    rlMultMatrixf(MatrixToFloat(rotMat));  // macro expands to (MatrixToFloatV(mat).v)

    // Central body
    DrawCube({0, 0, 0}, 0.15f, 0.06f, 0.15f, DARKGRAY);

    // Arms from center to each motor
    for (int i = 0; i < ROTOR_COUNT; i++)
        DrawCylinderEx({0, 0, 0}, rotors[i].localPos, 0.012f, 0.012f, 6, GRAY);

    // Motor hubs
    for (int i = 0; i < ROTOR_COUNT; i++)
        DrawSphere(rotors[i].localPos, 0.04f, rotors[i].color);

    // Rotor discs (semi-transparent, radius scales with thrust)
    BeginBlendMode(BLEND_ALPHA);
    for (int i = 0; i < ROTOR_COUNT; i++) {
        const Rotor& r   = rotors[i];
        float discR      = 0.10f + 0.10f * (r.thrust / maxThrust);
        Vector3 discBot  = {r.localPos.x, r.localPos.y - 0.005f, r.localPos.z};
        Vector3 discTop  = {r.localPos.x, r.localPos.y + 0.005f, r.localPos.z};
        DrawCylinderEx(discBot, discTop, discR, discR, 16, Fade(r.color, 0.40f));
    }
    EndBlendMode();

    // Spinner lines (rotate in rotor plane around motor's local Y axis)
    for (int i = 0; i < ROTOR_COUNT; i++) {
        const Rotor& r = rotors[i];
        float discR    = 0.10f + 0.10f * (r.thrust / maxThrust);
        float cx = r.localPos.x, cy = r.localPos.y, cz = r.localPos.z;
        float ca = cosf(r.spinAngle), sa = sinf(r.spinAngle);
        Vector3 p1 = {cx + discR * ca, cy, cz + discR * sa};
        Vector3 p2 = {cx - discR * ca, cy, cz - discR * sa};
        DrawLine3D(p1, p2, r.color);
    }

    rlPopMatrix();
}

void Drone::DrawHUDBars(int screenW, int screenH) const {
    const int barW    = 40;
    const int barH    = 100;
    const int padding = 12;
    const int totalW  = ROTOR_COUNT * barW + (ROTOR_COUNT - 1) * padding;
    int xStart = (screenW - totalW) / 2;
    int yBar   = screenH - barH - 50;

    static const char* labels[ROTOR_COUNT] = {"Q", "W", "A", "S"};

    for (int i = 0; i < ROTOR_COUNT; i++) {
        int x = xStart + i * (barW + padding);

        DrawRectangle(x, yBar, barW, barH, {30, 30, 30, 200});
        DrawRectangleLines(x, yBar, barW, barH, DARKGRAY);

        int fillH = (int)(barH * rotors[i].thrust / GetMaxThrust());
        if (fillH > 0)
            DrawRectangle(x, yBar + barH - fillH, barW, fillH, rotors[i].color);

        // Key label centered below bar
        int labelW = MeasureText(labels[i], 20);
        DrawText(labels[i], x + (barW - labelW) / 2, yBar + barH + 6, 20, rotors[i].color);
    }
}
