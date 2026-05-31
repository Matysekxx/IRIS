#include "NodeFactory.h"
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <cctype>
#include <algorithm>

using namespace iris::node;
using namespace iris::parser;

static TypeAnnotation parseType(const std::vector<Token> &tokens, size_t &index) {
    if (index >= tokens.size()) throw std::runtime_error("Expected type");
    std::string typeStr(tokens[index++].value);
    TypeAnnotation t = parseTypeAnnotation(typeStr);

    if (index < tokens.size() && tokens[index].value == "<") {
        index++;
        while (index < tokens.size() && tokens[index].value != ">") {
            t.params.push_back(parseType(tokens, index));
            if (index < tokens.size() && tokens[index].value == ",") index++;
        }
        if (index >= tokens.size() || tokens[index].value != ">") throw std::runtime_error(
            "Expected '>' after generic params");
        index++;
    }

    while (index + 1 < tokens.size() && tokens[index].value == "[" && tokens[index + 1].value == "]") {
        if (t.kind == TypeKind::Int) t.kind = TypeKind::IntArray;
        else if (t.kind == TypeKind::Double) t.kind = TypeKind::DoubleArray;
        else if (t.kind == TypeKind::String) t.kind = TypeKind::StringArray;
        else if (t.kind == TypeKind::Bool) t.kind = TypeKind::BoolArray;
        index += 2;
    }
    return t;
}

static TypeAnnotation tryParseTypeAnnot(const std::vector<Token> &tokens, size_t &index) {
    if (index < tokens.size() && tokens[index].value == ":") {
        index++;
        return parseType(tokens, index);
    }
    return TypeAnnotation(TypeKind::None);
}

NodeFactory::NodeFactory() { init(); }

std::unique_ptr<WaitNode> NodeFactory::parseWaitNode(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index - 1;
    index++; // skip '('
    auto expr = parseExpression(tokens, index);
    index++; // skip ')'
    auto node = std::make_unique<WaitNode>(std::move(expr));
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<VarDeclNode> NodeFactory::parseVarDeclNode(const std::vector<Token> &tokens, size_t &index,
                                                           bool isMutable) {
    size_t startIdx = index - 1;
    std::string name(tokens[index++].value);
    TypeAnnotation typeAnnot = tryParseTypeAnnot(tokens, index);
    index++; // skip '='
    auto node = std::make_unique<VarDeclNode>(name, parseExpression(tokens, index), isMutable, typeAnnot);
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<AssignmentNode> NodeFactory::parseAssigmentNode(const std::string &cmd,
                                                                const std::vector<Token> &tokens,
                                                                size_t &index) {
    size_t startIdx = index - 1;
    if (tokens[index].value == "=") {
        index++;
        auto node = std::make_unique<AssignmentNode>(cmd, parseExpression(tokens, index));
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    } else {
        std::string op(tokens[index].value);
        op.pop_back(); // remove '=' from '+=', '-=', etc.
        index++;
        auto varNode = std::make_unique<VariableNode>(cmd);
        varNode->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        auto expr = parseExpression(tokens, index);
        auto binaryOp = std::make_unique<BinaryOperationNode>(std::move(varNode), std::move(expr), op);
        binaryOp->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        auto node = std::make_unique<AssignmentNode>(cmd, std::move(binaryOp));
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
}

std::unique_ptr<ASTNode> NodeFactory::parseRepeatBlock(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index - 1;
    index++; // skip '('
    auto count = parseExpression(tokens, index);
    index++; // skip ')'
    auto node = std::make_unique<RepeatNode>(std::move(count), parseBlock(tokens, index));
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseWhileBlock(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index - 1;
    index++; // skip '('
    auto condition = parseExpression(tokens, index);
    index++; // skip ')'
    auto node = std::make_unique<WhileNode>(std::move(condition), parseBlock(tokens, index));
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseForBlock(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index - 1;
    index++; // skip '('
    std::unique_ptr<ASTNode> init = nullptr;
    if (index < tokens.size() && tokens[index].value != ";") {
        std::string initCmd(tokens[index++].value);
        init = create(initCmd, tokens, index);
    }
    index++; // skip ';'
    auto condition = parseExpression(tokens, index);
    index++; // skip ';'
    std::unique_ptr<ASTNode> increment = nullptr;
    if (index < tokens.size() && tokens[index].value != ")") {
        std::string incrCmd(tokens[index++].value);
        increment = create(incrCmd, tokens, index);
    }
    index++; // skip ')'
    auto node = std::make_unique<ForNode>(std::move(init), std::move(condition), std::move(increment),
                                     parseBlock(tokens, index));
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseIfBlock(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index - 1;
    index++; // skip '('
    auto condition = parseExpression(tokens, index);
    index++; // skip ')'
    auto thenBlock = parseBlock(tokens, index);
    std::vector<std::unique_ptr<ASTNode> > elseBlock;
    if (index < tokens.size() && tokens[index].value == "else") {
        index++;
        if (index < tokens.size() && tokens[index].value == "if") {
            index++;
            elseBlock.push_back(parseIfBlock(tokens, index));
        } else elseBlock = parseBlock(tokens, index);
    }
    auto node = std::make_unique<IfNode>(std::move(condition), std::move(thenBlock), std::move(elseBlock));
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::vector<std::unique_ptr<ASTNode> > NodeFactory::parseBlock(const std::vector<Token> &tokens,
                                                               size_t &index) {
    index++; // skip '{'
    std::vector<std::unique_ptr<ASTNode> > nodes;
    while (index < tokens.size() && tokens[index].value != "}") {
        std::string cmd(tokens[index++].value);
        if (auto node = create(cmd, tokens, index)) nodes.push_back(std::move(node));
    }
    index++; // skip '}'
    return nodes;
}

std::unique_ptr<ASTNode> NodeFactory::parsePrintNode(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index - 1;
    index++; // skip '('
    auto expr = parseExpression(tokens, index);
    index++; // skip ')'
    auto node = std::make_unique<PrintNode>(std::move(expr));
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseFunctionDecl(const std::vector<Token> &tokens, size_t &index,
                                                        bool isAbstract) {
    size_t startIdx = index - 1;
    std::string funcName(tokens[index++].value);
    index++; // skip '('
    std::vector<std::pair<std::string, TypeAnnotation> > params;
    while (index < tokens.size() && tokens[index].value != ")") {
        std::string pname(tokens[index++].value);
        params.emplace_back(std::move(pname), tryParseTypeAnnot(tokens, index));
        if (index < tokens.size() && tokens[index].value == ",") index++;
    }
    index++; // skip ')'
    TypeAnnotation returnType = tryParseTypeAnnot(tokens, index);
    std::vector<std::unique_ptr<ASTNode> > body;
    if (!isAbstract) body = parseBlock(tokens, index);
    else if (index < tokens.size() && tokens[index].value == ";") index++;
    auto node = std::make_unique<FunctionDeclNode>(std::move(funcName), std::move(params), std::move(body), returnType);
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseClassDecl(const std::vector<Token> &tokens, size_t &index,
                                                     bool isAbstract) {
    size_t startIdx = index - 1;
    std::string className(tokens[index++].value);
    std::vector<std::string> genericParams;
    if (index < tokens.size() && tokens[index].value == "<") {
        index++;
        while (index < tokens.size() && tokens[index].value != ">") {
            genericParams.emplace_back(tokens[index++].value);
            if (index < tokens.size() && tokens[index].value == ",") index++;
        }
        index++; // skip '>'
    }
    std::string parentName;
    if (index < tokens.size() && tokens[index].value == ":") {
        index++;
        parentName = std::string(tokens[index++].value);
        if (index < tokens.size() && tokens[index].value == "<") {
            int depth = 0;
            do {
                if (tokens[index].value == "<") depth++;
                else if (tokens[index].value == ">") depth--;
                index++;
            } while (depth > 0 && index < tokens.size());
        }
    }
    index++; // skip '{'
    std::vector<ClassFieldDecl> fields;
    std::vector<ClassMethodDecl> methods;
    while (index < tokens.size() && tokens[index].value != "}") {
        bool isOverride = false;
        if (tokens[index].value == "override") {
            isOverride = true;
            index++;
        }
        AccessModifier access = AccessModifier::Public;
        if (tokens[index].value == "public") {
            access = AccessModifier::Public;
            index++;
        } else if (tokens[index].value == "private") {
            access = AccessModifier::Private;
            index++;
        }
        bool isStatic = false;
        if (tokens[index].value == "static") {
            isStatic = true;
            index++;
        }
        bool methodAbstract = false;
        if (tokens[index].value == "abstract") {
            methodAbstract = true;
            index++;
        }

        if (index < tokens.size() && (tokens[index].value == "var" || tokens[index].value == "val")) {
            bool isMutable = tokens[index].value == "var";
            index++;
            std::string name(tokens[index++].value);
            fields.push_back({name, isMutable, isStatic, access, tryParseTypeAnnot(tokens, index)});
        } else if (index < tokens.size() && tokens[index].value == "fun") {
            index++;
            auto func = static_cast<FunctionDeclNode *>(parseFunctionDecl(tokens, index, methodAbstract).release());
            methods.push_back({access, isStatic, methodAbstract, std::unique_ptr<FunctionDeclNode>(func)});
        } else if (index < tokens.size() && tokens[index].value == className) {
            index++; // skip name
            index++; // skip '('
            std::vector<std::pair<std::string, TypeAnnotation> > params;
            while (index < tokens.size() && tokens[index].value != ")") {
                std::string pName(tokens[index++].value);
                params.push_back({pName, tryParseTypeAnnot(tokens, index)});
                if (index < tokens.size() && tokens[index].value == ",") index++;
            }
            index++; // skip ')'
            auto func = std::make_unique<FunctionDeclNode>(className, std::move(params), parseBlock(tokens, index),
                                                           TypeKind::None);
            func->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            methods.push_back({access, false, false, std::move(func)});
        } else index++;
    }
    index++; // skip '}'
    auto node = std::make_unique<ClassDeclNode>(std::move(className), std::move(genericParams), isAbstract,
                                           std::move(parentName), std::move(fields), std::move(methods));
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseImportNative(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index - 1;
    if (index >= tokens.size()) return nullptr;
    if (tokens[index].value == "native") {
        index++;
        if (index >= tokens.size()) throw std::runtime_error("Expected native entity name");
        std::string name(tokens[index++].value);
        if (name.front() == '"') name = name.substr(1, name.size() - 2);
        std::string alias;
        if (index < tokens.size() && tokens[index].value == "as") {
            index++;
            if (index >= tokens.size()) throw std::runtime_error("Expected alias after 'as'");
            alias = std::string(tokens[index++].value);
            if (alias.front() == '"') alias = alias.substr(1, alias.size() - 2);
        }
        auto node = std::make_unique<ImportNativeNode>("", name, alias.empty() ? name : alias);
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    return nullptr;
}

std::unique_ptr<ASTNode> NodeFactory::parseFrom(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index - 1;
    if (index >= tokens.size()) return nullptr;
    std::string mod(tokens[index++].value);
    if (mod.front() == '"') mod = mod.substr(1, mod.size() - 2);
    if (index >= tokens.size() || tokens[index].value != "import") throw std::runtime_error("Expected 'import'");
    index++;
    if (index >= tokens.size() || tokens[index].value != "native") throw std::runtime_error(
        "Expected 'native' after 'import'");
    index++;
    if (index >= tokens.size()) throw std::runtime_error("Expected native entity name");
    std::string ent(tokens[index++].value);
    if (ent.front() == '"') ent = ent.substr(1, ent.size() - 2);
    std::string alias;
    if (index < tokens.size() && tokens[index].value == "as") {
        index++;
        if (index >= tokens.size()) throw std::runtime_error("Expected alias after 'as'");
        alias = std::string(tokens[index++].value);
        if (alias.front() == '"') alias = alias.substr(1, alias.size() - 2);
    }
    auto node = std::make_unique<ImportNativeNode>(mod, ent, alias.empty() ? ent : alias);
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

void NodeFactory::init() {
    auto wrap = [this](auto method) {
        return [this, method](const std::vector<Token> &t, size_t &i) { return (this->*method)(t, i); };
    };
    handlers["repeat"] = wrap(&NodeFactory::parseRepeatBlock);
    handlers["while"] = wrap(&NodeFactory::parseWhileBlock);
    handlers["for"] = wrap(&NodeFactory::parseForBlock);
    handlers["if"] = wrap(&NodeFactory::parseIfBlock);
    handlers["switch"] = [this](const std::vector<Token> &t, size_t &i) {
        return parseSwitchExpression(t, i);
    };
    handlers["enum"] = wrap(&NodeFactory::parseEnumDecl);
    handlers["wait"] = wrap(&NodeFactory::parseWaitNode);
    handlers["print"] = wrap(&NodeFactory::parsePrintNode);
    handlers["fun"] = [this](const std::vector<Token> &t, size_t &i) {
        return parseFunctionDecl(t, i, false);
    };
    handlers["return"] = wrap(&NodeFactory::parseReturnNode);
    handlers["class"] = [this](const std::vector<Token> &t, size_t &i) {
        return parseClassDecl(t, i, false);
    };
    handlers["try"] = wrap(&NodeFactory::parseTryCatch);
    handlers["throw"] = wrap(&NodeFactory::parseThrowNode);
    handlers["import"] = wrap(&NodeFactory::parseImportNative);
    handlers["from"] = wrap(&NodeFactory::parseFrom);
    handlers["var"] = [this](const std::vector<Token> &t, size_t &i) {
        return parseVarDeclNode(t, i, true);
    };
    handlers["val"] = [this](const std::vector<Token> &t, size_t &i) {
        return parseVarDeclNode(t, i, false);
    };
}

std::unique_ptr<ASTNode> NodeFactory::create(const std::string &command, const std::vector<Token> &tokens,
                                             size_t &index) {
    if (handlers.contains(command)) return handlers[command](tokens, index);
    size_t startIdx = index - 1;
    if (index < tokens.size() && tokens[index].value == "[") {
        index++;
        auto idx = parseExpression(tokens, index);
        index++;
        index++; // skip ']', '='
        auto node = std::make_unique<IndexAssignNode>(command, std::move(idx), parseExpression(tokens, index));
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    if (index < tokens.size() && tokens[index].value == ".") {
        index++;
        std::string mem(tokens[index++].value);
        if (index < tokens.size() && tokens[index].value == "=") {
            index++;
            auto node = std::make_unique<FieldAssignNode>(command, mem, parseExpression(tokens, index));
            node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            return node;
        }
        if (index < tokens.size() && tokens[index].value == "(") {
            index++;
            std::vector<std::unique_ptr<ExpressionNode> > args;
            while (index < tokens.size() && tokens[index].value != ")") {
                args.push_back(parseExpression(tokens, index));
                if (index < tokens.size() && tokens[index].value == ",") index++;
            }
            index++;
            auto methodCall = std::make_unique<MethodCallNode>(command, mem, std::move(args));
            methodCall->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            auto node = std::make_unique<ExpressionStmtNode>(std::move(methodCall));
            node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            return node;
        }
    }
    if (index < tokens.size() && (tokens[index].value == "=" || tokens[index].value == "+=" || tokens[index].value == "-=" || tokens[
                                      index].value == "*=" || tokens[index].value == "/=")) {
        std::string op(tokens[index++].value);
        if (op == "=") {
            auto node = std::make_unique<AssignmentNode>(command, parseExpression(tokens, index));
            node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            return node;
        } else {
            op.pop_back(); // remove '='
            auto varNode = std::make_unique<VariableNode>(command);
            varNode->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            auto expr = parseExpression(tokens, index);
            auto binaryOp = std::make_unique<BinaryOperationNode>(std::move(varNode), std::move(expr), op);
            binaryOp->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            auto node = std::make_unique<AssignmentNode>(command, std::move(binaryOp));
            node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            return node;
        }
    }
    if (index < tokens.size() && tokens[index].value == "(") {
        index++;
        std::vector<std::unique_ptr<ExpressionNode> > args;
        while (index < tokens.size() && tokens[index].value != ")") {
            args.push_back(parseExpression(tokens, index));
            if (index < tokens.size() && tokens[index].value == ",") index++;
        }
        index++;
        auto funcCall = std::make_unique<FunctionCallNode>(command, std::move(args));
        funcCall->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        auto node = std::make_unique<ExpressionStmtNode>(std::move(funcCall));
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    return nullptr;
}

std::unique_ptr<ExpressionNode>
NodeFactory::parseExpression(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index;
    auto left = parseComparison(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index].value;
        if (op != "&&" && op != "||" && op != "==" && op != "!=" && op != "<" && op != ">" && op != "<=" && op != ">=")
            break;
        index++;
        auto right = parseComparison(tokens, index);
        auto binOp = std::make_unique<BinaryOperationNode>(std::move(left), std::move(right), std::string(op));
        binOp->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        left = std::move(binOp);
    }
    return left;
}

std::unique_ptr<ExpressionNode>
NodeFactory::parseComparison(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index;
    auto left = parseAdditive(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index].value;
        if (op != "+" && op != "-") break;
        index++;
        auto right = parseAdditive(tokens, index);
        auto binOp = std::make_unique<BinaryOperationNode>(std::move(left), std::move(right), std::string(op));
        binOp->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        left = std::move(binOp);
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseAdditive(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index;
    auto left = parseUnary(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index].value;
        if (op != "*" && op != "/" && op != "%") break;
        index++;
        auto right = parseUnary(tokens, index);
        auto binOp = std::make_unique<BinaryOperationNode>(std::move(left), std::move(right), std::string(op));
        binOp->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        left = std::move(binOp);
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseUnary(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index;
    if (index < tokens.size() && (tokens[index].value == "!" || tokens[index].value == "-")) {
        std::string op(tokens[index++].value);
        auto node = std::make_unique<UnaryOperationNode>(op, parseUnary(tokens, index));
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    return parseFactor(tokens, index);
}

std::unique_ptr<ExpressionNode> NodeFactory::parseFactor(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index;
    std::string_view token = tokens[index++].value;
    if (token == "(") {
        auto expr = parseExpression(tokens, index);
        index++;
        return expr;
    }
    if (token == "new") {
        std::string type(tokens[index++].value);
        if (index < tokens.size() && tokens[index].value == "<") {
            int d = 0;
            do {
                if (tokens[index].value == "<") d++;
                else if (tokens[index].value == ">") d--;
                index++;
            } while (d > 0 && index < tokens.size());
        }
        if (index < tokens.size() && tokens[index].value == "[") {
            index++;
            auto sz = parseExpression(tokens, index);
            index++;
            auto node = std::make_unique<ArrayAllocNode>(parseTypeAnnotation(type), std::move(sz));
            node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            return node;
        } else if (index < tokens.size() && tokens[index].value == "(") {
            index++;
            std::vector<std::unique_ptr<ExpressionNode> > args;
            while (index < tokens.size() && tokens[index].value != ")") {
                args.push_back(parseExpression(tokens, index));
                if (index < tokens.size() && tokens[index].value == ",") index++;
            }
            index++;
            auto node = std::make_unique<FunctionCallNode>(type, std::move(args));
            node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            return node;
        }
    }
    if (token.starts_with('"')) {
        auto node = std::make_unique<StringNode>(std::string(token.substr(1, token.size() - 2)));
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    if (token == "true") {
        auto node = std::make_unique<BooleanNode>(true);
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    if (token == "false") {
        auto node = std::make_unique<BooleanNode>(false);
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    if (std::isdigit(token[0])) {
        auto node = std::make_unique<NumberNode>(std::stoi(std::string(token)));
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }

    std::string name(token);
    if (index < tokens.size() && tokens[index].value == "[") {
        index++;
        auto idx = parseExpression(tokens, index);
        index++;
        auto varNode = std::make_unique<VariableNode>(name);
        varNode->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        auto node = std::make_unique<IndexAccessNode>(std::move(varNode), std::move(idx));
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    if (index < tokens.size() && tokens[index].value == ".") {
        index++;
        std::string mem(tokens[index++].value);
        if (index < tokens.size() && tokens[index].value == "(") {
            index++;
            std::vector<std::unique_ptr<ExpressionNode> > args;
            while (index < tokens.size() && tokens[index].value != ")") {
                args.push_back(parseExpression(tokens, index));
                if (index < tokens.size() && tokens[index].value == ",") index++;
            }
            index++;
            auto node = std::make_unique<MethodCallNode>(name, mem, std::move(args));
            node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
            return node;
        }
        auto node = std::make_unique<FieldAccessNode>(name, mem);
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    if (index < tokens.size() && tokens[index].value == "(") {
        index++;
        std::vector<std::unique_ptr<ExpressionNode> > args;
        while (index < tokens.size() && tokens[index].value != ")") {
            args.push_back(parseExpression(tokens, index));
            if (index < tokens.size() && tokens[index].value == ",") index++;
        }
        index++;
        auto node = std::make_unique<FunctionCallNode>(name, std::move(args));
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    auto node = std::make_unique<VariableNode>(name);
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseReturnNode(const std::vector<Token> &tokens, size_t &index) {
    size_t startIdx = index - 1;
    if (index >= tokens.size() || tokens[index].value == "}") {
        auto node = std::make_unique<ReturnNode>(nullptr);
        node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
        return node;
    }
    auto node = std::make_unique<ReturnNode>(parseExpression(tokens, index));
    node->location = {tokens[startIdx].file, tokens[startIdx].line, tokens[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseThrowNode(const std::vector<Token> &t, size_t &i) {
    size_t startIdx = i - 1;
    auto node = std::make_unique<ThrowNode>(parseExpression(t, i));
    node->location = {t[startIdx].file, t[startIdx].line, t[startIdx].column};
    return node;
}

std::unique_ptr<SwitchNode> NodeFactory::parseSwitchExpression(const std::vector<Token> &t, size_t &i) {
    size_t startIdx = i - 1;
    i += 2;
    auto expr = parseExpression(t, i);
    i += 2;
    std::vector<std::unique_ptr<CaseNode> > cases;
    while (i < t.size() && t[i].value != "}") {
        size_t caseStartIdx = i;
        std::unique_ptr<ExpressionNode> val = nullptr;
        if (t[i].value == "case") {
            i++;
            val = parseExpression(t, i);
        } else i++;
        i++;
        auto node = std::make_unique<CaseNode>(std::move(val), parseBlock(t, i), false);
        node->location = {t[caseStartIdx].file, t[caseStartIdx].line, t[caseStartIdx].column};
        cases.push_back(std::move(node));
    }
    i++;
    auto node = std::make_unique<SwitchNode>(std::move(expr), std::move(cases));
    node->location = {t[startIdx].file, t[startIdx].line, t[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseEnumDecl(const std::vector<Token> &t, size_t &i) {
    size_t startIdx = i - 1;
    std::string name(t[i++].value);
    i++;
    std::vector<std::pair<std::string, int> > vals;
    int next = 0;
    while (i < t.size() && t[i].value != "}") {
        std::string en(t[i++].value);
        if (i < t.size() && t[i].value == "=") {
            i++;
            next = std::stoi(std::string(t[i++].value));
        }
        vals.push_back({en, next++});
        if (i < t.size() && t[i].value == ",") i++;
    }
    i++;
    auto node = std::make_unique<EnumNode>(name, std::move(vals));
    node->location = {t[startIdx].file, t[startIdx].line, t[startIdx].column};
    return node;
}

std::unique_ptr<ASTNode> NodeFactory::parseTryCatch(const std::vector<Token> &t, size_t &i) {
    size_t startIdx = i - 1;
    auto tryB = parseBlock(t, i);
    i++;
    i++;
    std::string var(t[i++].value);
    i++;
    auto node = std::make_unique<TryCatchNode>(std::move(tryB), var, parseBlock(t, i));
    node->location = {t[startIdx].file, t[startIdx].line, t[startIdx].column};
    return node;
}
