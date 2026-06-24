#include "TraceOptimizer.h"
#include <algorithm>
#include <set>

#include <iostream>

using namespace iris::bytecode;

void TraceOptimizer::optimize(Trace &trace) {
    size_t oldSize = trace.entries.size();
    performLICM(trace);
    size_t sizeAfterLICM = trace.entries.size();
    performDCE(trace);
    size_t sizeAfterDCE = trace.entries.size();
    performGuardElimination(trace);
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
