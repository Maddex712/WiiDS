#pragma once
#include <string_view>
#include <cstdio>
inline void ShowRuntimeFatalPopup(std::string_view c, std::string_view d) noexcept { std::printf("FATAL POPUP [%.*s]\n%.*s\n", (int)c.size(), c.data(), (int)d.size(), d.data()); }
inline void MarkFatalErrorReported() {}
