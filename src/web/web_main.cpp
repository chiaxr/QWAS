#include "qwas_app.h"

#include <emscripten/emscripten.h>

namespace {
QwasApp app;

EM_JS(int, qwas_web_is_portrait, (), {
    if (typeof Module !== 'undefined' && typeof Module.__qwasIsPortrait === 'function') {
        return Module.__qwasIsPortrait() ? 1 : 0;
    }

    if (typeof window !== 'undefined') {
        return window.innerHeight > window.innerWidth ? 1 : 0;
    }

    return 0;
});

void Frame() {
    app.SetPaused(qwas_web_is_portrait() != 0);
    app.Frame();
}
}  // namespace

int main() {
    app.Init();
    emscripten_set_main_loop(Frame, 0, true);
    return 0;
}
