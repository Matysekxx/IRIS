

#include "Executor.h"

#include "../log/Logger.h"
#include "../parser/Parser.h"
#include "../device/Win32Driver.h"
#include "RuntimeContext.h"

Executor::Executor(const std::string &filePath) {
    if (!filePath.ends_with(".iris")) throw std::runtime_error("Invalid file extension");
    this->filePath = filePath;
    this->init();
}

void Executor::init() {
    this->logger = std::make_unique<Logger>();
    this->driver = std::make_unique<Win32Driver>();
    this->parser = std::make_unique<Parser>(filePath, logger.get());
}

void Executor::execute() {
    parser->parse();
    if (const auto program = parser->getProgram()) {

        auto ctx = RuntimeContext();
        ctx.driver = driver.get();
        ctx.logger = logger.get();

        program->execute(&ctx);
    }

}
