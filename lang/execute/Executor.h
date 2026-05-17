
#ifndef EXECUTE_H
#define EXECUTE_H
#include <string>

#include "../parser/Parser.h"
#include "../device/IDeviceDriver.h"


namespace iris::execute {
    class Executor {
    private:
        std::string filePath;
        std::unique_ptr<iris::log::Logger> logger;
        std::unique_ptr<iris::device::IDeviceDriver> driver;
        std::unique_ptr<iris::parser::Parser> parser;

    public:
        explicit Executor(const std::string &filePath);

        void init();

        void execute();
    };
}


#endif //EXECUTE_H
