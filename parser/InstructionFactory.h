//
// Created by chalo on 07.02.2026.
//

#ifndef INSTRUCTIONFACTORY_H
#define INSTRUCTIONFACTORY_H
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <variant>

#include "Instruction.h"


enum class OpCode : unsigned char;

class InstructionFactory {
    private:
    std::map<std::string, std::function<Instruction(const std::vector<std::string>&)>> factory;
    void init();
    public:
    InstructionFactory();
    ~InstructionFactory();
    Instruction create(const std::string &command, const std::vector<std::string> &args);

};



#endif //INSTRUCTIONFACTORY_H
