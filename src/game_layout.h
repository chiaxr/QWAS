#pragma once
// Screen layout and settings table shared by game logic (hit testing) and
// game UI (drawing), so button positions are defined in one place.
#include "game.h"

enum MenuItem { MENU_START, MENU_MODE, MENU_SETTINGS, MENU_INSTRUCTIONS, MENU_COUNT };

inline Rectangle GetRotorTouchZone(RotorID id, int screenW, int screenH) {
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

inline int GetEndScreenButtonsY(int screenH) {
    return screenH / 2 + 90;
}

inline Rectangle GetEndScreenButtonRect(int idx, int screenW, int btnTopY) {
    const int btnW = 280, btnH = 52, gap = 24;
    int bx = (screenW - (btnW * 2 + gap)) / 2 + idx * (btnW + gap);
    return {(float)bx, (float)btnTopY, (float)btnW, (float)btnH};
}

inline Rectangle GetMenuButtonRect(int idx, int screenW, int screenH) {
    const int btnW = 400, btnH = 52;
    int btnX = (screenW - btnW) / 2;
    int btnY = (int)(screenH * 0.43f) + idx * 66;
    return {(float)btnX, (float)btnY, (float)btnW, (float)btnH};
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

inline const SettingsEntry kSettingsEntries[SETTINGS_COUNT] = {
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

inline Rectangle GetDialogBackButtonRect(int bx, int by, int bw, int bh) {
    int bx2 = bx + (bw - DIALOG_BACK_BTN_W) / 2;
    int by2 = by + bh - DIALOG_BACK_BTN_BOTTOM_MARGIN - DIALOG_BACK_BTN_H;
    return { (float)bx2, (float)by2, (float)DIALOG_BACK_BTN_W, (float)DIALOG_BACK_BTN_H };
}

inline int GetDialogFooterY(int by, int bh) {
    return by + bh - DIALOG_FOOTER_BLOCK_H;
}

struct SettingsLayout { int bx, by, bw, bh, sliderX, sliderW; };

inline SettingsLayout GetSettingsLayout(int screenW, int screenH) {
    int bw = 820, bh = 60 + SETTINGS_COUNT * SETTINGS_ROW_H + DIALOG_FOOTER_BLOCK_H + 20;
    int bx = (screenW - bw) / 2;
    int by = (screenH - bh) / 2;
    return { bx, by, bw, bh, bx + 290, 300 };
}

// Top of settings row idx (rows start below the panel title)
inline int GetSettingsRowY(const SettingsLayout& L, int idx) {
    return L.by + 54 + idx * SETTINGS_ROW_H;
}

inline Rectangle GetSettingsRowRect(const SettingsLayout& L, int idx) {
    int ry = GetSettingsRowY(L, idx);
    return { (float)(L.bx + 2), (float)ry, (float)(L.bw - 4), (float)(SETTINGS_ROW_H - 2) };
}

inline Rectangle GetSettingsSliderHitRect(const SettingsLayout& L, int idx) {
    int ry = GetSettingsRowY(L, idx);
    return { (float)L.sliderX, (float)ry, (float)L.sliderW, (float)SETTINGS_ROW_H };
}

struct InstructionsLayout { int bx, by, bw, bh; };

inline InstructionsLayout GetInstructionsLayout(int screenW, int screenH) {
    const int bw = 760, bh = 680;
    int bx = (screenW - bw) / 2;
    int by = (screenH - bh) / 2;
    return { bx, by, bw, bh };
}
