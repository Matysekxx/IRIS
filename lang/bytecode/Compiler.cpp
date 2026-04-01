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
        case StmtType::ClassDecl: compileClassDecl(static_cast<ClassDeclNode*>(node)); return;
        case StmtType::FieldAssign: compileFieldAssign(static_cast<FieldAssignNode*>(node)); return;
        case StmtType::ExprStmt: compileExprStmt(static_cast<ExpressionStmtNode*>(node)); return;
        case StmtType::IndexAssign: compileIndexAssign(static_cast<IndexAssignNode*>(node)); return;
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
        case ExprType::FieldAccess: return compileFieldAccess(static_cast<FieldAccessNode*>(expr), dst);
        case ExprType::MethodCall: return compileMethodCall(static_cast<MethodCallNode*>(expr), dst);
        case ExprType::IndexAccess: return compileIndexAccess(static_cast<IndexAccessNode*>(expr), dst);
        case ExprType::ArrayAlloc: return compileArrayAlloc(static_cast<ArrayAllocNode*>(expr), dst);
        case ExprType::ArrayLiteral: return compileArrayLiteral(static_cast<ArrayLiteralNode*>(expr), dst);
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

    // Track class type for field/method access
    if (node->expression->getType() == ExprType::FunctionCall) {
        auto* call = static_cast<FunctionCallNode*>(node->expression.get());
        if (classIndex.contains(call->name)) {
            varClassMap[node->nameOfVariable] = call->name;
        }
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

void Compiler::compileClassDecl(ClassDeclNode* node) {
    uint16_t clsId = static_cast<uint16_t>(classes.size());
    classIndex[node->name] = clsId;
    ClassMeta meta;
    meta.name = node->name;

    for (uint16_t i = 0; i < node->fields.size(); i++) {
        auto& f = node->fields[i];
        meta.fields.push_back({f.name, f.isMutable, f.isPublic});
        meta.fieldIndex[f.name] = i;
    }

    classes.push_back(std::move(meta));

    // Compile methods as functions with implicit 'this' as first param
    std::string savedClassName = currentClassName;
    currentClassName = node->name;

    for (auto& m : node->methods) {
        auto* funcDecl = m.function.get();

        // Insert 'this' as first parameter
        std::vector<std::pair<std::string, TypeAnnotation>> params;
        params.emplace_back("this", TypeAnnotation::None);
        for (auto& p : funcDecl->params) {
            params.push_back(p);
        }

        uint16_t funcIdx = static_cast<uint16_t>(functions.size());
        std::string qualName = node->name + "." + funcDecl->name;
        functionIndex[qualName] = funcIdx;
        classes[clsId].methodIndex[funcDecl->name] = funcIdx;
        classes[clsId].methodPublic[funcDecl->name] = m.isPublic;
        functions.push_back({});

        // Save compiler state
        Chunk savedChunk = std::move(chunk);
        std::vector<Local> savedLocals = std::move(locals);
        int savedScopeDepth = scopeDepth;
        auto savedLoopStack = std::move(loopStack);
        uint8_t savedNextReg = nextReg;
        uint8_t savedMaxReg = maxReg;

        chunk = Chunk{};
        locals.clear();
        scopeDepth = 0;
        loopStack.clear();
        nextReg = 0;
        maxReg = 0;

        beginScope();
        for (auto& [pname, ptype] : params) {
            addLocal(pname, true, ptype);
            if (ptype != TypeAnnotation::None) {
                int idx = resolveLocal(pname);
                chunk.emit(encodeABC(OpCode::OP_TYPECHECK, locals[idx].reg, static_cast<uint8_t>(ptype), 0));
            }
        }
        for (auto& stmt : funcDecl->body) compileNode(stmt.get());

        uint8_t nullReg = allocReg();
        chunk.emit(encodeABC(OpCode::OP_LOADNULL, nullReg, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_RET, nullReg, 0, 0));

        functions[funcIdx].name = qualName;
        functions[funcIdx].arity = static_cast<int>(params.size());
        functions[funcIdx].chunk = std::move(chunk);
        functions[funcIdx].maxRegs = maxReg;
        functions[funcIdx].returnType = funcDecl->returnType;
        functions[funcIdx].paramTypes.reserve(params.size());
        for (auto& [pn, pt] : params)
            functions[funcIdx].paramTypes.push_back(pt);

        chunk = std::move(savedChunk);
        locals = std::move(savedLocals);
        scopeDepth = savedScopeDepth;
        loopStack = std::move(savedLoopStack);
        nextReg = savedNextReg;
        maxReg = savedMaxReg;
    }

    currentClassName = savedClassName;
}

void Compiler::compileFieldAssign(FieldAssignNode* node) {
    std::string className;
    if (node->objectName == "this") {
        className = currentClassName;
    } else {
        auto it = varClassMap.find(node->objectName);
        if (it == varClassMap.end()) throw std::runtime_error("Unknown class for '" + node->objectName + "'");
        className = it->second;
    }

    auto& meta = classes[classIndex[className]];
    auto fieldIt = meta.fieldIndex.find(node->fieldName);
    if (fieldIt == meta.fieldIndex.end())
        throw std::runtime_error("Unknown field '" + node->fieldName + "' on class '" + className + "'");

    // Access control: private fields only from inside the class
    if (!meta.fields[fieldIt->second].isPublic && currentClassName != className)
        throw std::runtime_error("Cannot access private field '" + node->fieldName + "'");
    if (!meta.fields[fieldIt->second].isMutable && node->objectName != "this")
        throw std::runtime_error("Cannot assign to immutable field '" + node->fieldName + "'");

    uint8_t save = nextReg;

    // Resolve object register
    uint8_t objReg;
    int localIdx = resolveLocal(node->objectName);
    if (localIdx != -1) {
        objReg = locals[localIdx].reg;
    } else {
        auto gIt = globalIndex.find(node->objectName);
        if (gIt == globalIndex.end()) throw std::runtime_error("Undefined variable '" + node->objectName + "'");
        objReg = allocReg();
        chunk.emit(encodeABx(OpCode::OP_GGLOB, objReg, gIt->second));
    }

    uint8_t valReg = compileExpression(node->expression.get());
    chunk.emit(encodeABC(OpCode::OP_SET_FIELD, valReg, objReg, static_cast<uint8_t>(fieldIt->second)));
    freeRegsTo(save);
}

void Compiler::compileExprStmt(ExpressionStmtNode* node) {
    uint8_t save = nextReg;
    compileExpression(node->expression.get());
    freeRegsTo(save);
}

uint8_t Compiler::compileFieldAccess(FieldAccessNode* node, uint8_t dst) {
    std::string className;
    if (node->objectName == "this") {
        className = currentClassName;
    } else {
        auto it = varClassMap.find(node->objectName);
        if (it == varClassMap.end()) throw std::runtime_error("Unknown class for '" + node->objectName + "'");
        className = it->second;
    }

    auto& meta = classes[classIndex[className]];
    auto fieldIt = meta.fieldIndex.find(node->fieldName);
    if (fieldIt == meta.fieldIndex.end())
        throw std::runtime_error("Unknown field '" + node->fieldName + "' on class '" + className + "'");

    if (!meta.fields[fieldIt->second].isPublic && currentClassName != className)
        throw std::runtime_error("Cannot access private field '" + node->fieldName + "'");

    uint8_t save = nextReg;
    uint8_t objReg;
    int localIdx = resolveLocal(node->objectName);
    if (localIdx != -1) {
        objReg = locals[localIdx].reg;
    } else {
        auto gIt = globalIndex.find(node->objectName);
        if (gIt == globalIndex.end()) throw std::runtime_error("Undefined variable '" + node->objectName + "'");
        objReg = allocReg();
        chunk.emit(encodeABx(OpCode::OP_GGLOB, objReg, gIt->second));
    }

    chunk.emit(encodeABC(OpCode::OP_GET_FIELD, dst, objReg, static_cast<uint8_t>(fieldIt->second)));
    freeRegsTo(save);
    return dst;
}

uint8_t Compiler::compileMethodCall(MethodCallNode* node, uint8_t dst) {
    // --- Class method calls (existing code) ---
    std::string className;
    if (node->objectName == "this") {
        className = currentClassName;
    } else {
        auto it = varClassMap.find(node->objectName);
        if (it == varClassMap.end()) throw std::runtime_error("Unknown class for '" + node->objectName + "'");
        className = it->second;
    }

    auto& meta = classes[classIndex[className]];
    auto methodIt = meta.methodIndex.find(node->methodName);
    if (methodIt == meta.methodIndex.end())
        throw std::runtime_error("Unknown method '" + node->methodName + "' on class '" + className + "'");

    if (!meta.methodPublic[node->methodName] && currentClassName != className)
        throw std::runtime_error("Cannot call private method '" + node->methodName + "'");

    uint16_t funcIdx = methodIt->second;

    // Layout: R[base]=this, R[base+1..]=args
    uint8_t base = nextReg;

    // Place object (this) at base
    uint8_t objReg = allocReg();
    int localIdx = resolveLocal(node->objectName);
    if (localIdx != -1) {
        if (locals[localIdx].reg != objReg)
            chunk.emit(encodeABC(OpCode::OP_MOVE, objReg, locals[localIdx].reg, 0));
    } else {
        auto gIt = globalIndex.find(node->objectName);
        if (gIt == globalIndex.end()) throw std::runtime_error("Undefined variable '" + node->objectName + "'");
        chunk.emit(encodeABx(OpCode::OP_GGLOB, objReg, gIt->second));
    }

    // Compile args
    for (auto& arg : node->args) {
        uint8_t r = allocReg();
        compileExpression(arg.get(), r);
    }

    uint8_t totalArgs = static_cast<uint8_t>(node->args.size() + 1); // +1 for this
    chunk.emit(encodeABC(OpCode::OP_CALL, base, static_cast<uint8_t>(funcIdx & 0xFF), totalArgs));
    freeRegsTo(base + 1);

    if (dst != base) chunk.emit(encodeABC(OpCode::OP_MOVE, dst, base, 0));
    return dst;
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

    // --- Collection constructors: array(), len() ---
    if (node->name == "array") {
        if (node->args.size() != 1) throw std::runtime_error("array() expects 1 arg (size)");
        uint8_t save = nextReg;
        uint8_t sizeReg = compileExpression(node->args[0].get());
        chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, dst, sizeReg, 0));
        freeRegsTo(save);
        return dst;
    }
    if (node->name == "len") {
        if (node->args.size() != 1) throw std::runtime_error("len() expects 1 arg");
        uint8_t save = nextReg;
        uint8_t collReg = compileExpression(node->args[0].get());
        chunk.emit(encodeABC(OpCode::OP_COLL_LEN, dst, collReg, 0));
        freeRegsTo(save);
        return dst;
    }

    auto it = functionIndex.find(node->name);
    if (it == functionIndex.end()) {
        // Check if it's a class instantiation
        auto classIt = classIndex.find(node->name);
        if (classIt != classIndex.end()) {
            // Create new instance
            uint16_t clsId = classIt->second;
            chunk.emit(encodeABx(OpCode::OP_NEW_OBJ, dst, clsId));

            // Call init() if exists
            auto& meta = classes[clsId];
            auto initIt = meta.methodIndex.find("init");
            if (initIt != meta.methodIndex.end()) {
                uint8_t callBase = allocReg();
                chunk.emit(encodeABC(OpCode::OP_MOVE, callBase, dst, 0));
                for (auto& arg : node->args) {
                    uint8_t r = allocReg();
                    compileExpression(arg.get(), r);
                }
                uint8_t totalArgs = static_cast<uint8_t>(node->args.size() + 1);
                chunk.emit(encodeABC(OpCode::OP_CALL, callBase, static_cast<uint8_t>(initIt->second & 0xFF), totalArgs));
                freeRegsTo(callBase);
            }
            return dst;
        }
        throw std::runtime_error("Undefined function: " + node->name);
    }

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

uint8_t Compiler::compileIndexAccess(IndexAccessNode* node, uint8_t dst) {
    uint8_t save = nextReg;
    uint8_t collReg = compileExpression(node->object.get());
    uint8_t idxReg = compileExpression(node->index.get());
    chunk.emit(encodeABC(OpCode::OP_IDX_GET, dst, collReg, idxReg));
    freeRegsTo(save);
    return dst;
}

void Compiler::compileIndexAssign(IndexAssignNode* node) {
    uint8_t save = nextReg;

    // Resolve collection register
    uint8_t collReg;
    int localIdx = resolveLocal(node->objectName);
    if (localIdx != -1) {
        collReg = locals[localIdx].reg;
    } else {
        auto gIt = globalIndex.find(node->objectName);
        if (gIt == globalIndex.end()) throw std::runtime_error("Undefined variable '" + node->objectName + "'");
        collReg = allocReg();
        chunk.emit(encodeABx(OpCode::OP_GGLOB, collReg, gIt->second));
    }

    uint8_t idxReg = compileExpression(node->index.get());
    uint8_t valReg = compileExpression(node->value.get());
    chunk.emit(encodeABC(OpCode::OP_IDX_SET, valReg, collReg, idxReg));
    freeRegsTo(save);
}

uint8_t Compiler::compileArrayAlloc(ArrayAllocNode* node, uint8_t dst) {
    uint8_t save = nextReg;
    uint8_t sizeReg = compileExpression(node->size.get());

    uint8_t elemType = 0; // UNTYPED
    if (node->elementType == TypeAnnotation::Int || node->elementType == TypeAnnotation::IntArray) elemType = 1; // INT
    else if (node->elementType == TypeAnnotation::Double || node->elementType == TypeAnnotation::DoubleArray) elemType = 2; // DOUBLE
    else if (node->elementType == TypeAnnotation::String || node->elementType == TypeAnnotation::StringArray) elemType = 3; // VALUE
    else if (node->elementType == TypeAnnotation::Bool || node->elementType == TypeAnnotation::BoolArray) elemType = 3; // VALUE

    chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, dst, sizeReg, elemType));
    freeRegsTo(save);
    return dst;
}

uint8_t Compiler::compileArrayLiteral(ArrayLiteralNode* node, uint8_t dst) {
    uint8_t save = nextReg;
    int size = node->elements.size();

    uint8_t sizeReg = allocReg();
    if (size >= -32767 && size <= 32767) {
        chunk.emit(encodeABx(OpCode::OP_LOADINT, sizeReg, static_cast<uint16_t>(size + 32767)));
    } else {
        uint16_t ki = chunk.addConstant(Value(size));
        chunk.emit(encodeABx(OpCode::OP_LOADK, sizeReg, ki));
    }

    // Creating untyped array first, the elements will determine the final type through IDX_SET updates.
    chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, dst, sizeReg, 0));

    for (int i = 0; i < size; ++i) {
        uint8_t valSave = nextReg;
        uint8_t valReg = compileExpression(node->elements[i].get());

        uint8_t idxReg = allocReg();
        if (i >= -32767 && i <= 32767) {
            chunk.emit(encodeABx(OpCode::OP_LOADINT, idxReg, static_cast<uint16_t>(i + 32767)));
        } else {
            uint16_t ki = chunk.addConstant(Value(i));
            chunk.emit(encodeABx(OpCode::OP_LOADK, idxReg, ki));
        }

        chunk.emit(encodeABC(OpCode::OP_IDX_SET, valReg, dst, idxReg));
        freeRegsTo(valSave);
    }

    freeRegsTo(save);
    return dst;
}
