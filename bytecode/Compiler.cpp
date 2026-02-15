#include "Compiler.h"
#include <stdexcept>

Chunk Compiler::compile(ProgramNode* program) {
    compileProgram(program);
    chunk.emitOp(OpCode::OP_HALT);
    return std::move(chunk);
}


void Compiler::compileNode(ASTNode* node) {
    if (auto* n = dynamic_cast<ProgramNode*>(node))       { compileProgram(n);       return; }
    if (auto* n = dynamic_cast<RepeatNode*>(node))        { compileRepeat(n);        return; }
    if (auto* n = dynamic_cast<WhileNode*>(node))         { compileWhile(n);         return; }
    if (auto* n = dynamic_cast<IfNode*>(node))            { compileIf(n);            return; }
    if (auto* n = dynamic_cast<PrintNode*>(node))           { compileLog(n);           return; }
    if (auto* n = dynamic_cast<VarDeclNode*>(node))       { compileVarDecl(n);       return; }
    if (auto* n = dynamic_cast<AssignmentNode*>(node))    { compileAssignment(n);    return; }
    if (auto* n = dynamic_cast<WaitNode*>(node))          { compileWait(n);          return; }
    throw std::runtime_error("Compiler: unknown AST node type");
}

void Compiler::compileExpression(ExpressionNode* expr) {
    if (auto* n = dynamic_cast<NumberNode*>(expr))          { compileNumber(n);    return; }
    if (auto* n = dynamic_cast<BooleanNode*>(expr))         { compileBoolean(n);   return; }
    if (auto* n = dynamic_cast<StringNode*>(expr))          { compileString(n);    return; }
    if (auto* n = dynamic_cast<VariableNode*>(expr))        { compileVariable(n);  return; }
    if (auto* n = dynamic_cast<BinaryOperationNode*>(expr)) { compileBinaryOp(n);  return; }
    if (auto* n = dynamic_cast<UnaryOperationNode*>(expr))  { compileUnaryOp(n);   return; }
    throw std::runtime_error("Compiler: unknown expression node type");
}


void Compiler::compileProgram(ProgramNode* node) {
    for (auto& stmt : node->statements) {
        compileNode(stmt.get());
    }
}

void Compiler::compileLog(PrintNode* node) {
    compileExpression(node->msg.get());
    chunk.emitOp(OpCode::OP_LOG);
}

void Compiler::compileWait(WaitNode* node) {
    compileExpression(node->duration.get());
    chunk.emitOp(OpCode::OP_WAIT);
}

void Compiler::compileVarDecl(VarDeclNode* node) {
    compileExpression(node->expression.get());
    uint16_t nameIdx = chunk.addConstant(node->nameOfVariable);
    chunk.emitOp(OpCode::OP_DECL_VAR);
    chunk.emitShort(nameIdx);
    chunk.emit(node->isMutable ? 1 : 0);
}

void Compiler::compileAssignment(AssignmentNode* node) {
    compileExpression(node->expression.get());
    uint16_t nameIdx = chunk.addConstant(node->nameOfVariable);
    chunk.emitOp(OpCode::OP_SET_VAR);
    chunk.emitShort(nameIdx);
}


void Compiler::compileIf(IfNode* node) {
    compileExpression(node->condition.get());

    size_t thenJump = chunk.emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk.emitOp(OpCode::OP_POP); 

    for (auto& stmt : node->thenBlock) {
        compileNode(stmt.get());
    }

    size_t elseJump = chunk.emitJump(OpCode::OP_JUMP);
    chunk.patchJump(thenJump);
    chunk.emitOp(OpCode::OP_POP);

    for (auto& stmt : node->elseBlock) {
        compileNode(stmt.get());
    }

    chunk.patchJump(elseJump);
}

void Compiler::compileWhile(WhileNode* node) {
    size_t loopStart = chunk.code.size();

    compileExpression(node->condition.get());

    size_t exitJump = chunk.emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk.emitOp(OpCode::OP_POP);

    for (auto& stmt : node->body) {
        compileNode(stmt.get());
    }

    chunk.emitLoop(loopStart);

    chunk.patchJump(exitJump);
    chunk.emitOp(OpCode::OP_POP);
}

void Compiler::compileRepeat(RepeatNode* node) {
    compileExpression(node->count.get());
    std::string counterName = "$__repeat_" + std::to_string(repeatCounter++);
    uint16_t nameIdx = chunk.addConstant(counterName);
    chunk.emitOp(OpCode::OP_DECL_VAR);
    chunk.emitShort(nameIdx);
    chunk.emit(1);

    size_t loopStart = chunk.code.size();

    chunk.emitOp(OpCode::OP_GET_VAR);
    chunk.emitShort(nameIdx);
    chunk.emitConstant(0);
    chunk.emitOp(OpCode::OP_GT);

    size_t exitJump = chunk.emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk.emitOp(OpCode::OP_POP);

    for (auto& stmt : node->body) {
        compileNode(stmt.get());
    }
    chunk.emitOp(OpCode::OP_GET_VAR);
    chunk.emitShort(nameIdx);
    chunk.emitConstant(1);
    chunk.emitOp(OpCode::OP_SUB);
    chunk.emitOp(OpCode::OP_SET_VAR);
    chunk.emitShort(nameIdx);

    chunk.emitLoop(loopStart);

    chunk.patchJump(exitJump);
    chunk.emitOp(OpCode::OP_POP);
}

void Compiler::compileNumber(NumberNode* node) {
    chunk.emitConstant(node->value);
}

void Compiler::compileBoolean(BooleanNode* node) {
    chunk.emitOp(node->value ? OpCode::OP_TRUE : OpCode::OP_FALSE);
}

void Compiler::compileString(StringNode* node) {
    chunk.emitConstant(node->value);
}

void Compiler::compileVariable(VariableNode* node) {
    uint16_t nameIdx = chunk.addConstant(node->nameOfVariable);
    chunk.emitOp(OpCode::OP_GET_VAR);
    chunk.emitShort(nameIdx);
}

void Compiler::compileUnaryOp(UnaryOperationNode* node) {
    compileExpression(node->operand.get());
    if (node->operation == "!") {
        chunk.emitOp(OpCode::OP_NOT);
    } else {
        throw std::runtime_error("Compiler: unknown unary operator: " + node->operation);
    }
}

void Compiler::compileBinaryOp(BinaryOperationNode* node) {
    compileExpression(node->leftNode.get());
    compileExpression(node->rightNode.get());

    const auto& op = node->operation;
    if (op == "+") chunk.emitOp(OpCode::OP_ADD);
    else if (op == "-") chunk.emitOp(OpCode::OP_SUB);
    else if (op == "*") chunk.emitOp(OpCode::OP_MUL);
    else if (op == "/") chunk.emitOp(OpCode::OP_DIV);
    else if (op == "==") chunk.emitOp(OpCode::OP_EQ);
    else if (op == "!=") chunk.emitOp(OpCode::OP_NEQ);
    else if (op == "<") chunk.emitOp(OpCode::OP_LT);
    else if (op == ">") chunk.emitOp(OpCode::OP_GT);
    else if (op == "<=") chunk.emitOp(OpCode::OP_LE);
    else if (op == ">=") chunk.emitOp(OpCode::OP_GE);
    else if (op == "&&") chunk.emitOp(OpCode::OP_AND);
    else if (op == "||") chunk.emitOp(OpCode::OP_OR);
    else if (op == "&") chunk.emitOp(OpCode::OP_BIT_AND);
    else if (op == "|") chunk.emitOp(OpCode::OP_BIT_OR);
    else if (op == "^") chunk.emitOp(OpCode::OP_BIT_XOR);
    else if (op == "<<") chunk.emitOp(OpCode::OP_SHL);
    else if (op == ">>") chunk.emitOp(OpCode::OP_SHR);
    else throw std::runtime_error("Compiler: unknown binary operator: " + op);
}
