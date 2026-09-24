#include "save.h"

#include "raylib.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <cstdlib>

namespace {
// localStorage can throw (private browsing, blocked site data), so failures
// behave like an empty save.
EM_JS(char*, qwas_web_load_save, (), {
    try {
        var s = localStorage.getItem('qwas_save');
        return s === null ? 0 : stringToNewUTF8(s);
    } catch (e) {
        return 0;
    }
});

EM_JS(void, qwas_web_store_save, (const char* text), {
    try {
        localStorage.setItem('qwas_save', UTF8ToString(text));
    } catch (e) {
    }
});
}  // namespace

std::string LoadSaveData() {
    char* text = qwas_web_load_save();
    if (!text) return "";
    std::string result(text);
    free(text);
    return result;
}

void StoreSaveData(const std::string& text) {
    qwas_web_store_save(text.c_str());
}

#else

namespace {
const char* SavePath() {
    return TextFormat("%sqwas_save.txt", GetApplicationDirectory());
}
}  // namespace

std::string LoadSaveData() {
    const char* path = SavePath();
    if (!FileExists(path)) return "";
    char* text = LoadFileText(path);
    if (!text) return "";
    std::string result(text);
    UnloadFileText(text);
    return result;
}

void StoreSaveData(const std::string& text) {
    SaveFileText(SavePath(), text.c_str());
}

#endif
