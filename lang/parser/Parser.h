

#ifndef PARSER_H
#define PARSER_H
#include <fstream>
#include <iosfwd>
#include <vector>
#include <memory>
#include "../node/ASTNode.h"
#include "NodeFactory.h"
#include "../log/Logger.h"

class Parser {
    private:
    std::ifstream file;
    Logger* logger;
    std::string sourceCode;

    std::vector<std::string_view> tokens;
    size_t currentToken = 0;

    NodeFactory factory;

    void tokenize(std::string_view source);

    std::unique_ptr<ProgramNode> parseProgram();
    std::unique_ptr<ASTNode> parseStatement();

    std::unique_ptr<ProgramNode> program;

    public:
    Parser(const std::string& filePath, Logger* logger);

    void parse();

    [[nodiscard]] ProgramNode* getProgram() const { return program.get(); }

    ~Parser();

};



#endif //PARSER_H
