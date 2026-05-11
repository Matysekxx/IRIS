#include "NodeFactory.h"
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <cctype>
#include <algorithm>

using namespace iris::node;
using namespace iris::parser;

static TypeAnnotation tryParseTypeAnnot(const std::vector<std::string_view>& tokens, size_t& index) {
    if (index < tokens.size() && tokens[index] == ":") {
        index++;
        if (index >= tokens.size()) throw std::runtime_error("Expected type after ':'");
        std::string typeStr(tokens[index]);
        index++;

        // Check for array notation e.g., int[]
        if (index + 1 < tokens.size() && tokens[index] == "[" && tokens[index+1] == "]") {
            typeStr += "[]";
            index += 2;
        }

        TypeAnnotation t = parseTypeAnnotation(typeStr);
        if (t == TypeAnnotation::None) {
             return TypeAnnotation::None; 
        }
        return t;
    }
    return TypeAnnotation::None;
}

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

std::unique_ptr<VarDeclNode> NodeFactory::parseVarDeclNode(const std::vector<std::string_view> &tokens, size_t &index, bool isMutable) {
    if (index >= tokens.size()) return nullptr;
    std::string name(tokens[index++]);

    TypeAnnotation typeAnnot = tryParseTypeAnnot(tokens, index);

    if (index >= tokens.size() || tokens[index] != "=") {
        throw std::runtime_error("Expected '=' after variable name '" + name + "'");
    }
    index++;
    return std::make_unique<VarDeclNode>(name, parseExpression(tokens, index), isMutable, typeAnnot);
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

std::unique_ptr<ASTNode> NodeFactory::parseForBlock(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'for'");
    index++;

    std::unique_ptr<ASTNode> init = nullptr;
    if (index < tokens.size() && tokens[index] != ";") {
        std::string initCmd(tokens[index++]);
        init = create(initCmd, tokens, index);
    }
    if (index >= tokens.size() || tokens[index] != ";") throw std::runtime_error("Expected ';' after for-loop init");
    index++;

    auto condition = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ";") throw std::runtime_error(
        "Expected ';' after for-loop condition");
    index++;

    std::unique_ptr<ASTNode> increment = nullptr;
    if (index < tokens.size() && tokens[index] != ")") {
        std::string incrCmd(tokens[index++]);
        increment = create(incrCmd, tokens, index);
    }
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error(
        "Expected ')' after for-loop increment");
    index++;

    auto body = parseBlock(tokens, index);
    return std::make_unique<ForNode>(std::move(init), std::move(condition), std::move(increment), std::move(body));
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
            auto block = parseBlock(tokens, index);
            for (auto& node : block) {
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

std::unique_ptr<ASTNode> NodeFactory::parseFunctionDecl(const std::vector<std::string_view> &tokens, size_t &index, bool isAbstract) {
    if (index >= tokens.size()) throw std::runtime_error("Expected function name after 'fun'");
    std::string funcName(tokens[index++]);

    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after function name");
    index++;

    std::vector<std::pair<std::string, TypeAnnotation>> params;
    while (index < tokens.size() && tokens[index] != ")") {
        std::string pname(tokens[index++]);
        TypeAnnotation ptype = tryParseTypeAnnot(tokens, index);
        params.emplace_back(std::move(pname), ptype);
        if (index < tokens.size() && tokens[index] == ",") {
            index++;
        }
    }
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after parameters");
    index++;

    TypeAnnotation returnType = tryParseTypeAnnot(tokens, index);
    
    std::vector<std::unique_ptr<ASTNode>> body;

    if (isAbstract) {
        if (index < tokens.size() && tokens[index] == ";") {
            index++;
        }
        return std::make_unique<FunctionDeclNode>(std::move(funcName), std::move(params), std::move(body), returnType);
    }

    if (index < tokens.size() && tokens[index] == "=") {
        index++;
        auto expr = parseExpression(tokens, index);
        body.push_back(std::make_unique<ReturnNode>(std::move(expr)));
        return std::make_unique<FunctionDeclNode>(std::move(funcName), std::move(params), std::move(body), returnType);
    }

    body = parseBlock(tokens, index);
    return std::make_unique<FunctionDeclNode>(std::move(funcName), std::move(params), std::move(body), returnType);
}

std::unique_ptr<ASTNode> NodeFactory::parseReturnNode(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] == "}") {
        return std::make_unique<ReturnNode>(nullptr);
    }
    auto expr = parseExpression(tokens, index);
    return std::make_unique<ReturnNode>(std::move(expr));
}

std::unique_ptr<ASTNode> NodeFactory::parseClassDecl(const std::vector<std::string_view> &tokens, size_t &index, bool isAbstract) {
    if (index >= tokens.size()) throw std::runtime_error("Expected class name after 'class'");
    std::string className(tokens[index++]);

    std::string parentName;
    if (index < tokens.size() && tokens[index] == ":") {
        index++;
        if (index >= tokens.size()) throw std::runtime_error("Expected parent class name after ':'");
        parentName = std::string(tokens[index++]);
    }

    if (index >= tokens.size() || tokens[index] != "{") throw std::runtime_error("Expected '{' after class name");
    index++;

    std::vector<ClassFieldDecl> fields;
    std::vector<ClassMethodDecl> methods;

    while (index < tokens.size() && tokens[index] != "}") {
        AccessModifier access = AccessModifier::PackagePrivate;
        if (tokens[index] == "public") { access = AccessModifier::Public; index++; }
        else if (tokens[index] == "private") { access = AccessModifier::Private; index++; }
        else if (tokens[index] == "package-private") { access = AccessModifier::PackagePrivate; index++; }
        
        bool methodAbstract = false;
        if (tokens[index] == "abstract") {
            methodAbstract = true;
            index++;
        }

        if (index < tokens.size() && (tokens[index] == "var" || tokens[index] == "val")) {
            if (methodAbstract) throw std::runtime_error("Fields cannot be abstract");
            bool isMutable = tokens[index] == "var";
            index++;
            if (index >= tokens.size()) throw std::runtime_error("Expected field name");
            std::string fieldName(tokens[index++]);
            TypeAnnotation type = tryParseTypeAnnot(tokens, index);
            fields.push_back({std::move(fieldName), isMutable, access, type});
        } else if (index < tokens.size() && tokens[index] == "fun") {
            index++;
            auto funcNode = parseFunctionDecl(tokens, index, methodAbstract);
            auto* funcDecl = static_cast<FunctionDeclNode*>(funcNode.release());
            methods.push_back({access, methodAbstract, std::unique_ptr<FunctionDeclNode>(funcDecl)});
        } else if (index < tokens.size() && tokens[index] == className) {
            // Java-style constructor: ClassName(...) { ... }
            index++;
            if (index >= tokens.size() || tokens[index] != "(") {
                throw std::runtime_error("Expected '(' after constructor name '" + className + "'");
            }
            // We use parseFunctionDecl but we need to handle the fact that we already consumed the name
            // Let's refactor parseFunctionDecl to take the name as an argument or just handle it here.
            // Actually, parseFunctionDecl expects the name after 'fun'.
            // I'll manually parse the rest of the constructor.
            
            std::vector<std::pair<std::string, TypeAnnotation>> params;
            index++; // consume '('
            while (index < tokens.size() && tokens[index] != ")") {
                std::string pName(tokens[index++]);
                TypeAnnotation pType = tryParseTypeAnnot(tokens, index);
                params.push_back({pName, pType});
                if (index < tokens.size() && tokens[index] == ",") index++;
            }
            if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after constructor params");
            index++;

            auto body = parseBlock(tokens, index);
            auto funcDecl = std::make_unique<FunctionDeclNode>(className, std::move(params), std::move(body), TypeAnnotation::None);
            methods.push_back({access, false, std::move(funcDecl)});
        } else {
            throw std::runtime_error("Expected 'var', 'val', or 'fun' in class body, got '" + std::string(tokens[index]) + "'");
        }
    }
    if (index >= tokens.size()) throw std::runtime_error("Expected '}' to end class");
    index++;

    return std::make_unique<ClassDeclNode>(std::move(className), isAbstract, std::move(parentName), std::move(fields), std::move(methods));
}

std::unique_ptr<ASTNode> NodeFactory::parseImportNative(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "native") {
        throw std::runtime_error("Expected 'native' after 'import'");
    }
    index++;
    if (index >= tokens.size()) {
        throw std::runtime_error("Expected native entity name after 'import native'");
    }
    std::string name(tokens[index++]);
    return std::make_unique<ImportNativeNode>(std::move(name));
}

void NodeFactory::init() {
    auto wrap = [this](auto method) {
        return [this, method](const std::vector<std::string_view>& t, size_t& i) { return (this->*method)(t, i); };
    };

    handlers["repeat"] = wrap(&NodeFactory::parseRepeatBlock);
    handlers["while"] = wrap(&NodeFactory::parseWhileBlock);
    handlers["for"] = wrap(&NodeFactory::parseForBlock);
    handlers["if"] = wrap(&NodeFactory::parseIfBlock);
    handlers["switch"] = [this](const std::vector<std::string_view>& t, size_t& i) -> std::unique_ptr<ASTNode> {
        return parseSwitchExpression(t, i);
    };
    handlers["enum"] = wrap(&NodeFactory::parseEnumDecl);
    handlers["wait"] = wrap(&NodeFactory::parseWaitNode);
    handlers["print"] = wrap(&NodeFactory::parsePrintNode);
    handlers["fun"] = [this](const std::vector<std::string_view>& t, size_t& i) { return parseFunctionDecl(t, i, false); };
    handlers["return"] = wrap(&NodeFactory::parseReturnNode);
    handlers["class"] = [this](const std::vector<std::string_view>& t, size_t& i) { return parseClassDecl(t, i, false); };
    handlers["try"] = wrap(&NodeFactory::parseTryCatch);
    handlers["throw"] = wrap(&NodeFactory::parseThrowNode);
    handlers["import"] = wrap(&NodeFactory::parseImportNative);

    handlers["break"] = [](const std::vector<std::string_view> &, size_t &) -> std::unique_ptr<ASTNode> {
        return std::make_unique<BreakNode>();
    };
    handlers["continue"] = [](const std::vector<std::string_view> &, size_t &) -> std::unique_ptr<ASTNode> {
        return std::make_unique<ContinueNode>();
    };

    handlers["var"] = [this](const std::vector<std::string_view>& t, size_t& i) { return parseVarDeclNode(t, i, true); };
    handlers["val"] = [this](const std::vector<std::string_view>& t, size_t& i) { return parseVarDeclNode(t, i, false); };
    
    handlers["abstract"] = [this](const std::vector<std::string_view>& t, size_t& i) -> std::unique_ptr<ASTNode> {
        if (i >= t.size()) throw std::runtime_error("Unexpected end after 'abstract'");
        std::string next(t[i++]);
        if (next == "class") {
            return parseClassDecl(t, i, true);
        } else if (next == "fun") {
            return parseFunctionDecl(t, i, true);
        }
        throw std::runtime_error("Expected 'class' or 'fun' after 'abstract'");
    };
}

std::unique_ptr<ASTNode> NodeFactory::create(const std::string& command, const std::vector<std::string_view>& tokens, size_t& index) {
    if (handlers.contains(command)) return handlers[command](tokens, index);

    if (index < tokens.size() && tokens[index] == "[") {
        return parseIndexAssign(command, tokens, index);
    }

    if (index < tokens.size() && tokens[index] == ".") {
        index++;
        if (index >= tokens.size()) return nullptr;
        std::string member(tokens[index++]);
        if (index < tokens.size() && tokens[index] == "=") {
            index++;
            return std::make_unique<FieldAssignNode>(command, member, parseExpression(tokens, index));
        }
        if (index < tokens.size() && tokens[index] == "(") {
            index++;
            std::vector<std::unique_ptr<ExpressionNode>> args;
            while (index < tokens.size() && tokens[index] != ")") {
                args.push_back(parseExpression(tokens, index));
                if (index < tokens.size() && tokens[index] == ",") index++;
            }
            if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')'");
            index++;
            return std::make_unique<ExpressionStmtNode>(
                std::make_unique<MethodCallNode>(command, member, std::move(args)));
        }
        return nullptr;
    }

    if (index < tokens.size() && tokens[index] == "=") return parseAssigmentNode(command, tokens, index);
    if (index < tokens.size() && tokens[index] == "(") {
        index++;
        std::vector<std::unique_ptr<ExpressionNode>> args;
        while (index < tokens.size() && tokens[index] != ")") {
            args.push_back(parseExpression(tokens, index));
            if (index < tokens.size() && tokens[index] == ",") index++;
        }
        if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')'");
        index++;
        return std::make_unique<ExpressionStmtNode>(
            std::make_unique<FunctionCallNode>(command, std::move(args)));
    }
    return nullptr;
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
        if (op != "*" && op != "/" && op != "%") break;
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
    if (index < tokens.size() && tokens[index] == "-") {
        index++;
        return std::make_unique<UnaryOperationNode>("-", parseUnary(tokens, index));
    }
    return parseFactor(tokens, index);
}

std::unique_ptr<ExpressionNode> NodeFactory::parseFactor(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size()) throw std::runtime_error("Unexpected end of expression");

    std::string_view token = tokens[index++];

    if (token == "[") {
        std::vector<std::unique_ptr<ExpressionNode>> elements;
        if (index < tokens.size() && tokens[index] != "]") {
            elements.push_back(parseExpression(tokens, index));
            while (index < tokens.size() && tokens[index] == ",") {
                index++;
                elements.push_back(parseExpression(tokens, index));
            }
        }
        if (index >= tokens.size() || tokens[index] != "]") {
            throw std::runtime_error("Expected ']' recursively in array literal");
        }
        index++;
        return std::make_unique<ArrayLiteralNode>(std::move(elements));
    }

    if (token == "(") {
        auto expr = parseExpression(tokens, index);
        if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')'");
        index++;
        return expr;
    }

    if (token.starts_with('"')) {
        std::string raw(token);
        if (raw.size() >= 2 && raw.back() == '"') {
            raw = raw.substr(1, raw.size() - 2);
        }

        if (raw.find("${") != std::string::npos) {
            std::vector<std::unique_ptr<ExpressionNode>> parts;
            size_t pos = 0;
            while (true) {
                size_t start = raw.find("${", pos);
                if (start == std::string::npos) {
                    if (pos < raw.length()) {
                        parts.push_back(std::make_unique<StringNode>(raw.substr(pos)));
                    }
                    break;
                }
                if (start > pos) {
                    parts.push_back(std::make_unique<StringNode>(raw.substr(pos, start - pos)));
                }
                size_t end = raw.find('}', start + 2);
                if (end == std::string::npos) throw std::runtime_error("Unclosed string interpolation ${");
                
                std::string varName = raw.substr(start + 2, end - start - 2);
                parts.push_back(std::make_unique<VariableNode>(varName));
                pos = end + 1;
            }
            return std::make_unique<StringInterpNode>(std::move(parts));
        }

        return std::make_unique<StringNode>(std::move(raw));
    }

    if (token == "true") return std::make_unique<BooleanNode>(true);
    if (token == "false") return std::make_unique<BooleanNode>(false);
    if (token == "switch") {
        index--; // put back 'switch' for parseSwitchExpression
        return parseSwitchExpression(tokens, index);
    }

    if (!token.empty() && (std::isdigit(token[0]))) {
        if (token.find('.') != std::string_view::npos) {
            try {
                return std::make_unique<DoubleNode>(std::stod(std::string(token)));
            } catch (...) {}
        } else {
            try {
                return std::make_unique<NumberNode>(static_cast<int>(std::stoll(std::string(token))));
            } catch (...) {}
        }
    }

    std::string name(token);

    if (index < tokens.size() && tokens[index] == "[") {
        index++;
        auto idxExpr = parseExpression(tokens, index);
        if (index >= tokens.size() || tokens[index] != "]")
            throw std::runtime_error("Expected ']' after index/size");
        index++;

        TypeAnnotation typeAnn = parseTypeAnnotation(name);
        if (typeAnn != TypeAnnotation::None) {
            return std::make_unique<ArrayAllocNode>(typeAnn, std::move(idxExpr));
        }

        auto obj = std::make_unique<VariableNode>(std::move(name));
        return std::make_unique<IndexAccessNode>(std::move(obj), std::move(idxExpr));
    }

    if (index < tokens.size() && tokens[index] == ".") {
        index++;
        if (index >= tokens.size()) throw std::runtime_error("Expected member name after '.'");
        std::string member(tokens[index++]);
        if (index < tokens.size() && tokens[index] == "(") {
            index++;
            std::vector<std::unique_ptr<ExpressionNode>> args;
            if (index < tokens.size() && tokens[index] != ")") {
                args.push_back(parseExpression(tokens, index));
                while (index < tokens.size() && tokens[index] == ",") {
                    index++;
                    args.push_back(parseExpression(tokens, index));
                }
            }
            if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')'");
            index++;
            return std::make_unique<MethodCallNode>(std::move(name), std::move(member), std::move(args));
        }
        return std::make_unique<FieldAccessNode>(std::move(name), std::move(member));
    }

    if (index < tokens.size() && tokens[index] == "(") {
        index++;
        std::vector<std::unique_ptr<ExpressionNode> > args;
        if (index < tokens.size() && tokens[index] != ")") {
            args.push_back(parseExpression(tokens, index));
            while (index < tokens.size() && tokens[index] == ",") {
                index++;
                args.push_back(parseExpression(tokens, index));
            }
        }
        if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error(
            "Expected ')' after function arguments");
        index++;
        return std::make_unique<FunctionCallNode>(std::move(name), std::move(args));
    }

    return std::make_unique<VariableNode>(std::move(name));
}

std::unique_ptr<ASTNode> NodeFactory::parseIndexAssign(const std::string& objName,
    const std::vector<std::string_view>& tokens, size_t& index) {
    index++;
    auto idxExpr = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != "]")
        throw std::runtime_error("Expected ']' in index assignment");
    index++;
    if (index >= tokens.size() || tokens[index] != "=")
        throw std::runtime_error("Expected '=' after ']' in index assignment");
    index++;
    auto valExpr = parseExpression(tokens, index);
    return std::make_unique<IndexAssignNode>(objName, std::move(idxExpr), std::move(valExpr));
}

std::unique_ptr<ASTNode> NodeFactory::parseTryCatch(const std::vector<std::string_view>& tokens, size_t& index) {
    auto tryBody = parseBlock(tokens, index);

    if (index >= tokens.size() || tokens[index] != "catch") throw std::runtime_error("Expected 'catch' after try block");
    index++;

    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'catch'");
    index++;

    if (index >= tokens.size()) throw std::runtime_error("Expected catch variable name");
    std::string catchVar(tokens[index++]);

    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after catch variable");
    index++;

    auto catchBody = parseBlock(tokens, index);

    return std::make_unique<TryCatchNode>(std::move(tryBody), std::move(catchVar), std::move(catchBody));
}

std::unique_ptr<SwitchNode> NodeFactory::parseSwitchExpression(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] != "switch") throw std::runtime_error("Expected 'switch'");
    index++;

    if (index >= tokens.size() || tokens[index] != "(") throw std::runtime_error("Expected '(' after 'switch'");
    index++;
    auto expr = parseExpression(tokens, index);
    if (index >= tokens.size() || tokens[index] != ")") throw std::runtime_error("Expected ')' after switch expression");
    index++;

    if (index >= tokens.size() || tokens[index] != "{") throw std::runtime_error("Expected '{' to start switch block");
    index++;

    std::vector<std::unique_ptr<CaseNode>> cases;
    while (index < tokens.size() && tokens[index] != "}") {
        std::unique_ptr<ExpressionNode> caseVal = nullptr;
        if (tokens[index] == "case") {
            index++;
            caseVal = parseExpression(tokens, index);
        } else if (tokens[index] == "default") {
            index++;
        } else {
            throw std::runtime_error("Expected 'case' or 'default' in switch block");
        }

        bool isArrow = false;
        if (index < tokens.size() && tokens[index] == ":") {
            index++;
        } else if (index < tokens.size() && tokens[index] == "->") {
            isArrow = true;
            index++;
        } else {
            throw std::runtime_error("Expected ':' or '->' after case/default");
        }

        std::vector<std::unique_ptr<ASTNode>> body;
        if (isArrow) {
            if (index < tokens.size() && tokens[index] == "{") {
                body = parseBlock(tokens, index);
            } else {
                std::string cmd(tokens[index++]);
                if (auto node = create(cmd, tokens, index)) {
                    body.push_back(std::move(node));
                }
            }
        } else {
            while (index < tokens.size() && tokens[index] != "case" && tokens[index] != "default" && tokens[index] != "}") {
                std::string cmd(tokens[index++]);
                if (auto node = create(cmd, tokens, index)) {
                    body.push_back(std::move(node));
                }
            }
        }
        cases.push_back(std::make_unique<CaseNode>(std::move(caseVal), std::move(body), isArrow));
    }

    if (index >= tokens.size() || tokens[index] != "}") throw std::runtime_error("Expected '}' to end switch block");
    index++;

    return std::make_unique<SwitchNode>(std::move(expr), std::move(cases));
}

std::unique_ptr<ASTNode> NodeFactory::parseEnumDecl(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size()) throw std::runtime_error("Expected enum name");
    std::string name(tokens[index++]);

    if (index >= tokens.size() || tokens[index] != "{") throw std::runtime_error("Expected '{' after enum name");
    index++;

    std::vector<std::pair<std::string, int>> values;
    int nextVal = 0;

    while (index < tokens.size() && tokens[index] != "}") {
        std::string entryName(tokens[index++]);
        if (index < tokens.size() && tokens[index] == "=") {
            index++;
            if (index >= tokens.size()) throw std::runtime_error("Expected value after '=' in enum");
            nextVal = std::stoi(std::string(tokens[index++]));
        }
        values.push_back({entryName, nextVal++});

        if (index < tokens.size() && tokens[index] == ",") {
            index++;
        }
    }

    if (index >= tokens.size() || tokens[index] != "}") throw std::runtime_error("Expected '}' after enum body");
    index++;

    return std::make_unique<EnumNode>(name, std::move(values));
}

std::unique_ptr<ASTNode> NodeFactory::parseThrowNode(const std::vector<std::string_view>& tokens, size_t& index) {
    if (index >= tokens.size()) throw std::runtime_error("Expected expression after 'throw'");
    auto expr = parseExpression(tokens, index);
    return std::make_unique<ThrowNode>(std::move(expr));
}
