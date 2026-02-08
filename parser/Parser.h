

#ifndef PARSER_H
#define PARSER_H
#include <fstream>
#include <iosfwd>
#include <vector>
#include <memory>
#include "../LTSNode.h"
#include "../log/Logger.h"

class Parser {
    private:
    std::ifstream file;
    Logger* logger;

    std::vector<std::string> tokens;
    size_t currentToken = 0;

    void tokenize(const std::string &source);

    std::unique_ptr<ProgramNode> parseProgram();
    std::unique_ptr<ASTNode> parseStatement();
    std::unique_ptr<MouseBlockNode> parseMouseBlock();
    std::unique_ptr<KeyboardBlockNode> parseKeyboardBlock();

    std::unique_ptr<ProgramNode> program;

    public:
    Parser(const std::string& filePath, Logger* logger);

    void parse();

    [[nodiscard]] ProgramNode* getProgram() const { return program.get(); }

    ~Parser();

};



#endif //PARSER_H
