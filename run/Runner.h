//
// Created by chalo on 07.02.2026.
//

#ifndef RUNNER_H
#define RUNNER_H
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "action/Action.h"
#include "../parser/Instruction.h"


class Logger;

class Runner {
private:
    Logger* log;
    void runInstruction(const Instruction &instruction);
    void init();
    std::map<OpCode, std::unique_ptr<Action>> actions;
public:
    explicit Runner(Logger* log);

    void run(const std::vector<Instruction> &instructions);
};



#endif //RUNNER_H
