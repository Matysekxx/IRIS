
#ifndef LTSNODE_H
#define LTSNODE_H
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <string>

/**
 * @brief Optional type annotation for variables and function parameters.
 */
enum class TypeAnnotation : uint8_t {
    None   = 0,
    Int    = 1,
    Double = 2,
    Bool   = 3,
    String = 4,
    IntArray = 5,
    DoubleArray = 6,
    BoolArray = 7,
    StringArray = 8
};

/** @brief Maps tag string ("int","double",...) to TypeAnnotation. Returns None for unknown. */
inline TypeAnnotation parseTypeAnnotation(std::string_view s) {
    if (s == "int")    return TypeAnnotation::Int;
    if (s == "double") return TypeAnnotation::Double;
    if (s == "bool")   return TypeAnnotation::Bool;
    if (s == "string") return TypeAnnotation::String;
    if (s == "int[]")    return TypeAnnotation::IntArray;
    if (s == "double[]") return TypeAnnotation::DoubleArray;
    if (s == "bool[]")   return TypeAnnotation::BoolArray;
    if (s == "string[]") return TypeAnnotation::StringArray;
    return TypeAnnotation::None;
}

inline const char* typeAnnotationName(TypeAnnotation t) {
    switch (t) {
        case TypeAnnotation::None:   return "none";
        case TypeAnnotation::Int:    return "int";
        case TypeAnnotation::Double: return "double";
        case TypeAnnotation::Bool:   return "bool";
        case TypeAnnotation::String: return "string";
        case TypeAnnotation::IntArray: return "int[]";
        case TypeAnnotation::DoubleArray: return "double[]";
        case TypeAnnotation::BoolArray: return "bool[]";
        case TypeAnnotation::StringArray: return "string[]";
    }
    return "unknown";
}


enum class StmtType {
    Program, Repeat, While, For, Print, VarDecl, Assignment,
    Wait, MouseBlock, Click, Move, Shift, KeyboardBlock,
    Write, Press, If, Break, Continue, FunctionDecl, Return,
    ClassDecl, FieldAssign, ExprStmt, IndexAssign
};

enum class ExprType {
    Number, Double, Boolean, Variable, String,
    BinaryOp, UnaryOp, FunctionCall,
    FieldAccess, MethodCall, IndexAccess,
    ArrayAlloc, ArrayLiteral
};


class ASTNode {
public:
    virtual ~ASTNode() = default;
    [[nodiscard]] virtual StmtType getType() const = 0;
};

class ExpressionNode {
public:
    virtual ~ExpressionNode() = default;
    [[nodiscard]] virtual ExprType getType() const = 0;
};


class NumberNode : public ExpressionNode {
public:
    int value;
    explicit NumberNode(int value) : value(value) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::Number; }
};

class DoubleNode : public ExpressionNode {
public:
    double value;
    explicit DoubleNode(double value) : value(value) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::Double; }
};

class BooleanNode : public ExpressionNode {
public:
    bool value;
    explicit BooleanNode(const bool value) : value(value) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::Boolean; }
};

class VariableNode : public ExpressionNode {
public:
    std::string nameOfVariable;
    explicit VariableNode(std::string name) : nameOfVariable(std::move(name)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::Variable; }
};

class StringNode : public ExpressionNode {
public:
    std::string value;
    explicit StringNode(std::string value) : value(std::move(value)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::String; }
};

class BinaryOperationNode : public ExpressionNode {
public:
    std::unique_ptr<ExpressionNode> leftNode;
    std::unique_ptr<ExpressionNode> rightNode;
    std::string operation{};
    explicit BinaryOperationNode(std::unique_ptr<ExpressionNode> leftNode,
        std::unique_ptr<ExpressionNode> rightNode, std::string operation) :
    leftNode(std::move(leftNode)), rightNode(std::move(rightNode)), operation(std::move(operation)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::BinaryOp; }
};

class UnaryOperationNode : public ExpressionNode {
public:
    std::unique_ptr<ExpressionNode> operand;
    std::string operation;
    UnaryOperationNode(std::string op, std::unique_ptr<ExpressionNode> operand)
        : operand(std::move(operand)), operation(std::move(op)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::UnaryOp; }
};

class FunctionCallNode : public ExpressionNode {
public:
    std::string name;
    std::vector<std::unique_ptr<ExpressionNode>> args;
    FunctionCallNode(std::string name, std::vector<std::unique_ptr<ExpressionNode>> args)
        : name(std::move(name)), args(std::move(args)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::FunctionCall; }
};


class ProgramNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> statements;
    [[nodiscard]] StmtType getType() const override { return StmtType::Program; }
};

class RepeatNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> count;
    std::vector<std::unique_ptr<ASTNode>> body;
    explicit RepeatNode(std::unique_ptr<ExpressionNode> count, std::vector<std::unique_ptr<ASTNode>> body) : count(std::move(count)), body(std::move(body)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::Repeat; }
};

class WhileNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<ASTNode>> body;
    explicit WhileNode(std::unique_ptr<ExpressionNode> condition, std::vector<std::unique_ptr<ASTNode>> body) : condition(std::move(condition)), body(std::move(body)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::While; }
};

class ForNode : public ASTNode {
public:
    std::unique_ptr<ASTNode> init;
    std::unique_ptr<ExpressionNode> condition;
    std::unique_ptr<ASTNode> increment;
    std::vector<std::unique_ptr<ASTNode>> body;
    ForNode(std::unique_ptr<ASTNode> init, std::unique_ptr<ExpressionNode> cond,
            std::unique_ptr<ASTNode> incr, std::vector<std::unique_ptr<ASTNode>> body)
        : init(std::move(init)), condition(std::move(cond)),
          increment(std::move(incr)), body(std::move(body)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::For; }
};

class PrintNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> msg;
    explicit PrintNode(std::unique_ptr<ExpressionNode> msg) : msg(std::move(msg)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::Print; }
};

class VarDeclNode : public ASTNode {
public:
    std::string nameOfVariable;
    std::unique_ptr<ExpressionNode> expression;
    bool isMutable;
    TypeAnnotation typeAnnotation = TypeAnnotation::None;
    explicit VarDeclNode(std::string name, std::unique_ptr<ExpressionNode> expr, const bool isMutable,
                         TypeAnnotation typeAnnot = TypeAnnotation::None)
        : nameOfVariable(std::move(name)), expression(std::move(expr)),
          isMutable(isMutable), typeAnnotation(typeAnnot) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::VarDecl; }
};

class AssignmentNode : public ASTNode {
public:
    std::string nameOfVariable;
    std::unique_ptr<ExpressionNode> expression;
    AssignmentNode(std::string name, std::unique_ptr<ExpressionNode> expr) : nameOfVariable(std::move(name)), expression(std::move(expr)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::Assignment; }
};

class WaitNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> duration;
    explicit WaitNode(std::unique_ptr<ExpressionNode> duration) : duration(std::move(duration)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::Wait; }
};

class MouseBlockNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> actions;
    [[nodiscard]] StmtType getType() const override { return StmtType::MouseBlock; }
};

class ClickNode : public ASTNode {
public:
    enum Button { Left, Right };
    Button button;
    std::optional<int> x;
    std::optional<int> y;

    explicit ClickNode(Button b, std::optional<int> px = std::nullopt, const std::optional<int> py = std::nullopt)
        : button(b), x(px), y(py) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::Click; }
};

class MoveNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> x;
    std::unique_ptr<ExpressionNode> y;
    MoveNode(std::unique_ptr<ExpressionNode> px,
        std::unique_ptr<ExpressionNode> py) : x(std::move(px)), y(std::move(py)){}
    [[nodiscard]] StmtType getType() const override { return StmtType::Move; }
};

class ShiftNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> deltaX;
    std::unique_ptr<ExpressionNode> deltaY;
    ShiftNode(std::unique_ptr<ExpressionNode> dx,
        std::unique_ptr<ExpressionNode> dy): deltaX(std::move(dx)), deltaY(std::move(dy)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::Shift; }
};

class KeyboardBlockNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> actions;
    [[nodiscard]] StmtType getType() const override { return StmtType::KeyboardBlock; }
};

class WriteNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> text;
    explicit WriteNode(std::unique_ptr<ExpressionNode> text) : text(std::move(text)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::Write; }
};

class PressNode : public ASTNode {
public:
    std::string key;
    explicit PressNode(std::string  k) : key(std::move(k)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::Press; }
};

class IfNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<ASTNode>> thenBlock;
    std::vector<std::unique_ptr<ASTNode>> elseBlock;
    IfNode(std::unique_ptr<ExpressionNode> condition, std::vector<std::unique_ptr<ASTNode>> thenBlock, std::vector<std::unique_ptr<ASTNode>> elseBlock)
        : condition(std::move(condition)), thenBlock(std::move(thenBlock)), elseBlock(std::move(elseBlock)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::If; }
};

class BreakNode : public ASTNode {
public:
    [[nodiscard]] StmtType getType() const override { return StmtType::Break; }
};

class ContinueNode : public ASTNode {
public:
    [[nodiscard]] StmtType getType() const override { return StmtType::Continue; }
};

class FunctionDeclNode : public ASTNode {
public:
    std::string name;
    // Each param: {name, optional type annotation}
    std::vector<std::pair<std::string, TypeAnnotation>> params;
    std::vector<std::unique_ptr<ASTNode>> body;
    TypeAnnotation returnType = TypeAnnotation::None;
    FunctionDeclNode(std::string name,
                     std::vector<std::pair<std::string, TypeAnnotation>> params,
                     std::vector<std::unique_ptr<ASTNode>> body,
                     TypeAnnotation returnType = TypeAnnotation::None)
        : name(std::move(name)), params(std::move(params)), body(std::move(body)),
          returnType(returnType) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::FunctionDecl; }
};

class ReturnNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> expression;
    explicit ReturnNode(std::unique_ptr<ExpressionNode> expr = nullptr)
        : expression(std::move(expr)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::Return; }
};

// ===== Class System Nodes =====

struct ClassFieldDecl {
    std::string name;
    bool isMutable;
    bool isPublic;
    TypeAnnotation type = TypeAnnotation::None;
};

struct ClassMethodDecl {
    bool isPublic;
    std::unique_ptr<FunctionDeclNode> function;
};

class ClassDeclNode : public ASTNode {
public:
    std::string name;
    std::string parentName;  // empty = no parent
    std::vector<ClassFieldDecl> fields;
    std::vector<ClassMethodDecl> methods;
    ClassDeclNode(std::string name, std::string parent, std::vector<ClassFieldDecl> fields, std::vector<ClassMethodDecl> methods)
        : name(std::move(name)), parentName(std::move(parent)), fields(std::move(fields)), methods(std::move(methods)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::ClassDecl; }
};

class FieldAccessNode : public ExpressionNode {
public:
    std::string objectName;
    std::string fieldName;
    FieldAccessNode(std::string obj, std::string field)
        : objectName(std::move(obj)), fieldName(std::move(field)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::FieldAccess; }
};

class MethodCallNode : public ExpressionNode {
public:
    std::string objectName;
    std::string methodName;
    std::vector<std::unique_ptr<ExpressionNode>> args;
    MethodCallNode(std::string obj, std::string method, std::vector<std::unique_ptr<ExpressionNode>> args)
        : objectName(std::move(obj)), methodName(std::move(method)), args(std::move(args)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::MethodCall; }
};

class FieldAssignNode : public ASTNode {
public:
    std::string objectName;
    std::string fieldName;
    std::unique_ptr<ExpressionNode> expression;
    FieldAssignNode(std::string obj, std::string field, std::unique_ptr<ExpressionNode> expr)
        : objectName(std::move(obj)), fieldName(std::move(field)), expression(std::move(expr)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::FieldAssign; }
};

class ExpressionStmtNode : public ASTNode {
public:
    std::unique_ptr<ExpressionNode> expression;
    explicit ExpressionStmtNode(std::unique_ptr<ExpressionNode> expr)
        : expression(std::move(expr)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::ExprStmt; }
};

// ===== Collection Index Access Nodes =====

class IndexAccessNode : public ExpressionNode {
public:
    std::unique_ptr<ExpressionNode> object;  // the collection expression (usually a variable)
    std::unique_ptr<ExpressionNode> index;   // the index/key expression
    IndexAccessNode(std::unique_ptr<ExpressionNode> obj, std::unique_ptr<ExpressionNode> idx)
        : object(std::move(obj)), index(std::move(idx)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::IndexAccess; }
};

class IndexAssignNode : public ASTNode {
public:
    std::string objectName;                  // variable name holding the collection
    std::unique_ptr<ExpressionNode> index;   // the index/key expression
    std::unique_ptr<ExpressionNode> value;   // the value to assign
    IndexAssignNode(std::string obj, std::unique_ptr<ExpressionNode> idx, std::unique_ptr<ExpressionNode> val)
        : objectName(std::move(obj)), index(std::move(idx)), value(std::move(val)) {}
    [[nodiscard]] StmtType getType() const override { return StmtType::IndexAssign; }
};

class ArrayAllocNode : public ExpressionNode {
public:
    TypeAnnotation elementType;
    std::unique_ptr<ExpressionNode> size;
    ArrayAllocNode(TypeAnnotation type, std::unique_ptr<ExpressionNode> sz)
        : elementType(type), size(std::move(sz)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::ArrayAlloc; }
};

class ArrayLiteralNode : public ExpressionNode {
public:
    std::vector<std::unique_ptr<ExpressionNode>> elements;
    explicit ArrayLiteralNode(std::vector<std::unique_ptr<ExpressionNode>> elems)
        : elements(std::move(elems)) {}
    [[nodiscard]] ExprType getType() const override { return ExprType::ArrayLiteral; }
};

#endif //LTSNODE_H
