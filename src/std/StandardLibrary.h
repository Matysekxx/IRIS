#ifndef STANDARD_LIBRARY_H
#define STANDARD_LIBRARY_H

#include "core/NativeRegistry.h"
#include "core/SIMDKernels.h"
#include "Math.h"
#include "NativeStreams.h"
#include "NativeSocket.h"
#include "System.h"
#include "DateTime.h"
#include "Regex.h"
#include "Base64.h"

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
        registry.bind("Math.tan", iris_math_tan);
        registry.bind("Math.sqrt", iris_math_sqrt);
        registry.bind("Math.pow", iris_math_pow);
        registry.bind("Math.abs", iris_math_abs);
        registry.bind("Math.floor", iris_math_floor);
        registry.bind("Math.ceil", iris_math_ceil);
        registry.bind("Math.round", iris_math_round);
        registry.bind("Math.log", iris_math_log);
        registry.bind("Math.log10", iris_math_log10);
        registry.bind("Math.exp", iris_math_exp);
        registry.bind("Math.min", iris_math_min);
        registry.bind("Math.max", iris_math_max);
        registry.bind("Math.atan2", iris_math_atan2);
        registry.bind("Math.asin", iris_math_asin);
        registry.bind("Math.acos", iris_math_acos);
        registry.bind("Math.atan", iris_math_atan);
        registry.bind("Math.sinh", iris_math_sinh);
        registry.bind("Math.cosh", iris_math_cosh);
        registry.bind("Math.tanh", iris_math_tanh);
        registry.bind("Math.degrees", iris_math_degrees);
        registry.bind("Math.radians", iris_math_radians);
        registry.bind("Math.cbrt", iris_math_cbrt);
        registry.bind("Math.hypot", iris_math_hypot);

        // System core functions
        registry.bind("System.time", iris_system_time);
        registry.registerFunction("System.hash", iris_system_hash, 1);
        registry.registerFunction("System.charToString", iris_system_char_to_string, 1);
        registry.registerFunction("System.assert", [](iris::core::Value *args, int argCount) {
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
        registry.registerFunction("System.stringReplace", iris_system_string_replace, 3);
        registry.registerFunction("System.stringStartsWith", iris_system_string_starts_with, 2);
        registry.registerFunction("System.stringEndsWith", iris_system_string_ends_with, 2);
        registry.registerFunction("System.stringContains", iris_system_string_contains, 2);
        registry.registerFunction("System.stringReverse", iris_system_string_reverse, 1);
        registry.registerFunction("System.stringRepeat", iris_system_string_repeat, 2);
        registry.registerFunction("System.stringPadLeft", iris_system_string_pad_left, 3);
        registry.registerFunction("System.stringPadRight", iris_system_string_pad_right, 3);

        // Native array SIMD operations
        registry.registerFunction("System.arraySum", [](iris::core::Value *args, int argCount) {
            if (argCount < 1 || !args[0].isArray()) return iris::core::Value(0.0);
            auto* arr = static_cast<iris::core::ArrayData*>(args[0].asPtr());
            if (arr->elemType == iris::core::ArrayData::DOUBLE) {
                return iris::core::Value(sum_array_double_simd(arr->getDblData(), arr->length));
            } else if (arr->elemType == iris::core::ArrayData::INT) {
                return iris::core::Value(static_cast<double>(sum_array_int_simd(arr->getIntData(), arr->length)));
            } else {
                double sum = 0;
                iris::core::Value* valData = arr->getValData();
                for (size_t i = 0; i < arr->length; ++i) {
                    sum += iris::core::toDouble(valData[i]);
                }
                return iris::core::Value(sum);
            }
        }, 1);

        // Native file system operations
        registry.registerFunction("System.fsExists", iris_system_fs_exists, 1);
        registry.registerFunction("System.fsDelete", iris_system_fs_delete, 1);
        registry.registerFunction("System.fsCreateDirectory", iris_system_fs_create_dir, 1);
        registry.registerFunction("System.fsListFiles", iris_system_fs_list_files, 1);
        registry.registerFunction("System.fsGetSize", iris_system_fs_size, 1);
        registry.registerFunction("System.fsIsDirectory", iris_system_fs_is_directory, 1);
        registry.registerFunction("System.fsIsFile", iris_system_fs_is_file, 1);
        registry.registerFunction("System.fsCopy", iris_system_fs_copy, 2);
        registry.registerFunction("System.fsRename", iris_system_fs_rename, 2);
        registry.registerFunction("System.fsReadText", iris_system_fs_read_text, 1);
        registry.registerFunction("System.fsWriteText", iris_system_fs_write_text, 2);

        // Native process execution
        registry.registerFunction("System.processExec", iris_system_process_exec, 1);

        // Native environment variables
        registry.registerFunction("System.getenv", iris_system_getenv, 1);
        registry.registerFunction("System.setenv", iris_system_setenv, 2);
        registry.registerFunction("System.exit", iris_system_exit, 1);
        registry.registerFunction("System.getType", iris_system_get_type, 1);
        registry.registerFunction("System.getClassName", iris_system_get_class_name, 1);
        registry.registerFunction("System.stringParseInt", iris_system_string_parse_int, 1);
        registry.registerFunction("System.stringParseDouble", iris_system_string_parse_double, 1);
        registry.registerFunction("System.random", iris_system_random, 0);
        registry.registerFunction("System.randomInt", iris_system_random_int, 2);
        registry.registerFunction("System.uuid", iris_system_uuid, 0);

        // Native socket networking
        registry.registerFunction("Net.Socket", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(new NativeSocket());
        }, 0);

        registry.registerFunction("Net.ServerSocket", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(new NativeServerSocket());
        }, 0);

        // DateTime functions
        registry.registerFunction("DateTime.now", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(iris_date_time_now());
        }, 0);

        registry.registerFunction("DateTime.utcNow", [](iris::core::Value *args, int argCount) {
            return iris::core::Value(iris_date_time_utc_now());
        }, 0);

        registry.registerFunction("DateTime.year", iris_date_time_year, 1);
        registry.registerFunction("DateTime.month", iris_date_time_month, 1);
        registry.registerFunction("DateTime.day", iris_date_time_day, 1);
        registry.registerFunction("DateTime.hour", iris_date_time_hour, 1);
        registry.registerFunction("DateTime.minute", iris_date_time_minute, 1);
        registry.registerFunction("DateTime.second", iris_date_time_second, 1);
        registry.registerFunction("DateTime.format", iris_date_time_format, 2);

        // Regex functions
        registry.registerFunction("Regex.match", iris_regex_match, 2);
        registry.registerFunction("Regex.find", iris_regex_find, 2);
        registry.registerFunction("Regex.replace", iris_regex_replace, 3);
        registry.registerFunction("Regex.split", iris_regex_split, 2);

        // Codec functions
        registry.registerFunction("Base64.encode", iris_base64_encode, 1);
        registry.registerFunction("Base64.decode", iris_base64_decode, 1);
        registry.registerFunction("Hex.encode", iris_hex_encode, 1);
        registry.registerFunction("Hex.decode", iris_hex_decode, 1);
    }
}

#endif //STANDARD_LIBRARY_H
