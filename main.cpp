
#include <chrono>
#include <iostream>
#include "execute/Executor.h"

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

int main(const int argc, char* argv[]) {
    std::string filePath;

    if (argc >= 2) {
        filePath = argv[1];
    } else {
        filePath = R"(C:\Users\chalo\CLionProjects\IRIS\main.iris)";
        std::cout << "Debug info: No arguments provided. Using default file: " << filePath << std::endl;
    }

    setupConsole();
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    const auto start = std::chrono::high_resolution_clock::now();
    try {
        auto executor = Executor(filePath);
        executor.execute();
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    const auto end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double, std::milli> duration = end - start;

    std::cout << "Operace trvala: " << duration.count() << " ms" << std::endl;
    return 0;
}
