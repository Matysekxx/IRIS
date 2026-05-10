#ifndef NODEFACTORY_H
#define NODEFACTORY_H

#include <functional>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

#include "../node/ASTNode.h"

namespace iris::parser {
    class NodeFactory {
    private:
        using Handler = std::function<std::unique_ptr<iris::node::ASTNode>(const std::vector<std::string_view>&, size_t&)>;
        std::unordered_map<std::string, Handler> handlers;

        void init();

        std::vector<std::unique_ptr<iris::node::ASTNode>> parseBlock(const std::vector<std::string_view>& tokens, size_t& index);

        std::unique_ptr<iris::node::ExpressionNode> parseExpression(const std::vector<std::string_view>& tokens, size_t& index);

        std::unique_ptr<iris::node::ExpressionNode> parseLogic(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ExpressionNode> parseBitwise(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ExpressionNode> parseComparison(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ExpressionNode> parseShift(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ExpressionNode> parseAdditive(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ExpressionNode> parseTerm(const std::vector<std::string_view>& tokens, size_t& index);

        std::unique_ptr<iris::node::ExpressionNode> parseUnary(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ExpressionNode> parseFactor(const std::vector<std::string_view>& tokens, size_t& index);

        std::unique_ptr<iris::node::WaitNode> parseWaitNode(const std::vector<std::string_view>& tokens, size_t& index);
        std::unique_ptr<iris::node::VarDeclNode> parseVarDeclNode(const std::vector<std::string_view> &tokens, size_t &index, bool isMutable);
        std::unique_ptr<iris::node::AssignmentNode> parseAssigmentNode(const std::string& cmd, const std::vector<std::string_view> &tokens, size_t &index);
        std::unique_ptr<iris::node::ASTNode> parseRepeatBlock(const std::vector<std::string_view> &tokens, size_t &index);
        std::unique_ptr<iris::node::ASTNode> parseWhileBlock(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ASTNode> parseForBlock(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ASTNode> parseIfBlock(const std::vector<std::string_view> &tokens, size_t &index);
        std::unique_ptr<iris::node::ASTNode> parseSwitchBlock(const std::vector<std::string_view> &tokens, size_t &index);
        std::unique_ptr<iris::node::ASTNode> parseEnumDecl(const std::vector<std::string_view> &tokens, size_t &index);
        std::unique_ptr<iris::node::ASTNode> parsePrintNode(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ASTNode> parseFunctionDecl(const std::vector<std::string_view> &tokens, size_t &index, bool isAbstract = false);

        std::unique_ptr<iris::node::ASTNode> parseReturnNode(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ASTNode> parseClassDecl(const std::vector<std::string_view> &tokens, size_t &index, bool isAbstract = false);

        std::unique_ptr<iris::node::ASTNode> parseThrowNode(const std::vector<std::string_view> &tokens, size_t &index);

        std::unique_ptr<iris::node::ASTNode> parseIndexAssign(const std::string& objName, const std::vector<std::string_view> &tokens, size_t &index);
        std::unique_ptr<iris::node::ASTNode> parseTryCatch(const std::vector<std::string_view> &tokens, size_t &index);
        std::unique_ptr<iris::node::ASTNode> parseImportNative(const std::vector<std::string_view> &tokens, size_t &index);
    public:
        NodeFactory();
        std::unique_ptr<iris::node::ASTNode> create(const std::string &command, const std::vector<std::string_view> &tokens, size_t &index);
    };
}

#endif //NODEFACTORY_H
