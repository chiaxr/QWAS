#pragma once
#include <string>

// Persistent storage for a small text blob (best scores, settings).
// Web: browser localStorage. Desktop: a file next to the executable.
// Returns an empty string when nothing has been saved or storage is unavailable.
std::string LoadSaveData();
void StoreSaveData(const std::string& text);
