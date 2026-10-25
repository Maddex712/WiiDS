#include "port/profile.h"
#include "port/heap_routing.h"
#include "port/port.h"
#include <stdio.h>
#include <string.h>

namespace {
char internalRoot[2048] = ".";
char dataRoot[2048] = ".";
bool kaizoExchange = false;
}
extern "C" void port_profile_configure(const char* internal, const char* external) {
    if (!internal || !*internal || !external || !*external)
        port_fatal("Activity profile storage paths are missing");
    if (snprintf(internalRoot, sizeof(internalRoot), "%s", internal) >= sizeof(internalRoot) ||
        snprintf(dataRoot, sizeof(dataRoot), "%s/GalaxyData", external) >= sizeof(dataRoot))
        port_fatal("Activity profile storage paths are too long");
    const char* name = strrchr(internalRoot, '/');
    kaizoExchange = name && strcmp(name, "/galaxy-kaizo") == 0;
    port_log("Active profile paths: internal=%s data=%s", internalRoot, dataRoot);
}
extern "C" const char* port_profile_internal_root() { return internalRoot; }
extern "C" const char* port_profile_data_root() { return dataRoot; }
std::string port_profile_data_path(const char* suffix) {
    PortHostAllocScope host;
    return std::string(dataRoot) + suffix;
}
std::string port_profile_internal_path(const char* suffix) {
    PortHostAllocScope host;
    return std::string(internalRoot) + suffix;
}
std::string port_profile_exchange_path(const char* suffix) {
    PortHostAllocScope host;
    return std::string(kaizoExchange ? "/sdcard/Download/WiiDS-Kaizo" : "/sdcard/Download") + suffix;
}
