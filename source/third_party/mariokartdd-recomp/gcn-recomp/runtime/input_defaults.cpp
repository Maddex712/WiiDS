// See input_defaults.h.
#include "input_defaults.h"
#include <map>
#include <string>

// Function-local: the game's static initialisers may run before this file's.
static std::map<std::string, float>& defaults() {
    static std::map<std::string, float> d;
    return d;
}

void input_set_default(const char* key, float value) { defaults()[key] = value; }

float input_default(const char* key, float fallback) {
    auto it = defaults().find(key);
    return it == defaults().end() ? fallback : it->second;
}
