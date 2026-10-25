#pragma once
#ifdef __cplusplus
#include <string>
extern "C" {
#endif
// Set once from the Activity before settings, caches, NAND or rendering start.
void port_profile_configure(const char* internal, const char* external);
const char* port_profile_internal_root(void);
const char* port_profile_data_root(void);
#ifdef __cplusplus
}
std::string port_profile_data_path(const char* suffix);
std::string port_profile_internal_path(const char* suffix);
std::string port_profile_exchange_path(const char* suffix);
#endif
