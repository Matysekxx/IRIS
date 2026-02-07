

#ifndef RUNNER_H
#define RUNNER_H
#include <map>
#include <memory>
#include <vector>

#include "action/Action.h"
#include "../parser/Instruction.h"
#include "../log/Logger.h"

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
