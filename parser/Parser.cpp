//
// Created by chalo on 07.02.2026.
//

#include "Parser.h"

#include <fstream>
#include <stdexcept>
#include <sstream>

#include "Instruction.h"
#include "../log/Logger.h"

Parser::Parser(std::string filePath, Logger* logger) {
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
    if (line.empty()) return;
    std::istringstream iss(line);
    std::string command;
    iss >> command;
    
    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg) {
        args.push_back(arg);
    }
    
    try {
        Instruction inst = factory.create(command, args);
        instructions.push_back(inst);
    } catch (const std::exception& e) {
        logger->error(e.what());
    }
}



Parser::~Parser() {
    if (this->file.is_open()) {
        this->file.close();
    }
}
