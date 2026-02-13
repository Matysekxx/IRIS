#ifndef NODEFACTORY_H
#define NODEFACTORY_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../node/ASTNode.h"

class NodeFactory {
private:
    using Handler = std::function<std::unique_ptr<ASTNode>(const std::vector<std::string>&, size_t&)>;
    std::map<std::string, Handler> handlers;

    std::map<std::string, Handler> mouseHandlers;
    std::map<std::string, Handler> keyboardHandlers;


    void init();

    std::unique_ptr<MouseBlockNode> parseMouseBlock(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<KeyboardBlockNode> parseKeyboardBlock(const std::vector<std::string>& tokens, size_t& index);

    std::unique_ptr<ExpressionNode> parseExpression(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<ExpressionNode> parseTerm(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<ExpressionNode> parseFactor(const std::vector<std::string>& tokens, size_t& index);

    std::unique_ptr<WaitNode> parseWaitNode(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<MoveNode> parseMoveNode(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<ClickNode> parseClickNode(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<ShiftNode> parseShiftNode(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<WriteNode> parseWriteNode(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<PressNode> parsePressNode(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<VarDeclNode> parseVarDeclNode(const std::vector<std::string> &tokens, size_t &index, bool isMutable);
    std::unique_ptr<AssignmentNode> parseAssigmentNode(const std::string& cmd, const std::vector<std::string> &tokens, size_t &index);
    std::unique_ptr<ASTNode> parseRepeatBlock(const std::vector<std::string> &tokens, size_t &index);
    std::unique_ptr<ASTNode> parseWhileBlock(const std::vector<std::string> &tokens, size_t &index);
    std::unique_ptr<ASTNode> parseLogNode(const std::vector<std::string> & tokens, size_t index);

public:
    NodeFactory();
    std::unique_ptr<ASTNode> create(const std::string& command, const std::vector<std::string>& tokens, size_t& index);
};

#endif //NODEFACTORY_H