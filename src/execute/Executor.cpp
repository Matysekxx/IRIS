#include "Executor.h"

#include "log/Logger.h"
#include "log/Diagnostic.h"
#include "frontend/Parser.h"
#include "device/Win32Driver.h"
#include "ir/Compiler.h"
#include "vm/VM.h"
#include <chrono>
#include <iostream>

#include "std/StandardLibrary.h"

using namespace iris::execute;
using namespace iris::parser;
using namespace iris::bytecode;
using namespace iris::device;
using namespace iris::log;

Executor::Executor(const std::string &filePath) {
    if (!filePath.ends_with(".iris"))
        throw std::runtime_error("Invalid file extension");
    this->filePath = filePath;
    this->init();
}

void Executor::init() {
    iris::std_lib::initialize();
    this->logger = std::make_unique<iris::log::Logger>();
    this->driver = std::make_unique<Win32Driver>();
    this->parser = std::make_unique<Parser>(filePath, logger.get());
}

void Executor::execute() {
    try {
        parser->parse();
        if (const auto program = parser->getProgram()) {
            Compiler compiler;
            Chunk bytecode = compiler.compile(program);

            auto &funcs = compiler.getFunctions();
            for (size_t fi = 0; fi < funcs.size(); ++fi) {
                if (true) {
                    // std::cout << "[DISASM] Function: " << funcs[fi].name << std::endl;
                    auto &code = funcs[fi].chunk.code;
                    for (size_t ci = 0; ci < code.size(); ++ci) {
                        uint32_t instr = code[ci];
                        int op = (int)decodeOp(instr);
                        int a = decodeA(instr);
                        int b = decodeB(instr);
                        int c = decodeC(instr);
                        int bx = decodeBx(instr);
                        int sbx = decodeSBx(instr);
                        // std::cout << "  [" << ci << "] OP=" << op << " A=" << a << " B=" << b << " C=" << c << " Bx=" << bx << " sBx=" << sbx << std::endl;
                    }
                }
            }

            VM vm;
            const auto start = std::chrono::high_resolution_clock::now();
            vm.execute(bytecode, driver.get(), logger.get(),
                       &compiler.getFunctions(),
                       &compiler.getClasses(),
                       &iris::core::NativeRegistry::getInstance().getFunctions());
            const auto end = std::chrono::high_resolution_clock::now();

            const std::chrono::duration<double, std::milli> duration = end - start;
            std::cout << "[INFO] Běh VM trval: " << duration.count() << " ms" << std::endl;
        } else {
            logger->error("Parsing failed");
        }
    } catch (const CompileError &e) {
        Diagnostic::error(e.location, e.what());
        throw; // re-throw so main() can report failure exit code
    } catch (const RuntimeError &e) {
        std::cerr << "IRIS Runtime Error: " << e.what() << std::endl;
        throw;
    } catch (const std::exception &e) {
        std::cerr << "IRIS Execution Error: " << e.what() << std::endl;
        throw;
    }
}
