
#ifndef LTSNODE_H
#define LTSNODE_H
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <string>

#include "../execute/RuntimeContext.h"

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void execute(RuntimeContext* ctx) = 0;
};

class ExpressionNode {
public:
    virtual ~ExpressionNode() = default;
    virtual Value evaluate(RuntimeContext* ctx) = 0;
};

class NumberNode : public ExpressionNode {
public:
    int value;
    explicit NumberNode(int value) : value(value) {}
    Value evaluate(RuntimeContext* ctx) override;
};

class VariableNode : public ExpressionNode {
public:
    std::string nameOfVariable;
    explicit VariableNode(std::string name) : nameOfVariable(std::move(name)) {}
    Value evaluate(RuntimeContext* ctx) override;
};

class StringNode : public ExpressionNode {
public:
    std::string value;
    explicit StringNode(std::string value) : value(std::move(value)) {}
    Value evaluate(RuntimeContext* ctx) override;
};

class BinaryOperationNode : public ExpressionNode {
public:
    std::unique_ptr<ExpressionNode> leftNode;
    std::unique_ptr<ExpressionNode> rightNode;
    char operation{};
    explicit BinaryOperationNode(std::unique_ptr<ExpressionNode> leftNode,
        std::unique_ptr<ExpressionNode> rightNode, char operation) :
    leftNode(std::move(leftNode)), rightNode(std::move(rightNode)), operation(operation) {}
    Value evaluate(RuntimeContext* ctx) override;
};

class ProgramNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> statements;
    void execute(RuntimeContext* ctx) override;
};

class VarDeclNode : public ASTNode {
public:
    std::string nameOfVariable;
    std::unique_ptr<ExpressionNode> expression;
    void execute(RuntimeContext *ctx) override;
};

class WaitNode : public ASTNode {
public:
    int duration;
    explicit WaitNode(const int d) : duration(d) {}
    void execute(RuntimeContext* ctx) override;
};

class MouseBlockNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> actions;
    void execute(RuntimeContext* ctx) override;
};

class ClickNode : public ASTNode {
public:
    enum Button { Left, Right };
    Button button;
    std::optional<int> x;
    std::optional<int> y;

    explicit ClickNode(Button b, std::optional<int> px = std::nullopt, const std::optional<int> py = std::nullopt)
        : button(b), x(px), y(py) {}
    void execute(RuntimeContext* ctx) override;
};

class MoveNode : public ASTNode {
public:
    int x, y;
    int speed;
    MoveNode(const int px, const int py, const int s) : x(px), y(py), speed(s) {}
    void execute(RuntimeContext* ctx) override;
};

class KeyboardBlockNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> actions;
    void execute(RuntimeContext* ctx) override;
};

class TypeNode : public ASTNode {
public:
    std::string text;
    explicit TypeNode(std::string  t) : text(std::move(t)) {}
    void execute(RuntimeContext* ctx) override;
};

class PressNode : public ASTNode {
public:
    std::string key;
    explicit PressNode(std::string  k) : key(std::move(k)) {}
    void execute(RuntimeContext* ctx) override;
};

class HybridClickNode : public ASTNode {
public:
    ClickNode::Button button;
    explicit HybridClickNode(const ClickNode::Button b) : button(b) {}
    void execute(RuntimeContext* ctx) override;
};

class HybridPressNode : public ASTNode {
public:
    std::string key;
    explicit HybridPressNode(std::string  k) : key(std::move(k)) {}
    void execute(RuntimeContext* ctx) override;
};

#endif //LTSNODE_H
