#include "Parser.h"

#include <fstream>
#include <stdexcept>
#include <iostream>

#include "../log/Logger.h"

Parser::Parser(const std::string &filePath, Logger *logger) {
    this->logger = logger;
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
    file.read(this->sourceCode.data(), fileSize);

    tokenize(this->sourceCode);

    try {
        this->program = parseProgram();
    } catch (const std::exception &e) {
        logger->error(std::string("Parsing error: ") + e.what());
    }
}

constexpr bool isDelimiter(char c) {
    switch (c) {
        case '{': case '}': case ',': case '.':
        case '+': case '-': case '*': case '/':
        case '=': case '(': case ')':
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
            tokens.emplace_back(source.substr(i, 1));
            i++;
            continue;
        }

        const size_t start = i;
        while (i < len) {
            char ch = source[i];
            if (isWhitespace(ch) || isDelimiter(ch) || ch == '"') {
                break;
            }
            i++;
        }
        tokens.emplace_back(source.substr(start, i - start));
    }
}

std::unique_ptr<ProgramNode> Parser::parseProgram() {
    auto prog = std::make_unique<ProgramNode>();
    while (currentToken < tokens.size()) {
        if (std::unique_ptr<ASTNode> stmt = parseStatement()) prog->statements.push_back(std::move(stmt));
    }
    return prog;
}

std::unique_ptr<ASTNode> Parser::parseStatement() {
    if (currentToken >= tokens.size()) return nullptr;
    const std::string_view token = tokens[currentToken++];
    return factory.create(std::string(token), tokens, currentToken);
}