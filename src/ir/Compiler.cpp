/**
 * @file Compiler.cpp
 * @brief Implementation of the IRIS bytecode compiler.
 */

#include "Compiler.h"
#include "PeepholeOptimizer.h"
#include "core/NativeRegistry.h"
#include <ranges>
#include <stdexcept>
#include <algorithm>
#include <unordered_set>
#include <iostream>

using namespace iris::bytecode;
using namespace iris::core;
using namespace iris::node;

Chunk Compiler::compile(ProgramNode *program) {
    compileProgram(program);
    chunk.emit(encodeABC(OpCode::OP_HALT, 0, 0, 0));
    PeepholeOptimizer::optimize(chunk);

    return std::move(chunk);
}

void Compiler::compileNode(ASTNode *node) {
    // std::cout << "[DEBUG] compileNode: " << static_cast<int>(node->getStmtType()) << " file: " << node->location.file << ":" << node->location.line << std::endl;
    switch (node->getStmtType()) {
        case StmtType::Program: compileProgram(static_cast<ProgramNode *>(node));
            return;
        case StmtType::Repeat: compileRepeat(static_cast<RepeatNode *>(node));
            return;
        case StmtType::While: compileWhile(static_cast<WhileNode *>(node));
            return;
        case StmtType::For: compileFor(static_cast<ForNode *>(node));
            return;
        case StmtType::If: compileIf(static_cast<IfNode *>(node));
            return;
        case StmtType::Print: compileLog(static_cast<PrintNode *>(node));
            return;
        case StmtType::VarDecl: compileVarDecl(static_cast<VarDeclNode *>(node));
            return;
        case StmtType::Assignment: compileAssignment(static_cast<AssignmentNode *>(node));
            return;
        case StmtType::Wait: compileWait(static_cast<WaitNode *>(node));
            return;
        case StmtType::Break: compileBreak(node);
            return;
        case StmtType::Continue: compileContinue(node);
            return;
        case StmtType::FunctionDecl: compileFunctionDecl(static_cast<FunctionDeclNode *>(node));
            return;
        case StmtType::Return: compileReturn(static_cast<ReturnNode *>(node));
            return;
        case StmtType::ClassDecl: compileClassDecl(static_cast<ClassDeclNode *>(node));
            return;
        case StmtType::FieldAssign: compileFieldAssign(static_cast<FieldAssignNode *>(node));
            return;
        case StmtType::ExprStmt: compileExprStmt(static_cast<ExpressionStmtNode *>(node));
            return;
        case StmtType::IndexAssign: compileIndexAssign(static_cast<IndexAssignNode *>(node));
            return;
        case StmtType::TryCatch: compileTryCatch(static_cast<TryCatchNode *>(node));
            return;
        case StmtType::Throw: compileThrow(static_cast<ThrowNode *>(node));
            return;
        case StmtType::Switch: compileSwitch(static_cast<SwitchNode *>(node));
            return;
        case StmtType::Enum: compileEnum(static_cast<EnumNode *>(node));
            return;
        case StmtType::ImportNative: compileImportNative(static_cast<ImportNativeNode *>(node));
            return;
        case StmtType::Export: compileExport(static_cast<ExportNode *>(node));
            return;
        case StmtType::ImportNamed: {
            auto *named = static_cast<ImportNamedNode *>(node);
            if (named->importKind == ImportKind::NATIVE) {
                for (auto &[name, alias] : named->bindings) {
                    auto &nativeReg = iris::core::NativeRegistry::getInstance();
                    std::string fullName = named->library.empty() ? name : named->library + "." + name;
                    if (nativeReg.hasFunction(fullName)) {
                        nativeFunctionIndex[alias] = nativeReg.getIndex(fullName);
                    } else if (!named->library.empty() && nativeReg.hasFunction(name)) {
                        nativeFunctionIndex[alias] = nativeReg.getIndex(name);
                    } else {
                        throw CompileError(node->location, "Unknown native entity: " + fullName);
                    }
                }
            }
            // FILE/STD imports are already spliced at parse time — no-op at compile
            return;
        }
        case StmtType::ImportDefault:
        case StmtType::ImportNamespace:
            // FILE/STD imports are resolved during parsing (AST splicing).
            // At compile time, the imported symbols are already in the global scope.
            return;
        default:
            throw CompileError(node->location, "Compiler: unknown AST node type");
    }
}

void Compiler::compileImportNative(ImportNativeNode *node) {
    auto &nativeReg = iris::core::NativeRegistry::getInstance();
    std::string fullName = node->moduleName.empty() ? node->name : node->moduleName + "." + node->name;

    if (nativeReg.hasFunction(fullName)) {
        std::string alias = node->alias.empty() ? node->name : node->alias;
        nativeFunctionIndex[alias] = nativeReg.getIndex(fullName);
        return;
    } else if (!node->moduleName.empty() && nativeReg.hasFunction(node->name)) {
        std::string alias = node->alias.empty() ? node->name : node->alias;
        nativeFunctionIndex[alias] = nativeReg.getIndex(node->name);
        return;
    }
    throw CompileError(node->location, "Unknown native entity: " + fullName);
}

void Compiler::compileExport(ExportNode *node) {
    if (node->declaration) {
        compileNode(node->declaration.get());
    }
}

ExprResult Compiler::compileExpression(ExpressionNode *expr, uint8_t dst) {
    if (dst == 255) dst = allocReg();
    switch (expr->getExprType()) {
        case ExprType::Number: return compileNumber(static_cast<NumberNode *>(expr), dst);
        case ExprType::Double: return compileDouble(static_cast<DoubleNode *>(expr), dst);
        case ExprType::Boolean: return compileBoolean(static_cast<BooleanNode *>(expr), dst);
        case ExprType::String: return compileString(static_cast<StringNode *>(expr), dst);
        case ExprType::Variable: return compileVariable(static_cast<VariableNode *>(expr), dst);
        case ExprType::BinaryOp: return compileBinaryOp(static_cast<BinaryOperationNode *>(expr), dst);
        case ExprType::UnaryOp: return compileUnaryOp(static_cast<UnaryOperationNode *>(expr), dst);
        case ExprType::FunctionCall: return compileFunctionCall(static_cast<FunctionCallNode *>(expr), dst);
        case ExprType::FieldAccess: return compileFieldAccess(static_cast<FieldAccessNode *>(expr), dst);
        case ExprType::MethodCall: return compileMethodCall(static_cast<MethodCallNode *>(expr), dst);
        case ExprType::IndexAccess: return compileIndexAccess(static_cast<IndexAccessNode *>(expr), dst);
        case ExprType::ArrayAlloc: return compileArrayAlloc(static_cast<ArrayAllocNode *>(expr), dst);
        case ExprType::ArrayLiteral: return compileArrayLiteral(static_cast<ArrayLiteralNode *>(expr), dst);
        case ExprType::StringInterp: return compileStringInterp(static_cast<StringInterpNode *>(expr), dst);
        case ExprType::Switch: return compileSwitch(static_cast<SwitchNode *>(expr), dst);
        case ExprType::Null:
            chunk.emit(encodeABC(OpCode::OP_LOADNULL, dst, 0, 0));
            return {dst, TypeKind::None};
        default:
            throw CompileError(expr->location, "Compiler: unknown expression node type");
    }
}

void Compiler::compileProgram(ProgramNode *node) {
    for (auto &stmt: node->statements) {
        compileNode(stmt.get());
    }
}

void Compiler::compileLog(PrintNode *node) {
    uint8_t save = nextReg;
    ExprResult res = compileExpression(node->msg.get());
    chunk.emit(encodeABC(OpCode::OP_LOG, res.reg, 0, 0));
    freeRegsTo(save);
}

void Compiler::compileWait(WaitNode *node) {
    uint8_t save = nextReg;
    ExprResult res = compileExpression(node->duration.get());
    chunk.emit(encodeABC(OpCode::OP_WAIT, res.reg, 0, 0));
    freeRegsTo(save);
}

void Compiler::compileVarDecl(VarDeclNode *node) {
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
        ExprResult res = compileExpression(node->expression.get());
        globalTypes[slot] = res.type;
        if (!annot.isNone())
            chunk.emit(encodeABC(OpCode::OP_TYPECHECK, res.reg, static_cast<uint8_t>(annot.kind), 0));
        chunk.emit(encodeABC(OpCode::OP_DGLOB, res.reg, static_cast<uint8_t>(slot >> 8),
                             static_cast<uint8_t>(slot & 0xFF)));
        freeRegsTo(save);
    } else {
        addLocal(node->nameOfVariable, node->isMutable, annot, node->location);
        int idx = resolveLocal(node->nameOfVariable);
        ExprResult res = compileExpression(node->expression.get(), locals[idx].reg);
        
        if (locals[idx].typeAnnot.isNone()) locals[idx].typeAnnot = res.type;

        if (!annot.isNone()) {
            if (!isCompatible(res.type, annot)) {
                throw CompileError(node->location, "Type mismatch: expected " + typeAnnotationName(annot) + " but got " + typeAnnotationName(res.type));
            }
            chunk.emit(encodeABC(OpCode::OP_TYPECHECK, locals[idx].reg, static_cast<uint8_t>(annot.kind), 0));
        }
    }

    if (!annot.isNone() && annot.kind == TypeKind::Object) {
        varClassMap[node->nameOfVariable] = annot.name;
    } else if (!isGlobalScope()) {
        int idx = resolveLocal(node->nameOfVariable);
        if (idx != -1 && locals[idx].typeAnnot.kind == TypeKind::Object) {
            varClassMap[node->nameOfVariable] = locals[idx].typeAnnot.name;
        }
    } else if (node->expression->getExprType() == ExprType::FunctionCall) {
        auto *call = static_cast<FunctionCallNode *>(node->expression.get());
        if (classIndex.contains(call->name)) {
            varClassMap[node->nameOfVariable] = call->name;
        }
    }
}

void Compiler::compileAssignment(AssignmentNode *node) {
    int arg = resolveLocal(node->nameOfVariable);
    if (arg != -1) {
        if (!locals[arg].isMutable) throw CompileError(node->location, "Variable is immutable.");
        
        // OPTIMIZATION: x = x + 1 -> INC x
        if (node->expression->getExprType() == ExprType::BinaryOp) {
            auto *bin = static_cast<BinaryOperationNode *>(node->expression.get());
            if (bin->operation == "+" || bin->operation == "-") {
                bool isInc = bin->operation == "+";
                auto *left = bin->leftNode.get();
                auto *right = bin->rightNode.get();
                
                if (left->getExprType() == ExprType::Variable) {
                    auto *var = static_cast<VariableNode *>(left);
                    if (var->nameOfVariable == node->nameOfVariable) {
                        if (right->getExprType() == ExprType::Number) {
                            auto *num = static_cast<NumberNode *>(right);
                            if (num->value == 1) {
                                chunk.emit(encodeABC(isInc ? OpCode::OP_INC : OpCode::OP_DEC, locals[arg].reg, 0, 0));
                                return;
                            }
                        }
                    }
                }
            }
        }
        
        compileExpression(node->expression.get(), locals[arg].reg);
    } else {
        auto it = globalIndex.find(node->nameOfVariable);
        if (it == globalIndex.end()) throw CompileError(node->location, "Undefined variable: " + node->nameOfVariable);
        uint8_t save = nextReg;
        ExprResult res = compileExpression(node->expression.get());
        chunk.emit(encodeABx(OpCode::OP_SGLOB, res.reg, it->second));
        freeRegsTo(save);
    }
}

void Compiler::compileIf(IfNode *node) {
    uint8_t save = nextReg;
    ExprResult cond = compileExpression(node->condition.get());

    size_t thenJump = chunk.emitJump(OpCode::OP_JMPF, cond.reg);
    freeRegsTo(save);

    beginScope();
    for (auto &stmt: node->thenBlock) compileNode(stmt.get());
    endScope();

    size_t elseJump = chunk.emitJump(OpCode::OP_JMP);
    chunk.patchJump(thenJump);

    beginScope();
    for (auto &stmt: node->elseBlock) compileNode(stmt.get());
    endScope();

    chunk.patchJump(elseJump);
}

void Compiler::compileWhile(WhileNode *node) {
    const size_t loopStart = chunk.code.size();
    size_t loopIdx = loopStack.size();
    loopStack.push_back({loopStart, {}, scopeDepth});
    breakableStack.push_back({BreakableType::Loop, loopIdx});

    uint8_t save = nextReg;
    ExprResult cond = compileExpression(node->condition.get());

    size_t exitJump = chunk.emitJump(OpCode::OP_JMPF, cond.reg);
    freeRegsTo(save);

    beginScope();
    for (auto &stmt: node->body) compileNode(stmt.get());
    endScope();

    chunk.emitLoop(loopStart);
    chunk.patchJump(exitJump);

    for (size_t breakJump: loopStack.back().breakJumps) {
        chunk.patchJump(breakJump);
    }
    loopStack.pop_back();
    breakableStack.pop_back();
}

void Compiler::compileFor(const ForNode *node) {
    beginScope();
    if (node->init) compileNode(node->init.get());

    const size_t loopStart = chunk.code.size();
    uint8_t save = nextReg;
    ExprResult cond = compileExpression(node->condition.get());

    size_t exitJump = chunk.emitJump(OpCode::OP_JMPF, cond.reg);
    freeRegsTo(save);

    size_t loopIdx = loopStack.size();
    loopStack.push_back({0, {}, scopeDepth});
    breakableStack.push_back({BreakableType::Loop, loopIdx});

    beginScope();
    for (auto &stmt: node->body) compileNode(stmt.get());
    endScope();

    loopStack.back().loopStart = chunk.code.size();
    if (node->increment) compileNode(node->increment.get());

    chunk.emitLoop(loopStart);
    chunk.patchJump(exitJump);

    for (size_t breakJump: loopStack.back().breakJumps) {
        chunk.patchJump(breakJump);
    }
    loopStack.pop_back();
    breakableStack.pop_back();
    endScope();
}

void Compiler::compileRepeat(RepeatNode *node) {
    // OPTIMIZATION: Loop Unrolling for small constant counts
    if (node->count->getExprType() == ExprType::Number) {
        int count = static_cast<NumberNode *>(node->count.get())->value;
        if (count >= 0 && count <= 8) {
            beginScope();
            for (int i = 0; i < count; ++i) {
                beginScope();
                for (auto &stmt : node->body) compileNode(stmt.get());
                endScope();
            }
            endScope();
            return;
        }
    }

    beginScope();
    const std::string counterName = "$__repeat_" + std::to_string(repeatCounter++);
    addLocal(counterName, true, TypeKind::Int, node->location);

    int counterIdx = resolveLocal(counterName);
    uint8_t counterReg = locals[counterIdx].reg;

    compileExpression(node->count.get(), counterReg);

    const size_t loopStart = chunk.code.size();
    size_t loopIdx = loopStack.size();
    loopStack.push_back({loopStart, {}, scopeDepth});
    breakableStack.push_back({BreakableType::Loop, loopIdx});

    uint8_t save = nextReg;
    uint8_t zeroReg = allocReg();
    chunk.emit(encodeABx(OpCode::OP_LOADINT, zeroReg, static_cast<uint16_t>(0 + 32767)));
    uint8_t condReg = allocReg();
    chunk.emit(encodeABC(OpCode::OP_GT_INT, condReg, counterReg, zeroReg));

    size_t exitJump = chunk.emitJump(OpCode::OP_JMPF, condReg);
    freeRegsTo(save);

    beginScope();
    for (auto &stmt: node->body) compileNode(stmt.get());
    endScope();

    chunk.emit(encodeABC(OpCode::OP_DEC, counterReg, 0, 0));

    chunk.emitLoop(loopStart);
    chunk.patchJump(exitJump);

    for (size_t breakJump: loopStack.back().breakJumps) {
        chunk.patchJump(breakJump);
    }
    loopStack.pop_back();
    breakableStack.pop_back();
    endScope();
}

void Compiler::compileBreak(ASTNode *node) {
    if (breakableStack.empty()) throw CompileError(node->location, "'break' outside breakable context");
    auto &b = breakableStack.back();
    if (b.type == BreakableType::Loop) {
        loopStack[b.index].breakJumps.push_back(chunk.emitJump(OpCode::OP_JMP));
    } else {
        switchStack[b.index].breakJumps.push_back(chunk.emitJump(OpCode::OP_JMP));
    }
}

void Compiler::compileContinue(ASTNode *node) {
    if (loopStack.empty()) throw CompileError(node->location, "'continue' outside loop");
    chunk.emitLoop(loopStack.back().loopStart);
}

void Compiler::compileFunctionDecl(FunctionDeclNode *node) {
    uint16_t funcIdx = static_cast<uint16_t>(functions.size());
    functionIndex[node->name] = funcIdx;
    
    FunctionObject f;
    f.name = node->name;
    f.arity = static_cast<int>(node->params.size());
    f.returnType = node->returnType;
    for (auto &[pname, ptype]: node->params)
        f.paramTypes.push_back(ptype);
    functions.push_back(std::move(f));

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
    for (auto &[pname, ptype]: node->params) {
        addLocal(pname, true, ptype, node->location);
        if (ptype.kind != TypeKind::None) {
            bool skipCheck = false;
            if (ptype.kind == TypeKind::Object && isGenericParam(ptype.name)) {
                skipCheck = true;
            }

            if (!skipCheck) {
                int idx = resolveLocal(pname);
                chunk.emit(encodeABC(OpCode::OP_TYPECHECK, locals[idx].reg, static_cast<uint8_t>(ptype.kind), 0));
            }
        }
    }
    for (auto &stmt: node->body) compileNode(stmt.get());

    uint8_t nullReg = allocReg();
    chunk.emit(encodeABC(OpCode::OP_LOADNULL, nullReg, 0, 0));
    chunk.emit(encodeABC(OpCode::OP_RET, nullReg, 0, 0));
    
    PeepholeOptimizer::optimize(chunk);

    functions[funcIdx].chunk = std::move(chunk);
    functions[funcIdx].maxRegs = maxReg;

    chunk = std::move(savedChunk);
    locals = std::move(savedLocals);
    scopeDepth = savedScopeDepth;
    loopStack = std::move(savedLoopStack);
    nextReg = savedNextReg;
    maxReg = savedMaxReg;
}

void Compiler::compileReturn(ReturnNode *node) {
    if (node->expression) {
        if (node->expression->getExprType() == ExprType::FunctionCall) {
            auto *call = static_cast<FunctionCallNode *>(node->expression.get());
            if (call->name != "print" && call->name != "wait" &&
                call->name != "array" && call->name != "len" && call->name != "super") {
                auto it = functionIndex.find(call->name);
                if (it != functionIndex.end()) {
                    uint8_t save = nextReg;
                    uint8_t base = nextReg;
                    for (auto &arg: call->args) {
                        allocReg();
                        compileExpression(arg.get(), nextReg - 1);
                    }
                    chunk.emit(encodeABC(OpCode::OP_TAILCALL, base,
                                         static_cast<uint8_t>(it->second & 0xFF),
                                         static_cast<uint8_t>(call->args.size())));
                    freeRegsTo(save);
                    return;
                }
            }
        } else if (node->expression->getExprType() == ExprType::MethodCall) {
            auto *call = static_cast<MethodCallNode *>(node->expression.get());
            std::string qualName = call->objectName + "." + call->methodName;
            auto sIt = functionIndex.find(qualName);
            if (sIt != functionIndex.end()) {
                uint8_t save = nextReg;
                uint8_t base = nextReg;
                for (auto &arg: call->args) {
                    allocReg();
                    compileExpression(arg.get(), nextReg - 1);
                }
                chunk.emit(encodeABC(OpCode::OP_TAILCALL, base,
                                     static_cast<uint8_t>(sIt->second & 0xFF),
                                     static_cast<uint8_t>(call->args.size())));
                freeRegsTo(save);
                return;
            }

            if (call->objectName != "super") {
                uint8_t save = nextReg;
                uint8_t base = nextReg;
                const uint8_t objReg = allocReg();

                if (call->objectName == "this") {
                    int thisLocal = resolveLocal("this");
                    if (thisLocal == -1)
                        throw CompileError(node->location, "this.method() must be called from a method");
                    if (locals[thisLocal].reg != objReg)
                        chunk.emit(encodeABC(OpCode::OP_MOVE, objReg, locals[thisLocal].reg, 0));
                } else {
                    int localIdx = resolveLocal(call->objectName);
                    if (localIdx != -1) {
                        if (locals[localIdx].reg != objReg)
                            chunk.emit(encodeABC(OpCode::OP_MOVE, objReg, locals[localIdx].reg, 0));
                    } else {
                        auto gIt = globalIndex.find(call->objectName);
                        if (gIt == globalIndex.end())
                            throw CompileError(node->location, "Undefined variable '" + call->objectName + "'");
                        chunk.emit(encodeABx(OpCode::OP_GGLOB, objReg, gIt->second));
                    }
                }

                for (auto &arg: call->args) {
                    allocReg();
                    compileExpression(arg.get(), nextReg - 1);
                }

                uint8_t totalArgs = static_cast<uint8_t>(call->args.size() + 1);
                uint16_t nameId = chunk.addConstant(Value(internString(call->methodName)));
                if (nameId > 255)
                    throw CompileError(node->location, "Too many unique strings in chunk for OP_TAIL_INVOKE B byte");
                chunk.emit(encodeABC(OpCode::OP_TAIL_INVOKE, base,
                                     static_cast<uint8_t>(nameId), totalArgs));
                freeRegsTo(save);
                return;
            }
        }
    }

    const uint8_t save = nextReg;
    uint8_t r;
    if (node->expression) {
        r = compileExpression(node->expression.get()).reg;
    } else {
        r = allocReg();
        chunk.emit(encodeABC(OpCode::OP_LOADNULL, r, 0, 0));
    }
    chunk.emit(encodeABC(OpCode::OP_RET, r, 0, 0));
    freeRegsTo(save);
}

void Compiler::compileClassDecl(ClassDeclNode *node) {
    uint16_t clsId = static_cast<uint16_t>(classes.size());
    classIndex[node->name] = clsId;
    ClassMeta meta;
    meta.name = node->name;
    meta.genericParams = node->genericParams;

    if (!node->parentName.empty()) {
        auto parentIt = classIndex.find(node->parentName);
        if (parentIt == classIndex.end())
            throw CompileError(node->location, "Unknown parent class: " + node->parentName);

        uint16_t parentId = parentIt->second;
        meta.parentClassId = static_cast<int16_t>(parentId);
        auto &parentMeta = classes[parentId];

        for (uint16_t i = 0; i < parentMeta.fields.size(); i++) {
            meta.fields.push_back(parentMeta.fields[i]);
            meta.fieldIndex[parentMeta.fields[i].name] = i;
        }

        for (auto &[methodName, funcIdx]: parentMeta.methodIndex) {
            meta.methodIndex[methodName] = funcIdx;
            meta.methodAccess[methodName] = parentMeta.methodAccess.at(methodName);
        }

        for (const auto &absM: parentMeta.abstractMethods) {
            meta.abstractMethods.push_back(absM);
        }
    }

    meta.isAbstract = node->isAbstract;

    for (uint16_t i = 0; i < node->fields.size(); i++) {
        auto &f = node->fields[i];
        if (f.isStatic) {
            std::string fullName = node->name + "." + f.name;
            if (!globalIndex.contains(fullName)) {
                globalIndex[fullName] = globalCount++;
            }
            uint8_t r = allocReg();
            chunk.emit(encodeABC(OpCode::OP_LOADNULL, r, 0, 0));
            uint16_t slot = globalIndex[fullName];
            chunk.emit(encodeABC(OpCode::OP_DGLOB, r, static_cast<uint8_t>(slot >> 8),
                                 static_cast<uint8_t>(slot & 0xFF)));
            freeReg();
        } else {
            uint16_t fIdx = static_cast<uint16_t>(meta.fields.size());
            meta.fields.push_back({f.name, f.isMutable, f.isStatic, f.access, f.type});
            meta.fieldIndex[f.name] = fIdx;
        }
    }

    classes.push_back(meta);

    std::string savedClassName = currentClassName;
    currentClassName = node->name;

    struct MethodToCompile {
        FunctionDeclNode *funcDecl;
        std::vector<std::pair<std::string, TypeAnnotation>> params;
        uint16_t funcIdx;
    };
    std::vector<MethodToCompile> methodsToCompile;

    for (auto &m: node->methods) {
        auto *funcDecl = m.function.get();
        const std::string &methName = funcDecl->name;

        if (m.isAbstract) {
            if (!meta.isAbstract) {
                throw CompileError(node->location,
                    "Class '" + meta.name + "' is not abstract and cannot contain abstract method '" + methName + "'");
            }
            if (std::find(meta.abstractMethods.begin(), meta.abstractMethods.end(), methName) == meta.abstractMethods.end()) {
                meta.abstractMethods.push_back(methName);
            }
            meta.methodIndex[methName] = 0xFFFF;
            meta.methodAccess[methName] = m.access;
            continue;
        }

        auto it = std::find(meta.abstractMethods.begin(), meta.abstractMethods.end(), methName);
        if (it != meta.abstractMethods.end()) {
            meta.abstractMethods.erase(it);
        }

        std::vector<std::pair<std::string, TypeAnnotation>> params;
        if (!m.isStatic) {
            params.emplace_back("this", TypeKind::None);
        }
        for (auto &p: funcDecl->params) {
            params.push_back(p);
        }

        uint16_t funcIdx = static_cast<uint16_t>(functions.size());
        std::string qualName = node->name + "." + methName;
        functionIndex[qualName] = funcIdx;
        if (!m.isStatic) {
            meta.methodIndex[methName] = funcIdx;
            meta.methodAccess[methName] = m.access;
        }

        FunctionObject f;
        f.name = qualName;
        f.arity = static_cast<int>(params.size());
        f.returnType = funcDecl->returnType;
        for (auto &[pn, pt]: params)
            f.paramTypes.push_back(pt);
        functions.push_back(std::move(f));

        methodsToCompile.push_back({funcDecl, std::move(params), funcIdx});
    }

    if (!meta.isAbstract && !meta.abstractMethods.empty()) {
        throw CompileError(node->location,
            "Class '" + meta.name + "' is not abstract and does not implement abstract method '" + meta.abstractMethods[0] + "' from parent");
    }

    // Update global classes entry before compiling bodies, so that constructors/methods are fully lookupable!
    classes[clsId] = meta;

    for (auto &mtc: methodsToCompile) {
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
        for (auto &[pname, ptype]: mtc.params) {
            addLocal(pname, true, ptype, node->location);
            if (ptype != TypeKind::None) {
                int idx = resolveLocal(pname);
                chunk.emit(encodeABC(OpCode::OP_TYPECHECK, locals[idx].reg, static_cast<uint8_t>(ptype.kind), 0));
            }
        }
        for (auto &stmt: mtc.funcDecl->body) compileNode(stmt.get());

        uint8_t nullReg = allocReg();
        chunk.emit(encodeABC(OpCode::OP_LOADNULL, nullReg, 0, 0));
        chunk.emit(encodeABC(OpCode::OP_RET, nullReg, 0, 0));

        functions[mtc.funcIdx].chunk = std::move(chunk);
        functions[mtc.funcIdx].maxRegs = maxReg;

        chunk = std::move(savedChunk);
        locals = std::move(savedLocals);
        scopeDepth = savedScopeDepth;
        loopStack = std::move(savedLoopStack);
        nextReg = savedNextReg;
        maxReg = savedMaxReg;
    }

    classes[clsId] = std::move(meta);
    currentClassName = savedClassName;
}

void Compiler::compileFieldAssign(FieldAssignNode *node) {
    std::string className;
    bool isStaticAccess = false;

    if (classIndex.contains(node->objectName)) {
        className = node->objectName;
        isStaticAccess = true;
    } else if (node->objectName == "this") {
        className = currentClassName;
    } else {
        auto it = varClassMap.find(node->objectName);
        if (it == varClassMap.end()) throw CompileError(node->location, "Unknown class for '" + node->objectName + "'");
        className = it->second;
    }

    if (isStaticAccess) {
        std::string fullName = className + "." + node->fieldName;
        auto gIt = globalIndex.find(fullName);
        if (gIt == globalIndex.end()) throw CompileError(node->location, "Unknown static field '" + fullName + "'");

        uint8_t save = nextReg;
        ExprResult val = compileExpression(node->expression.get());
        chunk.emit(encodeABx(OpCode::OP_SGLOB, val.reg, gIt->second));
        freeRegsTo(save);
        return;
    }

    auto &meta = classes[classIndex[className]];
    auto fieldIt = meta.fieldIndex.find(node->fieldName);
    if (fieldIt == meta.fieldIndex.end())
        throw CompileError(node->location, "Unknown field '" + node->fieldName + "' on class '" + className + "'");

    if (meta.fields[fieldIt->second].access == AccessModifier::Private && currentClassName != className)
        throw CompileError(node->location, "Cannot access private field '" + node->fieldName + "'");
    if (!meta.fields[fieldIt->second].isMutable && node->objectName != "this")
        throw CompileError(node->location, "Cannot assign to immutable field '" + node->fieldName + "'");

    uint8_t save = nextReg;
    uint8_t objReg;
    int localIdx = resolveLocal(node->objectName);
    if (localIdx != -1) {
        objReg = locals[localIdx].reg;
    } else {
        auto gIt = globalIndex.find(node->objectName);
        if (gIt == globalIndex.end()) throw CompileError(node->location, "Undefined variable '" + node->objectName + "'");
        objReg = allocReg();
        chunk.emit(encodeABx(OpCode::OP_GGLOB, objReg, gIt->second));
    }

    ExprResult val = compileExpression(node->expression.get());
    chunk.emit(encodeABC(OpCode::OP_SET_FIELD, val.reg, objReg, static_cast<uint8_t>(fieldIt->second)));
    freeRegsTo(save);
}

void Compiler::compileExprStmt(ExpressionStmtNode *node) {
    uint8_t save = nextReg;
    compileExpression(node->expression.get());
    freeRegsTo(save);
}

ExprResult Compiler::compileFieldAccess(FieldAccessNode *node, uint8_t dst) {
    TypeAnnotation receiverType = TypeKind::None;
    bool isStaticAccess = false;

    if (classIndex.contains(node->objectName)) {
        receiverType = TypeAnnotation(TypeKind::Object, node->objectName);
        isStaticAccess = true;
    } else if (node->objectName == "this") {
        int idx = resolveLocal("this");
        if (idx != -1) receiverType = locals[idx].typeAnnot;
        if (receiverType.isNone()) receiverType = TypeAnnotation(TypeKind::Object, currentClassName);
    } else {
        int localIdx = resolveLocal(node->objectName);
        if (localIdx != -1) {
            receiverType = locals[localIdx].typeAnnot;
        } else {
            auto it = varClassMap.find(node->objectName);
            if (it != varClassMap.end()) {
                receiverType = TypeAnnotation(TypeKind::Object, it->second);
            } else {
                std::string fullName = node->objectName + "." + node->fieldName;
                auto gIt = globalIndex.find(fullName);
                if (gIt != globalIndex.end()) {
                    chunk.emit(encodeABx(OpCode::OP_GGLOB, dst, gIt->second));
                    return {dst, TypeKind::Int};
                }
                throw CompileError(node->location, "Unknown class or variable for '" + node->objectName + "'");
            }
        }
    }

    if (isStaticAccess) {
        std::string fullName = receiverType.name + "." + node->fieldName;
        auto gIt = globalIndex.find(fullName);
        if (gIt == globalIndex.end()) throw CompileError(node->location, "Unknown static field '" + fullName + "'");
        chunk.emit(encodeABx(OpCode::OP_GGLOB, dst, gIt->second));
        return {dst, TypeKind::None};
    }

    if (receiverType.kind != TypeKind::Object) 
        throw CompileError(node->location, "Cannot access field '" + node->fieldName + "' on non-object type");

    auto &meta = classes[classIndex[receiverType.name]];
    auto fieldIt = meta.fieldIndex.find(node->fieldName);
    if (fieldIt == meta.fieldIndex.end())
        throw CompileError(node->location, "Unknown field '" + node->fieldName + "' on class '" + receiverType.name + "'");

    auto &fieldMeta = meta.fields[fieldIt->second];
    if (fieldMeta.access == AccessModifier::Private && currentClassName != receiverType.name)
        throw CompileError(node->location, "Cannot access private field '" + node->fieldName + "'");

    uint8_t save = nextReg;
    uint8_t objReg;
    int localIdx = resolveLocal(node->objectName);
    if (localIdx != -1) {
        objReg = locals[localIdx].reg;
    } else if (node->objectName == "this") {
        objReg = locals[resolveLocal("this")].reg;
    } else {
        auto gIt = globalIndex.find(node->objectName);
        if (gIt == globalIndex.end()) throw CompileError(node->location, "Undefined variable '" + node->objectName + "'");
        objReg = allocReg();
        chunk.emit(encodeABx(OpCode::OP_GGLOB, objReg, gIt->second));
    }

    // Resolve generic type
    auto savedMap = genericTypeMap;
    genericTypeMap.clear();
    for (size_t i = 0; i < meta.genericParams.size() && i < receiverType.params.size(); ++i) {
        genericTypeMap[meta.genericParams[i]] = receiverType.params[i];
    }
    TypeAnnotation resolvedType = resolveType(fieldMeta.type);
    genericTypeMap = savedMap;

    OpCode op = OpCode::OP_GET_FIELD;
    if (resolvedType.kind == TypeKind::Int) op = OpCode::OP_GET_FIELD_INT;
    else if (resolvedType.kind == TypeKind::Double) op = OpCode::OP_GET_FIELD_DBL;

    chunk.emit(encodeABC(op, dst, objReg, static_cast<uint8_t>(fieldIt->second)));
    freeRegsTo(save);
    return {dst, resolvedType};
}

ExprResult Compiler::compileMethodCall(MethodCallNode *node, uint8_t dst) {
    // Check if it's a static method call
    std::string qualName = node->objectName + "." + node->methodName;

    auto sIt = functionIndex.find(qualName);
    if (sIt != functionIndex.end()) {
        uint8_t base = nextReg;
        for (auto &arg: node->args) {
            allocReg();
            compileExpression(arg.get(), nextReg - 1);
        }
        chunk.emit(encodeABC(OpCode::OP_CALL, base, static_cast<uint8_t>(sIt->second & 0xFF),
                             static_cast<uint8_t>(node->args.size())));
        if (dst != base) {
            chunk.emit(encodeABC(OpCode::OP_MOVE, dst, base, 0));
            freeRegsTo(base);
        } else {
            freeRegsTo(base + 1);
        }
        return {dst, functions[sIt->second].returnType};
    }

    if (node->objectName == "super") {
        if (currentClassName.empty()) throw CompileError(node->location, "super.method() can only be used inside a class");
        auto clsIt = classIndex.find(currentClassName);
        if (clsIt == classIndex.end()) throw CompileError(node->location, "Internal error");
        auto &meta = classes[clsIt->second];
        if (meta.parentClassId < 0) throw CompileError(node->location,
            "super used in class '" + currentClassName + "' which has no parent");

        auto &pMeta = classes[meta.parentClassId];
        auto pIt = pMeta.methodIndex.find(node->methodName);
        if (pIt == pMeta.methodIndex.end()) throw CompileError(node->location,
            "Unknown method '" + node->methodName + "' on parent class");

        uint16_t funcIdx = pIt->second;
        uint8_t base = nextReg;
        uint8_t objReg = allocReg();

        int thisLocal = resolveLocal("this");
        if (thisLocal == -1) throw CompileError(node->location, "super.method() must be called from a method with 'this'");
        if (locals[thisLocal].reg != objReg) chunk.emit(encodeABC(OpCode::OP_MOVE, objReg, locals[thisLocal].reg, 0));

        for (auto &arg: node->args) {
            allocReg();
            compileExpression(arg.get(), nextReg - 1);
        }

        uint8_t totalArgs = static_cast<uint8_t>(node->args.size() + 1);
        chunk.emit(encodeABC(OpCode::OP_CALL, base, static_cast<uint8_t>(funcIdx & 0xFF), totalArgs));
        if (dst != base) {
            chunk.emit(encodeABC(OpCode::OP_MOVE, dst, base, 0));
            freeRegsTo(base);
        } else {
            freeRegsTo(base + 1);
        }
        return {dst, TypeKind::None};
    }

    TypeAnnotation receiverType = TypeKind::None;
    if (node->objectName == "this") {
        int idx = resolveLocal("this");
        if (idx != -1) receiverType = locals[idx].typeAnnot;
        if (receiverType.isNone()) receiverType = TypeAnnotation(TypeKind::Object, currentClassName);
    } else {
        int localIdx = resolveLocal(node->objectName);
        if (localIdx != -1) {
            receiverType = locals[localIdx].typeAnnot;
        } else {
            auto it = varClassMap.find(node->objectName);
            if (it != varClassMap.end()) {
                receiverType = TypeAnnotation(TypeKind::Object, it->second);
            }
        }
    }

    if (receiverType.kind == TypeKind::Object && classIndex.contains(receiverType.name)) {
        auto &meta = classes[classIndex[receiverType.name]];
        auto mIt = meta.methodIndex.find(node->methodName);
        if (mIt != meta.methodIndex.end() && mIt->second != 0xFFFF) {
            uint16_t funcIdx = mIt->second;
            auto &func = functions[funcIdx];
            
            // std::cout << "[DEBUG] compileMethodCall: " << node->objectName << "." << node->methodName << std::endl;
            // std::cout << "[DEBUG] receiverType: " << typeAnnotationName(receiverType) << " params size: " << receiverType.params.size() << std::endl;
            // std::cout << "[DEBUG] meta.genericParams size: " << meta.genericParams.size() << std::endl;
            for (auto& gp : meta.genericParams) {
                // std::cout << "[DEBUG]   meta.genericParam: " << gp << std::endl;
            }

            // Setup generic context
            auto savedMap = genericTypeMap;
            genericTypeMap.clear();
            for (size_t i = 0; i < meta.genericParams.size() && i < receiverType.params.size(); ++i) {
                genericTypeMap[meta.genericParams[i]] = receiverType.params[i];
            }

            uint8_t base = nextReg;
            uint8_t objReg = allocReg();
            
            // Load receiver
            if (node->objectName == "this") {
                chunk.emit(encodeABC(OpCode::OP_MOVE, objReg, locals[resolveLocal("this")].reg, 0));
            } else {
                int localIdx = resolveLocal(node->objectName);
                if (localIdx != -1) {
                    chunk.emit(encodeABC(OpCode::OP_MOVE, objReg, locals[localIdx].reg, 0));
                } else {
                    chunk.emit(encodeABx(OpCode::OP_GGLOB, objReg, globalIndex.at(node->objectName)));
                }
            }

            // Compile arguments and check types
            if (node->args.size() != (func.arity - 1)) 
                throw CompileError(node->location, "Method '" + node->methodName + "' expects " + std::to_string(func.arity - 1) + " args");

            for (size_t i = 0; i < node->args.size(); ++i) {
                uint8_t r = allocReg();
                ExprResult argRes = compileExpression(node->args[i].get(), r);
                TypeAnnotation expected = resolveType(func.paramTypes[i + 1]); // +1 skip 'this'
                if (!isCompatible(argRes.type, expected))
                    throw CompileError(node->args[i]->location, "Type mismatch: expected " + typeAnnotationName(expected) + " but got " + typeAnnotationName(argRes.type));
            }

            TypeAnnotation retType = resolveType(func.returnType);
            genericTypeMap = savedMap;

            uint8_t totalArgs = static_cast<uint8_t>(node->args.size() + 1);
            uint16_t nameId = chunk.addConstant(Value(internString(node->methodName)));
            uint16_t cacheIdx = static_cast<uint16_t>(chunk.methodCaches.size());
            MethodCacheEntry mce;
            mce.methodNameIdx = static_cast<uint8_t>(nameId);
            mce.argCount = totalArgs;
            chunk.methodCaches.push_back(mce);
            chunk.emit(encodeABC(OpCode::OP_INVOKE_MONO, base, 
                static_cast<uint8_t>((cacheIdx >> 8) & 0xFF),
                static_cast<uint8_t>(cacheIdx & 0xFF)));

            if (dst != base) {
                chunk.emit(encodeABC(OpCode::OP_MOVE, dst, base, 0));
                freeRegsTo(base);
            } else {
                freeRegsTo(base + 1);
            }
            return {dst, retType};
        }
    }

    // Default dynamic dispatch (no compile-time type info)
    uint8_t base = nextReg;
    const uint8_t objReg = allocReg();

    if (node->objectName == "this") {
        int thisLocal = resolveLocal("this");
        if (thisLocal == -1) throw CompileError(node->location, "this.method() must be called from a method");
        if (locals[thisLocal].reg != objReg) chunk.emit(encodeABC(OpCode::OP_MOVE, objReg, locals[thisLocal].reg, 0));
    } else {
        int localIdx = resolveLocal(node->objectName);
        if (localIdx != -1) {
            if (locals[localIdx].reg != objReg) chunk.emit(encodeABC(OpCode::OP_MOVE, objReg, locals[localIdx].reg, 0));
        } else {
            auto gIt = globalIndex.find(node->objectName);
            if (gIt == globalIndex.end()) throw CompileError(node->location, "Undefined variable '" + node->objectName + "'");
            chunk.emit(encodeABx(OpCode::OP_GGLOB, objReg, gIt->second));
        }
    }

    for (auto &arg: node->args) {
        allocReg();
        compileExpression(arg.get(), nextReg - 1);
    }

    uint8_t totalArgs = static_cast<uint8_t>(node->args.size() + 1);
    uint16_t nameId = chunk.addConstant(Value(internString(node->methodName)));
    uint16_t cacheIdx = static_cast<uint16_t>(chunk.methodCaches.size());
    MethodCacheEntry mce;
    mce.methodNameIdx = static_cast<uint8_t>(nameId);
    mce.argCount = totalArgs;
    chunk.methodCaches.push_back(mce);
    chunk.emit(encodeABC(OpCode::OP_INVOKE_MONO, base,
        static_cast<uint8_t>((cacheIdx >> 8) & 0xFF),
        static_cast<uint8_t>(cacheIdx & 0xFF)));

    if (dst != base) {
        chunk.emit(encodeABC(OpCode::OP_MOVE, dst, base, 0));
        freeRegsTo(base);
    } else {
        freeRegsTo(base + 1);
    }
    return {dst, TypeKind::None};
}

ExprResult Compiler::compileFunctionCall(FunctionCallNode *node, uint8_t dst) {
    if (node->name == "print") {
        if (node->args.size() != 1) throw CompileError(node->location, "print() expects 1 arg");
        uint8_t save = nextReg;
        ExprResult res = compileExpression(node->args[0].get());
        chunk.emit(encodeABC(OpCode::OP_LOG, res.reg, 0, 0));
        freeRegsTo(save);
        chunk.emit(encodeABC(OpCode::OP_LOADNULL, dst, 0, 0));
        return {dst, TypeKind::None};
    }
    if (node->name == "wait") {
        if (node->args.size() != 1) throw CompileError(node->location, "wait() expects 1 arg");
        uint8_t save = nextReg;
        ExprResult res = compileExpression(node->args[0].get());
        chunk.emit(encodeABC(OpCode::OP_WAIT, res.reg, 0, 0));
        freeRegsTo(save);
        chunk.emit(encodeABC(OpCode::OP_LOADNULL, dst, 0, 0));
        return {dst, TypeKind::None};
    }

    if (node->name == "array") {
        if (node->args.size() != 1) throw CompileError(node->location, "array() expects 1 arg (size)");
        uint8_t save = nextReg;
        ExprResult sizeRes = compileExpression(node->args[0].get());
        chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, dst, sizeRes.reg, 0));
        freeRegsTo(save);
        return {dst, TypeKind::None};
    }
    if (node->name == "len") {
        if (node->args.size() != 1) throw CompileError(node->location, "len() expects 1 arg");
        uint8_t save = nextReg;
        ExprResult collRes = compileExpression(node->args[0].get());
        chunk.emit(encodeABC(OpCode::OP_COLL_LEN, dst, collRes.reg, 0));
        freeRegsTo(save);
        return {dst, TypeKind::Int};
    }

    auto nativeIt = nativeFunctionIndex.find(node->name);
    if (nativeIt != nativeFunctionIndex.end()) {
        uint8_t base = nextReg;
        for (auto &arg: node->args) {
            allocReg();
            compileExpression(arg.get(), nextReg - 1);
        }
        uint16_t nativeIdx = nativeIt->second;
        chunk.emit(encodeABC(OpCode::OP_CALL_NATIVE, base, static_cast<uint8_t>(nativeIdx),
                             static_cast<uint8_t>(node->args.size())));
        if (dst != base) {
            chunk.emit(encodeABC(OpCode::OP_MOVE, dst, base, 0));
            freeRegsTo(base);
        } else {
            freeRegsTo(base + 1);
        }
        return {dst, TypeKind::None};
    }

    auto it = functionIndex.find(node->name);
    if (it != functionIndex.end()) {
        uint8_t base = nextReg;
        for (auto &arg: node->args) {
            allocReg();
            compileExpression(arg.get(), nextReg - 1);
        }
        chunk.emit(encodeABC(OpCode::OP_CALL, base, static_cast<uint8_t>(it->second & 0xFF),
                             static_cast<uint8_t>(node->args.size())));
        if (dst != base) {
            chunk.emit(encodeABC(OpCode::OP_MOVE, dst, base, 0));
            freeRegsTo(base);
        } else {
            freeRegsTo(base + 1);
        }
        return {dst, functions[it->second].returnType};
    }

    auto classIt = classIndex.find(node->name);
    if (classIt != classIndex.end()) {
        uint16_t clsId = classIt->second;
        auto &meta = classes[clsId];
        if (meta.isAbstract) throw CompileError(node->location, "Cannot instantiate abstract class '" + meta.name + "'");

        chunk.emit(encodeABx(OpCode::OP_NEW_OBJ, dst, clsId));
        auto methIt = meta.methodIndex.find(node->name);
        if (methIt != meta.methodIndex.end()) {
            uint8_t save = nextReg;
            uint8_t base = nextReg;
            uint16_t funcIdx = methIt->second;

            // First argument is 'this' (R[base])
            uint8_t thisReg = allocReg();
            chunk.emit(encodeABC(OpCode::OP_MOVE, thisReg, dst, 0));

            for (auto &arg: node->args) {
                uint8_t r = allocReg();
                compileExpression(arg.get(), r);
            }
            uint8_t totalArgs = static_cast<uint8_t>(node->args.size() + 1);
            chunk.emit(encodeABC(OpCode::OP_CALL, base, static_cast<uint8_t>(funcIdx & 0xFF), totalArgs));
            freeRegsTo(save);
        }
        
        // std::cout << "[DEBUG] compileFunctionCall class inst: " << meta.name << " genericArgs size: " << node->genericArgs.size() << std::endl;
        TypeAnnotation instantiatedType(TypeKind::Object, meta.name);
        instantiatedType.params = node->genericArgs; // Capture generic arguments
        return {dst, instantiatedType};
    }
    throw CompileError(node->location, "Undefined function: " + node->name);
}

ExprResult Compiler::compileNumber(NumberNode *node, uint8_t dst) {
    int val = node->value;
    if (val >= -32767 && val <= 32767) {
        chunk.emit(encodeABx(OpCode::OP_LOADINT, dst, static_cast<uint16_t>(val + 32767)));
    } else {
        uint16_t ki = chunk.addConstant(Value(val));
        chunk.emit(encodeABx(OpCode::OP_LOADK, dst, ki));
    }
    return {dst, TypeKind::Int};
}

ExprResult Compiler::compileDouble(DoubleNode *node, uint8_t dst) {
    double val = node->value;
    uint16_t f16 = doubleToFloat16(val);
    if (float16ToDouble(f16) == val) {
        chunk.emit(encodeABx(OpCode::OP_LOADDBL, dst, f16));
    } else {
        uint16_t ki = chunk.addConstant(Value(val));
        chunk.emit(encodeABx(OpCode::OP_LOADK, dst, ki));
    }
    return {dst, TypeKind::Double};
}

ExprResult Compiler::compileBoolean(BooleanNode *node, uint8_t dst) {
    chunk.emit(encodeABC(OpCode::OP_LOADBOOL, dst, node->value ? 1 : 0, 0));
    return {dst, TypeKind::Bool};
}

ExprResult Compiler::compileString(StringNode *node, uint8_t dst) {
    uint16_t ki = chunk.addConstant(Value(internString(node->value)));
    chunk.emit(encodeABx(OpCode::OP_LOADK, dst, ki));
    return {dst, TypeKind::String};
}

ExprResult Compiler::compileVariable(VariableNode *node, uint8_t dst) {
    if (node->nameOfVariable == "this") {
        int idx = resolveLocal("this");
        if (idx == -1) throw CompileError(node->location, "'this' used outside method");
        uint8_t srcReg = locals[idx].reg;
        if (srcReg != dst) chunk.emit(encodeABC(OpCode::OP_MOVE, dst, srcReg, 0));
        return {dst, TypeAnnotation(TypeKind::Object, currentClassName)};
    }

    int arg = resolveLocal(node->nameOfVariable);
    if (arg != -1) {
        uint8_t srcReg = locals[arg].reg;
        if (srcReg != dst) chunk.emit(encodeABC(OpCode::OP_MOVE, dst, srcReg, 0));
        return {dst, locals[arg].typeAnnot};
    }
    auto it = globalIndex.find(node->nameOfVariable);
    if (it == globalIndex.end()) throw CompileError(node->location, "Undefined variable: " + node->nameOfVariable);
    chunk.emit(encodeABx(OpCode::OP_GGLOB, dst, it->second));
    {
        auto tIt = globalTypes.find(it->second);
        if (tIt != globalTypes.end()) return {dst, tIt->second};
    }
    return {dst, TypeKind::None};
}

ExprResult Compiler::compileUnaryOp(UnaryOperationNode *node, uint8_t dst) {
    uint8_t save = nextReg;
    if (node->operation == "++" || node->operation == "--") {
        if (node->operand->getExprType() != ExprType::Variable) {
            throw CompileError(node->location, "Operator '" + node->operation + "' requires a variable");
        }
        auto* varNode = static_cast<VariableNode*>(node->operand.get());
        std::string op = (node->operation == "++") ? "+" : "-";
        
        // Desugar to assignment: var = var + 1
        auto binOp = std::make_unique<BinaryOperationNode>(
            std::make_unique<VariableNode>(varNode->nameOfVariable),
            std::make_unique<NumberNode>(1),
            op
        );
        auto assignment = std::make_unique<AssignmentNode>(varNode->nameOfVariable, std::move(binOp));
        compileAssignment(assignment.get());
        
        // Result of the expression is the new value
        return compileVariable(varNode, dst);
    }

    ExprResult res = compileExpression(node->operand.get());
    if (node->operation == "!") {
        chunk.emit(encodeABC(OpCode::OP_NOT, dst, res.reg, 0));
        freeRegsTo(save);
        return {dst, TypeKind::Bool};
    } else if (node->operation == "-") {
        chunk.emit(encodeABC(OpCode::OP_NEG, dst, res.reg, 0));
        freeRegsTo(save);
        return {dst, res.type};
    }
    throw CompileError(node->location, "Unknown unary operator");
}

ExprResult Compiler::compileBinaryOp(BinaryOperationNode *node, uint8_t dst) {
    auto leftExpr = node->leftNode.get();
    auto rightExpr = node->rightNode.get();

    if ((leftExpr->getExprType() == ExprType::Number || leftExpr->getExprType() == ExprType::Double) && 
        (rightExpr->getExprType() == ExprType::Number || rightExpr->getExprType() == ExprType::Double)) {
        double l = (leftExpr->getExprType() == ExprType::Number) ? (double)static_cast<NumberNode *>(leftExpr)->value : static_cast<DoubleNode *>(leftExpr)->value;
        double r = (rightExpr->getExprType() == ExprType::Number) ? (double)static_cast<NumberNode *>(rightExpr)->value : static_cast<DoubleNode *>(rightExpr)->value;
        
        if (node->operation == "==") return compileBoolean(new BooleanNode(l == r), dst);
        if (node->operation == "!=") return compileBoolean(new BooleanNode(l != r), dst);
        if (node->operation == "<") return compileBoolean(new BooleanNode(l < r), dst);
        if (node->operation == ">") return compileBoolean(new BooleanNode(l > r), dst);
        if (node->operation == "<=") return compileBoolean(new BooleanNode(l <= r), dst);
        if (node->operation == ">=") return compileBoolean(new BooleanNode(l >= r), dst);

        double res = 0;
        bool foldable = true;
        if (node->operation == "+") res = l + r;
        else if (node->operation == "-") res = l - r;
        else if (node->operation == "*") res = l * r;
        else if (node->operation == "/") { if (r != 0) res = l / r; else foldable = false; }
        else foldable = false;

        if (foldable) {
            if (leftExpr->getExprType() == ExprType::Number && rightExpr->getExprType() == ExprType::Number && res == (int)res) {
                NumberNode folded((int)res); return compileNumber(&folded, dst);
            }
            DoubleNode folded(res); return compileDouble(&folded, dst);
        }
    }

    if (leftExpr->getExprType() == ExprType::String && rightExpr->getExprType() == ExprType::String && node->operation == "+") {
        std::string res = static_cast<StringNode *>(leftExpr)->value + static_cast<StringNode *>(rightExpr)->value;
        StringNode folded(res); return compileString(&folded, dst);
    }

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

    uint8_t save = nextReg;
    ExprResult left = compileExpression(node->leftNode.get());
    const auto &op = node->operation;
    
    // Check for ADDI/SUBI before compiling right operand
    if (left.type == TypeKind::Int && rightExpr->getExprType() == ExprType::Number) {
        int val = static_cast<NumberNode *>(rightExpr)->value;
        if (val >= -128 && val <= 127) {
            if (op == "+") {
                chunk.emit(encodeABC(OpCode::OP_ADDI, dst, left.reg, static_cast<uint8_t>(val)));
                freeRegsTo(save);
                return {dst, TypeKind::Int};
            } else if (op == "-") {
                chunk.emit(encodeABC(OpCode::OP_SUBI, dst, left.reg, static_cast<uint8_t>(val)));
                freeRegsTo(save);
                return {dst, TypeKind::Int};
            }
        }
    }

    ExprResult right = compileExpression(node->rightNode.get());
    OpCode specialized = OpCode::OP_COUNT;
    TypeAnnotation resultType = TypeKind::None;

    if (left.type == TypeKind::Int && right.type == TypeKind::Int) {
        resultType = TypeKind::Int;
        if (op == "+") specialized = OpCode::OP_ADD_INT;
        else if (op == "-") specialized = OpCode::OP_SUB_INT;
        else if (op == "*") specialized = OpCode::OP_MUL_INT;
        else if (op == "/") specialized = OpCode::OP_DIV_INT;
        else if (op == "<") {
            specialized = OpCode::OP_LT_INT;
            resultType = TypeKind::Bool;
        } else if (op == ">") {
            specialized = OpCode::OP_GT_INT;
            resultType = TypeKind::Bool;
        } else if (op == "<=") {
            specialized = OpCode::OP_LE_INT;
            resultType = TypeKind::Bool;
        } else if (op == ">=") {
            specialized = OpCode::OP_GE_INT;
            resultType = TypeKind::Bool;
        } else if (op == "==") {
            specialized = OpCode::OP_EQ_INT;
            resultType = TypeKind::Bool;
        }
    } else if ((left.type == TypeKind::Double || left.type == TypeKind::Int) &&
               (right.type == TypeKind::Double || right.type == TypeKind::Int)) {
        resultType = TypeKind::Double;
        if (op == "+") specialized = OpCode::OP_ADD_DOUBLE;
        else if (op == "-") specialized = OpCode::OP_SUB_DOUBLE;
        else if (op == "*") specialized = OpCode::OP_MUL_DOUBLE;
        else if (op == "/") specialized = OpCode::OP_DIV_DOUBLE;
        else if (op == "<") {
            specialized = OpCode::OP_LT_DBL;
            resultType = TypeKind::Bool;
        } else if (op == ">") {
            specialized = OpCode::OP_GT_DBL;
            resultType = TypeKind::Bool;
        } else if (op == "<=") {
            specialized = OpCode::OP_LE_DBL;
            resultType = TypeKind::Bool;
        } else if (op == ">=") {
            specialized = OpCode::OP_GE_DBL;
            resultType = TypeKind::Bool;
        } else if (op == "==") {
            specialized = OpCode::OP_EQ_DBL;
            resultType = TypeKind::Bool;
        }
    }

    if (specialized != OpCode::OP_COUNT) { chunk.emit(encodeABC(specialized, dst, left.reg, right.reg)); } else {
        auto it = opTable.find(node->operation);
        if (it == opTable.end()) throw CompileError(node->location, "Unknown binary operator");
        chunk.emit(encodeABC(it->second, dst, left.reg, right.reg));
        if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") resultType = TypeKind::Bool;
    }
    freeRegsTo(save);
    return {dst, resultType};
}

void Compiler::beginScope() { scopeDepth++; }

void Compiler::endScope() {
    scopeDepth--;
    while (!locals.empty() && locals.back().depth > scopeDepth) {
        locals.pop_back();
        freeReg();
    }
}

void Compiler::addLocal(const std::string &name, bool isMutable, TypeAnnotation typeAnnot, const SourceLocation& loc) {
    for (auto &local: std::ranges::reverse_view(locals)) {
        if (local.depth < scopeDepth) break;
        if (local.name == name) throw CompileError(loc, "Variable redeclared: " + name);
    }
    uint8_t r = allocReg();
    locals.push_back({name, scopeDepth, isMutable, r, typeAnnot});
}

int Compiler::resolveLocal(const std::string &name) {
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) { if (locals[i].name == name) return i; }
    return -1;
}

ExprResult Compiler::compileIndexAccess(IndexAccessNode *node, uint8_t dst) {
    uint8_t save = nextReg;
    ExprResult coll = compileExpression(node->object.get());
    ExprResult idx = compileExpression(node->index.get());
    OpCode op = OpCode::OP_IDX_GET;
    TypeAnnotation elemType = TypeKind::None;
    if (coll.type.kind == TypeKind::DoubleArray) {
        op = OpCode::OP_IDX_GET_DBL;
        elemType = TypeKind::Double;
    } else if (coll.type.kind == TypeKind::IntArray) {
        op = OpCode::OP_IDX_GET_INT;
        elemType = TypeKind::Int;
    } else if (coll.type.kind == TypeKind::StringArray) {
        op = OpCode::OP_IDX_GET;
        elemType = TypeKind::String;
    } else if (coll.type.kind == TypeKind::BoolArray) {
        op = OpCode::OP_IDX_GET;
        elemType = TypeKind::Bool;
    } else if (coll.type.kind == TypeKind::Object || coll.type.kind == TypeKind::GenericParam) {
        op = OpCode::OP_IDX_GET;
        elemType = coll.type;
    }
    chunk.emit(encodeABC(op, dst, coll.reg, idx.reg));
    freeRegsTo(save);
    return {dst, elemType};
}

void Compiler::compileIndexAssign(IndexAssignNode *node) {
    uint8_t save = nextReg;
    ExprResult coll = compileExpression(node->collection.get());
    ExprResult idx = compileExpression(node->index.get());
    ExprResult val = compileExpression(node->value.get());
    
    OpCode op = OpCode::OP_IDX_SET;
    if (coll.type == TypeKind::DoubleArray) op = OpCode::OP_IDX_SET_DBL;
    else if (coll.type == TypeKind::IntArray) op = OpCode::OP_IDX_SET_INT;
    
    chunk.emit(encodeABC(op, val.reg, coll.reg, idx.reg));
    freeRegsTo(save);
}

ExprResult Compiler::compileArrayAlloc(ArrayAllocNode *node, uint8_t dst) {
    uint8_t save = nextReg;
    uint8_t elemTypeTag = 0;
    if (node->elementType.kind == TypeKind::Int || node->elementType.kind == TypeKind::IntArray) elemTypeTag = 1;
    else if (node->elementType.kind == TypeKind::Double || node->elementType.kind == TypeKind::DoubleArray) elemTypeTag = 2;
    else elemTypeTag = 3;

    if (node->sizes.size() == 1) {
        ExprResult sizeRes = compileExpression(node->sizes[0].get());
        chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, dst, sizeRes.reg, elemTypeTag));
    } else {
        compileRecursiveArrayAlloc(node, dst, 0, elemTypeTag);
    }

    freeRegsTo(save);
    TypeAnnotation resType = TypeKind::Object;
    if (node->sizes.size() == 1) {
        if (node->elementType.kind == TypeKind::Int) resType = TypeKind::IntArray;
        else if (node->elementType.kind == TypeKind::Double) resType = TypeKind::DoubleArray;
        else if (node->elementType.kind == TypeKind::String) resType = TypeKind::StringArray;
        else if (node->elementType.kind == TypeKind::Bool) resType = TypeKind::BoolArray;
    }
    // std::cout << "[DEBUG] compileArrayAlloc sizes: " << node->sizes.size() << " elementType.kind: " << (int)node->elementType.kind << " resType.kind: " << (int)resType.kind << std::endl;
    return {dst, resType};
}

void Compiler::compileRecursiveArrayAlloc(ArrayAllocNode *node, uint8_t dst, size_t dimIdx, uint8_t finalElemTypeTag) {
    uint8_t save = nextReg;
    ExprResult sizeRes = compileExpression(node->sizes[dimIdx].get());
    
    // Allocate current dimension. 
    // If it's not the last dimension, it must be VALUE (3) to hold sub-arrays.
    uint8_t typeTag = (dimIdx == node->sizes.size() - 1) ? finalElemTypeTag : 3;
    chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, dst, sizeRes.reg, typeTag));

    if (dimIdx < node->sizes.size() - 1) {
        // Create loop to initialize sub-arrays
        uint8_t iterReg = allocReg();
        chunk.emit(encodeABx(OpCode::OP_LOADINT, iterReg, 32767)); // iter = 0 (signed 32767 is 0)
        
        size_t loopStart = chunk.code.size();
        uint8_t condReg = allocReg();
        chunk.emit(encodeABC(OpCode::OP_LT_INT, condReg, iterReg, sizeRes.reg));
        size_t jmpIdx = chunk.emitJump(OpCode::OP_JMPF, condReg);
        
        uint8_t subArrayReg = allocReg();
        compileRecursiveArrayAlloc(node, subArrayReg, dimIdx + 1, finalElemTypeTag);
        
        chunk.emit(encodeABC(OpCode::OP_IDX_SET, subArrayReg, dst, iterReg));
        
        chunk.emit(encodeABC(OpCode::OP_INC, iterReg, 0, 0));
        chunk.emitLoop(loopStart);
        
        chunk.patchJump(jmpIdx);
    }
    
    freeRegsTo(save);
}

ExprResult Compiler::compileArrayLiteral(ArrayLiteralNode *node, uint8_t dst) {
    uint8_t save = nextReg;
    int size = static_cast<int>(node->elements.size());
    uint8_t sizeReg = allocReg();
    if (size >= -32767 && size <= 32767) chunk.emit(encodeABx(OpCode::OP_LOADINT, sizeReg,
                                                              static_cast<uint16_t>(size + 32767)));
    else chunk.emit(encodeABx(OpCode::OP_LOADK, sizeReg, chunk.addConstant(Value(size))));
    chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, dst, sizeReg, 0));
    for (int i = 0; i < size; ++i) {
        uint8_t valSave = nextReg;
        ExprResult valRes = compileExpression(node->elements[i].get());
        uint8_t idxReg = allocReg();
        if (i >= -32767 && i <= 32767) chunk.emit(encodeABx(OpCode::OP_LOADINT, idxReg,
                                                            static_cast<uint16_t>(i + 32767)));
        else chunk.emit(encodeABx(OpCode::OP_LOADK, idxReg, chunk.addConstant(Value(i))));
        chunk.emit(encodeABC(OpCode::OP_IDX_SET, valRes.reg, dst, idxReg));
        freeRegsTo(valSave);
    }
    freeRegsTo(save);
    return {dst, TypeKind::None};
}

void Compiler::compileTryCatch(TryCatchNode *node) {
    uint8_t catchVarReg = allocReg();
    uint32_t pushIndex = static_cast<uint32_t>(chunk.code.size());
    chunk.emit(encodeABx(OpCode::OP_PUSH_HANDLER, catchVarReg, 0));
    for (auto &stmt: node->tryBody) compileNode(stmt.get());
    chunk.emit(encodeABC(OpCode::OP_POP_HANDLER, 0, 0, 0));
    uint32_t jmpOverCatch = static_cast<uint32_t>(chunk.code.size());
    chunk.emit(encodesBx(OpCode::OP_JMP, 0));
    uint32_t catchStart = static_cast<uint32_t>(chunk.code.size());
    chunk.code[pushIndex] = encodeABx(OpCode::OP_PUSH_HANDLER, catchVarReg,
                                      static_cast<uint16_t>(catchStart - (pushIndex + 1)));
    beginScope();
    addLocal(node->catchVar, true, TypeKind::String, node->location);
    locals.back().reg = catchVarReg;
    for (auto &stmt: node->catchBody) compileNode(stmt.get());
    endScope();
    nextReg = catchVarReg;
    chunk.code[jmpOverCatch] = encodesBx(OpCode::OP_JMP, static_cast<int16_t>(chunk.code.size() - (jmpOverCatch + 1)));
}

ExprResult Compiler::compileStringInterp(StringInterpNode *node, uint8_t dst) {
    if (node->parts.empty()) {
        chunk.emit(encodeABx(OpCode::OP_LOADK, dst, static_cast<uint16_t>(chunk.addConstant(Value(internString(""))))));
        return {dst, TypeKind::String};
    }
    uint8_t save = nextReg;
    ExprResult res = compileExpression(node->parts[0].get());
    if (res.reg != dst) chunk.emit(encodeABC(OpCode::OP_MOVE, dst, res.reg, 0));
    for (size_t i = 1; i < node->parts.size(); i++) {
        ExprResult r = compileExpression(node->parts[i].get());
        chunk.emit(encodeABC(OpCode::OP_ADD, dst, dst, r.reg));
    }
    freeRegsTo(save);
    return {dst, TypeKind::String};
}

void Compiler::compileThrow(ThrowNode *node) {
    uint8_t save = nextReg;
    ExprResult res = compileExpression(node->expression.get());
    chunk.emit(encodeABC(OpCode::OP_THROW, res.reg, 0, 0));
    freeRegsTo(save);
}

ExprResult Compiler::compileSwitch(SwitchNode *node, uint8_t dst) {
    uint8_t save = nextReg;
    ExprResult exprRes = compileExpression(node->expression.get());
    bool isExpr = (dst != 255);
    uint8_t resultReg = isExpr ? dst : allocReg();
    size_t sIdx = switchStack.size();
    switchStack.push_back({});
    breakableStack.push_back({BreakableType::Switch, sIdx});
    std::vector<size_t> bodyJumps;
    size_t defaultJump = 0;
    bool hasDefault = false;
    for (auto &c: node->cases) {
        if (!c->value) {
            hasDefault = true;
            continue;
        }
        uint8_t caseSave = nextReg;
        ExprResult valRes = compileExpression(c->value.get());
        uint8_t condReg = allocReg();
        chunk.emit(encodeABC(OpCode::OP_EQ, condReg, exprRes.reg, valRes.reg));
        bodyJumps.push_back(chunk.emitJump(OpCode::OP_JMPT, condReg));
        freeRegsTo(caseSave);
    }
    if (hasDefault) defaultJump = chunk.emitJump(OpCode::OP_JMP);
    size_t skipEnd = chunk.emitJump(OpCode::OP_JMP);
    int bjIdx = 0;
    for (auto &c: node->cases) {
        if (c->value) chunk.patchJump(bodyJumps[bjIdx++]);
        else chunk.patchJump(defaultJump);
        beginScope();
        if (isExpr && c->isArrow && c->body.size() == 1 && c->body[0]->getStmtType() == StmtType::ExprStmt)
            compileExpression(static_cast<ExpressionStmtNode *>(c->body[0].get())->expression.get(), resultReg);
        else {
            for (auto &stmt: c->body) compileNode(stmt.get());
            if (isExpr) {
                if (!c->body.empty() && c->body.back()->getStmtType() == StmtType::ExprStmt) compileExpression(
                    static_cast<ExpressionStmtNode *>(c->body.back().get())->expression.get(), resultReg);
                else chunk.emit(encodeABC(OpCode::OP_LOADNULL, resultReg, 0, 0));
            }
        }
        endScope();
        if (c->isArrow || isExpr) switchStack.back().breakJumps.push_back(chunk.emitJump(OpCode::OP_JMP));
    }
    chunk.patchJump(skipEnd);
    for (size_t bj: switchStack.back().breakJumps) chunk.patchJump(bj);
    switchStack.pop_back();
    breakableStack.pop_back();
    if (!isExpr) {
        freeRegsTo(save);
        return {255, TypeKind::None};
    }
    freeRegsTo(save);
    return {resultReg, TypeKind::None};
}

void Compiler::compileEnum(EnumNode *node) {
    EnumMeta meta;
    meta.name = node->name;
    for (auto &[vName, vInt]: node->values) {
        meta.values.push_back(vName);
        meta.ordinals.push_back(vInt);
        std::string fullName = node->name + "." + vName;
        uint16_t slot = globalIndex.contains(fullName)
                            ? globalIndex[fullName]
                            : (globalIndex[fullName] = globalCount++);
        uint8_t r = allocReg();
        if (vInt >= -32767 && vInt <= 32767) chunk.emit(
            encodeABx(OpCode::OP_LOADINT, r, static_cast<uint16_t>(vInt + 32767)));
        else chunk.emit(encodeABx(OpCode::OP_LOADK, r, chunk.addConstant(Value(vInt))));
        chunk.emit(encodeABC(OpCode::OP_DGLOB, r, static_cast<uint8_t>(slot >> 8), static_cast<uint8_t>(slot & 0xFF)));
        freeReg();
    }
    uint16_t eIdx = static_cast<uint16_t>(enums.size());
    enumIndex[node->name] = eIdx;
    enums.push_back(std::move(meta));
    std::string valFuncName = node->name + ".values";
    uint16_t fIdx = static_cast<uint16_t>(functions.size());
    functionIndex[valFuncName] = fIdx;

    FunctionObject f;
    f.name = valFuncName;
    f.arity = 0;
    f.returnType = TypeKind::None;
    functions.push_back(std::move(f));

    Chunk sCh = std::move(chunk);
    std::vector<Local> sLoc = std::move(locals);
    int sSD = scopeDepth;
    uint8_t sNR = nextReg;
    uint8_t sMR = maxReg;
    chunk = Chunk{};
    locals.clear();
    scopeDepth = 0;
    nextReg = 0;
    maxReg = 0;
    uint8_t sizeReg = allocReg();
    chunk.emit(encodeABx(OpCode::OP_LOADINT, sizeReg, static_cast<uint16_t>(enums[eIdx].values.size() + 32767)));
    uint8_t arrReg = allocReg();
    chunk.emit(encodeABC(OpCode::OP_NEW_ARRAY, arrReg, sizeReg, 0));
    for (size_t i = 0; i < enums[eIdx].values.size(); i++) {
        uint8_t vR = allocReg();
        int v = enums[eIdx].ordinals[i];
        if (v >= -32767 && v <= 32767) chunk.emit(encodeABx(OpCode::OP_LOADINT, vR, static_cast<uint16_t>(v + 32767)));
        else chunk.emit(encodeABx(OpCode::OP_LOADK, vR, chunk.addConstant(Value(v))));
        uint8_t iR = allocReg();
        chunk.emit(encodeABx(OpCode::OP_LOADINT, iR, static_cast<uint16_t>(i + 32767)));
        chunk.emit(encodeABC(OpCode::OP_IDX_SET, vR, arrReg, iR));
        freeRegsTo(vR);
    }
    chunk.emit(encodeABC(OpCode::OP_RET, arrReg, 0, 0));
    functions[fIdx].chunk = std::move(chunk);
    functions[fIdx].maxRegs = maxReg;
    chunk = std::move(sCh);
    locals = std::move(sLoc);
    scopeDepth = sSD;
    nextReg = sNR;
    maxReg = sMR;
}
