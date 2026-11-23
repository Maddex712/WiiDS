#pragma once
#include <stdint.h>

/* Android ART can occupy 0x80000000. Keep disc addresses in their original
 * namespace, but use the uncached MEM1 address for native working pointers.
 * Never apply this conversion to arbitrary integers, flags, or disc metadata. */
#define XD_MEM1_GUEST_BASE UINT32_C(0x80000000)
#define XD_MEM1_HOST_BASE UINT32_C(0xC0000000)
#define XD_MEM1_BYTES UINT32_C(0x01800000)

static inline uintptr_t XdMem1ToHost(uintptr_t address) {
    return address >= XD_MEM1_GUEST_BASE &&
           address - XD_MEM1_GUEST_BASE < XD_MEM1_BYTES
        ? XD_MEM1_HOST_BASE + (address - XD_MEM1_GUEST_BASE) : address;
}

static inline uintptr_t XdMem1ToGuest(uintptr_t address) {
    return address >= XD_MEM1_HOST_BASE &&
           address - XD_MEM1_HOST_BASE < XD_MEM1_BYTES
        ? XD_MEM1_GUEST_BASE + (address - XD_MEM1_HOST_BASE) : address;
}

static inline int XdMem1Contains(uintptr_t address) {
    return address >= XD_MEM1_HOST_BASE &&
           address - XD_MEM1_HOST_BASE < XD_MEM1_BYTES;
}
