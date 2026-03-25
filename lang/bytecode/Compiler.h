#ifndef COMPILER_H
#define COMPILER_H

#include "Chunk.h"
#include "../node/ASTNode.h"
#include <vector>
#include <string>
#include <unordered_map>

/**
 * @brief Represents a local variable during compilation.
 */
struct Local {
    std::string name;
    int depth;
    bool isMutable;
    uint8_t reg;
    TypeAnnotation typeAnnot = TypeAnnotation::None; ///< Optional type constraint
};

/**
 * @brief Represents a compiled function.
 */
struct FunctionObject {
    std::string name;
    int arity;
    Chunk chunk;
    uint8_t maxRegs;
    TypeAnnotation returnType = TypeAnnotation::None;         ///< Expected return type
    std::vector<TypeAnnotation> paramTypes;                   ///< Expected type per parameter
};

/**
 * @brief Single-pass Compiler (AST -> Bytecode).
 * Handles register allocation, scope management, and control flow.
 */
class Compiler {
    Chunk chunk;
    int repeatCounter = 0;
    std::vector<Local> locals;
    int scopeDepth = 0;

    uint8_t nextReg = 0;
    uint8_t maxReg = 0;

    struct LoopContext {
        size_t loopStart;
        std::vector<size_t> breakJumps;
        int scopeDepthAtLoop;
    };
    std::vector<LoopContext> loopStack;

    std::vector<FunctionObject> functions;
    std::unordered_map<std::string, uint16_t> functionIndex;
    std::unordered_map<std::string, uint16_t> globalIndex;
    uint16_t globalCount = 0;

public:
    /**
     * @brief Compiles the entire program AST into a bytecode chunk.
     * @return The main chunk containing the compiled program.
     */
    Chunk compile(ProgramNode* program);

    const std::vector<FunctionObject>& getFunctions() const { return functions; }
    std::vector<FunctionObject>& getFunctions() { return functions; }

private:
    void compileNode(ASTNode* node);
    uint8_t compileExpression(ExpressionNode* expr, uint8_t dst = 255);

    void compileProgram(ProgramNode* node);
    void compileRepeat(RepeatNode* node);
    void compileWhile(WhileNode* node);
    void compileFor(const ForNode* node);
    void compileIf(IfNode* node);
    void compileLog(PrintNode* node);
    void compileVarDecl(VarDeclNode* node);
    void compileAssignment(AssignmentNode* node);
    void compileWait(WaitNode* node);
    void compileBreak();
    void compileContinue();
    void compileFunctionDecl(FunctionDeclNode* node);
    void compileReturn(ReturnNode* node);

    uint8_t compileNumber(NumberNode* node, uint8_t dst);
    uint8_t compileDouble(DoubleNode* node, uint8_t dst);
    uint8_t compileBoolean(BooleanNode* node, uint8_t dst);
    uint8_t compileString(StringNode* node, uint8_t dst);
    uint8_t compileVariable(VariableNode* node, uint8_t dst);
    uint8_t compileBinaryOp(BinaryOperationNode* node, uint8_t dst);
    uint8_t compileUnaryOp(UnaryOperationNode* node, uint8_t dst);
    uint8_t compileFunctionCall(FunctionCallNode* node, uint8_t dst);

    /** @brief Allocates a new register for temporary use. */
    uint8_t allocReg() {
        const uint8_t r = nextReg++;
        if (nextReg > maxReg) maxReg = nextReg;
        return r;
    }

    /** @brief Frees the last allocated register. */
    void freeReg() { nextReg--; }

    /** @brief Frees all registers above the specified index. */
    void freeRegsTo(const uint8_t to) { nextReg = to; }

    void beginScope();
    void endScope();
    void addLocal(const std::string& name, bool isMutable, TypeAnnotation typeAnnot = TypeAnnotation::None);
    int resolveLocal(const std::string& name);
    bool isGlobalScope() const { return scopeDepth == 0; }
};

#endif //COMPILER_H
