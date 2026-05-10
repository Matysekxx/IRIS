#include "Executor.h"

#include "../log/Logger.h"
#include "../parser/Parser.h"
#include "../device/Win32Driver.h"
#include "../bytecode/Compiler.h"
#include "../bytecode/VM.h"
#include <chrono>
#include <iostream>

using namespace iris::execute;
using namespace iris::parser;
using namespace iris::bytecode;
using namespace iris::device;

Executor::Executor(const std::string &filePath) {
    if (!filePath.ends_with(".iris"))
        throw std::runtime_error("Invalid file extension");
    this->filePath = filePath;
    this->init();
}

void Executor::init() {
    this->logger = std::make_unique<iris::log::Logger>();
    this->driver = std::make_unique<Win32Driver>();
    this->parser = std::make_unique<Parser>(filePath, logger.get());
}

void Executor::execute() {
    parser->parse();
    if (const auto program = parser->getProgram()) {
        try {
            Compiler compiler;
            Chunk bytecode = compiler.compile(program);

            VM vm;
            const auto start = std::chrono::high_resolution_clock::now();
            vm.execute(bytecode, driver.get(), logger.get(), &compiler.getFunctions(), &compiler.getClasses());
            const auto end = std::chrono::high_resolution_clock::now();

            const std::chrono::duration<double, std::milli> duration = end - start;
            std::cout << "[INFO] Běh VM trval: " << duration.count() << " ms" << std::endl;
        } catch (const std::exception &e) {
            logger->error(std::string("Execution error: ") + e.what());
        }
    } else {
        logger->error("Parsing failed");
    }
}
