#include "qwas_app.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

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

// The web main loop never returns, so there is no shutdown to save on. Save
// whenever the page is hidden instead (tab switch, minimise, or closing the tab).
EM_BOOL OnVisibilityChange(int, const EmscriptenVisibilityChangeEvent* event, void*) {
    if (event->hidden) app.SaveProgress();
    return EM_FALSE;
}

void Frame() {
    app.SetPaused(qwas_web_is_portrait() != 0);
    app.Frame();
}
}  // namespace

int main() {
    app.Init();
    emscripten_set_visibilitychange_callback(nullptr, false, OnVisibilityChange);
    emscripten_set_main_loop(Frame, 0, true);
    return 0;
}
