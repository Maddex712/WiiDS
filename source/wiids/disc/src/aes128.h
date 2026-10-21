#pragma once
// Minimal AES-128 CBC decryption for Wii partition clusters and title keys (FIPS-197).
#include <cstddef>
#include <cstdint>

namespace wiids::disc {

class Aes128Decryptor {
public:
    explicit Aes128Decryptor(const uint8_t key[16]);
    // CBC-decrypts `size` bytes (a multiple of 16) from `in` to `out` with the given IV.
    void CbcDecrypt(const uint8_t iv[16], const uint8_t* in, uint8_t* out, size_t size) const;

private:
    void DecryptBlock(const uint8_t in[16], uint8_t out[16]) const;
    uint8_t roundKeys_[176];
};

} // namespace wiids::disc
