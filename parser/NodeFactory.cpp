#include "NodeFactory.h"
#include <stdexcept>

NodeFactory::NodeFactory() {
    init();
}

void NodeFactory::init() {
    handlers["wait"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        if (index >= tokens.size()) return nullptr;
        return std::make_unique<WaitNode>(parseExpression(tokens, index));
    };

    handlers["mouse"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        if (index >= tokens.size()) return nullptr;
        if (tokens[index] == "{") {
            index++;
            return parseMouseBlock(tokens, index);
        }
        if (tokens[index] == ".") {
            index++;
            if (index >= tokens.size()) return nullptr;
            const std::string& cmd = tokens[index++];
            if (cmd == "click") {
                const std::string& btn = tokens[index++];
                return std::make_unique<HybridClickNode>(btn == "right" ? ClickNode::Right : ClickNode::Left);
            }
        }
        return nullptr;
    };

    handlers["keyboard"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        if (index >= tokens.size()) return nullptr;
        if (tokens[index] == "{") {
            index++;
            return parseKeyboardBlock(tokens, index);
        }
        if (tokens[index] == ".") {
            index++;
            if (index >= tokens.size()) return nullptr;
            const std::string& cmd = tokens[index++];
            if (cmd == "press") {
                const std::string& key = tokens[index++];
                return std::make_unique<HybridPressNode>(key);
            }
        }
        return nullptr;
    };

    handlers["var"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        if (index >= tokens.size()) return nullptr;
        std::string name = tokens[index++];

        if (index >= tokens.size() || tokens[index] != "=") {
            throw std::runtime_error("Expected '=' after variable name '" + name + "'");
        }
        index++;
        return std::make_unique<VarDeclNode>(name, parseExpression(tokens, index));
    };
}

std::unique_ptr<ASTNode> NodeFactory::create(const std::string& command, const std::vector<std::string>& tokens, size_t& index) {
    if (handlers.contains(command)) {
        return handlers[command](tokens, index);
    }
    return nullptr;
}

std::unique_ptr<MouseBlockNode> NodeFactory::parseMouseBlock(const std::vector<std::string>& tokens, size_t& index) {
    auto block = std::make_unique<MouseBlockNode>();
    while (index < tokens.size() && tokens[index] != "}") {
        const std::string& cmd = tokens[index++];
        if (cmd == "click") {
            const std::string& btn = tokens[index++];
            block->actions.push_back(std::make_unique<ClickNode>(btn == "right" ? ClickNode::Right : ClickNode::Left));
        } else if (cmd == "move") {
            auto x = parseExpression(tokens, index);
            if (index < tokens.size() && tokens[index] == ",") index++;
            auto y = parseExpression(tokens, index);
            block->actions.push_back(std::make_unique<MoveNode>(std::move(x), std::move(y)));
        }
    }
    if (index < tokens.size()) index++;
    return block;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseExpression(const std::vector<std::string> &tokens, size_t &index) {
    auto left = parseTerm(tokens, index);

    while (index < tokens.size()) {
        const std::string& op = tokens[index];
        if (op != "+" && op != "-") break;

        index++;
        auto right = parseTerm(tokens, index);
        left = std::make_unique<BinaryOperationNode>(std::move(left), std::move(right), op[0]);
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseTerm(const std::vector<std::string> &tokens, size_t &index) {
    auto left = parseFactor(tokens, index);

    while (index < tokens.size()) {
        const std::string& op = tokens[index];
        if (op != "*" && op != "/") break;

        index++;
        auto right = parseFactor(tokens, index);
        left = std::make_unique<BinaryOperationNode>(std::move(left), std::move(right), op[0]);
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseFactor(const std::vector<std::string> &tokens, size_t &index) {
    if (index >= tokens.size()) throw std::runtime_error("Unexpected end of expression");

    std::string token = tokens[index++];

    if (token == "(") {
        auto expr = parseExpression(tokens, index);
        if (index >= tokens.size() || tokens[index] != ")") {
            throw std::runtime_error("Expected ')'");
        }
        index++;
        return expr;
    }

    if (token.starts_with("\"")) {
        return std::make_unique<StringNode>(token.substr(1, token.size() - 2));
    }

    try {
        size_t pos;
        int val = std::stoi(token, &pos);
        if (pos == token.size()) {
            return std::make_unique<NumberNode>(val);
        }
    } catch (...) {}

    return std::make_unique<VariableNode>(token);
}

std::unique_ptr<KeyboardBlockNode> NodeFactory::parseKeyboardBlock(const std::vector<std::string>& tokens, size_t& index) {
    auto block = std::make_unique<KeyboardBlockNode>();
    while (index < tokens.size() && tokens[index] != "}") {
        const std::string& cmd = tokens[index++];
        if (cmd == "write") {
            block->actions.push_back(std::make_unique<TypeNode>(parseExpression(tokens, index)));
        } else if (cmd == "press") {
            std::string key = tokens[index++];
            block->actions.push_back(std::make_unique<PressNode>(key));
        }
    }
    if (index < tokens.size()) index++;
    return block;
}