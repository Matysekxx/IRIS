
#include "Logger.h"

#include <iomanip>
#include <iostream>

#include "LogType.h"

void Logger::log(const std::string &str) {
    std::cout << str << std::endl;
}

void Logger::log(const LogType type, const std::string &str) {
    std::cout << toString(type) << str << std::endl;
}

void Logger::info(const std::string &str) {
    this->log(LogType::INFO, str);
}

void Logger::warn(const std::string &str) {
    this->log(LogType::WARN, str);
}

void Logger::error(const std::string &str) {
    this->log(LogType::ERROR, str);
}

void Logger::debug(const std::string &str) {
    this->log(LogType::DEBUG, str);
}

