#include "game.h"
#include "game_layout.h"
#include "raymath.h"
#include <cmath>

namespace {
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
// Shared look for menu buttons and the difficulty switch
void DrawMenuButtonFrame(Rectangle rect, bool selected) {
    Color bg     = selected ? Color{45, 75, 45, 230} : Color{15, 15, 15, 210};
    Color border = selected ? Color{100, 200, 100, 255} : Color{55, 55, 55, 200};
    DrawRectangleRec(rect, bg);
    DrawRectangleLinesEx(rect, 2.0f, border);
}

Color MenuButtonTextColor(bool selected) {
    return selected ? WHITE : Color{200, 200, 200, 255};
}

void DrawMenuButton(const char* label, Rectangle rect, bool selected) {
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
void DrawDifficultySwitch(Rectangle rect, bool selected, float t) {
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
}  // namespace

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
    float surfaceY = (pad.Covers(drone.position) || startPad.Covers(drone.position)) ? PAD_TOP_Y : 0.0f;
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

// Retry / Return to menu buttons and hint shared by the DEAD and WIN screens
void Game::DrawEndScreenButtons(const char* retryLabel) const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int btnY = GetEndScreenButtonsY(sh);
    DrawMenuButton(retryLabel,        GetEndScreenButtonRect(0, sw, btnY), endScreenSelectedIdx == 0);
    DrawMenuButton("RETURN TO MENU",  GetEndScreenButtonRect(1, sw, btnY), endScreenSelectedIdx == 1);
    DrawCenteredText("Left/Right to navigate  |  Enter or click to select",
                     btnY + 66, 14, {100, 100, 100, 255});
}

void Game::DrawDead() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    drone.DrawHUDBars(sw, sh);

    if (deadTimer <= 0.0f) {
        DrawRectangle(0, 0, sw, sh, {0, 0, 0, 120});

        DrawCenteredText(GetCrashTitle(crashReason), sh / 2 - 100, 60, RED);
        DrawCenteredText(GetCrashHint(crashReason),  sh / 2 - 38,  24, LIGHTGRAY);

        DrawCenteredText(TextFormat("Progress: %.0f%%", runProgress), sh / 2, 30, WHITE);

        DrawEndScreenButtons("RETRY");
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

    DrawEndScreenButtons("FLY AGAIN");

    drone.DrawHUDBars(sw, sh);
}

void Game::DrawSettings() const {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    SettingsLayout L = GetSettingsLayout(sw, sh);
    DrawDialogPanel(L.bx, L.by, L.bw, L.bh, sw, sh, "PHYSICS SETTINGS");

    int labelX = L.bx + 20;
    int valueX = L.bx + L.bw - 120;

    for (int i = 0; i < SETTINGS_COUNT; i++) {
        int ry = GetSettingsRowY(L, i);
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
    DrawText("Reach the pad to win. Land slowly and level for a perfect landing.", lx, y, 17, WHITE);
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
