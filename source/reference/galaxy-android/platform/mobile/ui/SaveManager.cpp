#include "port/profile.h"
#include "port/port.h"
#include "SaveManager.h"
#include "SaveSyncManager.h"

#include <android/log.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MobileUI", __VA_ARGS__)

extern "C" void port_trigger_quick_save(void);
extern "C" void port_trigger_quick_load(int slot);
extern "C" uint64_t port_get_save_generation(void);
extern "C" int port_is_save_in_progress(void);
extern "C" int64_t port_host_time_ns(void);
extern "C" void port_android_vibrate(int ms);

namespace mobile_ui {

SaveManager::SaveManager() = default;

bool SaveManager::copyFileVerified(const std::string& src, const std::string& dst) {
    struct stat st;
    if (stat(src.c_str(), &st) != 0 || st.st_size <= 0) return false;

    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;

    std::string tmpDst = dst + ".tmp";
    FILE* out = fopen(tmpDst.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    char buf[16384];
    size_t n;
    size_t total = 0;
    bool writeErr = false;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            writeErr = true;
            break;
        }
        total += n;
    }
    if (ferror(in) || fflush(out) != 0) writeErr = true;
    int fd = fileno(out);
    if (fd < 0 || fsync(fd) != 0) writeErr = true;
    if (fclose(out) != 0) writeErr = true;
    if (fclose(in) != 0) writeErr = true;

    if (writeErr || total != (size_t)st.st_size) {
        unlink(tmpDst.c_str());
        return false;
    }

    if (rename(tmpDst.c_str(), dst.c_str()) != 0) {
        unlink(tmpDst.c_str());
        return false;
    }
    return true;
}

bool SaveManager::copyDirVerified(const std::string& src, const std::string& dst) {
    if (mkdir(dst.c_str(), 0770) != 0 && errno != EEXIST) return false;
    DIR* dir = opendir(src.c_str());
    if (!dir) return false;
    struct dirent* entry;
    bool success = true;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (entry == nullptr) {
            if (errno != 0) success = false;
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (strstr(entry->d_name, ".tmp") || strstr(entry->d_name, "_rollback") || strstr(entry->d_name, "_staging")) continue;

        std::string sPath = src + "/" + entry->d_name;
        std::string dPath = dst + "/" + entry->d_name;
        struct stat st;
        if (stat(sPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (!copyDirVerified(sPath, dPath)) {
                    success = false;
                    break;
                }
            } else if (S_ISREG(st.st_mode)) {
                if (!copyFileVerified(sPath, dPath)) {
                    success = false;
                    break;
                }
            }
        } else {
            success = false;
            break;
        }
    }
    if (closedir(dir) != 0) success = false;
    return success;
}

bool SaveManager::validateSaveDir(const std::string& dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;

    std::string directGameData = dir + "/GameData.bin";
    if (stat(directGameData.c_str(), &st) == 0 && st.st_size >= 512) return true;

    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    struct dirent* entry;
    bool found = false;
    while ((entry = readdir(d)) != nullptr) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        std::string sub = dir + "/" + entry->d_name;
        if (stat(sub.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (validateSaveDir(sub)) {
                    found = true;
                    break;
                }
            } else if (strstr(entry->d_name, "GameData.bin") && st.st_size >= 512) {
                found = true;
                break;
            }
        }
    }
    closedir(d);
    return found;
}

std::string SaveManager::triggerSaveAnywhere(bool haptics) {
    if (mPendingSaveSlot >= 0) return "A save is already pending.";
    mPendingSaveSlot = 0;
    mPendingSaveStartNs = port_host_time_ns();
    port_trigger_quick_save();
    if (haptics) port_android_vibrate(50);
    LOGI("Save anywhere requested; waiting for game sequence to commit");
    return "Saving game progress anywhere...";
}

std::string SaveManager::triggerSaveSlot(int slot, bool haptics) {
    if (mPendingSaveSlot >= 0) return "A save is already pending.";
    mPendingSaveSlot = slot;
    mPendingSaveStartNs = port_host_time_ns();
    port_trigger_quick_save();
    if (haptics) port_android_vibrate(50);
    LOGI("Save to slot %d requested; waiting for game sequence to commit", slot);
    char msg[64];
    snprintf(msg, sizeof(msg), "Saving to Slot %d...", slot);
    return std::string(msg);
}

std::string SaveManager::triggerLoadSlot(int slot, bool haptics) {
    if (mSavePath.empty()) return "Save path not set!";
    size_t lastSlash = mSavePath.find_last_of('/');
    if (lastSlash == std::string::npos) return "Invalid save path!";
    std::string nandDir = mSavePath.substr(0, lastSlash);
    std::string slotSuffix = (slot == 4) ? "_quick" : ("_slot" + std::to_string(slot));
    std::string slotDir = nandDir + slotSuffix;

    if (!validateSaveDir(slotDir)) {
        std::string pubSlot = port_profile_data_path("/saves/").c_str() + ((slot == 4) ? std::string("quick") : ("slot" + std::to_string(slot)));
        if (validateSaveDir(pubSlot)) {
            slotDir = pubSlot;
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), (slot == 4) ? "Quick Save slot is empty!" : "Slot %d is empty or corrupted!", slot);
            return std::string(msg);
        }
    }

    std::string rollbackDir = nandDir + "_rollback";
    copyDirVerified(nandDir, rollbackDir);

    if (copyDirVerified(slotDir, nandDir) && validateSaveDir(nandDir)) {
        port_trigger_quick_load(slot);
        if (haptics) port_android_vibrate(75);
        char msg[64];
        snprintf(msg, sizeof(msg), (slot == 4) ? "Quick Save Loaded & Stage Reloaded!" : "Slot %d Loaded & Stage Reloaded!", slot);
        LOGI("NAND verified restored from slot %d: %s", slot, slotDir.c_str());
        return std::string(msg);
    } else {
        copyDirVerified(rollbackDir, nandDir);
        LOGE("Failed to restore slot %d; rolled back to previous state", slot);
        return "Slot restore failed! Preserved current save.";
    }
}

bool SaveManager::update(std::string& outToastMsg) {
    if (mPendingSaveSlot < 0) return false;

    // Track this Quick Save, not unrelated mail/system-only NAND writes.
    const int status = port_get_quick_save_status();
    if (status == PORT_QUICK_SAVE_FAILED || status == PORT_QUICK_SAVE_REJECTED) {
        mPendingSaveSlot = -1;
        outToastMsg = status == PORT_QUICK_SAVE_REJECTED
            ? "Cannot save right now. Resume play and try again." : "Game save failed. Please try again.";
        return true;
    }
    const bool committed = status == PORT_QUICK_SAVE_SUCCEEDED && !port_is_save_in_progress();
    if (!committed) {
        if (port_host_time_ns() - mPendingSaveStartNs < 15000000000LL) return false;
        mPendingSaveSlot = -1;
        outToastMsg = "Save not confirmed. Resume play and try again.";
        return true;
    }

    int slot = mPendingSaveSlot;
    mPendingSaveSlot = -1;

    if (mSavePath.empty()) {
        outToastMsg = "Game saved; backup path is unavailable.";
        return true;
    }
    size_t lastSlash = mSavePath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        outToastMsg = "Game saved; backup path is invalid.";
        return true;
    }

    std::string nandDir = mSavePath.substr(0, lastSlash);
    mkdir(port_profile_data_path("").c_str(), 0775);
    mkdir(port_profile_data_path("/saves").c_str(), 0775);

    if (slot == 0) {
        const std::string backupDir = port_profile_data_path("/saves/current");
        if (validateSaveDir(nandDir) && copyDirVerified(nandDir, backupDir) && validateSaveDir(backupDir)) {
            outToastMsg = "Game progress saved.";
        } else {
            outToastMsg = "Game saved; backup copy failed.";
        }
        return true;
    } else {
        std::string slotSuffix = (slot == 4) ? "_quick" : ("_slot" + std::to_string(slot));
        std::string slotDir = nandDir + slotSuffix;
        const std::string suffix = std::to_string(port_host_time_ns());
        std::string slotStaging = slotDir + "_staging_" + suffix;
        std::string slotOld = slotDir + "_old_" + suffix;
        // Refuse an existing staging directory instead of merging stale files.
        if (mkdir(slotStaging.c_str(), 0770) != 0) {
            outToastMsg = "Game saved; snapshot staging failed.";
            return true;
        }

        if (copyDirVerified(nandDir, slotStaging) && validateSaveDir(slotStaging)) {
            struct stat st;
            const bool hadPrevious = stat(slotDir.c_str(), &st) == 0;
            if ((!hadPrevious && errno != ENOENT) ||
                (hadPrevious && rename(slotDir.c_str(), slotOld.c_str()) != 0)) {
                outToastMsg = "Game saved; previous snapshot could not be replaced.";
                return true;
            }
            if (rename(slotStaging.c_str(), slotDir.c_str()) != 0) {
                const bool restored = !hadPrevious || rename(slotOld.c_str(), slotDir.c_str()) == 0;
                outToastMsg = restored ? "Snapshot replacement failed; previous snapshot kept."
                                      : "Snapshot replacement failed; previous backup retained.";
                LOGE("Snapshot replacement failed; rollback restored=%d, backup=%s", restored, slotOld.c_str());
                return true;
            }

            std::string pubSlot = port_profile_data_path("/saves/").c_str() + ((slot == 4) ? std::string("quick") : ("slot" + std::to_string(slot)));
            if (!copyDirVerified(slotDir, pubSlot) ||
                !copyDirVerified(slotDir, port_profile_data_path("/saves/current"))) {
                outToastMsg = "Game and snapshot saved; shared backup copy failed.";
                return true;
            }

            char msg[96];
            snprintf(msg, sizeof(msg), (slot == 4) ? "Game progress and snapshot saved." : "Game progress saved to Slot %d.", slot);
            outToastMsg = std::string(msg);
            LOGI("NAND verified saved to slot %d: %s", slot, slotDir.c_str());
            return true;
        } else {
            outToastMsg = "Save snapshot failed verification!";
            LOGE("Save snapshot verification failed for slot %d", slot);
            return true;
        }
    }
}

bool SaveManager::exportSave(std::string& outToastMsg) {
    return SaveSyncManager::exportSave(mSavePath, outToastMsg);
}

bool SaveManager::importSave(std::string& outToastMsg) {
    return SaveSyncManager::importSave(mSavePath, outToastMsg);
}


}  // namespace mobile_ui
