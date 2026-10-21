#pragma once
#include <cstddef>
#include <cstdint>
class Memory {
public:
    static bool Contains(uint32_t address, size_t length = 1);
    static uint8_t* GetPointer(uint32_t address);
    static uint8_t* GetPointer(uint32_t address, size_t length);
};
