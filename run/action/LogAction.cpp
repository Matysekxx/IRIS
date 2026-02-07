#include "LogAction.h"
#include <variant>

LogAction::LogAction(Logger *logger) : logger(logger) {}

void LogAction::run(Instruction instruction) {
    if (instruction.op != OpCode::LOG) return;

    if (std::holds_alternative<std::string>(instruction.arg1)) {
        logger->info(std::get<std::string>(instruction.arg1));
    }
}