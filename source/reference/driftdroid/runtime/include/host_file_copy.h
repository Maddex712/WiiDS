#pragma once

// std::filesystem::copy_file, except on Nintendo Switch: newlib there has no working fchmod, so
// libstdc++'s copy_file creates the destination and then fails, leaving an empty file behind that
// later "exists" checks mistake for real data. Switch copies the bytes with plain streams.
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace HostFileCopy {

inline bool CopyFile(const std::filesystem::path& source, const std::filesystem::path& destination,
                     std::filesystem::copy_options options, std::error_code& ec) {
#if defined(__SWITCH__)
    ec.clear();
    if (std::filesystem::exists(destination, ec)) {
        if (ec) return false;
        if ((options & std::filesystem::copy_options::skip_existing) !=
            std::filesystem::copy_options::none) {
            return false;
        }
        if ((options & std::filesystem::copy_options::overwrite_existing) ==
            std::filesystem::copy_options::none) {
            ec = std::make_error_code(std::errc::file_exists);
            return false;
        }
    }
    ec.clear();
    std::FILE* in = std::fopen(source.c_str(), "rb");
    if (in == nullptr) {
        ec = std::error_code(errno, std::generic_category());
        return false;
    }
    std::FILE* out = std::fopen(destination.c_str(), "wb");
    if (out == nullptr) {
        ec = std::error_code(errno, std::generic_category());
        std::fclose(in);
        return false;
    }
    char buffer[64 * 1024];
    bool ok = true;
    for (;;) {
        const size_t read = std::fread(buffer, 1, sizeof(buffer), in);
        if (read > 0 && std::fwrite(buffer, 1, read, out) != read) {
            ok = false;
            break;
        }
        if (read < sizeof(buffer)) {
            ok = std::ferror(in) == 0;
            break;
        }
    }
    std::fclose(in);
    if (std::fclose(out) != 0) {
        ok = false;
    }
    if (!ok) {
        ec = std::make_error_code(std::errc::io_error);
        std::filesystem::remove(destination);
        return false;
    }
    return true;
#else
    return std::filesystem::copy_file(source, destination, options, ec);
#endif
}

} // namespace HostFileCopy
