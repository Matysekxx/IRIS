//
// Created by chalo on 07.02.2026.
//

#include "Logger.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "LogType.h"

static std::string getCurrentTime() {
    const std::time_t now = std::time(nullptr);
    const std::tm* ltm = std::localtime(&now);
    std::ostringstream oss;
    oss << (1900 + ltm->tm_year) << "-"
        << std::setw(2) << std::setfill('0') << (1 + ltm->tm_mon) << "-"
        << std::setw(2) << std::setfill('0') << ltm->tm_mday;
    return oss.str();
}

Logger::Logger() {
    const std::string fileName = "log_" + getCurrentTime() + ".txt";
    this->file.open(fileName, std::ios::app);
}



void Logger::log(const std::string &str) {
    this->file << str << std::endl;
    std::cout << str << std::endl;
}

void Logger::log(const LogType type, const std::string &str) {
    this->file << toString(type) << str << std::endl;
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

Logger::~Logger() {
    if (this->file.is_open()) {
        this->file.close();
    }
}

