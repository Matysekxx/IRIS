#ifndef SYSTEM_LIB_H
#define SYSTEM_LIB_H

#include "core/Native.h"
#include "core/Value.h"
#include "core/ArrayData.h"
#include <chrono>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <random>
#include <fstream>
#include <emmintrin.h>

namespace iris::std_lib {

    std::string getClassNameById(uint16_t classId);

    // --- Time ---
    inline double iris_system_time() {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        return static_cast<double>(millis);
    }

    // --- Hash ---
    inline iris::core::Value iris_system_hash(iris::core::Value* args, int argCount) {
        if (argCount < 1) return iris::core::Value(0);
        const auto& v = args[0];
        int h = 0;
        if (v.isInt()) h = static_cast<int>(std::hash<int>{}(v.asInt()));
        else if (v.isDouble()) h = static_cast<int>(std::hash<double>{}(v.asDouble()));
        else if (v.isBool()) h = static_cast<int>(std::hash<bool>{}(v.asBool()));
        else if (v.isString()) h = static_cast<int>(v.hash());
        else if (v.isHeap()) h = static_cast<int>(std::hash<void*>{}(v.asPtr()));
        return iris::core::Value(h);
    }

    inline iris::core::Value iris_system_char_to_string(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isInt()) return iris::core::Value("");
        char c = static_cast<char>(args[0].asInt());
        return iris::core::Value(std::string(1, c));
    }

    // --- String Primitives ---
    inline iris::core::Value iris_system_string_substring(iris::core::Value* args, int argCount) {
        if (argCount < 3 || !args[0].isString() || !args[1].isInt() || !args[2].isInt()) {
            return iris::core::Value("");
        }
        std::string s = args[0].str();
        int start = args[1].asInt();
        int end = args[2].asInt();
        if (start < 0) start = 0;
        if (end > (int)s.length()) end = (int)s.length();
        if (start > end) return iris::core::Value("");
        return iris::core::Value(s.substr(start, end - start));
    }

    inline iris::core::Value iris_system_string_index_of(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString()) {
            return iris::core::Value(-1);
        }
        std::string s = args[0].str();
        std::string sub = args[1].str();
        size_t idx = s.find(sub);
        if (idx == std::string::npos) return iris::core::Value(-1);
        return iris::core::Value(static_cast<int>(idx));
    }

    inline iris::core::Value iris_system_string_split(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString()) {
            return iris::core::Value();
        }
        std::string s = args[0].str();
        std::string delim = args[1].str();
        std::vector<std::string> tokens;
        if (delim.empty()) {
            for (char c : s) {
                tokens.push_back(std::string(1, c));
            }
        } else {
            size_t start = 0, end = 0;
            while ((end = s.find(delim, start)) != std::string::npos) {
                tokens.push_back(s.substr(start, end - start));
                start = end + delim.length();
            }
            tokens.push_back(s.substr(start));
        }

        auto* arr = iris::core::ArrayData::create(tokens.size(), iris::core::ArrayData::VALUE);
        for (size_t i = 0; i < tokens.size(); i++) {
            arr->getValData()[i] = iris::core::Value(tokens[i]);
        }
        return iris::core::Value(arr);
    }

    inline iris::core::Value iris_system_string_trim(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        std::string s = args[0].str();
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), s.end());
        return iris::core::Value(s);
    }

    inline void string_to_lower_sse2(char* dst, const char* src, size_t len) {
        size_t i = 0;
        const __m128i offset_A = _mm_set1_epi8('A' - 128);
        const __m128i offset_Z = _mm_set1_epi8('Z' - 128);
        const __m128i delta = _mm_set1_epi8(32);
        const __m128i bias = _mm_set1_epi8(static_cast<char>(128));
        const __m128i all_ones = _mm_set1_epi8(static_cast<char>(0xFF));

        for (; i + 15 < len; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
            __m128i biased = _mm_sub_epi8(chunk, bias);
            __m128i cmp_ge = _mm_andnot_si128(_mm_cmpgt_epi8(offset_A, biased), all_ones);
            __m128i cmp_le = _mm_andnot_si128(_mm_cmpgt_epi8(biased, offset_Z), all_ones);
            __m128i mask = _mm_and_si128(cmp_ge, cmp_le);
            __m128i add_val = _mm_and_si128(mask, delta);
            __m128i lower_chunk = _mm_add_epi8(chunk, add_val);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), lower_chunk);
        }
        for (; i < len; ++i) {
            unsigned char c = static_cast<unsigned char>(src[i]);
            if (c >= 'A' && c <= 'Z') dst[i] = static_cast<char>(c + 32);
            else dst[i] = static_cast<char>(c);
        }
    }

    inline void string_to_upper_sse2(char* dst, const char* src, size_t len) {
        size_t i = 0;
        const __m128i offset_a = _mm_set1_epi8('a' - 128);
        const __m128i offset_z = _mm_set1_epi8('z' - 128);
        const __m128i delta = _mm_set1_epi8(32);
        const __m128i bias = _mm_set1_epi8(static_cast<char>(128));
        const __m128i all_ones = _mm_set1_epi8(0xFF);

        for (; i + 15 < len; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
            __m128i biased = _mm_sub_epi8(chunk, bias);
            __m128i cmp_ge = _mm_andnot_si128(_mm_cmpgt_epi8(offset_a, biased), all_ones);
            __m128i cmp_le = _mm_andnot_si128(_mm_cmpgt_epi8(biased, offset_z), all_ones);
            __m128i mask = _mm_and_si128(cmp_ge, cmp_le);
            __m128i sub_val = _mm_and_si128(mask, delta);
            __m128i upper_chunk = _mm_sub_epi8(chunk, sub_val);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), upper_chunk);
        }
        for (; i < len; ++i) {
            unsigned char c = src[i];
            if (c >= 'a' && c <= 'z') dst[i] = c - 32;
            else dst[i] = c;
        }
    }

    inline iris::core::Value iris_system_string_to_lower(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        std::string s = args[0].str();
        string_to_lower_sse2(s.data(), s.data(), s.length());
        return iris::core::Value(s);
    }

    inline iris::core::Value iris_system_string_to_upper(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        std::string s = args[0].str();
        string_to_upper_sse2(s.data(), s.data(), s.length());
        return iris::core::Value(s);
    }

    // --- More String Primitives ---
    inline iris::core::Value iris_system_string_replace(iris::core::Value* args, int argCount) {
        if (argCount < 3 || !args[0].isString() || !args[1].isString() || !args[2].isString())
            return iris::core::Value("");
        std::string s = args[0].str();
        std::string search = args[1].str();
        std::string replace = args[2].str();
        if (search.empty()) return iris::core::Value(s);
        size_t pos = 0;
        while ((pos = s.find(search, pos)) != std::string::npos) {
            s.replace(pos, search.length(), replace);
            pos += replace.length();
        }
        return iris::core::Value(s);
    }

    inline iris::core::Value iris_system_string_starts_with(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString())
            return iris::core::Value(false);
        std::string s = args[0].str();
        std::string prefix = args[1].str();
        return iris::core::Value(s.find(prefix) == 0);
    }

    inline iris::core::Value iris_system_string_ends_with(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString())
            return iris::core::Value(false);
        std::string s = args[0].str();
        std::string suffix = args[1].str();
        if (suffix.length() > s.length()) return iris::core::Value(false);
        return iris::core::Value(s.rfind(suffix) == s.length() - suffix.length());
    }

    inline iris::core::Value iris_system_string_contains(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString())
            return iris::core::Value(false);
        std::string s = args[0].str();
        std::string sub = args[1].str();
        return iris::core::Value(s.find(sub) != std::string::npos);
    }

    inline iris::core::Value iris_system_string_reverse(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        std::string s = args[0].str();
        std::reverse(s.begin(), s.end());
        return iris::core::Value(s);
    }

    inline iris::core::Value iris_system_string_repeat(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isInt())
            return iris::core::Value("");
        std::string s = args[0].str();
        int count = args[1].asInt();
        if (count <= 0) return iris::core::Value("");
        std::string result;
        result.reserve(s.length() * count);
        for (int i = 0; i < count; i++) result += s;
        return iris::core::Value(result);
    }

    inline iris::core::Value iris_system_string_pad_left(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isInt())
            return iris::core::Value("");
        std::string s = args[0].str();
        int totalWidth = args[1].asInt();
        char padChar = (argCount >= 3 && args[2].isString() && !args[2].str().empty())
                       ? args[2].str()[0] : ' ';
        if (totalWidth <= (int)s.length()) return iris::core::Value(s);
        return iris::core::Value(std::string(totalWidth - s.length(), padChar) + s);
    }

    inline iris::core::Value iris_system_string_pad_right(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isInt())
            return iris::core::Value("");
        std::string s = args[0].str();
        int totalWidth = args[1].asInt();
        char padChar = (argCount >= 3 && args[2].isString() && !args[2].str().empty())
                       ? args[2].str()[0] : ' ';
        if (totalWidth <= (int)s.length()) return iris::core::Value(s);
        return iris::core::Value(s + std::string(totalWidth - s.length(), padChar));
    }

    // --- Filesystem Primitives ---
    inline iris::core::Value iris_system_fs_exists(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value(false);
        std::string path = args[0].str();
        return iris::core::Value(std::filesystem::exists(path));
    }

    inline iris::core::Value iris_system_fs_delete(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value(false);
        std::string path = args[0].str();
        try {
            return iris::core::Value(std::filesystem::remove_all(path) > 0);
        } catch (...) {
            return iris::core::Value(false);
        }
    }

    inline iris::core::Value iris_system_fs_create_dir(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value(false);
        std::string path = args[0].str();
        try {
            return iris::core::Value(std::filesystem::create_directories(path));
        } catch (...) {
            return iris::core::Value(false);
        }
    }

    inline iris::core::Value iris_system_fs_list_files(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value();
        std::string path = args[0].str();
        std::vector<std::string> files;
        try {
            if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    files.push_back(entry.path().generic_string());
                }
            }
        } catch (...) {}

        auto* arr = iris::core::ArrayData::create(files.size(), iris::core::ArrayData::VALUE);
        for (size_t i = 0; i < files.size(); i++) {
            arr->getValData()[i] = iris::core::Value(files[i]);
        }
        return iris::core::Value(arr);
    }

    inline iris::core::Value iris_system_fs_size(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value(0.0);
        std::string path = args[0].str();
        try {
            return iris::core::Value(static_cast<double>(std::filesystem::file_size(path)));
        } catch (const std::exception& e) {
            return iris::core::Value(0.0);
        } catch (...) {
            return iris::core::Value(0.0);
        }
    }

    inline iris::core::Value iris_system_fs_is_directory(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value(false);
        try {
            return iris::core::Value(std::filesystem::is_directory(args[0].str()));
        } catch (...) {
            return iris::core::Value(false);
        }
    }

    inline iris::core::Value iris_system_fs_is_file(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value(false);
        try {
            return iris::core::Value(std::filesystem::is_regular_file(args[0].str()));
        } catch (...) {
            return iris::core::Value(false);
        }
    }

    inline iris::core::Value iris_system_fs_copy(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString())
            return iris::core::Value(false);
        try {
            std::filesystem::copy(args[0].str(), args[1].str(),
                std::filesystem::copy_options::overwrite_existing |
                std::filesystem::copy_options::recursive);
            return iris::core::Value(true);
        } catch (...) {
            return iris::core::Value(false);
        }
    }

    inline iris::core::Value iris_system_fs_rename(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString())
            return iris::core::Value(false);
        try {
            std::filesystem::rename(args[0].str(), args[1].str());
            return iris::core::Value(true);
        } catch (...) {
            return iris::core::Value(false);
        }
    }

    inline iris::core::Value iris_system_fs_read_text(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        try {
            std::ifstream file(args[0].str());
            if (!file.is_open()) return iris::core::Value("");
            std::stringstream buffer;
            buffer << file.rdbuf();
            return iris::core::Value(buffer.str());
        } catch (...) {
            return iris::core::Value("");
        }
    }

    inline iris::core::Value iris_system_fs_write_text(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString())
            return iris::core::Value(false);
        try {
            std::ofstream file(args[0].str());
            if (!file.is_open()) return iris::core::Value(false);
            file << args[1].str();
            return iris::core::Value(true);
        } catch (...) {
            return iris::core::Value(false);
        }
    }

    // --- Process Execution Primitive ---
    inline iris::core::Value iris_system_process_exec(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value();
        std::string cmd = args[0].str();
        std::string result = "";
        int exitCode = -1;
#ifdef _WIN32
        FILE* pipe = _popen(cmd.c_str(), "r");
#else
        FILE* pipe = popen(cmd.c_str(), "r");
#endif
        if (pipe) {
            char buffer[256];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }
#ifdef _WIN32
            exitCode = _pclose(pipe);
#else
            exitCode = pclose(pipe);
#endif
        }

        auto* arr = iris::core::ArrayData::create(2, iris::core::ArrayData::VALUE);
        arr->getValData()[0] = iris::core::Value(exitCode);
        arr->getValData()[1] = iris::core::Value(result);
        return iris::core::Value(arr);
    }

    inline iris::core::Value iris_system_getenv(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        char* val = std::getenv(args[0].str().c_str());
        if (!val) return iris::core::Value("");
        return iris::core::Value(std::string(val));
    }

    inline iris::core::Value iris_system_setenv(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString()) return iris::core::Value(false);
        std::string name = args[0].str();
        std::string val = args[1].str();
#ifdef _WIN32
        std::string envStr = name + "=" + val;
        return iris::core::Value(_putenv(envStr.c_str()) == 0);
#else
        return iris::core::Value(setenv(name.c_str(), val.c_str(), 1) == 0);
#endif
    }

    inline iris::core::Value iris_system_exit(iris::core::Value* args, int argCount) {
        int code = (argCount >= 1 && args[0].isInt()) ? args[0].asInt() : 0;
        std::exit(code);
        return iris::core::Value();
    }

    inline iris::core::Value iris_system_get_type(iris::core::Value* args, int argCount) {
        if (argCount < 1) return iris::core::Value("null");
        const auto& v = args[0];
        if (v.isNull()) return iris::core::Value("null");
        if (v.isInt()) return iris::core::Value("int");
        if (v.isDouble()) return iris::core::Value("double");
        if (v.isBool()) return iris::core::Value("boolean");
        if (v.isString()) return iris::core::Value("string");
        if (v.isArray()) return iris::core::Value("array");
        if (v.isObject()) return iris::core::Value("object");
        return iris::core::Value("native");
    }

    inline iris::core::Value iris_system_get_class_name(iris::core::Value* args, int argCount) {
        if (argCount < 1) return iris::core::Value("null");
        const auto& v = args[0];
        if (v.isObject()) {
            auto* o = static_cast<iris::core::ObjectData*>(v.asPtr());
            return iris::core::Value(getClassNameById(o->classId));
        }
        return iris::core::Value("null");
    }

    // --- Random Primitives ---
    inline iris::core::Value iris_system_random(iris::core::Value* args, int argCount) {
        static std::mt19937 gen(std::random_device{}());
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        return iris::core::Value(dist(gen));
    }

    inline iris::core::Value iris_system_random_int(iris::core::Value* args, int argCount) {
        static std::mt19937 gen(std::random_device{}());
        if (argCount < 2 || !args[0].isInt() || !args[1].isInt())
            return iris::core::Value(0);
        int min = args[0].asInt();
        int max = args[1].asInt();
        std::uniform_int_distribution<int> dist(min, max);
        return iris::core::Value(dist(gen));
    }

    inline iris::core::Value iris_system_uuid(iris::core::Value* args, int argCount) {
        static std::mt19937 gen(std::random_device{}());
        static std::uniform_int_distribution<int> hexDist(0, 15);
        static std::uniform_int_distribution<int> variantDist(8, 11);

        std::stringstream ss;
        ss << std::hex;
        for (int i = 0; i < 8; i++) ss << hexDist(gen);
        ss << "-";
        for (int i = 0; i < 4; i++) ss << hexDist(gen);
        ss << "-4";
        for (int i = 0; i < 3; i++) ss << hexDist(gen);
        ss << "-";
        ss << variantDist(gen);
        for (int i = 0; i < 3; i++) ss << hexDist(gen);
        ss << "-";
        for (int i = 0; i < 12; i++) ss << hexDist(gen);
        return iris::core::Value(ss.str());
    }

    inline iris::core::Value iris_system_string_parse_int(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value(0);
        try {
            return iris::core::Value(std::stoi(args[0].str()));
        } catch (...) {
            return iris::core::Value(0);
        }
    }

    inline iris::core::Value iris_system_string_parse_double(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value(0.0);
        try {
            return iris::core::Value(std::stod(args[0].str()));
        } catch (...) {
            return iris::core::Value(0.0);
        }
    }

    // --- Array sum (native performance primitive) ---
    inline iris::core::Value iris_system_array_sum(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isArray()) return iris::core::Value(0.0);
        auto* arr = static_cast<iris::core::ArrayData*>(args[0].asPtr());
        double sum = 0.0;
        if (arr->elemType == iris::core::ArrayData::INT) {
            const int* data = arr->getIntData();
            for (size_t i = 0; i < arr->length; i++) sum += data[i];
        } else if (arr->elemType == iris::core::ArrayData::DOUBLE) {
            const double* data = arr->getDblData();
            for (size_t i = 0; i < arr->length; i++) sum += data[i];
        } else {
            const iris::core::Value* data = arr->getValData();
            for (size_t i = 0; i < arr->length; i++) sum += data[i].asDouble();
        }
        return iris::core::Value(sum);
    }

}

#endif //SYSTEM_LIB_H
