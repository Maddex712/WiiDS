#pragma once
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gx {
struct AndroidControllerProfile {
    Uint16 vendor, product, crc;
    std::map<std::string,std::string> overrides;
};
inline bool controllerTarget(const std::string& name) {
    static const char* names[] = {"a","b","x","y","rightshoulder","lefttrigger","righttrigger","start",
        "leftx","lefty","rightx","righty","dpup","dpdown","dpleft","dpright"};
    for (const char* value : names) if (name == value) return true;
    return false;
}
inline bool controllerExpression(const std::string& value, int axes = 64, int buttons = 36) {
    if (value == "none") return true;
    size_t at = 0;
    if (at < value.size() && (value[at] == '+' || value[at] == '-')) ++at;
    if (at >= value.size()) return false;
    const char kind = value[at++];
    if (kind != 'a' && kind != 'b') return false;
    if (kind == 'b' && at != 1) return false;
    const size_t start = at; int number = 0;
    while (at < value.size() && value[at] >= '0' && value[at] <= '9') {
        number = number * 10 + value[at++] - '0';
        if (number > 63 || at - start > 2) return false;
    }
    if (at == start || (kind == 'a' ? number >= axes : number >= buttons)) return false;
    if (kind == 'a' && at < value.size() && value[at] == '~') ++at;
    return at == value.size();
}
inline bool parseControllerProfile(const std::string& line, AndroidControllerProfile& profile) {
    unsigned vendor, product, crc; int end = 0;
    if (line.size() > 4096 || std::sscanf(line.c_str(), "%4x:%4x:%4x%n", &vendor, &product, &crc, &end) != 3 ||
        end < 1 || size_t(end) >= line.size() || line[end] != '\t') return false;
    AndroidControllerProfile candidate{Uint16(vendor),Uint16(product),Uint16(crc),{}};
    size_t at = size_t(end) + 1;
    while (at < line.size()) {
        size_t next = line.find(',', at); if (next == std::string::npos) next = line.size();
        const std::string entry = line.substr(at, next-at); const size_t colon = entry.find(':');
        if (colon == std::string::npos) return false;
        const std::string key = entry.substr(0,colon), value = entry.substr(colon+1);
        if (!controllerTarget(key) || !controllerExpression(value) || candidate.overrides.count(key)) return false;
        candidate.overrides[key] = value; at = next + 1;
    }
    if (candidate.overrides.empty()) return false;
    profile = std::move(candidate); return true;
}
inline std::vector<AndroidControllerProfile> readControllerProfiles(const char* path) {
    std::vector<AndroidControllerProfile> result;
    if (!path || !*path) return result;
    std::ifstream file(path); std::string line; size_t total = 0;
    while (result.size() < 64 && std::getline(file,line)) {
        total += line.size(); if (total > 128*1024) break;
        AndroidControllerProfile profile;
        if (parseControllerProfile(line,profile)) result.push_back(std::move(profile));
    }
    return result;
}
inline bool controllerProfileMatches(const AndroidControllerProfile& p, SDL_JoystickGUID guid, Uint16 vendor, Uint16 product) {
    return p.vendor == vendor && p.product == product && p.crc == Uint16(guid.data[2] | Uint16(guid.data[3]) << 8);
}
inline std::string controllerHidIgnore(const std::vector<AndroidControllerProfile>& profiles, const char* existing) {
    std::string result = existing ? existing : ""; std::set<unsigned> seen;
    // SDL treats product zero as a wildcard; never route an entire vendor
    // away from HIDAPI for an unidentified device.
    for (const auto& p : profiles) if (p.vendor && p.product && seen.insert(unsigned(p.vendor) << 16 | p.product).second) {
        char pair[32]; SDL_snprintf(pair,sizeof pair,"0x%04x/0x%04x",p.vendor,p.product);
        if (!result.empty()) result += ',';
        result += pair;
    }
    return result;
}
inline bool userControllerMapping(SDL_JoystickGUID guid, const AndroidControllerProfile& profile, int axes, int buttons,
        const char* base, std::string& result) {
    for (const auto& entry : profile.overrides) if (!controllerExpression(entry.second,axes,buttons)) return false;
    char id[33]; SDL_JoystickGetGUIDString(guid,id,sizeof id);
    result = std::string(id) + ",Configured controller,";
    // Preserve automatic bindings/metadata except the explicitly edited rows.
    const char* body = base ? std::strchr(base, ',') : nullptr;
    if (body) body = std::strchr(body+1, ',');
    if (body) {
        std::string text(body+1); size_t at = 0;
        while (at < text.size()) {
            size_t next = text.find(',',at); if (next == std::string::npos) next = text.size();
            std::string entry = text.substr(at,next-at); const size_t colon = entry.find(':');
            if (!entry.empty() && (colon == std::string::npos || !profile.overrides.count(entry.substr(0,colon)))) result += entry + ',';
            at = next + 1;
        }
    }
    for (const auto& entry : profile.overrides) if (entry.second != "none") result += entry.first + ':' + entry.second + ',';
    return true;
}
} // namespace gx
