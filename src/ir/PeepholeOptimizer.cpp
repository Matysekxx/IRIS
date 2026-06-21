#include "PeepholeOptimizer.h"
#include <unordered_set>

namespace iris::bytecode {

void PeepholeOptimizer::optimize(Chunk &ch) {
    auto &code = ch.code;
    if (code.empty()) return;
    std::unordered_set<size_t> targets;
    for (size_t i = 0; i < code.size(); i++) {
        OpCode op = decodeOp(code[i]);
        if (op == OpCode::OP_JMP || op == OpCode::OP_JMPF || op == OpCode::OP_LOOP) targets.insert(
            i + 1 + static_cast<int16_t>(decodeSBx(code[i])));
    }
    bool changed = true;
    int iters = 0;
    while (changed && iters++ < 10) {
        changed = false;
        for (size_t i = 0; i < code.size(); i++) {
            if (targets.count(i)) continue;
            
            OpCode op = decodeOp(code[i]);
            uint8_t a = decodeA(code[i]);
            
            if (op == OpCode::OP_MOVE && a == decodeB(code[i])) {
                code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0); // No-op
                changed = true;
                continue;
            }

            if (i + 1 < code.size() && !targets.count(i + 1)) {
                uint32_t i1 = code[i], i2 = code[i + 1];
                OpCode o1 = decodeOp(i1), o2 = decodeOp(i2);
                uint8_t a1 = decodeA(i1), a2 = decodeA(i2), b2 = decodeB(i2);

                if (o1 == OpCode::OP_LOADINT && o2 == OpCode::OP_MOVE && a1 == b2) {
                    code[i + 1] = encodeABx(OpCode::OP_LOADINT, a2, decodeBx(i1));
                    changed = true;
                } else if (o1 == OpCode::OP_LOADK && o2 == OpCode::OP_MOVE && a1 == b2) {
                    code[i + 1] = encodeABx(OpCode::OP_LOADK, a2, decodeBx(i1));
                    changed = true;
                } else if (o1 == OpCode::OP_MOVE && o2 == OpCode::OP_MOVE && a1 == b2 && a2 == decodeB(i1)) {
                    code[i + 1] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    changed = true;
                }
                // FUSION: LT_INT R1, R2, R3; JMPF R1, offset -> JGE_INT R2, R3, offset
                else if (o1 == OpCode::OP_LT_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    code[i] = encodeABC(OpCode::OP_JGE_INT, decodeB(i1), decodeC(i1), 0);
                    changed = true;
                }
                else if (o1 == OpCode::OP_GT_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    code[i] = encodeABC(OpCode::OP_JLE_INT, decodeB(i1), decodeC(i1), 0);
                    changed = true;
                }
                else if (o1 == OpCode::OP_LE_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    code[i] = encodeABC(OpCode::OP_JGT_INT, decodeB(i1), decodeC(i1), 0);
                    changed = true;
                }
                else if (o1 == OpCode::OP_GE_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    code[i] = encodeABC(OpCode::OP_JLT_INT, decodeB(i1), decodeC(i1), 0);
                    changed = true;
                }
                else if (o1 == OpCode::OP_EQ_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    code[i] = encodeABC(OpCode::OP_JNE_INT, decodeB(i1), decodeC(i1), 0);
                    changed = true;
                }
                // FUSION: LOADINT R1, imm; ADD_INT R2, R2, R1 -> ADDI_W R2, imm
                else if (o1 == OpCode::OP_LOADINT && o2 == OpCode::OP_ADD_INT && a1 == decodeC(i2) && decodeA(i2) == decodeB(i2)) {
                    code[i+1] = encodeABx(OpCode::OP_ADDI_W, decodeA(i2), decodeBx(i1));
                    code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    changed = true;
                }
                else if (o1 == OpCode::OP_LOADINT && o2 == OpCode::OP_SUB_INT && a1 == decodeC(i2) && decodeA(i2) == decodeB(i2)) {
                    code[i+1] = encodeABx(OpCode::OP_SUBI_W, decodeA(i2), decodeBx(i1));
                    code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    changed = true;
                }

                // FUSION: LOADBOOL R1, 0; JMPF R1, offset -> JMP offset (always taken)
                else if (o1 == OpCode::OP_LOADBOOL && o2 == OpCode::OP_JMPF && a1 == decodeA(i2) && decodeB(i1) == 0) {
                    code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    code[i+1] = encodesBx(OpCode::OP_JMP, decodeSBx(i2));
                    changed = true;
                }
                // FUSION: LOADBOOL R1, 1; JMPT R1, offset -> JMP offset (always taken)
                else if (o1 == OpCode::OP_LOADBOOL && o2 == OpCode::OP_JMPT && a1 == decodeA(i2) && decodeB(i1) == 1) {
                    code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    code[i+1] = encodesBx(OpCode::OP_JMP, decodeSBx(i2));
                    changed = true;
                }
                // FUSION: LOADK R1, ki; OP R2, R3, R1 -> OP_K R2, R3, ki
                else if (o1 == OpCode::OP_LOADK && a1 == decodeC(i2) && decodeBx(i1) <= 255) {
                    OpCode fused = OpCode::OP_COUNT;
                    if (o2 == OpCode::OP_ADD) fused = OpCode::OP_ADD_K;
                    else if (o2 == OpCode::OP_SUB) fused = OpCode::OP_SUB_K;
                    else if (o2 == OpCode::OP_MUL) fused = OpCode::OP_MUL_K;
                    else if (o2 == OpCode::OP_DIV) fused = OpCode::OP_DIV_K;
                    else if (o2 == OpCode::OP_LT) fused = OpCode::OP_LT_K;
                    else if (o2 == OpCode::OP_GT) fused = OpCode::OP_GT_K;
                    else if (o2 == OpCode::OP_EQ) fused = OpCode::OP_EQ_K;
                    
                    if (fused != OpCode::OP_COUNT) {
                        code[i+1] = encodeABC(fused, decodeA(i2), decodeB(i2), static_cast<uint8_t>(decodeBx(i1)));
                        code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                        changed = true;
                    }
                }
                // CONSTANT FOLDING: LOADINT R1, k1; LOADINT R2, k2; ADD_INT R3, R1, R2 -> LOADINT R3, k1+k2
                else if (i + 2 < code.size() && !targets.count(i + 2)) {
                    uint32_t i3 = code[i+2];
                    OpCode o3 = decodeOp(i3);
                    if (o1 == OpCode::OP_LOADINT && o2 == OpCode::OP_LOADINT && (o3 == OpCode::OP_ADD_INT || o3 == OpCode::OP_SUB_INT || o3 == OpCode::OP_MUL_INT)) {
                        uint8_t a3 = decodeA(i3); uint8_t b3 = decodeB(i3); uint8_t c3 = decodeC(i3);
                        if (a1 == b3 && a2 == c3) {
                            int v1 = decodeSBx(i1); int v2 = decodeSBx(i2);
                            int res = (o3 == OpCode::OP_ADD_INT) ? v1 + v2 : (o3 == OpCode::OP_SUB_INT ? v1 - v2 : v1 * v2);
                            if (res >= -32767 && res <= 32767) {
                                code[i+2] = encodeABx(OpCode::OP_LOADINT, a3, static_cast<uint16_t>(res + 32767));
                                changed = true;
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace
