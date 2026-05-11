#ifndef ASTNODE_H
#define ASTNODE_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace iris::node {
    enum class TypeAnnotation : uint8_t {
        None = 0,
        Int,
        Double,
        String,
        Bool,
        IntArray,
        DoubleArray,
        StringArray,
        BoolArray
    };

    inline std::string typeAnnotationName(TypeAnnotation t) {
        switch (t) {
            case TypeAnnotation::Int: return "int";
            case TypeAnnotation::Double: return "double";
            case TypeAnnotation::String: return "string";
            case TypeAnnotation::Bool: return "bool";
            case TypeAnnotation::IntArray: return "int[]";
            case TypeAnnotation::DoubleArray: return "double[]";
            case TypeAnnotation::StringArray: return "string[]";
            case TypeAnnotation::BoolArray: return "bool[]";
            default: return "any";
        }
    }

    inline TypeAnnotation parseTypeAnnotation(const std::string& s) {
        if (s == "int") return TypeAnnotation::Int;
        if (s == "double") return TypeAnnotation::Double;
        if (s == "string") return TypeAnnotation::String;
        if (s == "bool") return TypeAnnotation::Bool;
        if (s == "int[]") return TypeAnnotation::IntArray;
        if (s == "double[]") return TypeAnnotation::DoubleArray;
        if (s == "string[]") return TypeAnnotation::StringArray;
        if (s == "bool[]") return TypeAnnotation::BoolArray;
        return TypeAnnotation::None;
    }

    enum class ExprType {
        Number,
        Double,
        String,
        Boolean,
        Variable,
        BinaryOp,
        UnaryOp,
        FunctionCall,
        FieldAccess,
        MethodCall,
        IndexAccess,
        ArrayAlloc,
        ArrayLiteral,
        StringInterp
    };

    enum class StmtType {
        Program,
        Print,
        Wait,
        VarDecl,
        Assignment,
        Repeat,
        While,
        For,
        If,
        Break,
        Continue,
        FunctionDecl,
        Return,
        ClassDecl,
        FieldAssign,
        ExprStmt,
        IndexAssign,
        TryCatch,
        Throw,
        ImportNative,
        Switch,
        Case,
        Enum
    };

    struct ASTNode {
        virtual ~ASTNode() = default;
        virtual StmtType getStmtType() const = 0;
    };

    struct ExpressionNode : public ASTNode {
        // Expressions are NOT directly statements in this AST, 
        // they are usually part of a statement (like VarDecl) 
        // or wrapped in ExpressionStmtNode.
        // Thus, getStmtType() is not strictly needed for raw expressions,
        // but since they inherit from ASTNode, we must implement it.
        StmtType getStmtType() const override { return StmtType::ExprStmt; }
        virtual ExprType getExprType() const = 0;
    };

    struct NumberNode : public ExpressionNode {
        int value;
        explicit NumberNode(const int v) : value(v) {}
        ExprType getExprType() const override { return ExprType::Number; }
    };

    struct DoubleNode : public ExpressionNode {
        double value;
        explicit DoubleNode(const double v) : value(v) {}
        ExprType getExprType() const override { return ExprType::Double; }
    };

    struct BooleanNode : public ExpressionNode {
        bool value;
        explicit BooleanNode(const bool v) : value(v) {}
        ExprType getExprType() const override { return ExprType::Boolean; }
    };

    struct StringNode : public ExpressionNode {
        std::string value;
        explicit StringNode(std::string v) : value(std::move(v)) {}
        ExprType getExprType() const override { return ExprType::String; }
    };

    struct StringInterpNode : public ExpressionNode {
        std::vector<std::unique_ptr<ExpressionNode>> parts;
        explicit StringInterpNode(std::vector<std::unique_ptr<ExpressionNode>> p) : parts(std::move(p)) {}
        ExprType getExprType() const override { return ExprType::StringInterp; }
    };

    struct VariableNode : public ExpressionNode {
        std::string nameOfVariable;
        explicit VariableNode(std::string n) : nameOfVariable(std::move(n)) {}
        ExprType getExprType() const override { return ExprType::Variable; }
    };

    struct BinaryOperationNode : public ExpressionNode {
        std::unique_ptr<ExpressionNode> leftNode;
        std::unique_ptr<ExpressionNode> rightNode;
        std::string operation;
        BinaryOperationNode(std::unique_ptr<ExpressionNode> left,
                            std::unique_ptr<ExpressionNode> right,
                            std::string op)
            : leftNode(std::move(left)), rightNode(std::move(right)), operation(std::move(op)) {}
        ExprType getExprType() const override { return ExprType::BinaryOp; }
    };

    struct UnaryOperationNode : public ExpressionNode {
        std::string operation;
        std::unique_ptr<ExpressionNode> operand;
        UnaryOperationNode(std::string op, std::unique_ptr<ExpressionNode> operand)
            : operation(std::move(op)), operand(std::move(operand)) {}
        ExprType getExprType() const override { return ExprType::UnaryOp; }
    };

    struct FunctionCallNode : public ExpressionNode {
        std::string name;
        std::vector<std::unique_ptr<ExpressionNode>> args;
        FunctionCallNode(std::string n, std::vector<std::unique_ptr<ExpressionNode>> a)
            : name(std::move(n)), args(std::move(a)) {}
        ExprType getExprType() const override { return ExprType::FunctionCall; }
    };

    struct FieldAccessNode : public ExpressionNode {
        std::string objectName;
        std::string fieldName;
        FieldAccessNode(std::string obj, std::string field)
            : objectName(std::move(obj)), fieldName(std::move(field)) {}
        ExprType getExprType() const override { return ExprType::FieldAccess; }
    };

    struct MethodCallNode : public ExpressionNode {
        std::string objectName;
        std::string methodName;
        std::vector<std::unique_ptr<ExpressionNode>> args;
        MethodCallNode(std::string obj, std::string method, std::vector<std::unique_ptr<ExpressionNode>> a)
            : objectName(std::move(obj)), methodName(std::move(method)), args(std::move(a)) {}
        ExprType getExprType() const override { return ExprType::MethodCall; }
    };

    struct IndexAccessNode : public ExpressionNode {
        std::unique_ptr<ExpressionNode> object;
        std::unique_ptr<ExpressionNode> index;
        IndexAccessNode(std::unique_ptr<ExpressionNode> obj, std::unique_ptr<ExpressionNode> idx)
            : object(std::move(obj)), index(std::move(idx)) {}
        ExprType getExprType() const override { return ExprType::IndexAccess; }
    };

    struct ArrayAllocNode : public ExpressionNode {
        TypeAnnotation elementType;
        std::unique_ptr<ExpressionNode> size;
        ArrayAllocNode(TypeAnnotation type, std::unique_ptr<ExpressionNode> sz)
            : elementType(type), size(std::move(sz)) {}
        ExprType getExprType() const override { return ExprType::ArrayAlloc; }
    };

    struct ArrayLiteralNode : public ExpressionNode {
        std::vector<std::unique_ptr<ExpressionNode>> elements;
        explicit ArrayLiteralNode(std::vector<std::unique_ptr<ExpressionNode>> elems) : elements(std::move(elems)) {}
        ExprType getExprType() const override { return ExprType::ArrayLiteral; }
    };

    // Statements

    struct ImportNativeNode : public ASTNode {
        std::string name;
        explicit ImportNativeNode(std::string n) : name(std::move(n)) {}
        StmtType getStmtType() const override { return StmtType::ImportNative; }
    };

    struct ProgramNode : public ASTNode {
        std::vector<std::unique_ptr<ASTNode>> statements;
        StmtType getStmtType() const override { return StmtType::Program; }
    };

    struct PrintNode : public ASTNode {
        std::unique_ptr<ExpressionNode> msg;
        explicit PrintNode(std::unique_ptr<ExpressionNode> msg) : msg(std::move(msg)) {}
        StmtType getStmtType() const override { return StmtType::Print; }
    };

    struct WaitNode : public ASTNode {
        std::unique_ptr<ExpressionNode> duration;
        explicit WaitNode(std::unique_ptr<ExpressionNode> dur) : duration(std::move(dur)) {}
        StmtType getStmtType() const override { return StmtType::Wait; }
    };

    struct VarDeclNode : public ASTNode {
        std::string nameOfVariable;
        std::unique_ptr<ExpressionNode> expression;
        bool isMutable;
        TypeAnnotation typeAnnotation;
        VarDeclNode(std::string n, std::unique_ptr<ExpressionNode> e, bool mut = true, TypeAnnotation annot = TypeAnnotation::None)
            : nameOfVariable(std::move(n)), expression(std::move(e)), isMutable(mut), typeAnnotation(annot) {}
        StmtType getStmtType() const override { return StmtType::VarDecl; }
    };

    struct AssignmentNode : public ASTNode {
        std::string nameOfVariable;
        std::unique_ptr<ExpressionNode> expression;
        AssignmentNode(std::string n, std::unique_ptr<ExpressionNode> e) : nameOfVariable(std::move(n)), expression(std::move(e)) {}
        StmtType getStmtType() const override { return StmtType::Assignment; }
    };

    struct FieldAssignNode : public ASTNode {
        std::string objectName;
        std::string fieldName;
        std::unique_ptr<ExpressionNode> expression;
        FieldAssignNode(std::string obj, std::string field, std::unique_ptr<ExpressionNode> expr)
            : objectName(std::move(obj)), fieldName(std::move(field)), expression(std::move(expr)) {}
        StmtType getStmtType() const override { return StmtType::FieldAssign; }
    };

    struct IndexAssignNode : public ASTNode {
        std::string objectName;
        std::unique_ptr<ExpressionNode> index;
        std::unique_ptr<ExpressionNode> value;
        IndexAssignNode(std::string obj, std::unique_ptr<ExpressionNode> idx, std::unique_ptr<ExpressionNode> val)
            : objectName(std::move(obj)), index(std::move(idx)), value(std::move(val)) {}
        StmtType getStmtType() const override { return StmtType::IndexAssign; }
    };

    struct RepeatNode : public ASTNode {
        std::unique_ptr<ExpressionNode> count;
        std::vector<std::unique_ptr<ASTNode>> body;
        RepeatNode(std::unique_ptr<ExpressionNode> c, std::vector<std::unique_ptr<ASTNode>> b)
            : count(std::move(c)), body(std::move(b)) {}
        StmtType getStmtType() const override { return StmtType::Repeat; }
    };

    struct WhileNode : public ASTNode {
        std::unique_ptr<ExpressionNode> condition;
        std::vector<std::unique_ptr<ASTNode>> body;
        WhileNode(std::unique_ptr<ExpressionNode> cond, std::vector<std::unique_ptr<ASTNode>> b)
            : condition(std::move(cond)), body(std::move(b)) {}
        StmtType getStmtType() const override { return StmtType::While; }
    };

    struct ForNode : public ASTNode {
        std::unique_ptr<ASTNode> init;
        std::unique_ptr<ExpressionNode> condition;
        std::unique_ptr<ASTNode> increment;
        std::vector<std::unique_ptr<ASTNode>> body;
        ForNode(std::unique_ptr<ASTNode> i, std::unique_ptr<ExpressionNode> cond,
                std::unique_ptr<ASTNode> inc, std::vector<std::unique_ptr<ASTNode>> b)
            : init(std::move(i)), condition(std::move(cond)), increment(std::move(inc)), body(std::move(b)) {}
        StmtType getStmtType() const override { return StmtType::For; }
    };

    struct IfNode : public ASTNode {
        std::unique_ptr<ExpressionNode> condition;
        std::vector<std::unique_ptr<ASTNode>> thenBlock;
        std::vector<std::unique_ptr<ASTNode>> elseBlock;
        IfNode(std::unique_ptr<ExpressionNode> cond, std::vector<std::unique_ptr<ASTNode>> thenB, std::vector<std::unique_ptr<ASTNode>> elseB)
            : condition(std::move(cond)), thenBlock(std::move(thenB)), elseBlock(std::move(elseB)) {}
        StmtType getStmtType() const override { return StmtType::If; }
    };

    struct CaseNode : public ASTNode {
        std::unique_ptr<ExpressionNode> value; // nullptr for default
        std::vector<std::unique_ptr<ASTNode>> body;
        CaseNode(std::unique_ptr<ExpressionNode> val, std::vector<std::unique_ptr<ASTNode>> b)
            : value(std::move(val)), body(std::move(b)) {}
        StmtType getStmtType() const override { return StmtType::Case; }
    };

    struct SwitchNode : public ASTNode {
        std::unique_ptr<ExpressionNode> expression;
        std::vector<std::unique_ptr<CaseNode>> cases;
        SwitchNode(std::unique_ptr<ExpressionNode> expr, std::vector<std::unique_ptr<CaseNode>> c)
            : expression(std::move(expr)), cases(std::move(c)) {}
        StmtType getStmtType() const override { return StmtType::Switch; }
    };

    struct EnumNode : public ASTNode {
        std::string name;
        std::vector<std::pair<std::string, int>> values;
        EnumNode(std::string n, std::vector<std::pair<std::string, int>> v)
            : name(std::move(n)), values(std::move(v)) {}
        StmtType getStmtType() const override { return StmtType::Enum; }
    };

    struct BreakNode : public ASTNode {
        StmtType getStmtType() const override { return StmtType::Break; }
    };

    struct ContinueNode : public ASTNode {
        StmtType getStmtType() const override { return StmtType::Continue; }
    };

    struct FunctionDeclNode : public ASTNode {
        std::string name;
        std::vector<std::pair<std::string, TypeAnnotation>> params;
        std::vector<std::unique_ptr<ASTNode>> body;
        TypeAnnotation returnType;
        FunctionDeclNode(std::string n, std::vector<std::pair<std::string, TypeAnnotation>> p, std::vector<std::unique_ptr<ASTNode>> b, TypeAnnotation retType = TypeAnnotation::None)
            : name(std::move(n)), params(std::move(p)), body(std::move(b)), returnType(retType) {}
        StmtType getStmtType() const override { return StmtType::FunctionDecl; }
    };

    struct ReturnNode : public ASTNode {
        std::unique_ptr<ExpressionNode> expression;
        explicit ReturnNode(std::unique_ptr<ExpressionNode> expr) : expression(std::move(expr)) {}
        StmtType getStmtType() const override { return StmtType::Return; }
    };

    enum class AccessModifier {
        Public,
        Private,
        PackagePrivate
    };

    struct ClassFieldDecl {
        std::string name;
        bool isMutable;
        AccessModifier access;
        TypeAnnotation type;
    };

    struct ClassMethodDecl {
        AccessModifier access;
        bool isAbstract;
        std::unique_ptr<FunctionDeclNode> function;
    };

    struct ClassDeclNode : public ASTNode {
        std::string name;
        bool isAbstract;
        std::string parentName;
        std::vector<ClassFieldDecl> fields;
        std::vector<ClassMethodDecl> methods;

        ClassDeclNode(std::string n, bool abs, std::string p, std::vector<ClassFieldDecl> f, std::vector<ClassMethodDecl> m)
            : name(std::move(n)), isAbstract(abs), parentName(std::move(p)), fields(std::move(f)), methods(std::move(m)) {}
        StmtType getStmtType() const override { return StmtType::ClassDecl; }
    };

    struct ExpressionStmtNode : public ASTNode {
        std::unique_ptr<ExpressionNode> expression;
        explicit ExpressionStmtNode(std::unique_ptr<ExpressionNode> expr) : expression(std::move(expr)) {}
        StmtType getStmtType() const override { return StmtType::ExprStmt; }
    };

    struct TryCatchNode : public ASTNode {
        std::vector<std::unique_ptr<ASTNode>> tryBody;
        std::string catchVar;
        std::vector<std::unique_ptr<ASTNode>> catchBody;

        TryCatchNode(std::vector<std::unique_ptr<ASTNode>> t, std::string c, std::vector<std::unique_ptr<ASTNode>> cb)
            : tryBody(std::move(t)), catchVar(std::move(c)), catchBody(std::move(cb)) {}

        StmtType getStmtType() const override { return StmtType::TryCatch; }
    };

    struct ThrowNode : public ASTNode {
        std::unique_ptr<ExpressionNode> expression;
        explicit ThrowNode(std::unique_ptr<ExpressionNode> e) : expression(std::move(e)) {}
        StmtType getStmtType() const override { return StmtType::Throw; }
    };
}

#endif //ASTNODE_H
