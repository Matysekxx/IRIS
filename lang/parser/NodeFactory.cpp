#include "NodeFactory.h"
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <cctype>
#include <algorithm>

using namespace iris::node;
using namespace iris::parser;

static TypeAnnotation parseType(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size()) throw std::runtime_error("Expected type");
    std::string typeStr(tokens[index++]);
    TypeAnnotation t = parseTypeAnnotation(typeStr);

    if (index < tokens.size() && tokens[index] == "<") {
        index++;
        while (index < tokens.size() && tokens[index] != ">") {
            t.params.push_back(parseType(tokens, index));
            if (index < tokens.size() && tokens[index] == ",") index++;
        }
        if (index >= tokens.size() || tokens[index] != ">") throw std::runtime_error(
            "Expected '>' after generic params");
        index++;
    }

    while (index + 1 < tokens.size() && tokens[index] == "[" && tokens[index + 1] == "]") {
        if (t.kind == TypeKind::Int) t.kind = TypeKind::IntArray;
        else if (t.kind == TypeKind::Double) t.kind = TypeKind::DoubleArray;
        else if (t.kind == TypeKind::String) t.kind = TypeKind::StringArray;
        else if (t.kind == TypeKind::Bool) t.kind = TypeKind::BoolArray;
        index += 2;
    }
    return t;
}

static TypeAnnotation tryParseTypeAnnot(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index < tokens.size() && tokens[index] == ":") {
        index++;
        return parseType(tokens, index);
    }
    return TypeAnnotation(TypeKind::None);
}

NodeFactory::NodeFactory() { init(); }

std::unique_ptr<WaitNode> NodeFactory::parseWaitNode(const std::vector<std::string_view> &tokens, size_t &index) {
    index++; // skip '('
    auto expr = parseExpression(tokens, index);
    index++; // skip ')'
    return std::make_unique<WaitNode>(std::move(expr));
}

std::unique_ptr<VarDeclNode> NodeFactory::parseVarDeclNode(const std::vector<std::string_view> &tokens, size_t &index,
                                                           bool isMutable) {
    std::string name(tokens[index++]);
    TypeAnnotation typeAnnot = tryParseTypeAnnot(tokens, index);
    index++; // skip '='
    return std::make_unique<VarDeclNode>(name, parseExpression(tokens, index), isMutable, typeAnnot);
}

std::unique_ptr<AssignmentNode> NodeFactory::parseAssigmentNode(const std::string &cmd,
                                                                const std::vector<std::string_view> &tokens,
                                                                size_t &index) {
    if (tokens[index] == "=") {
        index++;
        return std::make_unique<AssignmentNode>(cmd, parseExpression(tokens, index));
    } else {
        std::string op(tokens[index]);
        op.pop_back(); // remove '=' from '+=', '-=', etc.
        index++;
        auto varNode = std::make_unique<VariableNode>(cmd);
        auto expr = parseExpression(tokens, index);
        auto binaryOp = std::make_unique<BinaryOperationNode>(std::move(varNode), std::move(expr), op);
        return std::make_unique<AssignmentNode>(cmd, std::move(binaryOp));
    }
}

std::unique_ptr<ASTNode> NodeFactory::parseRepeatBlock(const std::vector<std::string_view> &tokens, size_t &index) {
    index++; // skip '('
    auto count = parseExpression(tokens, index);
    index++; // skip ')'
    return std::make_unique<RepeatNode>(std::move(count), parseBlock(tokens, index));
}

std::unique_ptr<ASTNode> NodeFactory::parseWhileBlock(const std::vector<std::string_view> &tokens, size_t &index) {
    index++; // skip '('
    auto condition = parseExpression(tokens, index);
    index++; // skip ')'
    return std::make_unique<WhileNode>(std::move(condition), parseBlock(tokens, index));
}

std::unique_ptr<ASTNode> NodeFactory::parseForBlock(const std::vector<std::string_view> &tokens, size_t &index) {
    index++; // skip '('
    std::unique_ptr<ASTNode> init = nullptr;
    if (index < tokens.size() && tokens[index] != ";") {
        std::string initCmd(tokens[index++]);
        init = create(initCmd, tokens, index);
    }
    index++; // skip ';'
    auto condition = parseExpression(tokens, index);
    index++; // skip ';'
    std::unique_ptr<ASTNode> increment = nullptr;
    if (index < tokens.size() && tokens[index] != ")") {
        std::string incrCmd(tokens[index++]);
        increment = create(incrCmd, tokens, index);
    }
    index++; // skip ')'
    return std::make_unique<ForNode>(std::move(init), std::move(condition), std::move(increment),
                                     parseBlock(tokens, index));
}

std::unique_ptr<ASTNode> NodeFactory::parseIfBlock(const std::vector<std::string_view> &tokens, size_t &index) {
    index++; // skip '('
    auto condition = parseExpression(tokens, index);
    index++; // skip ')'
    auto thenBlock = parseBlock(tokens, index);
    std::vector<std::unique_ptr<ASTNode> > elseBlock;
    if (index < tokens.size() && tokens[index] == "else") {
        index++;
        if (index < tokens.size() && tokens[index] == "if") {
            index++;
            elseBlock.push_back(parseIfBlock(tokens, index));
        } else elseBlock = parseBlock(tokens, index);
    }
    return std::make_unique<IfNode>(std::move(condition), std::move(thenBlock), std::move(elseBlock));
}

std::vector<std::unique_ptr<ASTNode> > NodeFactory::parseBlock(const std::vector<std::string_view> &tokens,
                                                               size_t &index) {
    index++; // skip '{'
    std::vector<std::unique_ptr<ASTNode> > nodes;
    while (index < tokens.size() && tokens[index] != "}") {
        std::string cmd(tokens[index++]);
        if (auto node = create(cmd, tokens, index)) nodes.push_back(std::move(node));
    }
    index++; // skip '}'
    return nodes;
}

std::unique_ptr<ASTNode> NodeFactory::parsePrintNode(const std::vector<std::string_view> &tokens, size_t &index) {
    index++; // skip '('
    auto expr = parseExpression(tokens, index);
    index++; // skip ')'
    return std::make_unique<PrintNode>(std::move(expr));
}

std::unique_ptr<ASTNode> NodeFactory::parseFunctionDecl(const std::vector<std::string_view> &tokens, size_t &index,
                                                        bool isAbstract) {
    std::string funcName(tokens[index++]);
    index++; // skip '('
    std::vector<std::pair<std::string, TypeAnnotation> > params;
    while (index < tokens.size() && tokens[index] != ")") {
        std::string pname(tokens[index++]);
        params.emplace_back(std::move(pname), tryParseTypeAnnot(tokens, index));
        if (index < tokens.size() && tokens[index] == ",") index++;
    }
    index++; // skip ')'
    TypeAnnotation returnType = tryParseTypeAnnot(tokens, index);
    std::vector<std::unique_ptr<ASTNode> > body;
    if (!isAbstract) body = parseBlock(tokens, index);
    else if (index < tokens.size() && tokens[index] == ";") index++;
    return std::make_unique<FunctionDeclNode>(std::move(funcName), std::move(params), std::move(body), returnType);
}

std::unique_ptr<ASTNode> NodeFactory::parseClassDecl(const std::vector<std::string_view> &tokens, size_t &index,
                                                     bool isAbstract) {
    std::string className(tokens[index++]);
    std::vector<std::string> genericParams;
    if (index < tokens.size() && tokens[index] == "<") {
        index++;
        while (index < tokens.size() && tokens[index] != ">") {
            genericParams.emplace_back(tokens[index++]);
            if (index < tokens.size() && tokens[index] == ",") index++;
        }
        index++; // skip '>'
    }
    std::string parentName;
    if (index < tokens.size() && tokens[index] == ":") {
        index++;
        parentName = std::string(tokens[index++]);
        if (index < tokens.size() && tokens[index] == "<") {
            int depth = 0;
            do {
                if (tokens[index] == "<") depth++;
                else if (tokens[index] == ">") depth--;
                index++;
            } while (depth > 0 && index < tokens.size());
        }
    }
    index++; // skip '{'
    std::vector<ClassFieldDecl> fields;
    std::vector<ClassMethodDecl> methods;
    while (index < tokens.size() && tokens[index] != "}") {
        bool isOverride = false;
        if (tokens[index] == "override") {
            isOverride = true;
            index++;
        }
        AccessModifier access = AccessModifier::Public;
        if (tokens[index] == "public") {
            access = AccessModifier::Public;
            index++;
        } else if (tokens[index] == "private") {
            access = AccessModifier::Private;
            index++;
        }
        bool isStatic = false;
        if (tokens[index] == "static") {
            isStatic = true;
            index++;
        }
        bool methodAbstract = false;
        if (tokens[index] == "abstract") {
            methodAbstract = true;
            index++;
        }

        if (index < tokens.size() && (tokens[index] == "var" || tokens[index] == "val")) {
            bool isMutable = tokens[index] == "var";
            index++;
            std::string name(tokens[index++]);
            fields.push_back({name, isMutable, isStatic, access, tryParseTypeAnnot(tokens, index)});
        } else if (index < tokens.size() && tokens[index] == "fun") {
            index++;
            auto func = static_cast<FunctionDeclNode *>(parseFunctionDecl(tokens, index, methodAbstract).release());
            methods.push_back({access, isStatic, methodAbstract, std::unique_ptr<FunctionDeclNode>(func)});
        } else if (index < tokens.size() && tokens[index] == className) {
            index++; // skip name
            index++; // skip '('
            std::vector<std::pair<std::string, TypeAnnotation> > params;
            while (index < tokens.size() && tokens[index] != ")") {
                std::string pName(tokens[index++]);
                params.push_back({pName, tryParseTypeAnnot(tokens, index)});
                if (index < tokens.size() && tokens[index] == ",") index++;
            }
            index++; // skip ')'
            auto func = std::make_unique<FunctionDeclNode>(className, std::move(params), parseBlock(tokens, index),
                                                           TypeKind::None);
            methods.push_back({access, false, false, std::move(func)});
        } else index++;
    }
    index++; // skip '}'
    return std::make_unique<ClassDeclNode>(std::move(className), std::move(genericParams), isAbstract,
                                           std::move(parentName), std::move(fields), std::move(methods));
}

std::unique_ptr<ASTNode> NodeFactory::parseImportNative(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size()) return nullptr;
    if (tokens[index] == "native") {
        index++;
        if (index >= tokens.size()) throw std::runtime_error("Expected native entity name");
        std::string name(tokens[index++]);
        if (name.front() == '"') name = name.substr(1, name.size() - 2);
        std::string alias;
        if (index < tokens.size() && tokens[index] == "as") {
            index++;
            if (index >= tokens.size()) throw std::runtime_error("Expected alias after 'as'");
            alias = std::string(tokens[index++]);
            if (alias.front() == '"') alias = alias.substr(1, alias.size() - 2);
        }
        return std::make_unique<ImportNativeNode>("", name, alias.empty() ? name : alias);
    }
    return nullptr;
}

std::unique_ptr<ASTNode> NodeFactory::parseFrom(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size()) return nullptr;
    std::string mod(tokens[index++]);
    if (mod.front() == '"') mod = mod.substr(1, mod.size() - 2);
    if (index >= tokens.size() || tokens[index] != "import") throw std::runtime_error("Expected 'import'");
    index++;
    if (index >= tokens.size() || tokens[index] != "native") throw std::runtime_error(
        "Expected 'native' after 'import'");
    index++;
    if (index >= tokens.size()) throw std::runtime_error("Expected native entity name");
    std::string ent(tokens[index++]);
    if (ent.front() == '"') ent = ent.substr(1, ent.size() - 2);
    std::string alias;
    if (index < tokens.size() && tokens[index] == "as") {
        index++;
        if (index >= tokens.size()) throw std::runtime_error("Expected alias after 'as'");
        alias = std::string(tokens[index++]);
        if (alias.front() == '"') alias = alias.substr(1, alias.size() - 2);
    }
    return std::make_unique<ImportNativeNode>(mod, ent, alias.empty() ? ent : alias);
}

void NodeFactory::init() {
    auto wrap = [this](auto method) {
        return [this, method](const std::vector<std::string_view> &t, size_t &i) { return (this->*method)(t, i); };
    };
    handlers["repeat"] = wrap(&NodeFactory::parseRepeatBlock);
    handlers["while"] = wrap(&NodeFactory::parseWhileBlock);
    handlers["for"] = wrap(&NodeFactory::parseForBlock);
    handlers["if"] = wrap(&NodeFactory::parseIfBlock);
    handlers["switch"] = [this](const std::vector<std::string_view> &t, size_t &i) {
        return parseSwitchExpression(t, i);
    };
    handlers["enum"] = wrap(&NodeFactory::parseEnumDecl);
    handlers["wait"] = wrap(&NodeFactory::parseWaitNode);
    handlers["print"] = wrap(&NodeFactory::parsePrintNode);
    handlers["fun"] = [this](const std::vector<std::string_view> &t, size_t &i) {
        return parseFunctionDecl(t, i, false);
    };
    handlers["return"] = wrap(&NodeFactory::parseReturnNode);
    handlers["class"] = [this](const std::vector<std::string_view> &t, size_t &i) {
        return parseClassDecl(t, i, false);
    };
    handlers["try"] = wrap(&NodeFactory::parseTryCatch);
    handlers["throw"] = wrap(&NodeFactory::parseThrowNode);
    handlers["import"] = wrap(&NodeFactory::parseImportNative);
    handlers["from"] = wrap(&NodeFactory::parseFrom);
    handlers["var"] = [this](const std::vector<std::string_view> &t, size_t &i) {
        return parseVarDeclNode(t, i, true);
    };
    handlers["val"] = [this](const std::vector<std::string_view> &t, size_t &i) {
        return parseVarDeclNode(t, i, false);
    };
}

std::unique_ptr<ASTNode> NodeFactory::create(const std::string &command, const std::vector<std::string_view> &tokens,
                                             size_t &index) {
    if (handlers.contains(command)) return handlers[command](tokens, index);
    if (index < tokens.size() && tokens[index] == "[") {
        index++;
        auto idx = parseExpression(tokens, index);
        index++;
        index++; // skip ']', '='
        return std::make_unique<IndexAssignNode>(command, std::move(idx), parseExpression(tokens, index));
    }
    if (index < tokens.size() && tokens[index] == ".") {
        index++;
        std::string mem(tokens[index++]);
        if (index < tokens.size() && tokens[index] == "=") {
            index++;
            return std::make_unique<FieldAssignNode>(command, mem, parseExpression(tokens, index));
        }
        if (index < tokens.size() && tokens[index] == "(") {
            index++;
            std::vector<std::unique_ptr<ExpressionNode> > args;
            while (index < tokens.size() && tokens[index] != ")") {
                args.push_back(parseExpression(tokens, index));
                if (index < tokens.size() && tokens[index] == ",") index++;
            }
            index++;
            return std::make_unique<
                ExpressionStmtNode>(std::make_unique<MethodCallNode>(command, mem, std::move(args)));
        }
    }
    if (index < tokens.size() && (tokens[index] == "=" || tokens[index] == "+=" || tokens[index] == "-=" || tokens[
                                      index] == "*=" || tokens[index] == "/=")) {
        std::string op(tokens[index++]);
        if (op == "=") {
            return std::make_unique<AssignmentNode>(command, parseExpression(tokens, index));
        } else {
            op.pop_back(); // remove '='
            auto varNode = std::make_unique<VariableNode>(command);
            auto expr = parseExpression(tokens, index);
            auto binaryOp = std::make_unique<BinaryOperationNode>(std::move(varNode), std::move(expr), op);
            return std::make_unique<AssignmentNode>(command, std::move(binaryOp));
        }
    }
    if (index < tokens.size() && tokens[index] == "(") {
        index++;
        std::vector<std::unique_ptr<ExpressionNode> > args;
        while (index < tokens.size() && tokens[index] != ")") {
            args.push_back(parseExpression(tokens, index));
            if (index < tokens.size() && tokens[index] == ",") index++;
        }
        index++;
        return std::make_unique<ExpressionStmtNode>(std::make_unique<FunctionCallNode>(command, std::move(args)));
    }
    return nullptr;
}

std::unique_ptr<ExpressionNode>
NodeFactory::parseExpression(const std::vector<std::string_view> &tokens, size_t &index) {
    auto left = parseComparison(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index];
        if (op != "&&" && op != "||" && op != "==" && op != "!=" && op != "<" && op != ">" && op != "<=" && op != ">=")
            break;
        index++;
        left = std::make_unique<BinaryOperationNode>(std::move(left), parseComparison(tokens, index), std::string(op));
    }
    return left;
}

std::unique_ptr<ExpressionNode>
NodeFactory::parseComparison(const std::vector<std::string_view> &tokens, size_t &index) {
    auto left = parseAdditive(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index];
        if (op != "+" && op != "-") break;
        index++;
        left = std::make_unique<BinaryOperationNode>(std::move(left), parseAdditive(tokens, index), std::string(op));
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseAdditive(const std::vector<std::string_view> &tokens, size_t &index) {
    auto left = parseUnary(tokens, index);
    while (index < tokens.size()) {
        std::string_view op = tokens[index];
        if (op != "*" && op != "/" && op != "%") break;
        index++;
        left = std::make_unique<BinaryOperationNode>(std::move(left), parseUnary(tokens, index), std::string(op));
    }
    return left;
}

std::unique_ptr<ExpressionNode> NodeFactory::parseUnary(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index < tokens.size() && (tokens[index] == "!" || tokens[index] == "-")) {
        std::string op(tokens[index++]);
        return std::make_unique<UnaryOperationNode>(op, parseUnary(tokens, index));
    }
    return parseFactor(tokens, index);
}

std::unique_ptr<ExpressionNode> NodeFactory::parseFactor(const std::vector<std::string_view> &tokens, size_t &index) {
    std::string_view token = tokens[index++];
    if (token == "(") {
        auto expr = parseExpression(tokens, index);
        index++;
        return expr;
    }
    if (token == "new") {
        std::string type(tokens[index++]);
        if (index < tokens.size() && tokens[index] == "<") {
            int d = 0;
            do {
                if (tokens[index] == "<") d++;
                else if (tokens[index] == ">") d--;
                index++;
            } while (d > 0 && index < tokens.size());
        }
        if (index < tokens.size() && tokens[index] == "[") {
            index++;
            auto sz = parseExpression(tokens, index);
            index++;
            return std::make_unique<ArrayAllocNode>(parseTypeAnnotation(type), std::move(sz));
        } else if (index < tokens.size() && tokens[index] == "(") {
            index++;
            std::vector<std::unique_ptr<ExpressionNode> > args;
            while (index < tokens.size() && tokens[index] != ")") {
                args.push_back(parseExpression(tokens, index));
                if (index < tokens.size() && tokens[index] == ",") index++;
            }
            index++;
            return std::make_unique<FunctionCallNode>(type, std::move(args));
        }
    }
    if (token.starts_with('"')) return std::make_unique<StringNode>(std::string(token.substr(1, token.size() - 2)));
    if (token == "true") return std::make_unique<BooleanNode>(true);
    if (token == "false") return std::make_unique<BooleanNode>(false);
    if (std::isdigit(token[0])) return std::make_unique<NumberNode>(std::stoi(std::string(token)));

    std::string name(token);
    if (index < tokens.size() && tokens[index] == "[") {
        index++;
        auto idx = parseExpression(tokens, index);
        index++;
        return std::make_unique<IndexAccessNode>(std::make_unique<VariableNode>(name), std::move(idx));
    }
    if (index < tokens.size() && tokens[index] == ".") {
        index++;
        std::string mem(tokens[index++]);
        if (index < tokens.size() && tokens[index] == "(") {
            index++;
            std::vector<std::unique_ptr<ExpressionNode> > args;
            while (index < tokens.size() && tokens[index] != ")") {
                args.push_back(parseExpression(tokens, index));
                if (index < tokens.size() && tokens[index] == ",") index++;
            }
            index++;
            return std::make_unique<MethodCallNode>(name, mem, std::move(args));
        }
        return std::make_unique<FieldAccessNode>(name, mem);
    }
    if (index < tokens.size() && tokens[index] == "(") {
        index++;
        std::vector<std::unique_ptr<ExpressionNode> > args;
        while (index < tokens.size() && tokens[index] != ")") {
            args.push_back(parseExpression(tokens, index));
            if (index < tokens.size() && tokens[index] == ",") index++;
        }
        index++;
        return std::make_unique<FunctionCallNode>(name, std::move(args));
    }
    return std::make_unique<VariableNode>(name);
}

std::unique_ptr<ASTNode> NodeFactory::parseReturnNode(const std::vector<std::string_view> &tokens, size_t &index) {
    if (index >= tokens.size() || tokens[index] == "}") return std::make_unique<ReturnNode>(nullptr);
    return std::make_unique<ReturnNode>(parseExpression(tokens, index));
}

std::unique_ptr<ASTNode> NodeFactory::parseThrowNode(const std::vector<std::string_view> &t, size_t &i) {
    return std::make_unique<ThrowNode>(parseExpression(t, i));
}

std::unique_ptr<SwitchNode> NodeFactory::parseSwitchExpression(const std::vector<std::string_view> &t, size_t &i) {
    i += 2;
    auto expr = parseExpression(t, i);
    i += 2;
    std::vector<std::unique_ptr<CaseNode> > cases;
    while (i < t.size() && t[i] != "}") {
        std::unique_ptr<ExpressionNode> val = nullptr;
        if (t[i] == "case") {
            i++;
            val = parseExpression(t, i);
        } else i++;
        i++;
        cases.push_back(std::make_unique<CaseNode>(std::move(val), parseBlock(t, i), false));
    }
    i++;
    return std::make_unique<SwitchNode>(std::move(expr), std::move(cases));
}

std::unique_ptr<ASTNode> NodeFactory::parseEnumDecl(const std::vector<std::string_view> &t, size_t &i) {
    std::string name(t[i++]);
    i++;
    std::vector<std::pair<std::string, int> > vals;
    int next = 0;
    while (i < t.size() && t[i] != "}") {
        std::string en(t[i++]);
        if (i < t.size() && t[i] == "=") {
            i++;
            next = std::stoi(std::string(t[i++]));
        }
        vals.push_back({en, next++});
        if (i < t.size() && t[i] == ",") i++;
    }
    i++;
    return std::make_unique<EnumNode>(name, std::move(vals));
}

std::unique_ptr<ASTNode> NodeFactory::parseTryCatch(const std::vector<std::string_view> &t, size_t &i) {
    auto tryB = parseBlock(t, i);
    i++;
    i++;
    std::string var(t[i++]);
    i++;
    return std::make_unique<TryCatchNode>(std::move(tryB), var, parseBlock(t, i));
}
