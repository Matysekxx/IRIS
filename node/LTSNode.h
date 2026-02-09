
#ifndef LTSNODE_H
#define LTSNODE_H
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <string>

class Logger;

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void execute(Logger* logger) = 0;
};

class ProgramNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> statements;
    void execute(Logger* logger) override;
};

class WaitNode : public ASTNode {
public:
    int duration;
    explicit WaitNode(const int d) : duration(d) {}
    void execute(Logger* logger) override;
};

class MouseBlockNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> actions;
    void execute(Logger* logger) override;
};

class ClickNode : public ASTNode {
public:
    enum Button { Left, Right };
    Button button;
    std::optional<int> x;
    std::optional<int> y;

    explicit ClickNode(Button b, std::optional<int> px = std::nullopt, const std::optional<int> py = std::nullopt)
        : button(b), x(px), y(py) {}
    void execute(Logger* logger) override;
};

class MoveNode : public ASTNode {
public:
    int x, y;
    int speed;
    MoveNode(const int px, const int py, const int s) : x(px), y(py), speed(s) {}
    void execute(Logger* logger) override;
};

class KeyboardBlockNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> actions;
    void execute(Logger* logger) override;
};

class TypeNode : public ASTNode {
public:
    std::string text;
    explicit TypeNode(std::string  t) : text(std::move(t)) {}
    void execute(Logger* logger) override;
};

class PressNode : public ASTNode {
public:
    std::string key;
    explicit PressNode(std::string  k) : key(std::move(k)) {}
    void execute(Logger* logger) override;
};

class HybridClickNode : public ASTNode {
public:
    ClickNode::Button button;
    explicit HybridClickNode(const ClickNode::Button b) : button(b) {}
    void execute(Logger* logger) override;
};

class HybridPressNode : public ASTNode {
public:
    std::string key;
    explicit HybridPressNode(std::string  k) : key(std::move(k)) {}
    void execute(Logger* logger) override;
};

#endif //LTSNODE_H
