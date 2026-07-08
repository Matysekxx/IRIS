#include "TraceOptimizer.h"
#include <algorithm>
#include <set>
#include <unordered_map>

#include <iostream>

using namespace iris::bytecode;

void TraceOptimizer::optimize(Trace &trace) {
    size_t oldSize = trace.entries.size();
    performLICM(trace);
    size_t sizeAfterLICM = trace.entries.size();
    performDCE(trace);
    size_t sizeAfterDCE = trace.entries.size();
    performGuardElimination(trace);
    performConstantFolding(trace);
    performEscapeAnalysis(trace);
}

void TraceOptimizer::performGuardElimination(Trace &trace) {
    uint16_t knownType[256] = {0};

    auto analyzeEntry = [&](Trace::Entry& entry) {
        OpCode op = decodeOp(entry.instr);
        uint8_t A = decodeA(entry.instr);
        uint8_t B = decodeB(entry.instr);
        uint8_t C = decodeC(entry.instr);

        // 1. Check if guards can be eliminated
        switch (op) {
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL:
            case OpCode::OP_DIV:
            case OpCode::OP_LT:
            case OpCode::OP_GT:
            case OpCode::OP_LE:
            case OpCode::OP_GE:
            case OpCode::OP_BIT_AND:
            case OpCode::OP_BIT_OR:
            case OpCode::OP_BIT_XOR:
            case OpCode::OP_SHL:
            case OpCode::OP_SHR:
                if (knownType[B] == 0x7FFD) entry.skipGuardB = true;
                if (knownType[C] == 0x7FFD) entry.skipGuardC = true;
                break;
            case OpCode::OP_INC:
            case OpCode::OP_DEC:
                if (knownType[A] == 0x7FFD) entry.skipGuardA = true;
                break;
            case OpCode::OP_ADDI_W:
            case OpCode::OP_SUBI_W:
                if (knownType[B] == 0x7FFD) entry.skipGuardB = true;
                break;
            case OpCode::OP_GET_FIELD:
            case OpCode::OP_SET_FIELD:
                if (knownType[B] == 0xFFFC) entry.skipGuardB = true;
                break;
            case OpCode::OP_ADD_K:
            case OpCode::OP_SUB_K:
            case OpCode::OP_MUL_K:
            case OpCode::OP_DIV_K:
            case OpCode::OP_LT_K:
            case OpCode::OP_GT_K:
            case OpCode::OP_EQ_K:
                if (knownType[B] == 0x7FFD) entry.skipGuardB = true;
                break;
            default: break;
        }

        // 2. Update known types
        switch (op) {
            case OpCode::OP_LOADINT:
            case OpCode::OP_ADD_INT:
            case OpCode::OP_SUB_INT:
            case OpCode::OP_MUL_INT:
            case OpCode::OP_ADDI:
            case OpCode::OP_SUBI:
            case OpCode::OP_INC:
            case OpCode::OP_DEC:
            case OpCode::OP_BIT_AND:
            case OpCode::OP_BIT_OR:
            case OpCode::OP_BIT_XOR:
            case OpCode::OP_SHL:
            case OpCode::OP_SHR:
            case OpCode::OP_ADDI_W:
            case OpCode::OP_SUBI_W:
                knownType[A] = 0x7FFD;
                break;
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL:
            case OpCode::OP_DIV:
                // These might return double if guards fail, but in JIT they only continue if they are INT
                knownType[A] = 0x7FFD;
                knownType[B] = 0x7FFD;
                knownType[C] = 0x7FFD;
                break;
            case OpCode::OP_ADD_K:
            case OpCode::OP_SUB_K:
            case OpCode::OP_MUL_K:
                knownType[A] = 0x7FFD;
                knownType[B] = 0x7FFD;
                break;
            case OpCode::OP_DIV_K:
                knownType[A] = 0; // DIV can return non-int
                break;
            case OpCode::OP_EQ_K:
                knownType[A] = 0x7FFE; // bool
                knownType[B] = 0x7FFD; // B is int if guard passed
                break;
            case OpCode::OP_LT:
            case OpCode::OP_GT:
            case OpCode::OP_LE:
            case OpCode::OP_GE:
            case OpCode::OP_LT_INT:
            case OpCode::OP_GT_INT:
            case OpCode::OP_LE_INT:
            case OpCode::OP_GE_INT:
            case OpCode::OP_EQ_INT:
            case OpCode::OP_LT_K:
            case OpCode::OP_GT_K:
            case OpCode::OP_LOADBOOL:
                knownType[A] = 0x7FFE;
                break;
            case OpCode::OP_NEW_OBJ:
                knownType[A] = 0xFFFC;
                break;
            case OpCode::OP_GET_FIELD:
                knownType[B] = 0xFFFC;
                knownType[A] = entry.typeA;
                break;
            case OpCode::OP_GGLOB:
                knownType[A] = entry.typeA;
                break;
            case OpCode::OP_MOVE:
                knownType[A] = knownType[B];
                break;
            case OpCode::OP_LOADNULL:
                knownType[A] = 0x7FFF;
                break;
            case OpCode::OP_CALL:
            case OpCode::OP_INVOKE:
            case OpCode::OP_INVOKE_MONO:
            case OpCode::OP_CALL_NATIVE:
                knownType[A] = 0; // Return type unknown
                // Calls might invalidate many registers if we don't know side effects
                // For now, let's be safe.
                for (int i = 0; i < 256; i++) knownType[i] = 0;
                break;
            default:
                if (A < 256) knownType[A] = 0;
                break;
        }
    };

    for (auto& entry : trace.preamble) analyzeEntry(entry);
    for (auto& entry : trace.entries) analyzeEntry(entry);
}

void TraceOptimizer::performLICM(Trace &trace) {
    // 1. Identify all registers written to in the trace.
    std::set<uint8_t> writtenRegs;
    std::set<uint16_t> writtenGlobals;

    for (const auto& entry : trace.entries) {
        OpCode op = decodeOp(entry.instr);
        uint8_t A = decodeA(entry.instr);
        
        switch (op) {
            case OpCode::OP_SGLOB:
            case OpCode::OP_DGLOB:
                writtenGlobals.insert(decodeBx(entry.instr));
                break;
            case OpCode::OP_SET_FIELD:
            case OpCode::OP_IDX_SET:
            case OpCode::OP_IDX_SET_INT:
            case OpCode::OP_IDX_SET_DBL:
            case OpCode::OP_CALL:
            case OpCode::OP_CALL_NATIVE:
            case OpCode::OP_INVOKE:
            case OpCode::OP_INVOKE_MONO:
            case OpCode::OP_LOG:
                // Side effects or multiple writes
                writtenRegs.insert(A); // Most write to A
                // We should be conservative and assume they might write to any register if it's a call
                // But for now let's assume register 0-255.
                break;
            case OpCode::OP_JMP:
            case OpCode::OP_JMPF:
            case OpCode::OP_JMPT:
            case OpCode::OP_LOOP:
            case OpCode::OP_RET:
                break;
            default:
                writtenRegs.insert(A);
                break;
        }
    }

    // 2. Iteratively find invariant instructions.
    std::vector<bool> isInvariant(trace.entries.size(), false);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < trace.entries.size(); ++i) {
            if (isInvariant[i]) continue;
            
            const auto& entry = trace.entries[i];
            OpCode op = decodeOp(entry.instr);
            uint8_t A = decodeA(entry.instr);
            uint8_t B = decodeB(entry.instr);
            uint8_t C = decodeC(entry.instr);
            uint16_t Bx = decodeBx(entry.instr);

            bool invariant = false;
            switch (op) {
                case OpCode::OP_LOADK:
                case OpCode::OP_LOADINT:
                case OpCode::OP_LOADBOOL:
                case OpCode::OP_LOADNULL:
                    invariant = true;
                    break;
                case OpCode::OP_GGLOB:
                    if (writtenGlobals.find(Bx) == writtenGlobals.end()) {
                        invariant = true;
                    }
                    break;
                case OpCode::OP_MOVE:
                case OpCode::OP_MOVE_INT:
                    if (writtenRegs.find(B) == writtenRegs.end()) invariant = true;
                    break;
                case OpCode::OP_ADD:
                case OpCode::OP_SUB:
                case OpCode::OP_MUL:
                case OpCode::OP_DIV:
                case OpCode::OP_MOD:
                case OpCode::OP_BIT_AND:
                case OpCode::OP_BIT_OR:
                case OpCode::OP_BIT_XOR:
                case OpCode::OP_SHL:
                case OpCode::OP_SHR:
                case OpCode::OP_LT:
                case OpCode::OP_GT:
                case OpCode::OP_LE:
                case OpCode::OP_GE:
            case OpCode::OP_ADD_INT:
            case OpCode::OP_SUB_INT:
            case OpCode::OP_MUL_INT:
            case OpCode::OP_DIV_INT:
            case OpCode::OP_LT_INT:
            case OpCode::OP_GT_INT:
            case OpCode::OP_LE_INT:
            case OpCode::OP_GE_INT:
            case OpCode::OP_EQ_INT:
                if (writtenRegs.find(B) == writtenRegs.end() && writtenRegs.find(C) == writtenRegs.end()) {
                    invariant = true;
                }
                break;
            case OpCode::OP_ADDI:
            case OpCode::OP_SUBI:
            case OpCode::OP_ADDI_W:
            case OpCode::OP_SUBI_W:
            case OpCode::OP_ADD_K:
            case OpCode::OP_SUB_K:
            case OpCode::OP_MUL_K:
            case OpCode::OP_DIV_K:
            case OpCode::OP_EQ_K:
            case OpCode::OP_NOT:
            case OpCode::OP_NEG:
                if (writtenRegs.find(B) == writtenRegs.end()) invariant = true;
                break;
                case OpCode::OP_INC:
                case OpCode::OP_DEC:
                    // These use A as input too!
                    if (writtenRegs.find(A) == writtenRegs.end()) invariant = true;
                    break;
                default:
                    break;
            }

            if (invariant) {
                // To move it to preamble, its output register A must not be written 
                // by any OTHER instruction in the trace that is not invariant.
                // For simplicity, we only move if A is NOT in writtenRegs anymore.
                // But wait, A *is* in writtenRegs because this instruction writes to it.
                // We mean: no OTHER instruction writes to A.
                
                int writesToA = 0;
                for(const auto& e : trace.entries) if(decodeA(e.instr) == A) writesToA++;
                
                if (writesToA == 1) {
                    isInvariant[i] = true;
                    writtenRegs.erase(A); 
                    changed = true;
                }
            }
        }
    }

    // 3. Move invariant instructions to preamble.
    std::vector<Trace::Entry> newEntries;
    for (size_t i = 0; i < trace.entries.size(); ++i) {
        if (isInvariant[i]) {
            trace.preamble.push_back(trace.entries[i]);
        } else {
            newEntries.push_back(trace.entries[i]);
        }
    }
    trace.entries = std::move(newEntries);
}

void TraceOptimizer::performDCE(Trace &trace) {
    if (trace.entries.empty()) return;

    std::vector<bool> isDead(trace.entries.size(), false);
    std::vector<bool> regUsed(256, true); 

    for (int i = (int)trace.entries.size() - 1; i >= 0; --i) {
        const auto& entry = trace.entries[i];
        OpCode op = decodeOp(entry.instr);
        uint8_t A = decodeA(entry.instr);
        uint8_t B = decodeB(entry.instr);
        uint8_t C = decodeC(entry.instr);

        bool pure = false;
        switch (op) {
            case OpCode::OP_LOADK: case OpCode::OP_LOADINT: case OpCode::OP_LOADBOOL: case OpCode::OP_LOADNULL:
            case OpCode::OP_MOVE: case OpCode::OP_MOVE_INT:
            case OpCode::OP_ADD: case OpCode::OP_SUB: case OpCode::OP_MUL: case OpCode::OP_DIV:
            case OpCode::OP_ADD_INT: case OpCode::OP_SUB_INT: case OpCode::OP_LT_INT:
            case OpCode::OP_ADDI: case OpCode::OP_SUBI: case OpCode::OP_ADDI_W: case OpCode::OP_SUBI_W:
            case OpCode::OP_INC: case OpCode::OP_DEC:
            case OpCode::OP_ADD_K: case OpCode::OP_SUB_K: case OpCode::OP_MUL_K: case OpCode::OP_DIV_K: case OpCode::OP_EQ_K:
            case OpCode::OP_BIT_AND: case OpCode::OP_BIT_OR: case OpCode::OP_BIT_XOR:
            case OpCode::OP_GGLOB:
                pure = true;
                break;
            default: break;
        }

        if (pure && !regUsed[A]) {
            isDead[i] = true;
        } else {
            if (pure) regUsed[A] = false;
            else regUsed[A] = true; // Non-pure might modify A in ways we don't track

            regUsed[B] = true;
            regUsed[C] = true;
            if (op == OpCode::OP_INC || op == OpCode::OP_DEC) regUsed[A] = true;
        }
    }

    std::vector<Trace::Entry> newEntries;
    for (size_t i = 0; i < trace.entries.size(); ++i) {
        if (!isDead[i]) newEntries.push_back(trace.entries[i]);
    }
    trace.entries = std::move(newEntries);
}

void TraceOptimizer::performConstantFolding(Trace &trace) {
    std::unordered_map<int, uint64_t> knownConst;

    auto foldInt = [&](int absA, int32_t val) {
        if (val >= -32767 && val <= 32767)
            return true;
        return false;
    };

    auto process = [&](Trace::Entry& entry) {
        uint32_t instr = entry.instr;
        OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr);
        uint8_t B = decodeB(instr);
        uint8_t C = decodeC(instr);
        int bo = entry.registerBaseOffset;
        int absA = bo + A;
        int absB = bo + B;
        int absC = bo + C;

        bool knownB = knownConst.count(absB) != 0;
        bool knownC = knownConst.count(absC) != 0;
        bool knownA = knownConst.count(absA) != 0;
        uint64_t bitsB = knownB ? knownConst[absB] : 0;
        uint64_t bitsC = knownC ? knownConst[absC] : 0;
        uint64_t bitsA = knownA ? knownConst[absA] : 0;

        switch (op) {
            case OpCode::OP_LOADINT: {
                knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)decodeSBx(instr);
                return;
            }
            case OpCode::OP_LOADBOOL: {
                knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | (B ? 1ULL : 0ULL);
                return;
            }
            case OpCode::OP_LOADNULL: {
                knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_NULL;
                return;
            }
            case OpCode::OP_LOADK: {
                if (entry.constants) {
                    uint16_t ki = decodeBx(instr);
                    if (ki < entry.constants->size())
                        knownConst[absA] = (*entry.constants)[ki].bits;
                    else knownConst.erase(absA);
                } else knownConst.erase(absA);
                return;
            }
            case OpCode::OP_LOADDBL: {
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_MOVE:
            case OpCode::OP_MOVE_INT: {
                if (knownB) knownConst[absA] = bitsB;
                else knownConst.erase(absA);
                return;
            }
            case OpCode::OP_NOT: {
                if (knownB) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    bool truthy = !vb.isNull() && (!vb.isBool() || vb.asBool());
                    uint64_t rbits = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | (!truthy ? 1ULL : 0ULL);
                    entry.instr = encodeABC(OpCode::OP_LOADBOOL, A, !truthy ? 1 : 0, 0);
                    knownConst[absA] = rbits;
                } else knownConst.erase(absA);
                return;
            }
            case OpCode::OP_NEG: {
                if (knownB) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    iris::core::Value res = iris::core::numericNegate(vb);
                    if (res.isInt()) {
                        int32_t iv = res.asInt();
                        if (foldInt(absA, iv)) {
                            entry.instr = encodeABx(OpCode::OP_LOADINT, A, static_cast<uint16_t>(iv + 32767));
                            knownConst[absA] = res.bits;
                            return;
                        }
                    }
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_INC: {
                if (knownA) {
                    iris::core::Value va = iris::core::Value::fromRawBits(bitsA);
                    if (va.isInt()) {
                        int32_t iv = va.asInt() + 1;
                        if (foldInt(absA, iv)) {
                            entry.instr = encodeABx(OpCode::OP_LOADINT, A, static_cast<uint16_t>(iv + 32767));
                            knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)iv;
                            return;
                        }
                    }
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_DEC: {
                if (knownA) {
                    iris::core::Value va = iris::core::Value::fromRawBits(bitsA);
                    if (va.isInt()) {
                        int32_t iv = va.asInt() - 1;
                        if (foldInt(absA, iv)) {
                            entry.instr = encodeABx(OpCode::OP_LOADINT, A, static_cast<uint16_t>(iv + 32767));
                            knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)iv;
                            return;
                        }
                    }
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_ADDI:
            case OpCode::OP_SUBI: {
                if (knownB) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    if (vb.isInt()) {
                        int32_t iv = (op == OpCode::OP_ADDI) ? vb.asInt() + (int8_t)C : vb.asInt() - (int8_t)C;
                        if (foldInt(absA, iv)) {
                            entry.instr = encodeABx(OpCode::OP_LOADINT, A, static_cast<uint16_t>(iv + 32767));
                            knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)iv;
                            return;
                        }
                    }
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_ADDI_W:
            case OpCode::OP_SUBI_W: {
                if (knownB) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    if (vb.isInt()) {
                        int32_t iv = (op == OpCode::OP_ADDI_W) ? vb.asInt() + (int8_t)C : vb.asInt() - (int8_t)C;
                        if (foldInt(absA, iv)) {
                            entry.instr = encodeABx(OpCode::OP_LOADINT, A, static_cast<uint16_t>(iv + 32767));
                            knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_INT | (uint32_t)iv;
                            return;
                        }
                    }
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL: {
                if (knownB && knownC) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    iris::core::Value vc = iris::core::Value::fromRawBits(bitsC);
                    iris::core::Value res;
                    if (op == OpCode::OP_ADD) res = iris::core::numericAdd(vb, vc);
                    else if (op == OpCode::OP_SUB) res = iris::core::numericSub(vb, vc);
                    else res = iris::core::numericMul(vb, vc);
                    if (res.isInt()) {
                        int32_t iv = res.asInt();
                        if (foldInt(absA, iv)) {
                            entry.instr = encodeABx(OpCode::OP_LOADINT, A, static_cast<uint16_t>(iv + 32767));
                            knownConst[absA] = res.bits;
                            return;
                        }
                    }
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_ADD_K:
            case OpCode::OP_SUB_K:
            case OpCode::OP_MUL_K:
            case OpCode::OP_DIV_K: {
                if (knownB && entry.constants && C < entry.constants->size()) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    iris::core::Value vc = (*entry.constants)[C];
                    iris::core::Value res;
                    if (op == OpCode::OP_ADD_K) res = iris::core::numericAdd(vb, vc);
                    else if (op == OpCode::OP_SUB_K) res = iris::core::numericSub(vb, vc);
                    else if (op == OpCode::OP_MUL_K) res = iris::core::numericMul(vb, vc);
                    else res = iris::core::numericDiv(vb, vc);
                    if (res.isInt()) {
                        int32_t iv = res.asInt();
                        if (foldInt(absA, iv)) {
                            entry.instr = encodeABx(OpCode::OP_LOADINT, A, static_cast<uint16_t>(iv + 32767));
                            knownConst[absA] = res.bits;
                            return;
                        }
                    }
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_DIV:
            case OpCode::OP_MOD: {
                if (knownB && knownC) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    iris::core::Value vc = iris::core::Value::fromRawBits(bitsC);
                    iris::core::Value res = (op == OpCode::OP_DIV) ? iris::core::numericDiv(vb, vc) : iris::core::numericMod(vb, vc);
                    if (res.isInt()) {
                        int32_t iv = res.asInt();
                        if (foldInt(absA, iv)) {
                            entry.instr = encodeABx(OpCode::OP_LOADINT, A, static_cast<uint16_t>(iv + 32767));
                            knownConst[absA] = res.bits;
                            return;
                        }
                    }
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_EQ:
            case OpCode::OP_NEQ: {
                if (knownB && knownC) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    iris::core::Value vc = iris::core::Value::fromRawBits(bitsC);
                    bool eq = (vb == vc);
                    bool result = (op == OpCode::OP_EQ) ? eq : !eq;
                    entry.instr = encodeABC(OpCode::OP_LOADBOOL, A, result ? 1 : 0, 0);
                    knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | (result ? 1ULL : 0ULL);
                } else knownConst.erase(absA);
                return;
            }
            case OpCode::OP_LT:
            case OpCode::OP_GT:
            case OpCode::OP_LE:
            case OpCode::OP_GE: {
                if (knownB && knownC) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    iris::core::Value vc = iris::core::Value::fromRawBits(bitsC);
                    bool lt = iris::core::numericLT(vb, vc);
                    bool gt = iris::core::numericGT(vb, vc);
                    bool result;
                    if (op == OpCode::OP_LT) result = lt;
                    else if (op == OpCode::OP_GT) result = gt;
                    else if (op == OpCode::OP_LE) result = !gt;
                    else result = !lt;
                    entry.instr = encodeABC(OpCode::OP_LOADBOOL, A, result ? 1 : 0, 0);
                    knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | (result ? 1ULL : 0ULL);
                } else knownConst.erase(absA);
                return;
            }
            case OpCode::OP_EQ_K: {
                if (knownB && entry.constants && C < entry.constants->size()) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    iris::core::Value vc = (*entry.constants)[C];
                    bool result = (vb == vc);
                    entry.instr = encodeABC(OpCode::OP_LOADBOOL, A, result ? 1 : 0, 0);
                    knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | (result ? 1ULL : 0ULL);
                    return;
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_LT_K:
            case OpCode::OP_GT_K: {
                if (knownB && entry.constants && C < entry.constants->size()) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    iris::core::Value vc = (*entry.constants)[C];
                    bool result = (op == OpCode::OP_LT_K) ? iris::core::numericLT(vb, vc) : iris::core::numericGT(vb, vc);
                    entry.instr = encodeABC(OpCode::OP_LOADBOOL, A, result ? 1 : 0, 0);
                    knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | (result ? 1ULL : 0ULL);
                    return;
                }
                knownConst.erase(absA);
                return;
            }
            case OpCode::OP_AND:
            case OpCode::OP_OR: {
                if (knownB && knownC) {
                    iris::core::Value vb = iris::core::Value::fromRawBits(bitsB);
                    iris::core::Value vc = iris::core::Value::fromRawBits(bitsC);
                    bool bTruthy = !vb.isNull() && (!vb.isBool() || vb.asBool());
                    bool cTruthy = !vc.isNull() && (!vc.isBool() || vc.asBool());
                    bool result = (op == OpCode::OP_AND) ? (bTruthy && cTruthy) : (bTruthy || cTruthy);
                    entry.instr = encodeABC(OpCode::OP_LOADBOOL, A, result ? 1 : 0, 0);
                    knownConst[absA] = iris::core::Value::QNAN | iris::core::Value::TAG_BOOL | (result ? 1ULL : 0ULL);
                } else knownConst.erase(absA);
                return;
            }
            default:
                knownConst.erase(absA);
                return;
        }
    };

    for (auto& entry : trace.preamble) process(entry);
    for (auto& entry : trace.entries) process(entry);
}

void TraceOptimizer::performEscapeAnalysis(Trace &trace) {
    // Map: register -> index of NEW_OBJ/NEW_ARRAY entry that defined it (in preamble+entries)
    // -1 means not an allocation result
    std::unordered_map<uint8_t, int> allocOwner;

    // Map: entry index -> whether it has escaped
    std::unordered_map<int, bool> escaped;

    int totalEntries = (int)(trace.preamble.size() + trace.entries.size());

    // Forward pass to build allocOwner and detect escapes
    auto processEntry = [&](Trace::Entry& entry, int idx) {
        uint32_t instr = entry.instr;
        OpCode op = decodeOp(instr);
        uint8_t A = decodeA(instr);
        uint8_t B = decodeB(instr);
        uint8_t C = decodeC(instr);

        // Record allocation sites
        if (op == OpCode::OP_NEW_OBJ || op == OpCode::OP_NEW_ARRAY) {
            allocOwner[A] = idx;
            if (escaped.find(idx) == escaped.end())
                escaped[idx] = false;
        }

        // Detect escape-causing operations
        bool escapesA = false, escapesB = false, escapesC = false;

        switch (op) {
            case OpCode::OP_SET_FIELD:
                // A (value being stored into field) escapes
                escapesA = true;
                break;
            case OpCode::OP_SGLOB:
            case OpCode::OP_DGLOB:
                // A (value being stored to global) escapes
                escapesA = true;
                break;
            case OpCode::OP_RET:
                // A (return value) escapes
                escapesA = true;
                break;
            case OpCode::OP_CALL:
            case OpCode::OP_TAILCALL:
                // All arguments escape (conservative)
                escapesB = true;
                // A register is destination, but we don't know if call args escape
                // For simplicity, mark caller as unknown
                break;
            case OpCode::OP_INVOKE:
            case OpCode::OP_INVOKE_MONO:
            case OpCode::OP_TAIL_INVOKE:
                // Base object (A) and all args escape (conservative)
                escapesA = true;
                break;
            case OpCode::OP_IDX_SET:
            case OpCode::OP_IDX_SET_INT:
            case OpCode::OP_IDX_SET_DBL:
                // A (value being stored into array) escapes
                escapesA = true;
                break;
            default:
                break;
        }

        // Mark escaped allocations
        auto markEscaped = [&](uint8_t reg) {
            auto it = allocOwner.find(reg);
            if (it != allocOwner.end()) {
                int ownerIdx = it->second;
                if (escaped.find(ownerIdx) != escaped.end())
                    escaped[ownerIdx] = true;
            }
        };

        if (escapesA) markEscaped(A);
        if (escapesB) markEscaped(B);
        if (escapesC) markEscaped(C);

        // Propagate ownership through MOVE
        if (op == OpCode::OP_MOVE || op == OpCode::OP_MOVE_INT) {
            auto it = allocOwner.find(B);
            if (it != allocOwner.end()) {
                allocOwner[A] = it->second;
            }
        }
    };

    int idx = 0;
    for (int i = 0; i < (int)trace.preamble.size(); i++, idx++)
        processEntry(trace.preamble[i], idx);
    for (int i = 0; i < (int)trace.entries.size(); i++, idx++)
        processEntry(trace.entries[i], idx);

    // Mark non-escaping allocations
    int entryIdx = 0;
    auto markNonEscaping = [&](Trace::Entry& entry) {
        uint32_t instr = entry.instr;
        OpCode op = decodeOp(instr);
        if (op == OpCode::OP_NEW_OBJ || op == OpCode::OP_NEW_ARRAY) {
            auto it = escaped.find(entryIdx);
            if (it != escaped.end() && !it->second) {
                entry.nonEscaping = true;
            }
        }
        entryIdx++;
    };

    entryIdx = 0;
    for (auto& entry : trace.preamble) markNonEscaping(entry);
    for (auto& entry : trace.entries) markNonEscaping(entry);
}
