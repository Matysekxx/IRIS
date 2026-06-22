#ifndef BASE64_LIB_H
#define BASE64_LIB_H

#include "core/Native.h"
#include "core/Value.h"
#include <string>
#include <vector>

namespace iris::std_lib {

    inline iris::core::Value iris_base64_encode(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        const std::string& input = args[0].str();
        static const char* CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;

        size_t i = 0;
        while (i < input.length()) {
            unsigned char b1 = input[i++];
            unsigned char b2 = (i < input.length()) ? input[i++] : 0;
            unsigned char b3 = (i < input.length()) ? input[i++] : 0;

            result += CHARS[b1 >> 2];
            result += CHARS[((b1 & 0x03) << 4) | (b2 >> 4)];
            result += (i - 1 < input.length()) ? CHARS[((b2 & 0x0F) << 2) | (b3 >> 6)] : '=';
            result += (i < input.length()) ? CHARS[b3 & 0x3F] : '=';
        }
        return iris::core::Value(result);
    }

    inline int base64_char_val(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    inline iris::core::Value iris_base64_decode(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        const std::string& input = args[0].str();
        std::string result;

        size_t i = 0;
        while (i < input.length() && input[i] != '=') {
            int b1 = base64_char_val(input[i++]);
            if (b1 < 0) continue;
            if (i >= input.length() || input[i] == '=') break;
            int b2 = base64_char_val(input[i++]);
            if (b2 < 0) continue;

            result += static_cast<char>((b1 << 2) | (b2 >> 4));

            if (i >= input.length() || input[i] == '=') break;
            int b3 = base64_char_val(input[i++]);
            if (b3 < 0) continue;
            result += static_cast<char>(((b2 & 0x0F) << 4) | (b3 >> 2));

            if (i >= input.length() || input[i] == '=') break;
            int b4 = base64_char_val(input[i++]);
            if (b4 < 0) continue;
            result += static_cast<char>(((b3 & 0x03) << 6) | b4);
        }
        return iris::core::Value(result);
    }

    inline iris::core::Value iris_hex_encode(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        const std::string& input = args[0].str();
        static const char* HEX = "0123456789abcdef";
        std::string result;
        result.reserve(input.length() * 2);
        for (unsigned char c : input) {
            result += HEX[c >> 4];
            result += HEX[c & 0x0F];
        }
        return iris::core::Value(result);
    }

    inline iris::core::Value iris_hex_decode(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        const std::string& input = args[0].str();
        std::string result;
        result.reserve(input.length() / 2);
        auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        for (size_t i = 0; i + 1 < input.length(); i += 2) {
            result += static_cast<char>((hex_val(input[i]) << 4) | hex_val(input[i + 1]));
        }
        return iris::core::Value(result);
    }
}

#endif
