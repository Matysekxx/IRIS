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

    if (isGlobalScope()) {
        const uint16_t nameIdx = chunk.addConstant(node->nameOfVariable);
        chunk.emitOp(OpCode::OP_DECL_VAR);
        chunk.emitShort(nameIdx);
        chunk.emit(node->isMutable ? 1 : 0);
    } else {
        addLocal(node->nameOfVariable, node->isMutable);
    }
}

void Compiler::compileAssignment(AssignmentNode* node) {
    compileExpression(node->expression.get());

    int arg = resolveLocal(node->nameOfVariable);
    if (arg != -1) {
        if (!locals[arg].isMutable) {
             throw std::runtime_error("Variable '" + node->nameOfVariable + "' is immutable.");
        }
        chunk.emitOp(OpCode::OP_SET_LOCAL);
        chunk.emit(static_cast<uint8_t>(arg));
    } else {
        uint16_t nameIdx = chunk.addConstant(node->nameOfVariable);
        chunk.emitOp(OpCode::OP_SET_VAR);
        chunk.emitShort(nameIdx);
    }
}


void Compiler::compileIf(IfNode* node) {
    compileExpression(node->condition.get());

    const size_t thenJump = chunk.emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk.emitOp(OpCode::OP_POP); 

    beginScope();
    for (auto& stmt : node->thenBlock) {
        compileNode(stmt.get());
    }
    endScope();

    size_t elseJump = chunk.emitJump(OpCode::OP_JUMP);
    chunk.patchJump(thenJump);
    chunk.emitOp(OpCode::OP_POP);

    beginScope();
    for (auto& stmt : node->elseBlock) {
        compileNode(stmt.get());
    }
    endScope();

    chunk.patchJump(elseJump);
}

void Compiler::compileWhile(WhileNode* node) {
    size_t loopStart = chunk.code.size();

    compileExpression(node->condition.get());

    size_t exitJump = chunk.emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk.emitOp(OpCode::OP_POP);

    beginScope();
    for (auto& stmt : node->body) {
        compileNode(stmt.get());
    }
    endScope();

    chunk.emitLoop(loopStart);

    chunk.patchJump(exitJump);
    chunk.emitOp(OpCode::OP_POP);
}

void Compiler::compileRepeat(RepeatNode* node) {
    compileExpression(node->count.get());
    
    beginScope();
    const std::string counterName = "$__repeat_" + std::to_string(repeatCounter++);
    addLocal(counterName, true);

    const size_t loopStart = chunk.code.size();

    const int counterIdx = resolveLocal(counterName);
    chunk.emitOp(OpCode::OP_GET_LOCAL);
    chunk.emit(static_cast<uint8_t>(counterIdx));
    
    chunk.emitConstant(0);
    chunk.emitOp(OpCode::OP_GT);

    size_t exitJump = chunk.emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk.emitOp(OpCode::OP_POP);

    beginScope();
    for (auto& stmt : node->body) {
        compileNode(stmt.get());
    }
    endScope();

    chunk.emitOp(OpCode::OP_GET_LOCAL);
    chunk.emit(static_cast<uint8_t>(counterIdx));
    chunk.emitConstant(1);
    chunk.emitOp(OpCode::OP_SUB);
    chunk.emitOp(OpCode::OP_SET_LOCAL);
    chunk.emit(static_cast<uint8_t>(counterIdx));

    chunk.emitLoop(loopStart);

    chunk.patchJump(exitJump);
    chunk.emitOp(OpCode::OP_POP);
    endScope();
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
    int arg = resolveLocal(node->nameOfVariable);
    if (arg != -1) {
        chunk.emitOp(OpCode::OP_GET_LOCAL);
        chunk.emit(static_cast<uint8_t>(arg));
    } else {
        uint16_t nameIdx = chunk.addConstant(node->nameOfVariable);
        chunk.emitOp(OpCode::OP_GET_VAR);
        chunk.emitShort(nameIdx);
    }
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

void Compiler::beginScope() {
    scopeDepth++;
}

void Compiler::endScope() {
    scopeDepth--;
    while (!locals.empty() && locals.back().depth > scopeDepth) {
        chunk.emitOp(OpCode::OP_POP);
        locals.pop_back();
    }
}

void Compiler::addLocal(const std::string& name, bool isMutable) {
    for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
        if (it->depth < scopeDepth) break;
        if (it->name == name) {
            throw std::runtime_error("Variable with this name already declared in this scope: " + name);
        }
    }
    locals.push_back({name, scopeDepth, isMutable});
}

int Compiler::resolveLocal(const std::string& name) {
    for (int i = locals.size() - 1; i >= 0; i--) {
        if (locals[i].name == name) {
            return i;
        }
    }
    return -1;
}
