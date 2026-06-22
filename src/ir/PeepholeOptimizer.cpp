#include "PeepholeOptimizer.h"
#include "core/Value.h"
#include <unordered_set>
#include <cmath>

namespace iris::bytecode {

void PeepholeOptimizer::optimize(Chunk &ch) {
    auto &code = ch.code;
    if (code.empty()) return;

    // Collect branch targets for safety (we skip optimizations that cross targets)
    std::unordered_set<size_t> targets;
    for (size_t i = 0; i < code.size(); i++) {
        OpCode op = decodeOp(code[i]);
        if (op == OpCode::OP_JMP || op == OpCode::OP_JMPF || op == OpCode::OP_JMPT || op == OpCode::OP_LOOP) {
            targets.insert(i + 1 + static_cast<int16_t>(decodeSBx(code[i])));
        }
    }

    // Also collect fallthrough-after-jump as implicit targets
    for (size_t i = 0; i + 1 < code.size(); i++) {
        OpCode op = decodeOp(code[i]);
        if (op == OpCode::OP_JMP) {
            targets.insert(i + 1);
        }
    }

    bool changed = true;
    int iters = 0;
    while (changed && iters++ < 10) {
        changed = false;
        for (size_t i = 0; i < code.size(); i++) {
            if (targets.count(i)) continue;

            OpCode op = decodeOp(code[i]);
            uint8_t a = decodeA(code[i]);

            // REDUNDANT MOVE: MOVE R_a, R_a -> no-op
            if (op == OpCode::OP_MOVE && a == decodeB(code[i])) {
                code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                changed = true;
                continue;
            }

            // NEG + NEG: NEG R_a, R_b; NEG R_c, R_a -> MOVE R_c, R_b (or no-op if same reg)
            if (i + 1 < code.size() && !targets.count(i + 1)) {
                uint32_t i1 = code[i], i2 = code[i + 1];
                OpCode o1 = decodeOp(i1), o2 = decodeOp(i2);
                uint8_t a1 = decodeA(i1), a2 = decodeA(i2), b1 = decodeB(i1), b2 = decodeB(i2);

                if (o1 == OpCode::OP_NEG && o2 == OpCode::OP_NEG && a1 == b2) {
                    if (a2 == b1) {
                        code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                        code[i + 1] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    } else {
                        code[i + 1] = encodeABC(OpCode::OP_MOVE, a2, b1, 0);
                    }
                    changed = true;
                    continue;
                }
            }

            // Two-instruction optimizations (require next instruction not a target)
            if (i + 1 < code.size() && !targets.count(i + 1)) {
                uint32_t i1 = code[i], i2 = code[i + 1];
                OpCode o1 = decodeOp(i1), o2 = decodeOp(i2);
                uint8_t a1 = decodeA(i1), a2 = decodeA(i2), b2 = decodeB(i2);

                // LOAD FORWARDING: LOADINT R1, k; MOVE R2, R1 -> LOADINT R2, k
                if (o1 == OpCode::OP_LOADINT && o2 == OpCode::OP_MOVE && a1 == b2) {
                    code[i + 1] = encodeABx(OpCode::OP_LOADINT, a2, decodeBx(i1));
                    changed = true;
                    continue;
                }
                // LOAD FORWARDING: LOADK R1, k; MOVE R2, R1 -> LOADK R2, k
                if (o1 == OpCode::OP_LOADK && o2 == OpCode::OP_MOVE && a1 == b2) {
                    code[i + 1] = encodeABx(OpCode::OP_LOADK, a2, decodeBx(i1));
                    changed = true;
                    continue;
                }
                // REDUNDANT MOVE CHAIN: MOVE R1, R2; MOVE R2, R1 -> both no-op
                if (o1 == OpCode::OP_MOVE && o2 == OpCode::OP_MOVE && a1 == b2 && a2 == decodeB(i1)) {
                    code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    code[i + 1] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    changed = true;
                    continue;
                }

                // JUMP THREADING: JMP L1; (at L1) JMP L2 -> JMP L2
                if (o1 == OpCode::OP_JMP && o2 == OpCode::OP_JMP) {
                    int16_t off1 = static_cast<int16_t>(decodeSBx(i1));
                    int16_t off2 = static_cast<int16_t>(decodeSBx(i2));
                    code[i] = encodesBx(OpCode::OP_JMP, off1 + off2);
                    changed = true;
                    continue;
                }
                // JUMP THREADING: JMPF R, L1; (at L1) JMP L2 -> JMPF R, L1+L2
                if ((o1 == OpCode::OP_JMPF || o1 == OpCode::OP_JMPT) && o2 == OpCode::OP_JMP) {
                    int16_t off1 = static_cast<int16_t>(decodeSBx(i1));
                    int16_t off2 = static_cast<int16_t>(decodeSBx(i2));
                    code[i] = encodesBx(o1, off1 + off2);
                    changed = true;
                    continue;
                }

                // FUSION: LT_INT R1, R2, R3; JMPF R1, offset -> JGE_INT R2, R3, offset
                // Offset stored as signed byte in C field; JGE_INT reads from one slot earlier
                // than JMPF, so offset = JMPF_offset + 1. JMPF is replaced with NOP.
                if (o1 == OpCode::OP_LT_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    int16_t off = static_cast<int16_t>(decodeSBx(i2));
                    if (off >= -129 && off <= 126) {
                        code[i] = encodeABC(OpCode::OP_JGE_INT, decodeB(i1), decodeC(i1), static_cast<uint8_t>(static_cast<int8_t>(off + 1)));
                        code[i + 1] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                        changed = true;
                        continue;
                    }
                }
                if (o1 == OpCode::OP_GT_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    int16_t off = static_cast<int16_t>(decodeSBx(i2));
                    if (off >= -129 && off <= 126) {
                        code[i] = encodeABC(OpCode::OP_JLE_INT, decodeB(i1), decodeC(i1), static_cast<uint8_t>(static_cast<int8_t>(off + 1)));
                        code[i + 1] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                        changed = true;
                        continue;
                    }
                }
                if (o1 == OpCode::OP_LE_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    int16_t off = static_cast<int16_t>(decodeSBx(i2));
                    if (off >= -129 && off <= 126) {
                        code[i] = encodeABC(OpCode::OP_JGT_INT, decodeB(i1), decodeC(i1), static_cast<uint8_t>(static_cast<int8_t>(off + 1)));
                        code[i + 1] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                        changed = true;
                        continue;
                    }
                }
                if (o1 == OpCode::OP_GE_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    int16_t off = static_cast<int16_t>(decodeSBx(i2));
                    if (off >= -129 && off <= 126) {
                        code[i] = encodeABC(OpCode::OP_JLT_INT, decodeB(i1), decodeC(i1), static_cast<uint8_t>(static_cast<int8_t>(off + 1)));
                        code[i + 1] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                        changed = true;
                        continue;
                    }
                }
                if (o1 == OpCode::OP_EQ_INT && o2 == OpCode::OP_JMPF && a1 == decodeA(i2)) {
                    int16_t off = static_cast<int16_t>(decodeSBx(i2));
                    if (off >= -129 && off <= 126) {
                        code[i] = encodeABC(OpCode::OP_JNE_INT, decodeB(i1), decodeC(i1), static_cast<uint8_t>(static_cast<int8_t>(off + 1)));
                        code[i + 1] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                        changed = true;
                        continue;
                    }
                }

                // FUSION: LOADINT R1, imm; ADD_INT R2, R2, R1 -> ADDI_W R2, imm
                if (o1 == OpCode::OP_LOADINT && o2 == OpCode::OP_ADD_INT && a1 == decodeC(i2) && decodeA(i2) == decodeB(i2)) {
                    code[i+1] = encodeABx(OpCode::OP_ADDI_W, decodeA(i2), decodeBx(i1));
                    code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    changed = true;
                    continue;
                }
                if (o1 == OpCode::OP_LOADINT && o2 == OpCode::OP_SUB_INT && a1 == decodeC(i2) && decodeA(i2) == decodeB(i2)) {
                    code[i+1] = encodeABx(OpCode::OP_SUBI_W, decodeA(i2), decodeBx(i1));
                    code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    changed = true;
                    continue;
                }

                // FUSION: LOADBOOL R1, 0; JMPF R1, offset -> JMP offset (always taken)
                if (o1 == OpCode::OP_LOADBOOL && o2 == OpCode::OP_JMPF && a1 == decodeA(i2) && decodeB(i1) == 0) {
                    code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    code[i+1] = encodesBx(OpCode::OP_JMP, decodeSBx(i2));
                    changed = true;
                    continue;
                }
                // FUSION: LOADBOOL R1, 1; JMPT R1, offset -> JMP offset (always taken)
                if (o1 == OpCode::OP_LOADBOOL && o2 == OpCode::OP_JMPT && a1 == decodeA(i2) && decodeB(i1) == 1) {
                    code[i] = encodeABC(OpCode::OP_COUNT, 0, 0, 0);
                    code[i+1] = encodesBx(OpCode::OP_JMP, decodeSBx(i2));
                    changed = true;
                    continue;
                }

                // FUSION: LOADK R1, ki; OP R2, R3, R1 -> OP_K R2, R3, ki
                if (o1 == OpCode::OP_LOADK && a1 == decodeC(i2) && decodeBx(i1) <= 255) {
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
                        continue;
                    }
                }
            }

            // Three-instruction optimizations (constant folding)
            if (i + 2 < code.size() && !targets.count(i + 2)) {
                uint32_t i1 = code[i], i2 = code[i+1], i3 = code[i+2];
                OpCode o1 = decodeOp(i1), o2 = decodeOp(i2), o3 = decodeOp(i3);

                // INTEGER CONSTANT FOLDING: LOADINT R1,k1; LOADINT R2,k2; OP_INT R3,R1,R2 -> LOADINT R3,k1 op k2
                if (o1 == OpCode::OP_LOADINT && o2 == OpCode::OP_LOADINT) {
                    uint8_t a3 = decodeA(i3); uint8_t b3 = decodeB(i3); uint8_t c3 = decodeC(i3);
                    uint8_t a1 = decodeA(i1); uint8_t a2 = decodeA(i2);
                    if (a1 == b3 && a2 == c3) {
                        int v1 = static_cast<int>(decodeBx(i1)) - 32767;
                        int v2 = static_cast<int>(decodeBx(i2)) - 32767;
                        bool foldable = true;
                        int res = 0;
                        if (o3 == OpCode::OP_ADD_INT) res = v1 + v2;
                        else if (o3 == OpCode::OP_SUB_INT) res = v1 - v2;
                        else if (o3 == OpCode::OP_MUL_INT) res = v1 * v2;
                        else foldable = false;

                        if (foldable && res >= -32767 && res <= 32767) {
                            code[i+2] = encodeABx(OpCode::OP_LOADINT, a3, static_cast<uint16_t>(res + 32767));
                            changed = true;
                            continue;
                        }
                    }
                }

                // DOUBLE CONSTANT FOLDING: LOADK R1,d1; LOADK R2,d2; OP_DOUBLE R3,R1,R2 -> LOADK R3,d1 op d2
                if (o1 == OpCode::OP_LOADK && o2 == OpCode::OP_LOADK) {
                    const auto& c1 = ch.constants[decodeBx(i1)];
                    const auto& c2 = ch.constants[decodeBx(i2)];
                    if (c1.isDouble() && c2.isDouble()) {
                        uint8_t a3 = decodeA(i3); uint8_t b3 = decodeB(i3); uint8_t c3 = decodeC(i3);
                        uint8_t a1 = decodeA(i1); uint8_t a2 = decodeA(i2);
                        if (a1 == b3 && a2 == c3) {
                            double d1 = c1.asDouble();
                            double d2 = c2.asDouble();
                            bool foldable = true;
                            double res = 0;
                            if (o3 == OpCode::OP_ADD_DOUBLE) res = d1 + d2;
                            else if (o3 == OpCode::OP_SUB_DOUBLE) res = d1 - d2;
                            else if (o3 == OpCode::OP_MUL_DOUBLE) res = d1 * d2;
                            else if (o3 == OpCode::OP_DIV_DOUBLE) { if (d2 != 0.0) res = d1 / d2; else foldable = false; }
                            else foldable = false;

                            if (foldable && std::isfinite(res)) {
                                code[i+2] = encodeABx(OpCode::OP_LOADK, a3, ch.addConstant(iris::core::Value(res)));
                                changed = true;
                                continue;
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace
