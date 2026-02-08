
#include "Parser.h"

#include <fstream>
#include <stdexcept>
#include <sstream>
#include <iostream>

#include "../log/Logger.h"

Parser::Parser(const std::string& filePath, Logger* logger) {
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
    } catch (const std::exception& e) {
        logger->error(std::string("Parsing error: ") + e.what());
    }
}

void Parser::tokenize(const std::string &source) {
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
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
        } else if (c == '{' || c == '}' || c == ',' || c == '.') {
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
            tokens.emplace_back(1, c);
        } else if (c == '"') {
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
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
        auto stmt = parseStatement();
        if (stmt) program->statements.push_back(std::move(stmt));
    }
    return program;
}

std::unique_ptr<ASTNode> Parser::parseStatement() {
    if (currentToken >= tokens.size()) return nullptr;
    const std::string token = tokens[currentToken++];

    if (token == "wait") {
        const std::string durationStr = tokens[currentToken++];
        int duration = 0;
        if (durationStr.ends_with("s")) {
            duration = std::stoi(durationStr.substr(0, durationStr.size() - 1)) * 1000;
        } else {
            duration = std::stoi(durationStr);
        }
        return std::make_unique<WaitNode>(duration);
    }

    if (token == "mouse") {
        if (tokens[currentToken] == "{") {
            currentToken++;
            return parseMouseBlock();
        }
        if (tokens[currentToken] == ".") {
            currentToken++;
            std::string cmd = tokens[currentToken++];
            if (cmd == "click") {
                const std::string btn = tokens[currentToken++];
                return std::make_unique<HybridClickNode>(btn == "right" ? ClickNode::Right : ClickNode::Left);
            }
        }
    }

    if (token == "keyboard") {
        if (tokens[currentToken] == "{") {
            currentToken++;
            return parseKeyboardBlock();
        }
        if (tokens[currentToken] == ".") {
            currentToken++;
            std::string cmd = tokens[currentToken++];
            if (cmd == "press") {
                const std::string key = tokens[currentToken++];
                return std::make_unique<HybridPressNode>(key);
            }
        }
    }
    return nullptr;
}

std::unique_ptr<MouseBlockNode> Parser::parseMouseBlock() {
    auto block = std::make_unique<MouseBlockNode>();
    while (tokens[currentToken] != "}") {
        std::string cmd = tokens[currentToken++];
        if (cmd == "click") {
            std::string btn = tokens[currentToken++];
            block->actions.push_back(std::make_unique<ClickNode>(btn == "right" ? ClickNode::Right : ClickNode::Left));
        } else if (cmd == "at") {
            int x = std::stoi(tokens[currentToken++]);
            if (tokens[currentToken] == ",") currentToken++;
            int y = std::stoi(tokens[currentToken++]);
            block->actions.push_back(std::make_unique<MoveNode>(x, y, 0));
        }
    }
    currentToken++;
    return block;
}

std::unique_ptr<KeyboardBlockNode> Parser::parseKeyboardBlock() {
    auto block = std::make_unique<KeyboardBlockNode>();
    while (tokens[currentToken] != "}") {
        std::string cmd = tokens[currentToken++];
        if (cmd == "type") {
            std::string text = tokens[currentToken++];
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
                text = text.substr(1, text.size() - 2);
            }
            block->actions.push_back(std::make_unique<TypeNode>(text));
        } else if (cmd == "press") {
            std::string key = tokens[currentToken++];
            block->actions.push_back(std::make_unique<PressNode>(key));
        }
    }
    currentToken++;
    return block;
}



Parser::~Parser() {
    if (this->file.is_open()) {
        this->file.close();
    }
}
