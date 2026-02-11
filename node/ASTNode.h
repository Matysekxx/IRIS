
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
    explicit VarDeclNode(std::string name, std::unique_ptr<ExpressionNode> expr) : nameOfVariable(std::move(name)), expression(std::move(expr)) {}
};

class WaitNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> duration;
    explicit WaitNode(std::unique_ptr<ExpressionNode> duration) : duration(std::move(duration)) {}
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
    std::unique_ptr<ExpressionNode> x;
    std::unique_ptr<ExpressionNode> y;
    MoveNode(std::unique_ptr<ExpressionNode> px,
        std::unique_ptr<ExpressionNode> py) : x(std::move(px)), y(std::move(py)){}
    void execute(RuntimeContext* ctx) override;
};

class ShiftNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> deltaX;
    std::unique_ptr<ExpressionNode> deltaY;
    ShiftNode(std::unique_ptr<ExpressionNode> dx,
        std::unique_ptr<ExpressionNode> dy): deltaX(std::move(dx)), deltaY(std::move(dy)) {}
    void execute(RuntimeContext *ctx) override;
};

class KeyboardBlockNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> actions;
    void execute(RuntimeContext* ctx) override;
};

class TypeNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> text;
    explicit TypeNode(std::unique_ptr<ExpressionNode> text) : text(std::move(text)) {}
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
