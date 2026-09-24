#pragma once

#include "game.h"

class QwasApp {
public:
    void Init();
    void Frame();
    void Shutdown();
    void SaveProgress() const;
    void SetPaused(bool paused);

private:
    Game game = {};
    bool paused = false;
};
