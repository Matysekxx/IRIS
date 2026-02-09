#ifndef NODEFACTORY_H
#define NODEFACTORY_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../node/LTSNode.h"

class NodeFactory {
private:
    using Handler = std::function<std::unique_ptr<ASTNode>(const std::vector<std::string>&, size_t&)>;
    std::map<std::string, Handler> handlers;

    void init();

    std::unique_ptr<MouseBlockNode> parseMouseBlock(const std::vector<std::string>& tokens, size_t& index);
    std::unique_ptr<KeyboardBlockNode> parseKeyboardBlock(const std::vector<std::string>& tokens, size_t& index);

public:
    NodeFactory();
    std::unique_ptr<ASTNode> create(const std::string& command, const std::vector<std::string>& tokens, size_t& index);
};

#endif //NODEFACTORY_H