// /dev/sdio/slot0: the Wii's SD card slot (sdio.cpp), backed by the image in sd_card.cpp.
#pragma once

#include <cstdint>

namespace Sdio {

constexpr int32_t kFd = 5;
constexpr const char* kDevicePath = "/dev/sdio/slot0";
// An ioctl IOS answers only later (EVENT_REGISTER waits for a card change): queue no completion for it.
constexpr int32_t kNoReply = 0x7FFF0001;

int32_t Ioctl(uint32_t cmd, uint32_t inPtr, uint32_t inLen, uint32_t outPtr, uint32_t outLen);
int32_t Ioctlv(uint32_t cmd, uint32_t numIn, uint32_t numOut, uint32_t vectorPtr);
// The async caller of an ioctl that returned kNoReply; EVENT_UNREGISTER completes it.
void SetPendingEvent(uint32_t callback, uint32_t callbackArg);

} // namespace Sdio
