#include "Compiler.h"
#include <ranges>
#include <stdexcept>

Chunk Compiler::compile(ProgramNode* program) {
    compileProgram(program);
    chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
    return std::move(chunk);
}

void Compiler::compileNode(ASTNode* node) {
    switch (node->getType()) {
        case StmtType::Program: compileProgram(static_cast<ProgramNode*>(node)); return;
        case StmtType::Repeat: compileRepeat(static_cast<RepeatNode*>(node)); return;
        case StmtType::While: compileWhile(static_cast<WhileNode*>(node)); return;
        case StmtType::For: compileFor(static_cast<ForNode*>(node)); return;
        case StmtType::If: compileIf(static_cast<IfNode*>(node)); return;
        case StmtType::Print: compileLog(static_cast<PrintNode*>(node)); return;
        case StmtType::VarDecl: compileVarDecl(static_cast<VarDeclNode*>(node)); return;
        case StmtType::Assignment: compileAssignment(static_cast<AssignmentNode*>(node)); return;
        case StmtType::Wait: compileWait(static_cast<WaitNode*>(node)); return;
        case StmtType::Break: compileBreak(); return;
        case StmtType::Continue: compileContinue(); return;
        case StmtType::FunctionDecl: compileFunctionDecl(static_cast<FunctionDeclNode*>(node)); return;
        case StmtType::Return: compileReturn(static_cast<ReturnNode*>(node)); return;
        default:
            throw std::runtime_error("Compiler: unknown AST node type");
    }
}

uint8_t Compiler::compileExpression(ExpressionNode* expr, uint8_t dst) {
    if (dst == 255) dst = allocReg();
    switch (expr->getType()) {
        case ExprType::Number: return compileNumber(static_cast<NumberNode*>(expr), dst);
        case ExprType::Double: return compileDouble(static_cast<DoubleNode*>(expr), dst);
        case ExprType::Boolean: return compileBoolean(static_cast<BooleanNode*>(expr), dst);
        case ExprType::String: return compileString(static_cast<StringNode*>(expr), dst);
        case ExprType::Variable: return compileVariable(static_cast<VariableNode*>(expr), dst);
        case ExprType::BinaryOp: return compileBinaryOp(static_cast<BinaryOperationNode*>(expr), dst);
        case ExprType::UnaryOp: return compileUnaryOp(static_cast<UnaryOperationNode*>(expr), dst);
        case ExprType::FunctionCall: return compileFunctionCall(static_cast<FunctionCallNode*>(expr), dst);
        default:
            throw std::runtime_error("Compiler: unknown expression node type");
    }
}

void Compiler::compileProgram(ProgramNode* node) {
    for (auto& stmt : node->statements) {
        compileNode(stmt.get());
    }
}

void Compiler::compileLog(PrintNode* node) {
    uint8_t save = nextReg;
    uint8_t r = compileExpression(node->msg.get());
    chunk.emit(encodeABC(OpCode::OP_LOG, r, 0, 0));
    freeRegsTo(save);
}

void Compiler::compileWait(WaitNode* node) {
    uint8_t save = nextReg;
    uint8_t r = compileExpression(node->duration.get());
    chunk.emit(encodeABC(OpCode::OP_WAIT, r, 0, 0));
    freeRegsTo(save);
}

void Compiler::compileVarDecl(VarDeclNode* node) {
    const TypeAnnotation annot = node->typeAnnotation;
    if (isGlobalScope()) {
        uint16_t slot;
        auto it = globalIndex.find(node->nameOfVariable);
        if (it == globalIndex.end()) {
            slot = globalCount++;
            globalIndex[node->nameOfVariable] = slot;
        } else {
            slot = it->second;
        }
        uint8_t save = nextReg;
        uint8_t r = compileExpression(node->expression.get());
        // Runtime type check if annotation is present
        if (annot != TypeAnnotation::None)
            chunk.emit(encodeABC(OpCode::OP_TYPECHECK, r, static_cast<uint8_t>(annot), 0));
        chunk.emit(encodeABC(OpCode::OP_DGLOB, r, static_cast<uint8_t>(slot >> 8), static_cast<uint8_t>(slot & 0xFF)));
        freeRegsTo(save);
    } else {
        addLocal(node->nameOfVariable, node->isMutable, annot);
        int idx = resolveLocal(node->nameOfVariable);
        compileExpression(node->expression.get(), locals[idx].reg);
        // Runtime type check if annotation is present
        if (annot != TypeAnnotation::None)
            chunk.emit(encodeABC(OpCode::OP_TYPECHECK, locals[idx].reg, static_cast<uint8_t>(annot), 0));
    }
}

void Compiler::compileAssignment(AssignmentNode* node) {
    int arg = resolveLocal(node->nameOfVariable);
    if (arg != -1) {
        if (!locals[arg].isMutable) throw std::runtime_error("Variable is immutable.");
        compileExpression(node->expression.get(), locals[arg].reg);
    } else {
        auto it = globalIndex.find(node->nameOfVariable);
        if (it == globalIndex.end()) throw std::runtime_error("Undefined variable.");
        uint8_t save = nextReg;
        uint8_t r = compileExpression(node->expression.get());
        chunk.emit(encodeABx(OpCode::OP_SGLOB, r, it->second));
        freeRegsTo(save);
    }
}

void Compiler::compileIf(IfNode* node) {
    uint8_t save = nextReg;
    uint8_t cond = compileExpression(node->condition.get());

    size_t thenJump = chunk.emitJump(OpCode::OP_JMPF, cond);
    freeRegsTo(save);

    beginScope();
    for (auto& stmt : node->thenBlock) compileNode(stmt.get());
    endScope();

    size_t elseJump = chunk.emitJump(OpCode::OP_JMP);
    chunk.patchJump(thenJump);

    beginScope();
    for (auto& stmt : node->elseBlock) compileNode(stmt.get());
    endScope();

    chunk.patchJump(elseJump);
}

void Compiler::compileWhile(WhileNode* node) {
    const size_t loopStart = chunk.code.size();
    loopStack.push_back({loopStart, {}, scopeDepth});

    uint8_t save = nextReg;
    uint8_t cond = compileExpression(node->condition.get());

    size_t exitJump = chunk.emitJump(OpCode::OP_JMPF, cond);
    freeRegsTo(save);

    beginScope();
    for (auto& stmt : node->body) compileNode(stmt.get());
    endScope();

    chunk.emitLoop(loopStart);
    chunk.patchJump(exitJump);

    for (size_t breakJump : loopStack.back().breakJumps) {
        chunk.patchJump(breakJump);
    }
    loopStack.pop_back();
}

void Compiler::compileFor(const ForNode* node) {
    beginScope();
    if (node->init) compileNode(node->init.get());

    const size_t loopStart = chunk.code.size();
    uint8_t save = nextReg;
    uint8_t cond = compileExpression(node->condition.get());

    size_t exitJump = chunk.emitJump(OpCode::OP_JMPF, cond);
    freeRegsTo(save);

    loopStack.push_back({0, {}, scopeDepth});

    beginScope();
    for (auto& stmt : node->body) compileNode(stmt.get());
    endScope();

    loopStack.back().loopStart = chunk.code.size();
    if (node->increment) compileNode(node->increment.get());

    chunk.emitLoop(loopStart);
    chunk.patchJump(exitJump);

    for (size_t breakJump : loopStack.back().breakJumps) {
        chunk.patchJump(breakJump);
    }
    loopStack.pop_back();
    endScope();
}

void Compiler::compileRepeat(RepeatNode* node) {
    beginScope();
    const std::string counterName = "$__repeat_" + std::to_string(repeatCounter++);
    addLocal(counterName, true);

    int counterIdx = resolveLocal(counterName);
    uint8_t counterReg = locals[counterIdx].reg;

    compileExpression(node->count.get(), counterReg);

    const size_t loopStart = chunk.code.size();
    loopStack.push_back({loopStart, {}, scopeDepth});

    uint8_t save = nextReg;
    uint8_t zeroReg = allocReg();
    chunk.emit(encodeABx(OpCode::OP_LOADINT, zeroReg, static_cast<uint16_t>(0 + 32767)));
    uint8_t condReg = allocReg();
    chunk.emit(encodeABC(OpCode::OP_GT, condReg, counterReg, zeroReg));

    size_t exitJump = chunk.emitJump(OpCode::OP_JMPF, condReg);
    freeRegsTo(save);

    beginScope();
    for (auto& stmt : node->body) compileNode(stmt.get());
    endScope();

    save = nextReg;
    uint8_t oneReg = allocReg();
    chunk.emit(encodeABx(OpCode::OP_LOADINT, oneReg, 1 + 32767));
    chunk.emit(encodeABC(OpCode::OP_SUB, counterReg, counterReg, oneReg));
    freeRegsTo(save);

    chunk.emitLoop(loopStart);
    chunk.patchJump(exitJump);

    for (size_t breakJump : loopStack.back().breakJumps) {
        chunk.patchJump(breakJump);
    }
    loopStack.pop_back();
    endScope();
}

void Compiler::compileBreak() {
    if (loopStack.empty()) throw std::runtime_error("'break' outside loop");
    loopStack.back().breakJumps.push_back(chunk.emitJump(OpCode::OP_JMP));
}

void Compiler::compileContinue() {
    if (loopStack.empty()) throw std::runtime_error("'continue' outside loop");
    chunk.emitLoop(loopStack.back().loopStart);
}

void Compiler::compileFunctionDecl(FunctionDeclNode* node) {
    uint16_t funcIdx = static_cast<uint16_t>(functions.size());
    functionIndex[node->name] = funcIdx;
    functions.push_back({});

    // Save compiler state
    Chunk savedChunk = std::move(chunk);
    std::vector<Local> savedLocals = std::move(locals);
    int savedScopeDepth = scopeDepth;
    auto savedLoopStack = std::move(loopStack);
    uint8_t savedNextReg = nextReg;
    uint8_t savedMaxReg = maxReg;

    // Reset for new function
    chunk = Chunk{};
    locals.clear();
    scopeDepth = 0;
    loopStack.clear();
    nextReg = 0;
    maxReg = 0;

    beginScope();
    // Add params as locals, emit OP_TYPECHECK for typed params
    for (auto& [pname, ptype] : node->params) {
        addLocal(pname, true, ptype);
        if (ptype != TypeAnnotation::None) {
            int idx = resolveLocal(pname);
            chunk.emit(encodeABC(OpCode::OP_TYPECHECK, locals[idx].reg, static_cast<uint8_t>(ptype), 0));
        }
    }
    for (auto& stmt : node->body) compileNode(stmt.get());

    // Implicit return null
    uint8_t nullReg = allocReg();
    chunk.emit(encodeABC(OpCode::OP_LOADNULL, nullReg, 0, 0));
    chunk.emit(encodeABC(OpCode::OP_RET, nullReg, 0, 0));

    functions[funcIdx].name = node->name;
    functions[funcIdx].arity = static_cast<int>(node->params.size());
    functions[funcIdx].chunk = std::move(chunk);
    functions[funcIdx].maxRegs = maxReg;
    functions[funcIdx].returnType = node->returnType;
    // Store param types for call-site checking
    functions[funcIdx].paramTypes.reserve(node->params.size());
    for (auto& [pname, ptype] : node->params)
        functions[funcIdx].paramTypes.push_back(ptype);

    // Restore state
    chunk = std::move(savedChunk);
    locals = std::move(savedLocals);
    scopeDepth = savedScopeDepth;
    loopStack = std::move(savedLoopStack);
    nextReg = savedNextReg;
    maxReg = savedMaxReg;
}

void Compiler::compileReturn(ReturnNode* node) {
    const uint8_t save = nextReg;
    uint8_t r;
    if (node->expression) {
        r = compileExpression(node->expression.get());
    } else {
        r = allocReg();
        chunk.emit(encodeABC(OpCode::OP_LOADNULL, r, 0, 0));
    }
    chunk.emit(encodeABC(OpCode::OP_RET, r, 0, 0));
    freeRegsTo(save);
}

uint8_t Compiler::compileFunctionCall(FunctionCallNode* node, uint8_t dst) {
    if (node->name == "print") {
        if (node->args.size() != 1) throw std::runtime_error("print() expects 1 arg");
        uint8_t save = nextReg;
        uint8_t r = compileExpression(node->args[0].get());
        chunk.emit(encodeABC(OpCode::OP_LOG, r, 0, 0));
        freeRegsTo(save);
        chunk.emit(encodeABC(OpCode::OP_LOADNULL, dst, 0, 0));
        return dst;
    }
    if (node->name == "wait") {
        if (node->args.size() != 1) throw std::runtime_error("wait() expects 1 arg");
        uint8_t save = nextReg;
        uint8_t r = compileExpression(node->args[0].get());
        chunk.emit(encodeABC(OpCode::OP_WAIT, r, 0, 0));
        freeRegsTo(save);
        chunk.emit(encodeABC(OpCode::OP_LOADNULL, dst, 0, 0));
        return dst;
    }

    auto it = functionIndex.find(node->name);
    if (it == functionIndex.end()) throw std::runtime_error("Undefined function: " + node->name);

    uint8_t base = nextReg;
    for (auto& arg : node->args) {
        uint8_t r = allocReg();
        compileExpression(arg.get(), r);
    }

    chunk.emit(encodeABC(OpCode::OP_CALL, base, static_cast<uint8_t>(it->second & 0xFF), static_cast<uint8_t>(node->args.size())));
    freeRegsTo(base + 1);

    if (dst != base) chunk.emit(encodeABC(OpCode::OP_MOVE, dst, base, 0));
    return dst;
}

uint8_t Compiler::compileNumber(NumberNode* node, uint8_t dst) {
    int val = node->value;
    if (val >= -32767 && val <= 32767) {
        chunk.emit(encodeABx(OpCode::OP_LOADINT, dst, static_cast<uint16_t>(val + 32767)));
    } else {
        uint16_t ki = chunk.addConstant(Value(val));
        chunk.emit(encodeABx(OpCode::OP_LOADK, dst, ki));
    }
    return dst;
}

uint8_t Compiler::compileDouble(DoubleNode* node, uint8_t dst) {
    uint16_t ki = chunk.addConstant(Value(node->value));
    chunk.emit(encodeABx(OpCode::OP_LOADK, dst, ki));
    return dst;
}

uint8_t Compiler::compileBoolean(BooleanNode* node, uint8_t dst) {
    chunk.emit(encodeABC(OpCode::OP_LOADBOOL, dst, node->value ? 1 : 0, 0));
    return dst;
}

uint8_t Compiler::compileString(StringNode* node, uint8_t dst) {
    uint16_t ki = chunk.addConstant(Value(node->value));
    chunk.emit(encodeABx(OpCode::OP_LOADK, dst, ki));
    return dst;
}

uint8_t Compiler::compileVariable(VariableNode* node, uint8_t dst) {
    int arg = resolveLocal(node->nameOfVariable);
    if (arg != -1) {
        uint8_t srcReg = locals[arg].reg;
        if (srcReg != dst) chunk.emit(encodeABC(OpCode::OP_MOVE, dst, srcReg, 0));
        return dst;
    }
    auto it = globalIndex.find(node->nameOfVariable);
    if (it == globalIndex.end()) throw std::runtime_error("Undefined variable.");
    chunk.emit(encodeABx(OpCode::OP_GGLOB, dst, it->second));
    return dst;
}

uint8_t Compiler::compileUnaryOp(UnaryOperationNode* node, uint8_t dst) {
    uint8_t save = nextReg;
    uint8_t r = compileExpression(node->operand.get());
    if (node->operation == "!") chunk.emit(encodeABC(OpCode::OP_NOT, dst, r, 0));
    else if (node->operation == "-") chunk.emit(encodeABC(OpCode::OP_NEG, dst, r, 0));
    else throw std::runtime_error("Unknown unary operator");
    freeRegsTo(save);
    return dst;
}

uint8_t Compiler::compileBinaryOp(BinaryOperationNode* node, uint8_t dst) {
    static const std::unordered_map<std::string, OpCode> opTable = {
        {"+", OpCode::OP_ADD}, {"-", OpCode::OP_SUB}, {"*", OpCode::OP_MUL},
        {"/", OpCode::OP_DIV}, {"%", OpCode::OP_MOD},
        {"==", OpCode::OP_EQ}, {"!=", OpCode::OP_NEQ},
        {"<", OpCode::OP_LT}, {">", OpCode::OP_GT},
        {"<=", OpCode::OP_LE}, {">=", OpCode::OP_GE},
        {"&&", OpCode::OP_AND}, {"||", OpCode::OP_OR},
        {"&", OpCode::OP_BIT_AND}, {"|", OpCode::OP_BIT_OR}, {"^", OpCode::OP_BIT_XOR},
        {"<<", OpCode::OP_SHL}, {">>", OpCode::OP_SHR},
    };

    // Constant folding
    if (node->leftNode->getType() == ExprType::Number && node->rightNode->getType() == ExprType::Number) {
        int a = static_cast<NumberNode*>(node->leftNode.get())->value;
        int b = static_cast<NumberNode*>(node->rightNode.get())->value;
        const auto& op = node->operation;
        int result;
        bool folded = true;
        if (op == "+") result = a + b;
        else if (op == "-") result = a - b;
        else if (op == "*") result = a * b;
        else if (op == "/" && b != 0) result = a / b;
        else if (op == "%" && b != 0) result = a % b;
        else folded = false;
        if (folded) {
            if (result >= -32767 && result <= 32767) {
                chunk.emit(encodeABx(OpCode::OP_LOADINT, dst, static_cast<uint16_t>(result + 32767)));
            } else {
                uint16_t ki = chunk.addConstant(Value(result));
                chunk.emit(encodeABx(OpCode::OP_LOADK, dst, ki));
            }
            return dst;
        }
    }

    uint8_t save = nextReg;
    uint8_t rB = compileExpression(node->leftNode.get());
    uint8_t rC = compileExpression(node->rightNode.get());

    auto it = opTable.find(node->operation);
    if (it == opTable.end()) throw std::runtime_error("Unknown binary operator");
    chunk.emit(encodeABC(it->second, dst, rB, rC));
    freeRegsTo(save);
    return dst;
}

void Compiler::beginScope() {
    scopeDepth++;
}

void Compiler::endScope() {
    scopeDepth--;
    while (!locals.empty() && locals.back().depth > scopeDepth) {
        locals.pop_back();
        freeReg();
    }
}

void Compiler::addLocal(const std::string& name, bool isMutable, TypeAnnotation typeAnnot) {
    for (auto & local : std::ranges::reverse_view(locals)) {
        if (local.depth < scopeDepth) break;
        if (local.name == name) throw std::runtime_error("Variable redeclared: " + name);
    }
    uint8_t r = allocReg();
    locals.push_back({name, scopeDepth, isMutable, r, typeAnnot});
}

int Compiler::resolveLocal(const std::string& name) {
    for (int i = locals.size() - 1; i >= 0; i--) {
        if (locals[i].name == name) return i;
    }
    return -1;
}
