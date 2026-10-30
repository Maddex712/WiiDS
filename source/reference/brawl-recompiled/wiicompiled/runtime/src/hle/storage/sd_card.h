// The virtual SD card behind /dev/sdio/slot0 (sdio.cpp): a FAT disk image. With a UserData/SD folder the image is
// built from it (FatFs) and files the game wrote are copied back to the folder at shutdown; a Dolphin-style
// UserData/sd.raw is used as is.
#pragma once

#include <cstddef>
#include <cstdint>

namespace SdCard {

// Opens (building it first if needed) the card image. False when no card is configured or it failed.
bool Present();
uint64_t SizeBytes();
bool Read(uint64_t offset, void* destination, size_t length);
bool Write(uint64_t offset, const void* source, size_t length);
// Called on the game thread while the card is built from the folder (a large mod takes minutes), so the window
// can stay alive and show how far it is.
using ProgressHandler = void (*)(uint64_t doneBytes, uint64_t totalBytes);
void SetProgressHandler(ProgressHandler handler);
// Closes the image and copies files the game created or changed back to the SD folder.
void Shutdown();

} // namespace SdCard
