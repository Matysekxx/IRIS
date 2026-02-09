#include "NodeFactory.h"
#include <stdexcept>

NodeFactory::NodeFactory() {
    init();
}

void NodeFactory::init() {
    handlers["wait"] = [](const std::vector<std::string>& tokens, size_t& index) -> std::unique_ptr<ASTNode> {
        if (index >= tokens.size()) return nullptr;
        const std::string& durationStr = tokens[index++];
        int duration = 0;
        if (durationStr.ends_with("s")) {
            duration = std::stoi(durationStr.substr(0, durationStr.size() - 1)) * 1000;
        } else {
            duration = std::stoi(durationStr);
        }
        return std::make_unique<WaitNode>(duration);
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
            if (cmd == "click") {
                const std::string& btn = tokens[index++];
                return std::make_unique<HybridClickNode>(btn == "right" ? ClickNode::Right : ClickNode::Left);
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
            if (cmd == "press") {
                const std::string& key = tokens[index++];
                return std::make_unique<HybridPressNode>(key);
            }
        }
        return nullptr;
    };
}

std::unique_ptr<ASTNode> NodeFactory::create(const std::string& command, const std::vector<std::string>& tokens, size_t& index) {
    if (handlers.contains(command)) {
        return handlers[command](tokens, index);
    }
    return nullptr;
}

std::unique_ptr<MouseBlockNode> NodeFactory::parseMouseBlock(const std::vector<std::string>& tokens, size_t& index) {
    auto block = std::make_unique<MouseBlockNode>();
    while (index < tokens.size() && tokens[index] != "}") {
        const std::string& cmd = tokens[index++];
        if (cmd == "click") {
            const std::string& btn = tokens[index++];
            block->actions.push_back(std::make_unique<ClickNode>(btn == "right" ? ClickNode::Right : ClickNode::Left));
        } else if (cmd == "at") {
            const int x = std::stoi(tokens[index++]);
            if (index < tokens.size() && tokens[index] == ",") index++;
            const int y = std::stoi(tokens[index++]);
            block->actions.push_back(std::make_unique<MoveNode>(x, y, 0));
        }
    }
    if (index < tokens.size()) index++;
    return block;
}

std::unique_ptr<KeyboardBlockNode> NodeFactory::parseKeyboardBlock(const std::vector<std::string>& tokens, size_t& index) {
    auto block = std::make_unique<KeyboardBlockNode>();
    while (index < tokens.size() && tokens[index] != "}") {
        const std::string& cmd = tokens[index++];
        if (cmd == "type") {
            std::string text = tokens[index++];
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
                text = text.substr(1, text.size() - 2);
            }
            block->actions.push_back(std::make_unique<TypeNode>(text));
        } else if (cmd == "press") {
            std::string key = tokens[index++];
            block->actions.push_back(std::make_unique<PressNode>(key));
        }
    }
    if (index < tokens.size()) index++;
    return block;
}