#include "Parser.h"

#include <fstream>
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <algorithm>

#include "log/Logger.h"

using namespace iris::node;
using namespace iris::parser;

Parser::Parser(const std::string &filePath, iris::log::Logger *logger, std::unordered_set<std::string> *sharedImports) {
    this->logger = logger;
    this->filePath = filePath;
    this->sharedImports = sharedImports;

    if (this->sharedImports == nullptr) {
        this->rootImports = std::make_unique<std::unordered_set<std::string> >();
        this->sharedImports = this->rootImports.get();
    }

    std::error_code ec;
    std::string canonicalPath = std::filesystem::canonical(filePath, ec).generic_string();
    if (!ec) {
        this->sharedImports->insert(canonicalPath);
    }

    this->file = std::ifstream(filePath, std::ios::ate | std::ios::binary);
    if (!this->file.is_open()) {
        throw std::runtime_error("File could not be opened: " + filePath);
    }
}

Parser::~Parser() {
    if (this->file.is_open()) {
        this->file.close();
    }
}

void Parser::parse() {
    const size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    this->sourceCode.resize(fileSize);
    file.read(this->sourceCode.data(), static_cast<long long>(fileSize));

    tokenize(this->sourceCode);

    try {
        this->program = parseProgram();
    } catch (const std::exception &e) {
        logger->error(std::string("Parsing error: ") + e.what());
        throw;
    }
}

struct CharTraits {
    uint8_t bits[256];
    static constexpr uint8_t IS_WHITESPACE = 1;
    static constexpr uint8_t IS_DELIMITER = 2;

    constexpr CharTraits() : bits{} {
        for (int i = 0; i < 256; i++) {
            char c = (char)i;
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') bits[i] |= IS_WHITESPACE;
            if (c == '{' || c == '}' || c == ',' || c == '.' || c == '+' || c == '-' ||
                c == '*' || c == '/' || c == '%' || c == '=' || c == '(' || c == ')' ||
                c == '[' || c == ']' || c == '<' || c == '>' || c == '!' || c == '&' ||
                c == '|' || c == '^' || c == ';' || c == ':') bits[i] |= IS_DELIMITER;
        }
    }
};

static constexpr CharTraits charTraits;

inline bool isDelimiter(char c) {
    return charTraits.bits[(uint8_t)c] & CharTraits::IS_DELIMITER;
}

inline bool isWhitespace(char c) {
    return charTraits.bits[(uint8_t)c] & CharTraits::IS_WHITESPACE;
}

#include <unordered_map>

static const std::unordered_map<std::string_view, TokenKind> KEYWORDS = {
    {"import", TokenKind::IMPORT}, {"fun", TokenKind::FUN}, {"var", TokenKind::VAR},
    {"val", TokenKind::VAL}, {"if", TokenKind::IF}, {"else", TokenKind::ELSE},
    {"while", TokenKind::WHILE}, {"for", TokenKind::FOR}, {"repeat", TokenKind::REPEAT},
    {"return", TokenKind::RETURN}, {"class", TokenKind::CLASS}, {"abstract", TokenKind::ABSTRACT},
    {"interface", TokenKind::INTERFACE}, {"enum", TokenKind::ENUM}, {"try", TokenKind::TRY},
    {"catch", TokenKind::CATCH}, {"throw", TokenKind::THROW}, {"switch", TokenKind::SWITCH},
    {"case", TokenKind::CASE}, {"default", TokenKind::DEFAULT}, {"true", TokenKind::TRUE_VAL},
    {"false", TokenKind::FALSE_VAL}, {"null", TokenKind::NULL_VAL}, {"wait", TokenKind::WAIT},
    {"native", TokenKind::NATIVE}, {"from", TokenKind::FROM}, {"as", TokenKind::AS},
    {"export", TokenKind::EXPORT}
};

static const std::unordered_map<std::string_view, TokenKind> OPERATORS = {
    {"{", TokenKind::LBRACE}, {"}", TokenKind::RBRACE}, {"(", TokenKind::LPAREN},
    {")", TokenKind::RPAREN}, {"[", TokenKind::LBRACKET}, {"]", TokenKind::RBRACKET},
    {",", TokenKind::COMMA}, {".", TokenKind::DOT}, {":", TokenKind::COLON},
    {";", TokenKind::SEMICOLON}, {"+", TokenKind::PLUS}, {"-", TokenKind::MINUS},
    {"*", TokenKind::STAR}, {"/", TokenKind::SLASH}, {"%", TokenKind::PERCENT},
    {"=", TokenKind::EQUAL}, {"==", TokenKind::EQ_EQ}, {"!=", TokenKind::NOT_EQ},
    {"<", TokenKind::LT}, {">", TokenKind::GT}, {"<=", TokenKind::LE},
    {">=", TokenKind::GE}, {"&&", TokenKind::AND}, {"||", TokenKind::OR},
    {"!", TokenKind::NOT}, {"&", TokenKind::BIT_AND}, {"|", TokenKind::BIT_OR},
    {"^", TokenKind::BIT_XOR}, {"<<", TokenKind::SHL}, {">>", TokenKind::SHR},
    {"++", TokenKind::INC}, {"--", TokenKind::DEC}
};

void Parser::tokenize(std::string_view source) {
    tokens.reserve(source.length() / 4);

    size_t i = 0;
    const size_t len = source.length();
    
    // Skip UTF-8 BOM
    if (len >= 3 && (uint8_t)source[0] == 0xEF && (uint8_t)source[1] == 0xBB && (uint8_t)source[2] == 0xBF) {
        i = 3;
    }

    int line = 1;
    int column = 1;

    while (i < len) {
        const char c = source[i];

        if (isWhitespace(c)) {
            if (c == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
            i++;
            continue;
        }

        if (c == '/' && i + 1 < len) {
            if (source[i + 1] == '/') {
                i += 2;
                column += 2;
                if (i < len && source[i] == '/') {
                    i++;
                    column++;
                    std::string doc;
                    while (i < len && source[i] != '\n') {
                        if (source[i] != '\r') doc += source[i];
                        i++;
                        column++;
                    }
                    while (!doc.empty() && (doc.front() == ' ' || doc.front() == '\t')) doc.erase(doc.begin());
                    docComments[line] = doc;
                } else {
                    while (i < len && source[i] != '\n') {
                        i++;
                        column++;
                    }
                }
                continue;
            }
            if (source[i + 1] == '*') {
                i += 2;
                column += 2;
                std::string doc;
                while (i + 1 < len && !(source[i] == '*' && source[i + 1] == '/')) {
                    if (source[i] == '\n') {
                        if (!doc.empty() && doc.back() != ' ') doc += ' ';
                        line++;
                        column = 1;
                    } else {
                        doc += source[i];
                        column++;
                    }
                    i++;
                }
                if (i + 1 < len) {
                    i += 2;
                    column += 2;
                }
                if (source[i - 2] == '/' && source[i - 3] != ' ' && !doc.empty() && doc.back() == ' ') doc.pop_back();
                docComments[line] = doc;
                continue;
            }
        }

        if (c == '"') {
            const size_t start = i;
            const int startLine = line;
            const int startColumn = column;

            i++; // skip "
            column++;
            while (i < len && source[i] != '"') {
                if (source[i] == '\\' && i + 1 < len) {
                    i += 2;
                    column += 2;
                    continue;
                }
                if (source[i] == '\n') {
                    line++;
                    column = 1;
                } else {
                    column++;
                }
                i++;
            }

            if (i < len && source[i] == '"') {
                i++; // skip "
                column++;
                tokens.emplace_back(source.substr(start, i - start), TokenKind::STRING, startLine, startColumn, filePath);
            } else {
                throw std::runtime_error("Never ending string starting at line " + std::to_string(startLine) + ":" + std::to_string(startColumn));
            }
            continue;
        }

        if (isDelimiter(c)) {
            const int startLine = line;
            const int startColumn = column;
            if (i + 1 < len) {
                const char next = source[i + 1];
                std::string_view op2 = source.substr(i, 2);
                if (auto it = OPERATORS.find(op2); it != OPERATORS.end()) {
                    tokens.emplace_back(op2, it->second, startLine, startColumn, filePath);
                    i += 2; column += 2;
                    continue;
                }
            }
            std::string_view op1 = source.substr(i, 1);
            if (auto it = OPERATORS.find(op1); it != OPERATORS.end()) {
                tokens.emplace_back(op1, it->second, startLine, startColumn, filePath);
            } else {
                tokens.emplace_back(op1, TokenKind::UNKNOWN, startLine, startColumn, filePath);
            }
            i++; column++;
            continue;
        }

        const size_t start = i;
        const int startLine = line;
        const int startColumn = column;
        while (i < len) {
            const char ch = source[i];
            if (isWhitespace(ch) || ch == '"') break;
            if (ch == '.' && i > start && i + 1 < len &&
                std::isdigit(static_cast<unsigned char>(source[start])) &&
                std::isdigit(static_cast<unsigned char>(source[i + 1]))) {
                i++; column++;
                continue;
            }
            if (isDelimiter(ch)) break;
            i++; column++;
        }
        if (start < i) {
            std::string_view val = source.substr(start, i - start);
            if (std::isdigit(static_cast<unsigned char>(val[0]))) {
                tokens.emplace_back(val, TokenKind::NUMBER, startLine, startColumn, filePath);
            } else if (auto it = KEYWORDS.find(val); it != KEYWORDS.end()) {
                tokens.emplace_back(val, it->second, startLine, startColumn, filePath);
            } else {
                tokens.emplace_back(val, TokenKind::IDENTIFIER, startLine, startColumn, filePath);
            }
        } else if (i < len) {
            i++; column++;
        }
    }
}

std::unique_ptr<ProgramNode> Parser::parseProgram() {
    auto prog = std::make_unique<ProgramNode>();
    while (currentToken < tokens.size()) {
        const std::string_view token = tokens[currentToken].value;

        // Handle file imports: import "path/to/file"
        if (token == "import" && currentToken + 1 < tokens.size() && tokens[currentToken + 1].type == TokenKind::STRING) {
            currentToken++;
            if (currentToken >= tokens.size()) {
                throw std::runtime_error("Expected string literal after 'import'");
            }
            std::string path(tokens[currentToken++].value);

            if (path.length() >= 2 && path.front() == '"' && path.back() == '"') {
                path = path.substr(1, path.length() - 2);
            } else {
                throw std::runtime_error("Import path must be a string literal");
            }

            std::filesystem::path resolvedPath = resolveModulePath(path);
            std::error_code ec;
            std::string canonicalStr = std::filesystem::weakly_canonical(resolvedPath, ec).generic_string();

            if (this->sharedImports->find(canonicalStr) == this->sharedImports->end()) {
                this->sharedImports->insert(canonicalStr);

                try {
                    if (!std::filesystem::exists(resolvedPath)) {
                        throw std::runtime_error("Module file not found: " + resolvedPath.string());
                    }

                    Parser subParser(resolvedPath.string(), this->logger, this->sharedImports);
                    subParser.parse();
                    auto subProg = subParser.getProgram();

                    for (auto &stmt: subProg->statements) {
                        prog->statements.push_back(std::move(stmt));
                    }
                } catch (const std::exception &e) {
                    logger->error("Failed to import module '" + path + "': " + e.what());
                }
            }
        }
        // Handle JS-style imports: import { a, b } from "path", import x from "path", import * as ns from "path"
        // Also catches 'import native' (no module file to load)
        else if (token == "import" && currentToken + 1 < tokens.size()) {
            size_t startLine = tokens[currentToken].line;
            currentToken++;
            size_t savedIndex = currentToken - 1;
            auto importNode = factory.create("import", tokens, currentToken);
            if (!importNode) {
                currentToken = savedIndex;
                continue;
            }
            auto it = docComments.find(startLine);
            if (it != docComments.end()) {
                importNode->doc = it->second;
            }

            std::string modulePath;
            if (auto *named = dynamic_cast<ImportNamedNode *>(importNode.get())) {
                modulePath = named->modulePath;
            } else if (auto *def = dynamic_cast<ImportDefaultNode *>(importNode.get())) {
                modulePath = def->modulePath;
            } else if (auto *ns = dynamic_cast<ImportNamespaceNode *>(importNode.get())) {
                modulePath = ns->modulePath;
            }

            if (!modulePath.empty()) {
                std::filesystem::path resolvedPath = resolveModulePath(modulePath);
                std::error_code ec;
                std::string canonicalStr = std::filesystem::weakly_canonical(resolvedPath, ec).generic_string();

                if (this->sharedImports->find(canonicalStr) == this->sharedImports->end()) {
                    this->sharedImports->insert(canonicalStr);

                    try {
                        if (!std::filesystem::exists(resolvedPath)) {
                            throw std::runtime_error("Module file not found: " + resolvedPath.string());
                        }

                        Parser subParser(resolvedPath.string(), this->logger, this->sharedImports);
                        subParser.parse();
                        auto subProg = subParser.getProgram();

                        // Collect exported symbol names from the module
                        std::unordered_map<std::string, std::string> exportedSymbols;
                        for (auto &stmt : subProg->statements) {
                            if (stmt->getStmtType() == StmtType::Export) {
                                auto *exportNode = static_cast<ExportNode *>(stmt.get());
                                if (exportNode->declaration) {
                                    if (auto *fd = dynamic_cast<FunctionDeclNode *>(exportNode->declaration.get())) {
                                        exportedSymbols[fd->name] = fd->name;
                                    } else if (auto *vd = dynamic_cast<VarDeclNode *>(exportNode->declaration.get())) {
                                        exportedSymbols[vd->nameOfVariable] = vd->nameOfVariable;
                                    } else if (auto *cd = dynamic_cast<ClassDeclNode *>(exportNode->declaration.get())) {
                                        exportedSymbols[cd->name] = cd->name;
                                    }
                                }
                                if (exportNode->isDefault) {
                                    exportedSymbols["__default__"] = "__default__";
                                }
                            }
                            // Also track non-wrapped declarations as implicit exports for splicing
                            if (auto *fd = dynamic_cast<FunctionDeclNode *>(stmt.get())) {
                                exportedSymbols[fd->name] = fd->name;
                            } else if (auto *vd = dynamic_cast<VarDeclNode *>(stmt.get())) {
                                exportedSymbols[vd->nameOfVariable] = vd->nameOfVariable;
                            } else if (auto *cd = dynamic_cast<ClassDeclNode *>(stmt.get())) {
                                exportedSymbols[cd->name] = cd->name;
                            }
                        }
                        (void)exportedSymbols; // Reserved for future name-filtering

                        // Splice all exported declarations into parent
                        for (auto &stmt : subProg->statements) {
                            if (stmt->getStmtType() == StmtType::Export) {
                                auto *exportNode = static_cast<ExportNode *>(stmt.get());
                                if (exportNode->declaration) {
                                    prog->statements.push_back(std::move(exportNode->declaration));
                                }
                            } else {
                                prog->statements.push_back(std::move(stmt));
                            }
                        }
                    } catch (const std::exception &e) {
                        logger->error("Failed to import module '" + modulePath + "': " + e.what());
                    }
                }
            }

            // Add the import node itself (for tracking/reference)
            prog->statements.push_back(std::move(importNode));
        } else {
            // Handle regular statements, including 'import native'
            if (std::unique_ptr<ASTNode> stmt = parseStatement()) {
                prog->statements.push_back(std::move(stmt));
            }
        }
    }
    return prog;
}

std::unique_ptr<ASTNode> Parser::parseStatement() {
    if (currentToken >= tokens.size()) return nullptr;
    const Token& token = tokens[currentToken++];
    auto node = factory.create(std::string(token.value), tokens, currentToken);
    if (node) {
        auto it = docComments.find(token.line);
        if (it != docComments.end()) {
            node->doc = it->second;
        }
    }
    return node;
}

static std::filesystem::path findIrisStd(const std::string &modPath); // forward decl

std::filesystem::path Parser::resolveModulePath(const std::string &path) const {
    std::filesystem::path resolvedPath;
    if (path.starts_with("iris:")) {
        std::string stlModule = path.substr(5);
        if (!stlModule.ends_with(".iris")) stlModule += ".iris";
        resolvedPath = findIrisStd(stlModule);
    } else {
        std::string modPath = path;
        if (!modPath.ends_with(".iris")) {
            std::replace(modPath.begin(), modPath.end(), '.', '/');
            modPath += ".iris";
        }

        // 1. Try relative to the source file's directory
        std::filesystem::path currentDir = std::filesystem::path(this->filePath).parent_path();
        resolvedPath = currentDir / modPath;

        // 2. Fallback to iris_std/ directory (search from project root)
        if (!std::filesystem::exists(resolvedPath)) {
            resolvedPath = findIrisStd(modPath);
        }
    }
    return resolvedPath;
}

/// Search for a module in iris_std/ by checking common locations relative to cwd.
static std::filesystem::path findIrisStd(const std::string &modPath) {
    auto cwd = std::filesystem::current_path();
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(cwd / "iris_std" / modPath);
    candidates.push_back(cwd / ".." / "iris_std" / modPath);
    candidates.push_back(cwd / ".." / ".." / "iris_std" / modPath);

    for (auto &cand : candidates) {
        std::error_code ec;
        auto canon = std::filesystem::weakly_canonical(cand, ec);
        if (!ec && std::filesystem::exists(canon)) {
            return canon;
        }
    }
    return candidates[0];
}
