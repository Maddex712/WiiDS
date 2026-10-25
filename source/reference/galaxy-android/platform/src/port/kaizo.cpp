#include "port/kaizo.h"
#include "port/port.h"
#include "Game/System/GameEventFlag.hpp"
#include "Game/System/GalaxyCometScheduler.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
bool enabled = false;
char tableBytes[32768];
struct FlagChange { int index; const char* originalName; GameEventFlag flag; } changes[24];
bool flagsApplied = false;
GalaxyCometTimePaper comets[4][64];
int cometCounts[4], changeCount;
const char* kHeader = "KMGR21|b0b43ee39e3b2c2b7a3dcc4bc33d77718cb97da4b95f490146ec8a80ceddd548";
bool endsWith(const char* text, const char* suffix) {
    if (!text) return false;
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}
const char* optional(char* text) { return strcmp(text, "-") == 0 ? nullptr : text; }
int number(const char* text, int limit) {
    char* end; long value = strtol(text, &end, 10);
    if (!*text || *end || value < 0 || value > limit) port_fatal("Invalid Kaizo native table number");
    return (int)value;
}
}

bool port_kaizo_enabled() { return enabled; }

void port_kaizo_configure(const char* internalPath, const char* externalPath) {
    enabled = false; changeCount = 0;
    memset(comets, 0, sizeof(comets)); memset(cometCounts, 0, sizeof(cometCounts));
    if (!endsWith(internalPath, "/galaxy-kaizo")) return;
    if (!endsWith(externalPath, "/galaxy-kaizo")) port_fatal("Kaizo profile paths disagree");
    char path[2048];
    if (snprintf(path, sizeof(path), "%s/GalaxyData/sys/KaizoTables.txt", externalPath) >= (int)sizeof(path))
        port_fatal("Kaizo profile path is too long");
    FILE* in = fopen(path, "rb");
    if (!in) port_fatal("Kaizo data is not ready; finish its setup in Your games");
    size_t size = fread(tableBytes, 1, sizeof(tableBytes) - 1, in);
    bool tooLarge = fgetc(in) != EOF; fclose(in); tableBytes[size] = 0;
    if (tooLarge || !size) port_fatal("Invalid Kaizo native tables");
    char* lineState = nullptr;
    char* line = strtok_r(tableBytes, "\n", &lineState);
    if (!line || strcmp(line, kHeader)) port_fatal("Kaizo patch version does not match this engine");
    while ((line = strtok_r(nullptr, "\n", &lineState))) {
        char* fields[11]; int count = 0; char* state = nullptr;
        for (char* f = strtok_r(line, "|", &state); f && count < 11; f = strtok_r(nullptr, "|", &state)) fields[count++] = f;
        if (count == 4 && !strcmp(fields[0], "C")) {
            int dome = number(fields[1], 4) - 1;
            if (dome < 0 || cometCounts[dome] >= 63) port_fatal("Invalid Kaizo comet table");
            GalaxyCometTimePaper& row = comets[dome][cometCounts[dome]++];
            row.mGalaxyName = fields[2]; row.mCategory = number(fields[3], 1);
        } else if (count == 11 && !strcmp(fields[0], "F") && changeCount < 24) {
            FlagChange& row = changes[changeCount++];
            row.index = number(fields[1], 4095);
            row.originalName = fields[2];
            row.flag = {fields[3], (u8)number(fields[4], 11), (u8)number(fields[5], 1),
                (u8)number(fields[6], 255), (u8)number(fields[7], 255),
                (u32)number(fields[8], 0x7fffffff), optional(fields[9]), optional(fields[10])};
        } else port_fatal("Invalid Kaizo native table row");
    }
    if (changeCount != 24) port_fatal("Incomplete Kaizo event table");
    for (int i = 0; i < 4; ++i) if (!cometCounts[i]) port_fatal("Incomplete Kaizo comet table");
    enabled = true;
    port_log("Kaizo Rebalanced 2.1: isolated profile; 24 event rows and four comet tables loaded");
}

void port_kaizo_apply_flags(GameEventFlag* flags, size_t count) {
    if (!enabled || flagsApplied) return;
    for (int i = 0; i < changeCount; ++i) {
        const FlagChange& row = changes[i];
        if ((size_t)row.index >= count || !flags[row.index].mName || strcmp(flags[row.index].mName, row.originalName))
            port_fatal("Kaizo event flag layout differs from the supported engine");
        flags[row.index] = row.flag;
    }
    flagsApplied = true;
}

const GalaxyCometTimePaper* port_kaizo_comet_table(int dome, const GalaxyCometTimePaper* original) {
    return enabled && dome >= 1 && dome <= 4 ? comets[dome - 1] : original;
}
