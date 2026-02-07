
#ifndef EXECUTE_H
#define EXECUTE_H
#include <memory>
#include <string>


class Runner;
class Logger;
class Parser;

class Executor {
private:
    std::string filePath;
    std::unique_ptr<Logger> logger;
    std::unique_ptr<Parser> parser;
    std::unique_ptr<Runner> runner;
    public:
    explicit Executor(const std::string &filePath);

    void init();

    void execute() const;

};



#endif //EXECUTE_H
