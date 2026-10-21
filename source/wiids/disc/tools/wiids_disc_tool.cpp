// Host test for wiids_disc.
//   wiids_disc_tool id  <file> [more parts...]
//   wiids_disc_tool wii <destDir> <file> [more parts...]
//   wiids_disc_tool gc  <dest.iso> <file> [more parts...]
#include "wiids_disc.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <thread>

using namespace wiids::disc;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: id <file...> | wii <dir> <file...> | gc <iso> <file...>\n");
        return 2;
    }
    const std::string mode = argv[1];
    const int first = mode == "id" ? 2 : 3;
    std::vector<std::unique_ptr<Part>> parts;
    try {
        for (int i = first; i < argc; ++i) parts.push_back(OpenPath(argv[i]));
        if (mode == "id") {
            const Info info = Identify(parts);
            std::printf("format=%s platform=%s id=%s disc=%d rev=%d region=%s size=%llu title=%s\n",
                        FormatName(info.format), info.platform == Platform::Wii ? "Wii" : "GameCube",
                        info.gameId.c_str(), info.discNumber, info.revision, info.region.c_str(),
                        static_cast<unsigned long long>(info.discSize), info.title.c_str());
            return 0;
        }
        Progress progress;
        const auto start = std::chrono::steady_clock::now();
        if (mode == "wii") ExtractWii(parts, argv[2], progress);
        else if (mode == "gc") ExtractGameCubeIso(parts, argv[2], progress);
        else { std::fprintf(stderr, "unknown mode\n"); return 2; }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("ok: %llu/%llu bytes in %.1fs\n", static_cast<unsigned long long>(progress.done.load()),
                    static_cast<unsigned long long>(progress.total.load()), s);
        return 0;
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
}
