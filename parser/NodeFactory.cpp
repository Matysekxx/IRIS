#include "NodeFactory.h"
#include <stdexcept>
#include <charconv>

NodeFactory::NodeFactory() {
    init();
}

std::unique_ptr<WaitNode> NodeFactory::parseWaitNode(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'wait'");
    index++;
    auto expr = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after 'wait' argument");
    index++;
    return std::make_unique<WaitNode>(std::move(expr));
}

std::unique_ptr<MoveNode> NodeFactory::parseMoveNode(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'move'");
    index++;
    auto x = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ",") throw std::runtime_error("Expected ',' in 'move'");
    index++;
    auto y = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after 'move' arguments");
    index++;
    return std::make_unique<MoveNode>(std::move(x), std::move(y));
}

std::unique_ptr<ClickNode> NodeFactory::parseClickNode(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'click'");
    index++;
    if (index >= tokens.size()) throw std::runtime_error("Unexpected end of script");
    const std::string_view btn = tokens[index++];
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after 'click' argument");
    index++;
    return std::make_unique<ClickNode>(btn == "right" ? ClickNode::Right : ClickNode::Left);
}

std::unique_ptr<ShiftNode> NodeFactory::parseShiftNode(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'shift'");
    index++;
    auto dx = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ",") throw std::runtime_error("Expected ',' in 'shift'");
    index++;
    auto dy = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after 'shift' arguments");
    index++;
    return std::make_unique<ShiftNode>(std::move(dx), std::move(dy));
}

std::unique_ptr<WriteNode> NodeFactory::parseWriteNode(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'write'");
    index++;
    auto expr = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after 'write' argument");
    index++;
    return std::make_unique<WriteNode>(std::move(expr));
}

std::unique_ptr<PressNode> NodeFactory::parsePressNode(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'press'");
    index++;
    if (index >= tokens.size()) throw std::runtime_error("Unexpected end of script");
    std::string key(tokens[index++]);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after 'press' argument");
    index++;
    return std::make_unique<PressNode>(key);
}

std::unique_ptr<VarDeclNode> NodeFactory::parseVarDeclNode(const std::vector<std::string_view> &tokens, size_t &index, bool isMutable) {
    if (index >= tokens.size()) return nullptr;
    std::string name(tokens[index++]);

    if (index >= tokens.size() || tokens[index] != "=") {
        throw std::runtime_error("Expected '=' after variable name '" + name + "'");
    }
    index++;
    return std::make_unique<VarDeclNode>(name, parseExpression(tokens, index), isMutable);
}

std::unique_ptr<AssignmentNode> NodeFactory::parseAssigmentNode(const std::string& cmd, const std::vector<std::string_view> &tokens, size_t &index) {
    if (index < tokens.size() && tokens[index] == "=") {
        index++;
        return std::make_unique<AssignmentNode>(cmd, parseExpression(tokens, index));
    }
    return nullptr;
}

std::unique_ptr<ASTNode> NodeFactory::parseRepeatBlock(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'repeat'");
    index++;
    auto count = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after repeat count");
    index++;
    auto nodes = parseBlock(tokens, index);
    return std::make_unique<RepeatNode>(std::move(count), std::move(nodes));
}

std::unique_ptr<ASTNode> NodeFactory::parseWhileBlock(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'while'");
    index++;
    auto condition = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after while condition");
    index++;
    return std::make_unique<WhileNode>(std::move(condition), parseBlock(tokens, index));
}

std::unique_ptr<ASTNode> NodeFactory::parseIfBlock(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'if'");
    index++;
    auto condition = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after if condition");
    index++;

    auto thenBlock = parseBlock(tokens, index);

    std::vector<std::unique_ptr<ASTNode>> elseBlock;
    if (index < tokens.size() && tokens[index] == "else") {
        index++;
        if (index < tokens.size() && tokens[index] == "if") {
            index++;
            elseBlock.push_back(parseIfBlock(tokens, index));
        } else if (index < tokens.size() && tokens[index] == "{") {
            for (auto block = parseBlock(tokens, index);
                auto& node : block) {
                elseBlock.push_back(std::move(node));
            }
        } else {
            throw std::runtime_error("Expected '{' or 'if' after 'else'");
        }
    }
    return std::make_unique<IfNode>(std::move(condition), std::move(thenBlock), std::move(elseBlock));
}

 
std::vector<std::unique_ptr<ASTNode>> NodeFactory::parseBlock(const std::vector<std::string_view> &tokens, size_t &index) {
     if (index >= tokens.size() || tokens[index] != "{") throw std::runtime_error("Expected '{' to start a block");
     index++;
 
     std::vector<std::unique_ptr<ASTNode>> nodes;
     while (index < tokens.size() && tokens[index] != "}") {
         std::string cmd(tokens[index++]);
         if (auto node = create(cmd, tokens, index)) {
             nodes.push_back(std::move(node));
         }
     }
     if (index >= tokens.size()) throw std::runtime_error("Expected '}' to end a block");
     index++;
     return nodes;
}


std::unique_ptr<ASTNode> NodeFactory::parsePrintNode(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'print'");
    index++;
    auto expr = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after 'print' message");
    index++;
    return std::make_unique<PrintNode>(std::move(expr));
}

void NodeFactory::init() {
    auto wrap = [this](auto method) {
        return [this, method](const std::vector<std::string_view>& t, size_t& i) { return (this->*method)(t, i); };
    };

    mouseHandlers["click"] = wrap(&NodeFactory::parseClickNode);
    mouseHandlers["move"] = wrap(&NodeFactory::parseMoveNode);
    mouseHandlers["shift"] = wrap(&NodeFactory::parseShiftNode);

    keyboardHandlers["write"] = wrap(&NodeFactory::parseWriteNode);
    keyboardHandlers["press"] = wrap(&NodeFactory::parsePressNode);

    handlers["repeat"] = wrap(&NodeFactory::parseRepeatBlock);
    handlers["while"] = wrap(&NodeFactory::parseWhileBlock);
    handlers["if"] = wrap(&NodeFactory::parseIfBlock);
    handlers["wait"] = wrap(&NodeFactory::parseWaitNode);
    handlers["print"] = wrap(&NodeFactory::parsePrintNode);

    handlers["mouse"] = [this](const std::vector<std::string_view>& t, size_t& i) -> std::unique_ptr<ASTNode> {
        if (i >= t.size()) return nullptr;
        if (t[i] == "{") { i++; return parseMouseBlock(t, i); }
        if (t[i] == ".") {
            i++;
            if (const std::string cmd(t[i++]); mouseHandlers.contains(cmd)) return mouseHandlers[cmd](t, i);
        }
        return nullptr;
    };

    handlers["keyboard"] = [this](const std::vector<std::string_view>& t, size_t& i) -> std::unique_ptr<ASTNode> {
        if (i >= t.size()) return nullptr;
        if (t[i] == "{") { i++; return parseKeyboardBlock(t, i); }
        if (t[i] == ".") {
            i++;
            if (const std::string cmd(t[i++]); keyboardHandlers.contains(cmd)) return keyboardHandlers[cmd](t, i);
        }
        return nullptr;
    };

    handlers["var"] = [this](const std::vector<std::string_view>& t, size_t& i) { return parseVarDeclNode(t, i, true); };
    handlers["val"] = [this](const std::vector<std::string_view>& t, size_t& i) { return parseVarDeclNode(t, i, false); };
}

std::unique_ptr<ASTNode> NodeFactory::create(const std::string& command, const std::vector<std::string_view>& tokens, size_t& index) {
    if (handlers.contains(command)) return handlers[command](tokens, index);
    if (index < tokens.size() && tokens[index] == "=") return parseAssigmentNode(command, tokens, index);
    return nullptr;
}

std::unique_ptr<MouseBlockNode> NodeFactory::parseMouseBlock(const std::vector<std::string_view>& tokens, size_t& index) {
    auto block = std::make_unique<MouseBlockNode>();
    while (index < tokens.size() && tokens[index] != "}") {
        const std::string cmd(tokens[index++]);
        if (mouseHandlers.contains(cmd)) {
            block->actions.push_back(mouseHandlers[cmd](tokens, index));
        }
    }
    if (index < tokens.size()) index++;
    return block;
}

std::unique_ptr<KeyboardBlockNode> NodeFactory::parseKeyboardBlock(const std::vector<std::string_view>& tokens, size_t& index) {
    auto block = std::make_unique<KeyboardBlockNode>();
    while (index < tokens.size() && tokens[index] != "}") {
        if (std::string cmd(tokens[index++]);
            keyboardHandlers.contains(cmd)) block->actions.push_back(keyboardHandlers[cmd](tokens, index));
    }
    if (index < tokens.size()) index++;
    return block;
}


std::unique_ptr<ExpressionNode> NodeFactory::parseExpression(const std::vector<std::string_view> &tokens, size_t &index) {
    return parseLogic(tokens, index);
}

std::unique_ptr<ExpressionNode> NodeFactory::parseLogic(const std::vector<std::string_view> &tokens, size_t &index) {
    auto left = parseBitwise(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index];
        if (op != "&&" && op != "||") break;
        index++;
        left = std::make_unique<BinaryOperationNode>(std::move(left), parseBitwise(tokens, index), std::string(op));
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseBitwise(const std::vector<std::string_view> &tokens, size_t &index) {
    auto left = parseComparison(tokens, index);
    while (index < tokens.size()) {
        std::string op(tokens[index]);
        if (op != "&" && op != "|" && op != "^") break;
        index++;
        left = std::make_unique<BinaryOperationNode>(std::move(left), parseComparison(tokens, index), op);
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseComparison(const std::vector<std::string_view> &tokens, size_t &index) {
    auto left = parseShift(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index];
        if (op != "==" && op != "!=" && op != "<" && op != ">" && op != "<=" && op != ">=") break;
        index++;
        left = std::make_unique<BinaryOperationNode>(std::move(left), parseShift(tokens, index), std::string(op));
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseShift(const std::vector<std::string_view> &tokens, size_t &index) {
    auto left = parseAdditive(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index];
        if (op != "<<" && op != ">>") break;
        index++;
        left = std::make_unique<BinaryOperationNode>(std::move(left), parseAdditive(tokens, index), std::string(op));
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseAdditive(const std::vector<std::string_view> &tokens, size_t &index) {
    auto left = parseTerm(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index];
        if (op != "+" && op != "-") break;

        index++;
        left = std::make_unique<BinaryOperationNode>(std::move(left), parseTerm(tokens, index), std::string(op));
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseTerm(const std::vector<std::string_view> &tokens, size_t &index) {
    auto left = parseUnary(tokens, index);
    while (index < tokens.size()) {
        std::string op(tokens[index]);
        if (op != "*" && op != "/") break;
        index++;
        left = std::make_unique<BinaryOperationNode>(std::move(left), parseUnary(tokens, index), op);
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseUnary(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index < tokens.size() && tokens[index] == "!") {
        index++;
        return std::make_unique<UnaryOperationNode>("!", parseUnary(tokens, index));
    }
    return parseFactor(tokens, index);
}

std::unique_ptr<ExpressionNode> NodeFactory::parseFactor(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size()) throw std::runtime_error("Unexpected end of expression");

    std::string_view token = tokens[index++];

    if (token == "(") {
        auto expr = parseExpression(tokens, index);
        if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')'");
        index++;
        return expr;
    }

    if (token.starts_with('"')) {
        return std::make_unique<StringNode>(std::string(token.substr(1, token.size() - 2)));
    }

    if (token == "true") return std::make_unique<BooleanNode>(true);
    if (token == "false") return std::make_unique<BooleanNode>(false);

    if (!token.empty() && (std::isdigit(token[0]) || (token[0] == '-' && token.size() > 1))) {
        int val;
        auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
        if (ec == std::errc()) return std::make_unique<NumberNode>(val);
    }

    return std::make_unique<VariableNode>(std::string(token));
}