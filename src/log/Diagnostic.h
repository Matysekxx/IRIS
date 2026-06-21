#ifndef DIAGNOSTIC_H
#define DIAGNOSTIC_H

#include <iostream>
#include <string>
#include "frontend/ASTNode.h"

namespace iris::log {
    /**
     * @brief Helper to format and print compiler and runtime errors.
     */
    class Diagnostic {
    public:
        static void error(const iris::node::SourceLocation& loc, const std::string& message) {
            std::cerr << "\x1b[1;31merror\x1b[0m: ";
            if (!loc.file.empty()) {
                std::cerr << loc.file << ":" << loc.line << ":" << loc.column << ": ";
            }
            std::cerr << message << std::endl;
            // In a real compiler, we might want to print the actual line of code here.
        }

        static void warn(const iris::node::SourceLocation& loc, const std::string& message) {
            std::cerr << "\x1b[1;33mwarning\x1b[0m: ";
            if (!loc.file.empty()) {
                std::cerr << loc.file << ":" << loc.line << ":" << loc.column << ": ";
            }
            std::cerr << message << std::endl;
        }
    };
}

#endif //DIAGNOSTIC_H
