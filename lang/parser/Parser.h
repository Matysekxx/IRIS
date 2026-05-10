#ifndef PARSER_H
#define PARSER_H
#include <fstream>
#include <iosfwd>
#include <vector>
#include <memory>
#include <unordered_set>
#include <string>
#include "../node/ASTNode.h"
#include "NodeFactory.h"
#include "../log/Logger.h"

namespace iris::parser {
    class Parser {
        private:
        std::ifstream file;
        iris::log::Logger* logger;
        std::string filePath;
        std::string sourceCode;
        std::unordered_set<std::string>* sharedImports;
        std::unique_ptr<std::unordered_set<std::string>> rootImports;

        std::vector<std::string_view> tokens;
        size_t currentToken = 0;

        NodeFactory factory;

        void tokenize(std::string_view source);

        std::unique_ptr<iris::node::ProgramNode> parseProgram();
        std::unique_ptr<iris::node::ASTNode> parseStatement();

        std::unique_ptr<iris::node::ProgramNode> program;

        public:
        Parser(const std::string& filePath, iris::log::Logger* logger, std::unordered_set<std::string>* sharedImports = nullptr);

        void parse();

        [[nodiscard]] iris::node::ProgramNode* getProgram() const { return program.get(); }

        ~Parser();

    };
}

#endif //PARSER_H
