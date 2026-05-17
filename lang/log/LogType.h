
#ifndef LOGTYPE_H
#define LOGTYPE_H
#include <string>

namespace iris::log {
    enum class LogType { INFO, WARN, ERROR, DEBUG };

    using namespace std::string_literals;

    inline std::string toString(const LogType type) {
        switch (type) {
            case LogType::INFO:
                return "\033[32m[INFO]\033[0m ";
            case LogType::WARN:
                return "\033[33m[WARN]\033[0m ";
            case LogType::ERROR:
                return "\033[31m[ERROR]\033[0m ";
            case LogType::DEBUG:
                return "\033[34m[DEBUG]\033[0m ";
            default:
                return "UNKNOWN ";
        }
    }
}

#endif //LOGTYPE_H
