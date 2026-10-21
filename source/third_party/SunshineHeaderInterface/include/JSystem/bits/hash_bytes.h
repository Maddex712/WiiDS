namespace {
    inline std::size_t unaligned_load(const char *p) {
        std::size_t result;
        __builtin_memcpy(&result, p, sizeof(result));
        return result;
    }

#if __SIZEOF_SIZE_T__ == 8
    // Loads n bytes, where 1 <= n < 8.
    inline std::size_t load_bytes(const char *p, int n) {
        std::size_t result = 0;
        --n;
        do
            result = (result << 8) + static_cast<unsigned char>(p[n]);
        while (--n >= 0);
        return result;
    }

    inline std::size_t shift_mix(std::size_t v) { return v ^ (v >> 47); }
#endif
}  // namespace

namespace JSystem {
#if __SIZEOF_SIZE_T__ == 4

    // Implementation of Murmur hash for 32-bit size_t.
    inline size_t _Hash_bytes(const void *ptr, size_t len, size_t seed) {
        const size_t m  = 0x5bd1e995;
        size_t hash     = seed ^ len;
        const char *buf = static_cast<const char *>(ptr);

        // Mix 4 bytes at a time into the hash.
        while (len >= 4) {
            size_t k = unaligned_load(buf);
            k *= m;
            k ^= k >> 24;
            k *= m;
            hash *= m;
            hash ^= k;
            buf += 4;
            len -= 4;
        }

        size_t k;
        // Handle the last few bytes of the input array.
        switch (len) {
        case 3:
            k = static_cast<unsigned char>(buf[2]);
            hash ^= k << 16;
            [[gnu::fallthrough]];
        case 2:
            k = static_cast<unsigned char>(buf[1]);
            hash ^= k << 8;
            [[gnu::fallthrough]];
        case 1:
            k = static_cast<unsigned char>(buf[0]);
            hash ^= k;
            hash *= m;
        };

        // Do a few final mixes of the hash.
        hash ^= hash >> 13;
        hash *= m;
        hash ^= hash >> 15;
        return hash;
    }

    // Implementation of FNV hash for 32-bit size_t.
    // N.B. This function should work on unsigned char, otherwise it does not
    // correctly implement the FNV-1a algorithm (see PR59406).
    // The existing behaviour is retained for backwards compatibility.
    inline size_t _Fnv_hash_bytes(const void *ptr, size_t len, size_t hash) {
        const char *cptr = static_cast<const char *>(ptr);
        for (; len; --len) {
            hash ^= static_cast<size_t>(*cptr++);
            hash *= static_cast<size_t>(16777619UL);
        }
        return hash;
    }

#elif __SIZEOF_SIZE_T__ == 8

    // Implementation of Murmur hash for 64-bit size_t.
    inline size_t _Hash_bytes(const void *ptr, size_t len, size_t seed) {
        static const size_t mul = (((size_t)0xc6a4a793UL) << 32UL) + (size_t)0x5bd1e995UL;
        const char *const buf   = static_cast<const char *>(ptr);

        // Remove the bytes not divisible by the sizeof(size_t).  This
        // allows the main loop to process the data as 64-bit integers.
        const size_t len_aligned = len & ~(size_t)0x7;
        const char *const end    = buf + len_aligned;
        size_t hash              = seed ^ (len * mul);
        for (const char *p = buf; p != end; p += 8) {
            const size_t data = shift_mix(unaligned_load(p) * mul) * mul;
            hash ^= data;
            hash *= mul;
        }
        if ((len & 0x7) != 0) {
            const size_t data = load_bytes(end, len & 0x7);
            hash ^= data;
            hash *= mul;
        }
        hash = shift_mix(hash) * mul;
        hash = shift_mix(hash);
        return hash;
    }

    // Implementation of FNV hash for 64-bit size_t.
    // N.B. This function should work on unsigned char, otherwise it does not
    // correctly implement the FNV-1a algorithm (see PR59406).
    // The existing behaviour is retained for backwards compatibility.
    inline size_t _Fnv_hash_bytes(const void *ptr, size_t len, size_t hash) {
        const char *cptr = static_cast<const char *>(ptr);
        for (; len; --len) {
            hash ^= static_cast<size_t>(*cptr++);
            hash *= static_cast<size_t>(1099511628211ULL);
        }
        return hash;
    }

#else

    // Dummy hash implementation for unusual sizeof(size_t).
    inline size_t _Hash_bytes(const void *ptr, size_t len, size_t seed) {
        size_t hash      = seed;
        const char *cptr = reinterpret_cast<const char *>(ptr);
        for (; len; --len)
            hash = (hash * 131) + *cptr++;
        return hash;
    }

    inline size_t _Fnv_hash_bytes(const void *ptr, size_t len, size_t seed) {
        return _Hash_bytes(ptr, len, seed);
    }

#endif /* __SIZEOF_SIZE_T__ */
}  // namespace JSystem