# QWAS - Quadrotor With Awkward Strokes

![App Screenshot](doc/qwas.png)

QWAS is a small 3D drone physics game inspired by QWOP. Instead of controlling a runner's legs, you control a quadrotor's four propellers independently. Coordinating four thrust sources into stable flight is harder than it sounds.

[Try it here now!](https://chiaxr.github.io/QWAS/)

## Gameplay

Hold a motor input to ramp up that motor's thrust; release it and the thrust drops quickly.

```text
Q  W    front motors
A  S    rear motors
```

| Input | Motor | Color |
| --- | --- | --- |
| Q / top-left quadrant | Front-left | Red |
| W / top-right quadrant | Front-right | Blue |
| A / bottom-left quadrant | Rear-left | Green |
| S / bottom-right quadrant | Rear-right | Yellow |

Goal: fly from the green starting pad to the orange landing pad 25 meters ahead. Reaching the pad wins; land gently with low speed and low tilt for a perfect landing. Progress is measured by straight-line distance to the landing pad.

Crash conditions:

- Any rotor or the drone body hits the ground outside the starting or landing pad (in easy mode, slow and level touchdowns on the grass are safe).
- The drone climbs above 15 m.
- The drone leaves the flight area: more than 20 m to either side, 10 m behind the starting pad, or 10 m past the landing pad.

### Difficulty

Choose a difficulty with the EASY / HARD switch on the main menu. Easy is the default.

- **Hard:** the original physics. Released motors cut to zero thrust and nothing keeps the drone level.
- **Easy:** the drone gently levels itself, released motors idle below hover thrust, and held motors are capped at a softer maximum. Releasing every motor still drops the drone fast enough to crash from a few meters up.

### Timer, best scores and ghost

- The run timer starts on your first motor input and is shown on the HUD and win screen.
- Best progress and best landing time are tracked per difficulty.
- Your fastest landing of the session is replayed as a translucent ghost drone alongside later runs on the same difficulty. It is hidden if the physics settings have changed since it was recorded.
- Best scores, best times, the selected difficulty and the physics settings are saved between sessions: in browser local storage on the web, and in `qwas_save.txt` next to the executable on desktop.

## Controls

| Input | Action |
| --- | --- |
| Q / W / A / S <br> Touch quadrant | Hold to increase motor thrust; release to cut it |
| Up / Down | Navigate the menu, or choose the physics setting to adjust in Settings |
| Left / Right | Set Easy/Hard when the difficulty switch is selected, adjust the selected physics setting in Settings, or choose Retry/Menu on the crash/win screens |
| Enter / Space <br> Mouse click / Tap | Select the highlighted menu, settings-exit, or crash/win-screen option, or toggle the difficulty switch |
| R | Reset physics settings to defaults (in Settings), or restart (during flight or on crash/win) |
| Backspace (or Esc on web) | Back out to the main menu |
| Esc / window close | Quit the native desktop build (progress and settings are saved) |

The web version is landscape-only. In portrait orientation the canvas is hidden, the game is paused, and a rotate prompt is shown until the viewport returns to landscape.

## Native Build

Requirements: CMake 3.16+, a C++17 compiler, and Git. raylib 6.0 is fetched automatically by CMake.

The native desktop commands are unchanged:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the native executable:

```bash
./build/QWAS
```

On Windows with Visual Studio generators, the executable is typically under:

```powershell
build\Debug\QWAS.exe
```

Platform notes:

- macOS native builds link Cocoa, IOKit, and OpenGL frameworks.
- Linux native builds use the normal raylib desktop dependencies such as OpenGL and X11/Wayland development packages.
- The desktop build does not depend on Emscripten, JavaScript, or web files.

## Local Emscripten Build

Install and activate Emscripten first (CI uses version 6.0.9, pinned in `.github/workflows/pages.yml`), then configure with `emcmake`:

```bash
emcmake cmake -S . -B build-web \
  -DCMAKE_BUILD_TYPE=Release \
  -DQWAS_BUILD_NATIVE=OFF \
  -DQWAS_BUILD_WEB=ON

cmake --build build-web --target qwas_web
```

Expected outputs:

```text
build-web/dist/index.html
build-web/dist/index.js
build-web/dist/index.wasm
```

Serve locally with:

```bash
emrun build-web/dist/index.html
```

You can also serve `build-web/dist` with any static file server.

## Project Structure

```text
QWAS/
|-- CMakeLists.txt
|-- .github/workflows/pages.yml
|-- include/
|   |-- drone.h
|   |-- game.h
|   |-- qwas_app.h
|   `-- save.h
|-- src/
|   |-- drone.cpp
|   |-- game.cpp
|   |-- game_layout.h
|   |-- game_ui.cpp
|   |-- main.cpp
|   |-- qwas_app.cpp
|   |-- save.cpp
|   `-- web/
|       `-- web_main.cpp
|-- web/
|   `-- emscripten_shell.html
`-- doc/
    `-- qwas.png
```

`qwas_game` contains the shared drone, game, and application code: `drone.cpp` is the flight physics, `game.cpp` the game state, input, scoring and ghost replay, `game_ui.cpp` all drawing (sharing button layout through `game_layout.h`), and `save.cpp` the persistent storage. `QWAS` is the native launcher. `qwas_web` is the Emscripten launcher and uses `emscripten_set_main_loop()`.
