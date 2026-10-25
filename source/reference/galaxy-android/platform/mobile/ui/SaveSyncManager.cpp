#include "port/profile.h"
#include "SaveSyncManager.h"
#include "DolphinSaveConverter.h"

#include <stdio.h>
#include <sys/stat.h>
#include <vector>

static bool writeAtomic(const std::string& path, const uint8_t* buf, size_t sz) {
    if (!buf || sz == 0) return false;
    std::string tmp = path + ".tmp";
    FILE* out = fopen(tmp.c_str(), "wb");
    if (!out) return false;
    size_t written = fwrite(buf, 1, sz, out);
    fflush(out);
    fclose(out);
    if (written != sz) {
        remove(tmp.c_str());
        return false;
    }
    return rename(tmp.c_str(), path.c_str()) == 0;
}

namespace mobile_ui {

bool SaveSyncManager::exportSave(const std::string& savePath, std::string& outToastMsg) {
    if (savePath.empty()) return false;
    size_t lastSlash = savePath.rfind('/');
    if (lastSlash == std::string::npos) return false;
    std::string saveRoot = savePath.substr(0, lastSlash);

    const char* titles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
    const char* fileNames[] = {"GameData.bin", "save.bin"};
    FILE* in = nullptr;

    for (const char* t : titles) {
        for (const char* fn : fileNames) {
            std::string candidate = saveRoot + "/title/00010000/" + t + "/data/" + fn;
            in = fopen(candidate.c_str(), "rb");
            if (in) break;
        }
        if (in) break;
    }
    if (!in) {
        for (const char* fn : fileNames) {
            std::string candidate = saveRoot + "/" + fn;
            in = fopen(candidate.c_str(), "rb");
            if (in) break;
        }
    }
    if (!in) {
        std::string extCandidate = port_profile_data_path("/saves/GameData.bin").c_str();
        in = fopen(extCandidate.c_str(), "rb");
    }
    if (!in) {
        outToastMsg = "No save data found yet (play & save first)!";
        return false;
    }

    std::vector<uint8_t> data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        data.insert(data.end(), buf, buf + n);
    }
    fclose(in);

    mkdir("/sdcard/Download", 0770);
    mkdir(port_profile_exchange_path("").c_str(), 0770);
    std::string dstSave1 = port_profile_exchange_path("/GalaxyAndroid_save.bin").c_str();
    std::string dstSave2 = port_profile_exchange_path("/GameData.bin").c_str();
    std::string dstDolphin = port_profile_exchange_path("/GameData_Dolphin.bin").c_str();

    writeAtomic(dstSave1, data.data(), data.size());
    writeAtomic(dstSave2, data.data(), data.size());

    std::vector<uint8_t> dolphinData;
    if (DolphinSaveConverter::convertNativeToDolphin(data.data(), data.size(), dolphinData)) {
        writeAtomic(dstDolphin, dolphinData.data(), dolphinData.size());
    }

    outToastMsg = "Exported to " + port_profile_exchange_path("") + " (includes Dolphin save)!";
    return true;
}

bool SaveSyncManager::importSave(const std::string& savePath, std::string& outToastMsg) {
    if (savePath.empty()) return false;
    size_t lastSlash = savePath.rfind('/');
    if (lastSlash == std::string::npos) return false;
    std::string saveRoot = savePath.substr(0, lastSlash);

    const std::string downloadCandidates[] = {
        port_profile_exchange_path("/GalaxyAndroid_save.bin").c_str(),
        port_profile_exchange_path("/GameData.bin").c_str(),
        port_profile_exchange_path("/GameData_Dolphin.bin").c_str(),
        port_profile_exchange_path("/save.bin").c_str(),
        "/sdcard/dolphin-emu/Wii/title/00010000/524d4745/data/GameData.bin",
        "/sdcard/dolphin-emu/Wii/title/00010000/524d4750/data/GameData.bin",
        "/sdcard/dolphin-emu/Wii/title/00010000/524d474a/data/GameData.bin",
        "/sdcard/dolphin-emu/Wii/title/00010000/524d474b/data/GameData.bin",
        port_profile_data_path("/saves/GameData.bin").c_str(),
        port_profile_data_path("/saves/GameData_Dolphin.bin").c_str()
    };

    FILE* in = nullptr;
    const char* matchedPath = nullptr;
    for (const std::string& candidate : downloadCandidates) {
        const char* path = candidate.c_str();
        if (port_profile_exchange_path("").find("WiiDS-Kaizo") != std::string::npos && candidate.find("/sdcard/dolphin-emu/") == 0) continue;
        in = fopen(path, "rb");
        if (in) { matchedPath = path; break; }
    }
    if (!in) {
        outToastMsg = "Put GameData.bin in Download or Dolphin folder!";
        return false;
    }

    std::vector<uint8_t> data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        data.insert(data.end(), buf, buf + n);
    }
    fclose(in);

    if (data.size() < 0x140) {
        outToastMsg = "Save file is too small or invalid!";
        return false;
    }

    bool wasDolphin = false;
    if (DolphinSaveConverter::isDolphinSave(data.data(), data.size())) {
        std::vector<uint8_t> nativeData;
        if (!DolphinSaveConverter::convertDolphinToNative(data.data(), data.size(), nativeData)) {
            outToastMsg = "Failed to convert Dolphin save (corrupt or invalid)!";
            return false;
        }
        data = std::move(nativeData);
        wasDolphin = true;
    } else if (!DolphinSaveConverter::isNativeSave(data.data(), data.size())) {
        outToastMsg = "Save format unrecognized or corrupted!";
        return false;
    }

    const char* titles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
    mkdir((saveRoot + "/title").c_str(), 0770);
    mkdir((saveRoot + "/title/00010000").c_str(), 0770);
    bool anySuccess = false;
    for (const char* t : titles) {
        std::string dir = saveRoot + "/title/00010000/" + t + "/data";
        mkdir((saveRoot + "/title/00010000/" + t).c_str(), 0770);
        mkdir(dir.c_str(), 0770);
        anySuccess |= writeAtomic(dir + "/GameData.bin", data.data(), data.size());
        writeAtomic(dir + "/save.bin", data.data(), data.size());
    }

    mkdir(port_profile_data_path("").c_str(), 0770);
    mkdir(port_profile_data_path("/saves").c_str(), 0770);
    writeAtomic(port_profile_data_path("/saves/GameData.bin").c_str(), data.data(), data.size());

    if (!anySuccess) {
        outToastMsg = "Failed to write save data to storage!";
        return false;
    }

    outToastMsg = wasDolphin ? "Imported & converted Dolphin save successfully!" : "Imported save data successfully!";
    return true;
}

}  // namespace mobile_ui
