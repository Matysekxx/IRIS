#include "NodeFactory.h"
#include <stdexcept>

NodeFactory::NodeFactory() {
    init();
}

std::unique_ptr<WaitNode> NodeFactory::parseWaitNode(const std::vector<std::string> &tokens, size_t &index) {
    if (index >= tokens.size()) return nullptr;
    return std::make_unique<WaitNode>(parseExpression(tokens, index));
}

std::unique_ptr<MoveNode> NodeFactory::parseMoveNode(const std::vector<std::string> &tokens, size_t &index) {
    auto x = parseExpression(tokens, index);
    if (index < tokens.size() && tokens[index] == ",") index++;
    auto y = parseExpression(tokens, index);
    return std::make_unique<MoveNode>(std::move(x), std::move(y));
}

std::unique_ptr<ClickNode> NodeFactory::parseClickNode(const std::vector<std::string> &tokens, size_t &index) {
    const std::string& btn = tokens[index++];
    return std::make_unique<ClickNode>(btn == "right" ? ClickNode::Right : ClickNode::Left);
}

std::unique_ptr<ShiftNode> NodeFactory::parseShiftNode(const std::vector<std::string> &tokens, size_t &index) {
    auto dx = parseExpression(tokens, index);
    if (index < tokens.size() && tokens[index] == ",") index++;
    auto dy = parseExpression(tokens, index);
    return std::make_unique<ShiftNode>(std::move(dx), std::move(dy));
}

std::unique_ptr<WriteNode> NodeFactory::parseWriteNode(const std::vector<std::string> &tokens, size_t &index) {
    return std::make_unique<WriteNode>(parseExpression(tokens, index));
}

std::unique_ptr<PressNode> NodeFactory::parsePressNode(const std::vector<std::string> &tokens, size_t &index) {
    std::string key = tokens[index++];
    return std::make_unique<PressNode>(key);
}

std::unique_ptr<VarDeclNode> NodeFactory::parseVarDeclNode(const std::vector<std::string> &tokens, size_t &index, bool isMutable) {
    if (index >= tokens.size()) return nullptr;
    std::string name = tokens[index++];

    if (index >= tokens.size() || tokens[index] != "=") {
        throw std::runtime_error("Expected '=' after variable name '" + name + "'");
    }
    index++;
    return std::make_unique<VarDeclNode>(name, parseExpression(tokens, index), isMutable);
}

std::unique_ptr<AssignmentNode> NodeFactory::parseAssigmentNode(const std::string& cmd, const std::vector<std::string> &tokens, size_t &index) {
    if (index < tokens.size()) {
        index++;
        return std::make_unique<AssignmentNode>(cmd, parseExpression(tokens, index));
    }
    return nullptr;
}

std::unique_ptr<ASTNode> NodeFactory::parseRepeatBlock(const std::vector<std::string> &tokens, size_t &index) {
    if (index >= tokens.size()) return nullptr;
    auto count = parseExpression(tokens, index);

    if (index < tokens.size() && tokens[index] == "{") {
        index++;
        auto nodes = std::vector<std::unique_ptr<ASTNode>>();
        while (index < tokens.size() && tokens[index] != "}") {
            const std::string& cmd = tokens[index];
            index++;
            if (auto node = create(cmd, tokens, index)) {
                nodes.push_back(std::move(node));
            }
        }
        if (index < tokens.size()) index++;
        return std::make_unique<RepeatNode>(std::move(count), std::move(nodes));
    }
    return nullptr;
}

std::unique_ptr<ASTNode> NodeFactory::parseWhileBlock(const std::vector<std::string> &tokens, size_t &index) {
    auto condition = parseExpression(tokens, index);
    if (index < tokens.size() && tokens[index] == "{") {
        index++;
        auto nodes = std::vector<std::unique_ptr<ASTNode>>();
        while (index < tokens.size() && tokens[index] != "}") {
            const std::string& cmd = tokens[index];
            index++;
            if (auto node = create(cmd, tokens, index)) {
                nodes.push_back(std::move(node));
            }
        }
        if (index < tokens.size()) index++;
        return std::make_unique<WhileNode>(std::move(condition), std::move(nodes));
    }
}


std::unique_ptr<ASTNode> NodeFactory::parseLogNode(const std::vector<std::string> &tokens, size_t index) {
    if (index >= tokens.size()) return nullptr;
    auto msg = parseExpression(tokens, index);
    return std::make_unique<LogNode>(std::move(msg));

}



void NodeFactory::init() {
    mouseHandlers["click"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        return parseClickNode(tokens, index);
    };
    mouseHandlers["move"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        return parseMoveNode(tokens, index);
    };
    mouseHandlers["shift"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        return parseShiftNode(tokens, index);
    };

    keyboardHandlers["write"] = [this](const std::vector<std::string>& tokens, size_t index) -> std::unique_ptr<ASTNode> {
        return parseWriteNode(tokens, index);
    };
    keyboardHandlers["press"] = [this](const std::vector<std::string>& tokens, size_t index) -> std::unique_ptr<ASTNode> {
        return parsePressNode(tokens, index);
    };



    handlers["repeat"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        return parseRepeatBlock(tokens, index);
    };

    handlers["while"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        return parseWhileBlock(tokens, index);
    };

    handlers["wait"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        return parseWaitNode(tokens, index);
    };

    handlers["log"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        return parseLogNode(tokens, index);
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
            if (mouseHandlers.contains(cmd)) {
                return mouseHandlers[cmd](tokens, index);
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
            if (keyboardHandlers.contains(cmd)) {
                return keyboardHandlers[cmd](tokens, index);
            }
        }
        return nullptr;
    };
    handlers["var"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        return parseVarDeclNode(tokens, index, true);
    };

    handlers["val"] = [this](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        return parseVarDeclNode(tokens, index, false);
    };
}

std::unique_ptr<ASTNode> NodeFactory::create(const std::string& command, const std::vector<std::string>& tokens, size_t& index) {
    if (handlers.contains(command)) {
        return handlers[command](tokens, index);
    }
    if (tokens[index] == "=") return parseAssigmentNode(command, tokens, index);
    return nullptr;
}

std::unique_ptr<MouseBlockNode> NodeFactory::parseMouseBlock(const std::vector<std::string>& tokens, size_t& index) {
    auto block = std::make_unique<MouseBlockNode>();
    while (index < tokens.size() && tokens[index] != "}") {
        const std::string& cmd = tokens[index++];
        if (mouseHandlers.contains(cmd)) {
            block->actions.push_back(mouseHandlers[cmd](tokens, index));
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

    if (token == "true") return std::make_unique<BooleanNode>(true);
    if (token == "false") return std::make_unique<BooleanNode>(false);

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
        if (keyboardHandlers.contains(cmd)) {
            block->actions.push_back(keyboardHandlers[cmd](tokens, index));
        }
    }
    if (index < tokens.size()) index++;
    return block;
}