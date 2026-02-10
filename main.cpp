
#include <iostream>
#include "execute/Executor.h"

int main(const int argc, char* argv[]) {
    std::string filePath;

    if (argc >= 2) {
        filePath = argv[1];
    } else {
        filePath = R"(C:\Users\chalo\CLionProjects\IRIS\main.iris)";
        std::cout << "Debug info: No arguments provided. Using default file: " << filePath << std::endl;
    }

    try {
        auto executor = Executor(filePath);
        executor.execute();
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
