#ifndef COMPILER_H
#define COMPILER_H

#include "Chunk.h"
#include "../node/ASTNode.h"

/**
 * @brief Compiles the AST (Abstract Syntax Tree) into Bytecode (Chunk).
 *
 * Traverses the AST recursively and emits opcodes to a Chunk.
 */
class Compiler {
    Chunk chunk;
    int repeatCounter = 0;

public:
    /**
     * @brief Compiles a ProgramNode into a Chunk of bytecode.
     */
    Chunk compile(ProgramNode* program);

private:
    /** Dispatches compilation to specific methods based on node type. */
    void compileNode(ASTNode* node);
    /** Dispatches expression compilation. */
    void compileExpression(ExpressionNode* expr);

    void compileProgram(ProgramNode* node);
    void compileRepeat(RepeatNode* node);
    void compileWhile(WhileNode* node);
    void compileIf(IfNode* node);
    void compileLog(PrintNode* node);
    void compileVarDecl(VarDeclNode* node);
    void compileAssignment(AssignmentNode* node);
    void compileWait(WaitNode* node);

    void compileNumber(NumberNode* node);
    void compileBoolean(BooleanNode* node);
    void compileString(StringNode* node);
    void compileVariable(VariableNode* node);
    void compileBinaryOp(BinaryOperationNode* node);
    void compileUnaryOp(UnaryOperationNode* node);
};

#endif //COMPILER_H
