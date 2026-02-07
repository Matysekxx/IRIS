//
// Created by chalo on 07.02.2026.
//

#ifndef LOGGER_H
#define LOGGER_H

#include <fstream>
#include <iosfwd>


enum class LogType;

class Logger {
    std::ofstream file;
    public:
    Logger();
    ~Logger();

    void log(const std::string &str);
    void log(LogType type, const std::string &str);

    void info(const std::string &str);

    void warn(const std::string &str);

    void error(const std::string &str);

    void debug(const std::string &str);
};



#endif //LOGGER_H
