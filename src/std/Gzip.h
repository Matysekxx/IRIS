#ifndef GZIP_LIB_H
#define GZIP_LIB_H

#include "core/Native.h"
#include <string>
#include <vector>
#include <cstdint>

namespace iris::std_lib {

    // Simple, self-contained LZ77 compressor (own scheme, not RFC 1952).
    // Format: byte 0 = 0x1A (magic). Then tokens:
    //   0x00 <byte>             literal
    //   0x01 <u16 dist> <u8 len>  copy len bytes from dist bytes back
    static inline std::string lz77_compress(const std::string& in) {
        std::string out;
        out.push_back(0x1A);
        const size_t n = in.size();
        const int WIN = 4096;
        size_t i = 0;
        while (i < n) {
            size_t bestLen = 0, bestDist = 0;
            size_t start = (i > (size_t)WIN) ? i - WIN : 0;
            for (size_t j = start; j < i; j++) {
                size_t len = 0;
                while (i + len < n && len < 255 && in[j + (len % (i - j))] == in[i + len]) len++;
                if (len > bestLen) { bestLen = len; bestDist = i - j; }
            }
            if (bestLen >= 3) {
                out.push_back(0x01);
                out.push_back((char)(bestDist & 0xFF));
                out.push_back((char)((bestDist >> 8) & 0xFF));
                out.push_back((char)bestLen);
                i += bestLen;
            } else {
                out.push_back(0x00);
                out.push_back(in[i]);
                i++;
            }
        }
        return out;
    }

    static inline std::string lz77_decompress(const std::string& in) {
        std::string out;
        if (in.empty() || (uint8_t)in[0] != 0x1A) return out;
        size_t i = 1;
        while (i < in.size()) {
            uint8_t tag = (uint8_t)in[i++];
            if (tag == 0x00) {
                if (i < in.size()) out.push_back(in[i++]);
            } else if (tag == 0x01) {
                if (i + 2 >= in.size()) break;
                uint32_t dist = (uint8_t)in[i] | ((uint32_t)(uint8_t)in[i+1] << 8);
                uint32_t len = (uint8_t)in[i+2];
                i += 3;
                for (uint32_t k = 0; k < len; k++) {
                    if (dist <= out.size())
                        out.push_back(out[out.size() - dist]);
                }
            } else {
                break;
            }
        }
        return out;
    }

    inline iris::core::Value iris_gzip_compress(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        return iris::core::Value(lz77_compress(args[0].str()));
    }
    inline iris::core::Value iris_gzip_decompress(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        return iris::core::Value(lz77_decompress(args[0].str()));
    }
}

#endif
