//
// Created by chalo on 07.02.2026.
//

#ifndef PARSER_H
#define PARSER_H
#include <fstream>
#include <iosfwd>
#include <vector>
#include "Instruction.h"
#include "InstructionFactory.h"


class Logger;

class Parser {
    private:
    std::ifstream file;
    Logger* logger;
    InstructionFactory factory;

    std::vector<Instruction> instructions;
    public:
    Parser(std::string filePath, Logger* logger);

    void parse();

    void parseLine(std::string line);

    [[nodiscard]] const std::vector<Instruction>& getInstructions() const { return instructions; }

    ~Parser();

};



#endif //PARSER_H
