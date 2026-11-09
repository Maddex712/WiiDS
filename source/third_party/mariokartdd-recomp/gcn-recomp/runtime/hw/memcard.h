#pragma once
#include <cstdint>
#include <string>
#include <vector>

class MemCard {
public:
    MemCard(const std::string& path, uint32_t size_mbit);
    void select();
    // Returns true if the card raises its (EXI) interrupt, e.g. after erase/program.
    bool deselect();
    uint8_t transfer(uint8_t in, bool reading);
    void flush();
    void format();
    void write_serial(uint8_t* header);

private:
    std::string path_;
    uint32_t size_mbit_;
    std::vector<uint8_t> data_;
    uint8_t status_;
    uint32_t pos_ = 0;
    uint8_t cmd_ = 0;
    uint32_t addr_ = 0;
    std::vector<uint8_t> prog_;
    bool int_enabled_ = true;
    bool dirty_ = false;
};
