

#include "Executor.h"

#include "../log/Logger.h"
#include "../parser/Parser.h"

Executor::Executor(const std::string &filePath) {
    if (!filePath.ends_with(".iris")) throw std::runtime_error("Invalid file extension");
    this->filePath = filePath;
    this->init();
}

void Executor::init() {
    this->logger = std::make_unique<Logger>();
    this->parser = std::make_unique<Parser>(filePath, logger.get());
}

void Executor::execute() {
    parser->parse();
    if (const auto program = parser->getProgram()) {
        program->execute(logger.get());
    }

}
