#pragma once
#include "ios_test_support.h"
namespace NetworkDeferredContract {
enum class StartDisposition { Started, ImmediateResult, Unsupported };
struct StartOutcome { StartDisposition disposition; int32_t result; };
}
extern "C" bool Network_HLE_IsFd(uint32_t);
extern "C" int32_t Network_HLE_OpenDevice(const char*, uint32_t);
extern "C" int32_t Network_HLE_Close(uint32_t);
extern "C" int32_t Network_HLE_Ioctl(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
extern "C" int32_t Network_HLE_Ioctlv(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
NetworkDeferredContract::StartOutcome Network_HLE_StartIoctlAsync(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
NetworkDeferredContract::StartOutcome Network_HLE_StartIoctlvAsync(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
