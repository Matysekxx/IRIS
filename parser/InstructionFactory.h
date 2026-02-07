

#ifndef INSTRUCTIONFACTORY_H
#define INSTRUCTIONFACTORY_H
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Instruction.h"



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
