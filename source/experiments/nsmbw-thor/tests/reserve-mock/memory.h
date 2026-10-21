#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
class Memory {
public:
 inline static std::vector<uint8_t> bytes = std::vector<uint8_t>(0x1800000);
 static bool Contains(uint32_t p, uint32_t n) { return p >= 0x80000000 && uint64_t(p) + n <= 0x81800000ull; }
 static bool TryRead32(uint32_t p, uint32_t& v) {
  if (!Contains(p,4)) return false;
  const auto i=p-0x80000000u; v=uint32_t(bytes[i])<<24|uint32_t(bytes[i+1])<<16|uint32_t(bytes[i+2])<<8|bytes[i+3]; return true;
 }
 static bool TryWrite32(uint32_t p,uint32_t v) {
  if (!Contains(p,4)) return false; const auto i=p-0x80000000u;
  for(unsigned b=0;b<4;b++) bytes[i+b]=v>>((3-b)*8); return true;
 }
 static void Byte(uint32_t p,uint32_t v) { bytes[p-0x80000000u]=v; }
 static void Half(uint32_t p,uint32_t v) { Byte(p,v>>8); Byte(p+1,v); }
};
