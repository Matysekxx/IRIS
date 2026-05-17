#include "Parser.h"

#include <fstream>
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <algorithm>

#include "../log/Logger.h"

using namespace iris::node;
using namespace iris::parser;

Parser::Parser(const std::string &filePath, iris::log::Logger *logger, std::unordered_set<std::string>* sharedImports) {
    this->logger = logger;
    this->filePath = filePath;
    this->sharedImports = sharedImports;
    
    if (this->sharedImports == nullptr) {
        this->rootImports = std::make_unique<std::unordered_set<std::string>>();
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
    }
}

constexpr bool isDelimiter(char c) {
    switch (c) {
        case '{':
        case '}':
        case ',':
        case '.':
        case '+':
        case '-':
        case '*':
        case '/':
        case '%':
        case '=':
        case '(':
        case ')':
        case '[':
        case ']':
        case '<':
        case '>':
        case '!':
        case '&':
        case '|':
        case '^':
        case ';':
        case ':':
            return true;
        default:
            return false;
    }
}

constexpr bool isWhitespace(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

void Parser::tokenize(std::string_view source) {
    tokens.reserve(source.length() / 4);

    size_t i = 0;
    const size_t len = source.length();

    while (i < len) {
        const char c = source[i];

        if (isWhitespace(c)) {
            i++;
            continue;
        }

        if (c == '/' && i + 1 < len && source[i + 1] == '/') {
            i += 2;
            while (i < len && source[i] != '\n') i++;
            continue;
        }

        if (c == '"') {
            const size_t start = i;
            const size_t end = source.find('"', start + 1);

            if (end != std::string_view::npos) {
                tokens.emplace_back(source.substr(start, end - start + 1));
                i = end + 1;
            } else {
                throw std::runtime_error("Never ending string starting at index " + std::to_string(start));
            }
            continue;
        }

        if (isDelimiter(c)) {
            if (i + 1 < len) {
                if (const char next = source[i + 1];
                    (c == '&' && next == '&') || (c == '|' && next == '|') ||
                    (c == '=' && next == '=') || (c == '!' && next == '=') ||
                    (c == '<' && next == '=') || (c == '>' && next == '=') ||
                    (c == '<' && next == '<') || (c == '>' && next == '>')) {
                    tokens.emplace_back(source.substr(i, 2));
                    i += 2;
                    continue;
                }
            }
            tokens.emplace_back(source.substr(i, 1));
            i++;
            continue;
        }

        const size_t start = i;
        while (i < len) {
            const char ch = source[i];
            if (isWhitespace(ch) || ch == '"') break;
            if (ch == '.' && i > start && i + 1 < len &&
                std::isdigit(static_cast<unsigned char>(source[i - 1])) &&
                std::isdigit(static_cast<unsigned char>(source[i + 1]))) {
                i++;
                continue;
            }
            if (isDelimiter(ch)) break;
            i++;
        }
        tokens.emplace_back(source.substr(start, i - start));
    }
}

std::unique_ptr<ProgramNode> Parser::parseProgram() {
    auto prog = std::make_unique<ProgramNode>();
    while (currentToken < tokens.size()) {
        const std::string_view token = tokens[currentToken];
        
        // Handle file imports: import "path/to/file"
        if (token == "import" && (currentToken + 1 < tokens.size() && tokens[currentToken+1] != "native")) {
            currentToken++;
            if (currentToken >= tokens.size()) {
                throw std::runtime_error("Expected string literal after 'import'");
            }
            std::string path(tokens[currentToken++]);
            
            if (path.length() >= 2 && path.front() == '"' && path.back() == '"') {
                path = path.substr(1, path.length() - 2);
            } else {
                throw std::runtime_error("Import path must be a string literal");
            }

            std::filesystem::path resolvedPath;
            if (path.starts_with("iris:")) {
                std::string stlModule = path.substr(5);
                if (!stlModule.ends_with(".iris")) stlModule += ".iris";
                resolvedPath = std::filesystem::current_path() / "std" / stlModule;
            } else {
                if (!path.ends_with(".iris")) {
                    std::replace(path.begin(), path.end(), '.', '/');
                    path += ".iris";
                }
                
                // 1. Try local path relative to current file
                std::filesystem::path currentDir = std::filesystem::path(this->filePath).parent_path();
                resolvedPath = currentDir / path;
                
                // 2. Fallback to std/ directory if not found locally
                if (!std::filesystem::exists(resolvedPath)) {
                    std::filesystem::path stdPath = std::filesystem::current_path() / "std" / path;
                    if (std::filesystem::exists(stdPath)) {
                        resolvedPath = stdPath;
                    }
                }
            }

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
                    
                    for (auto& stmt : subProg->statements) {
                        prog->statements.push_back(std::move(stmt));
                    }
                } catch (const std::exception& e) {
                    logger->error("Failed to import module '" + path + "': " + e.what());
                }
            }
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
    const std::string_view token = tokens[currentToken++];
    return factory.create(std::string(token), tokens, currentToken);
}
