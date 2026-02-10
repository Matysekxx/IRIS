#include "Parser.h"

#include <fstream>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <set>

#include "../log/Logger.h"

Parser::Parser(const std::string &filePath, Logger *logger) {
    this->logger = logger;
    this->file = std::ifstream(filePath);
    if (!this->file.is_open()) {
        throw std::runtime_error("File could not be opened");
    }
}

void Parser::parse() {
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    tokenize(content);

    try {
        this->program = parseProgram();
    } catch (const std::exception &e) {
        logger->error(std::string("Parsing error: ") + e.what());
    }
}

void Parser::tokenize(const std::string &source) {
    static const std::set delimiters = {
            '{', '}', ',', '.', '+', '-', '*', '/', '=', '(', ')'
    };

    std::string current;
    bool inString = false;

    for (size_t i = 0; i < source.length(); ++i) {
        char c = source[i];

        if (inString) {
            if (c == '"') {
                inString = false;
                tokens.push_back("\"" + current + "\"");
                current.clear();
            } else {
                current += c;
            }
            continue;
        }

        if (std::isspace(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else if (delimiters.contains(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            tokens.emplace_back(1, c);
        } else if (c == '"') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            inString = true;
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);
}

std::unique_ptr<ProgramNode> Parser::parseProgram() {
    auto program = std::make_unique<ProgramNode>();
    while (currentToken < tokens.size()) {
        std::unique_ptr<ASTNode> stmt = parseStatement();
        if (stmt) program->statements.push_back(std::move(stmt));
    }
    return program;
}

std::unique_ptr<ASTNode> Parser::parseStatement() {
    if (currentToken >= tokens.size()) return nullptr;
    const std::string token = tokens[currentToken++];

    return factory.create(token, tokens, currentToken);
}



Parser::~Parser() {
    if (this->file.is_open()) {
        this->file.close();
    }
}
