
#ifndef EXECUTE_H
#define EXECUTE_H
#include <string>

#include "parser/Parser.h"
#include "run/Runner.h"


class Executor {
private:
    std::string filePath;
    std::unique_ptr<Logger> logger;
    std::unique_ptr<Parser> parser;
    std::unique_ptr<Runner> runner;
    public:
    explicit Executor(const std::string &filePath);

    void init();

    void execute();

};



#endif //EXECUTE_H
