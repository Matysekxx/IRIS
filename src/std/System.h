#ifndef SYSTEM_LIB_H
#define SYSTEM_LIB_H

#include "../core/Native.h"
#include "../core/Value.h"
#include "../core/ArrayData.h"
#include <chrono>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace iris::std_lib {

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
        else if (v.isString()) h = static_cast<int>(std::hash<std::string>{}(v.str()));
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

        auto* arr = new iris::core::ArrayData(tokens.size(), iris::core::ArrayData::VALUE);
        for (size_t i = 0; i < tokens.size(); i++) {
            arr->valData[i] = iris::core::Value(tokens[i]);
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

    inline iris::core::Value iris_system_string_to_lower(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        std::string s = args[0].str();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return iris::core::Value(s);
    }

    inline iris::core::Value iris_system_string_to_upper(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        std::string s = args[0].str();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        return iris::core::Value(s);
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

        auto* arr = new iris::core::ArrayData(files.size(), iris::core::ArrayData::VALUE);
        for (size_t i = 0; i < files.size(); i++) {
            arr->valData[i] = iris::core::Value(files[i]);
        }
        return iris::core::Value(arr);
    }

    inline iris::core::Value iris_system_fs_size(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value(0);
        std::string path = args[0].str();
        try {
            return iris::core::Value(static_cast<double>(std::filesystem::file_size(path)));
        } catch (...) {
            return iris::core::Value(0.0);
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

        auto* arr = new iris::core::ArrayData(2, iris::core::ArrayData::VALUE);
        arr->valData[0] = iris::core::Value(exitCode);
        arr->valData[1] = iris::core::Value(result);
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

}

#endif //SYSTEM_LIB_H
