#ifndef STANDARD_LIBRARY_H
#define STANDARD_LIBRARY_H

#include "../core/NativeRegistry.h"
#include "Math.h"
#include "NativeStreams.h"
#include "NativeSocket.h"
#include "System.h"

namespace iris::std_lib {
    inline void initialize() {
        auto &registry = iris::core::NativeRegistry::getInstance();

        // IO functions
        registry.registerFunction("IO.FileInputStream", [](iris::core::Value *args, int argCount) {
            if (argCount < 1 || !args[0].isString()) return iris::core::Value();
            return iris::core::Value(new NativeFileInputStream(args[0].str()));
        }, 1);

        registry.registerFunction("IO.FileOutputStream", [](iris::core::Value *args, int argCount) {
            if (argCount < 1 || !args[0].isString()) return iris::core::Value();
            bool append = (argCount >= 2 && args[1].isBool()) ? args[1].asBool() : false;
            return iris::core::Value(new NativeFileOutputStream(args[0].str(), append));
        }, 2);

        // Math functions
        registry.bind("Math.sin", iris_math_sin);
        registry.bind("Math.cos", iris_math_cos);
        registry.bind("Math.sqrt", iris_math_sqrt);
        registry.bind("Math.pow", iris_math_pow);
        registry.bind("Math.abs", iris_math_abs);

        // System core functions
        registry.bind("System.time", iris_system_time);
        registry.registerFunction("System.hash", iris_system_hash, 1);
        registry.registerFunction("System.charToString", iris_system_char_to_string, 1);
        registry.registerFunction("System.assert", [](iris::core::Value *args, int argCount) {
            std::cout << "[DEBUG] System.assert native called: argCount=" << argCount << " args[0]=" << toString(args[0]) << " args[1]=" << (argCount >= 2 ? toString(args[1]) : "N/A") << std::endl;
            if (argCount < 1) return iris::core::Value();
            if (!args[0].asBool()) {
                std::string msg = (argCount >= 2) ? toString(args[1]) : "Assertion failed";
                throw std::runtime_error(msg);
            }
            return iris::core::Value();
        }, 1);

        // Native string operations
        registry.registerFunction("System.stringSubstring", iris_system_string_substring, 3);
        registry.registerFunction("System.stringIndexOf", iris_system_string_index_of, 2);
        registry.registerFunction("System.stringSplit", iris_system_string_split, 2);
        registry.registerFunction("System.stringTrim", iris_system_string_trim, 1);
        registry.registerFunction("System.stringToLower", iris_system_string_to_lower, 1);
        registry.registerFunction("System.stringToUpper", iris_system_string_to_upper, 1);

        // Native file system operations
        registry.registerFunction("System.fsExists", iris_system_fs_exists, 1);
        registry.registerFunction("System.fsDelete", iris_system_fs_delete, 1);
        registry.registerFunction("System.fsCreateDirectory", iris_system_fs_create_dir, 1);
        registry.registerFunction("System.fsListFiles", iris_system_fs_list_files, 1);
        registry.registerFunction("System.fsGetSize", iris_system_fs_size, 1);

        // Native process execution
        registry.registerFunction("System.processExec", iris_system_process_exec, 1);

        // Native environment variables
        registry.registerFunction("System.getenv", iris_system_getenv, 1);
        registry.registerFunction("System.setenv", iris_system_setenv, 2);
        registry.registerFunction("System.exit", iris_system_exit, 1);
        registry.registerFunction("System.getType", iris_system_get_type, 1);
        registry.registerFunction("System.stringParseInt", iris_system_string_parse_int, 1);
        registry.registerFunction("System.stringParseDouble", iris_system_string_parse_double, 1);

        // Native socket networking
        registry.registerFunction("Net.Socket", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(new NativeSocket());
        }, 0);

        registry.registerFunction("Net.ServerSocket", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(new NativeServerSocket());
        }, 0);
    }
}

#endif //STANDARD_LIBRARY_H
