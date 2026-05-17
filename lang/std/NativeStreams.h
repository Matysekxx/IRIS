#ifndef NATIVE_STREAMS_H
#define NATIVE_STREAMS_H

#include "../core/Native.h"
#include "../core/Value.h"
#include <fstream>
#include <string>
#include <vector>

namespace iris::std_lib {
    /**
     * @brief Low-level File Input Stream.
     */
    class NativeFileInputStream : public iris::core::NativeObject {
        std::ifstream stream;

    public:
        explicit NativeFileInputStream(const std::string &path) : stream(path, std::ios::binary) {
        }

        iris::core::Value callMethod(const std::string &name, iris::core::Value *args, int argCount) override {
            if (name == "read") {
                if (!stream.is_open() || stream.eof()) return iris::core::Value(-1);
                return iris::core::Value(static_cast<int>(stream.get()));
            }
            if (name == "close") {
                stream.close();
                return iris::core::Value();
            }
            if (name == "isOpen") {
                return iris::core::Value(stream.is_open());
            }
            return iris::core::NativeObject::callMethod(name, args, argCount);
        }

        std::string toString() const override { return "FileInputStream"; }
    };

    /**
     * @brief Low-level File Output Stream.
     */
    class NativeFileOutputStream : public iris::core::NativeObject {
        std::ofstream stream;

    public:
        NativeFileOutputStream(const std::string &path, bool append)
            : stream(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc)) {
        }

        iris::core::Value callMethod(const std::string &name, iris::core::Value *args, int argCount) override {
            if (name == "write") {
                if (argCount < 1 || !stream.is_open()) return iris::core::Value(false);
                stream.put(static_cast<char>(args[0].asInt));
                return iris::core::Value(true);
            }
            if (name == "flush") {
                stream.flush();
                return iris::core::Value();
            }
            if (name == "close") {
                stream.close();
                return iris::core::Value();
            }
            return iris::core::NativeObject::callMethod(name, args, argCount);
        }

        std::string toString() const override { return "FileOutputStream"; }
    };
}

#endif //NATIVE_STREAMS_H
