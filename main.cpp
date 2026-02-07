
#include "Executor.h"

int main(const int argc, char* argv[]) {
    if (argc < 2) {
        return 1;
    }
    const std::string filePath = argv[1];
    auto executor = Executor(filePath);
    executor.execute();
    return 0;
}
