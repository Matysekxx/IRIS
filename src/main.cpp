
#include <chrono>
#include <iostream>
#include "execute/Executor.h"
#include "parser/Parser.h"
#include "bytecode/Compiler.h"
#include "log/Logger.h"
#include "std/StandardLibrary.h"

#ifdef _WIN32
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif

void setupConsole() {
#ifdef _WIN32
    if (HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: IRIS <file.iris>" << std::endl;
        return 1;
    }

    // Initialize standard library
    iris::std_lib::initialize();

    std::string filePath = argv[1];
    setupConsole();
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    const auto start = std::chrono::high_resolution_clock::now();
    try {
        auto executor = iris::execute::Executor(filePath);
        executor.execute();
    } catch (const std::exception &e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    auto end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double, std::milli> duration = end - start;

    std::cout << "[INFO] Celkový čas (včetně parsování): " << duration.count() << " ms" << std::endl;
    
    // DEBUG: Verify offsets for JIT
    // std::cout << "[DEBUG OFFSET] ArrayData::intData: " << offsetof(iris::core::ArrayData, intData) << std::endl;
    // std::cout << "[DEBUG OFFSET] ObjectData::overflowFields: " << offsetof(iris::core::ObjectData, overflowFields) << std::endl;
    // std::cout << "[DEBUG OFFSET] ObjectData::inlineFields: " << offsetof(iris::core::ObjectData, inlinedFields) << std::endl;
    std::cout << "Done executing, returning 0." << std::endl;
    return 0;
    }
