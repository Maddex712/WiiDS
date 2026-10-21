#if defined(__SWITCH__) || defined(MII_DATA_TEST_MAIN)

#include "switch_mii_data.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace SwitchMiiData {
namespace {

// Layout as in MiiDatabaseEditor.kt: "RNOD", then 100 "My Miis" slots of 74 bytes from offset 4,
// and a CRC-16 (XMODEM) of everything before 0x1F1DE stored there big-endian.
constexpr size_t kMiiBytes = 74;
constexpr size_t kDatabaseBytes = 779968;
constexpr size_t kTableOffset = 4;
constexpr size_t kSlots = 100;
constexpr size_t kCrcOffset = 0x1F1DE;
constexpr const char* kDatabaseName = "RFL_DB.dat";

bool ReadFile(const fs::path& path, std::vector<uint8_t>& out, size_t limit) {
    FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    out.resize(limit + 1);
    const size_t n = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    out.resize(n);
    return n <= limit;
}

bool WriteFile(const fs::path& path, const std::vector<uint8_t>& data) {
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    return std::fclose(file) == 0 && ok;
}

uint16_t Crc16(const uint8_t* data, size_t size) {
    uint16_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint16_t>(data[i] << 8);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0 ? static_cast<uint16_t>((crc << 1) ^ 0x1021u) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

bool ValidDatabase(const std::vector<uint8_t>& db) {
    if (db.size() != kDatabaseBytes || std::memcmp(db.data(), "RNOD", 4) != 0) {
        return false;
    }
    const uint16_t stored = static_cast<uint16_t>((db[kCrcOffset] << 8) | db[kCrcOffset + 1]);
    return stored == Crc16(db.data(), kCrcOffset);
}

bool Empty(const uint8_t* slot) {
    return std::all_of(slot, slot + kMiiBytes, [](uint8_t b) { return b == 0; });
}

std::string LowerExtension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

}  // namespace

Result Export(const fs::path& database, const fs::path& folder) {
    std::vector<uint8_t> db;
    if (!ReadFile(database, db, kDatabaseBytes) || db.empty()) {
        return {false, "There is no Mii data yet. Start Mario Kart Wii once first."};
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (!WriteFile(folder / kDatabaseName, db)) {
        return {false, "Could not write to the Mii folder on the SD card."};
    }
    return {true, "Saved your Miis as RFL_DB.dat in /switch/WiiCompiled/Mii."};
}

Result Replace(const fs::path& database, const fs::path& folder) {
    std::vector<uint8_t> incoming;
    if (!ReadFile(folder / kDatabaseName, incoming, kDatabaseBytes) || incoming.empty()) {
        return {false, "Put the RFL_DB.dat you want to use in /switch/WiiCompiled/Mii first."};
    }
    if (!ValidDatabase(incoming)) {
        return {false, "That RFL_DB.dat is not a valid Mii database, so nothing was changed."};
    }
    // The Miis in use until now are kept beside the database.
    std::vector<uint8_t> current;
    if (ReadFile(database, current, kDatabaseBytes) && !current.empty()) {
        fs::path backup = database;
        backup += ".bak";
        WriteFile(backup, current);
    }
    std::error_code ec;
    fs::create_directories(database.parent_path(), ec);
    if (!WriteFile(database, incoming)) {
        return {false, "Could not write the Mii database."};
    }
    return {true, "Mii database replaced. The previous one was kept as RFL_DB.dat.bak."};
}

Result AddSingleMiis(const fs::path& database, const fs::path& folder) {
    std::vector<uint8_t> db;
    if (!ReadFile(database, db, kDatabaseBytes) || db.empty()) {
        return {false, "There is no Mii data yet. Start Mario Kart Wii once first."};
    }
    if (!ValidDatabase(db)) {
        return {false, "The game's Mii database did not pass its check, so nothing was changed."};
    }
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string ext = LowerExtension(it->path());
        if (it->is_regular_file(ec) && (ext == ".rcd" || ext == ".mii" || ext == ".miigx")) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        return {false, "Put single Mii files (.rcd, .mii or .miigx) in /switch/WiiCompiled/Mii first."};
    }

    const std::vector<uint8_t> original = db;
    int added = 0, already = 0, invalid = 0, noRoom = 0;
    for (const fs::path& path : files) {
        std::vector<uint8_t> mii;
        if (!ReadFile(path, mii, kMiiBytes) || mii.size() != kMiiBytes || Empty(mii.data())) {
            ++invalid;
            continue;
        }
        uint8_t* table = db.data() + kTableOffset;
        uint8_t* free = nullptr;
        bool present = false;
        for (size_t slot = 0; slot < kSlots; ++slot) {
            uint8_t* at = table + slot * kMiiBytes;
            if (std::memcmp(at, mii.data(), kMiiBytes) == 0) {
                present = true;
                break;
            }
            if (free == nullptr && Empty(at)) {
                free = at;
            }
        }
        if (present) {
            ++already;
        } else if (free == nullptr) {
            ++noRoom;
        } else {
            std::memcpy(free, mii.data(), kMiiBytes);
            ++added;
        }
    }
    if (added > 0) {
        const uint16_t crc = Crc16(db.data(), kCrcOffset);
        db[kCrcOffset] = static_cast<uint8_t>(crc >> 8);
        db[kCrcOffset + 1] = static_cast<uint8_t>(crc);
        fs::path backup = database;
        backup += ".bak";
        WriteFile(backup, original);
        if (!WriteFile(database, db)) {
            return {false, "Could not write the Mii database."};
        }
    }
    char text[200];
    int n = std::snprintf(text, sizeof(text), "Added %d Mii%s.", added, added == 1 ? "" : "s");
    if (already > 0) {
        n += std::snprintf(text + n, sizeof(text) - n, " %d already there.", already);
    }
    if (noRoom > 0) {
        n += std::snprintf(text + n, sizeof(text) - n, " %d did not fit (all 100 slots are used).", noRoom);
    }
    if (invalid > 0) {
        std::snprintf(text + n, sizeof(text) - n, " %d not a single-Mii file.", invalid);
    }
    return {added > 0 || (already > 0 && noRoom == 0 && invalid == 0), text};
}

}  // namespace SwitchMiiData

#ifdef MII_DATA_TEST_MAIN
// PC check: mii_test <export|replace|add> <RFL_DB.dat> <folder>
int main(int argc, char** argv) {
    if (argc < 4) {
        return 2;
    }
    const std::string op = argv[1];
    const SwitchMiiData::Result r = op == "export"    ? SwitchMiiData::Export(argv[2], argv[3])
                                    : op == "replace" ? SwitchMiiData::Replace(argv[2], argv[3])
                                                      : SwitchMiiData::AddSingleMiis(argv[2], argv[3]);
    std::printf("%s: %s\n", r.ok ? "ok" : "FAILED", r.message.c_str());
    return r.ok ? 0 : 1;
}
#endif
#endif
