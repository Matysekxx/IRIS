
#include "Executor.h"

#include "log/Logger.h"
#include "parser/Parser.h"
#include "run/Runner.h"

Executor::Executor(const std::string &filePath) {
    if (!filePath.ends_with(".iris")) throw std::runtime_error("Invalid file extension");
    this->filePath = filePath;
    this->init();
}

void Executor::init() {
    this->logger = std::make_unique<Logger>();
    this->parser = std::make_unique<Parser>(filePath, logger.get());
    this->runner = std::make_unique<Runner>(logger.get());
}

void Executor::execute() {
    parser->parse();
    const std::vector<Instruction> instructions = parser->getInstructions();
    runner->run(instructions);
}

