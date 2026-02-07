
#include "Parser.h"

#include <fstream>
#include <stdexcept>
#include <sstream>

#include "../log/Logger.h"

Parser::Parser(const std::string& filePath, Logger* logger) {
    this->logger = logger;
    this->file = std::ifstream(filePath);
    if (!this->file.is_open()) {
        throw std::runtime_error("File could not be opened");
    }
}

void Parser::parse() {
    for (std::string line; std::getline(file, line);) {
        parseLine(line);
    }
}

void Parser::parseLine(std::string line) {
    auto trim = [](std::string& s) {
        const auto first = s.find_first_not_of(" \t");
        if (first == std::string::npos) {
            s.clear();
            return;
        }
        const auto last = s.find_last_not_of(" \t");
        s = s.substr(first, (last - first + 1));
    };

    if (const size_t pos = line.find("//");
        pos != std::string::npos) {
        line.erase(pos);
    }

    const size_t openParen = line.find('(');
    const size_t closeParen = line.find(')');
    if (openParen == std::string::npos || closeParen == std::string::npos || closeParen < openParen) return;

    std::string command = line.substr(0, openParen);
    trim(command);
    if (command.empty()) return;

    std::vector<std::string> args;
    std::stringstream ss(line.substr(openParen + 1, closeParen - openParen - 1));
    std::string segment;
    
    while(std::getline(ss, segment, ',')) {
        trim(segment);
        args.push_back(segment);
    }

    try {
        instructions.push_back(factory.create(command, args));
    } catch (const std::exception& e) {
        logger->error(e.what());
    }
}



Parser::~Parser() {
    if (this->file.is_open()) {
        this->file.close();
    }
}
