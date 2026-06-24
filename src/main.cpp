#include "qwas_app.h"
#include "raylib.h"

int main() {
    QwasApp app;
    app.Init();

    while (!WindowShouldClose()) {
        app.Frame();
    }

    app.Shutdown();
    return 0;
}
