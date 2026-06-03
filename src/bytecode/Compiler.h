#ifndef COMPILER_H
#define COMPILER_H

#include "Chunk.h"
#include "../node/ASTNode.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace iris::bytecode {
    using namespace iris::node;

    /**
     * @brief Represents a local variable during compilation.
     */
    struct Local {
        std::string name;
        int depth;
        bool isMutable;
        uint8_t reg;
        TypeAnnotation typeAnnot = TypeKind::None; ///< Optional type constraint
    };

    /**
     * @brief Represents a compiled function.
     */
    struct FunctionObject {
        std::string name;
        int arity;
        Chunk chunk;
        uint8_t maxRegs;
        TypeAnnotation returnType = TypeKind::None; ///< Expected return type
        std::vector<TypeAnnotation> paramTypes; ///< Expected type per parameter
    };

    /**
     * @brief Metadata for a compiled class.
     */
    struct ClassFieldMeta {
        std::string name;
        bool isMutable;
        bool isStatic;
        AccessModifier access;
        TypeAnnotation type;
    };

    struct ClassMeta {
        std::string name;
        std::vector<std::string> genericParams;
        bool isAbstract = false;
        int16_t parentClassId = -1;
        std::vector<ClassFieldMeta> fields;
        std::unordered_map<std::string, uint16_t> fieldIndex;
        std::unordered_map<std::string, uint16_t> methodIndex; ///< method name → function index
        std::unordered_map<std::string, AccessModifier> methodAccess;
        std::vector<std::string> abstractMethods; ///< methods that need to be implemented
    };

    struct ExprResult {
        uint8_t reg;
        TypeAnnotation type;
    };

    struct CompileError : public std::runtime_error {
        iris::node::SourceLocation location;
        CompileError(iris::node::SourceLocation loc, const std::string& msg)
            : std::runtime_error(msg), location(std::move(loc)) {}
    };

    /**
     * @brief Single-pass Compiler (AST -> Bytecode).
     *
     * The Compiler traverses the Abstract Syntax Tree (AST) once and generates
     * register-based bytecode. It handles:
     * - Register allocation and reuse
     * - Scope management (locals vs globals)
     * - Control flow (jumps, loops, breaks)
     * - Function and Class declarations
     * - Type inference and specialized opcode generation
     * - Peephole optimization
     */
    class Compiler {
        Chunk chunk; ///< Current bytecode chunk being generated
        int repeatCounter = 0; ///< Counter for unique internal repeat loop variables
        std::vector<Local> locals; ///< Local variable stack
        int scopeDepth = 0; ///< Current lexical scope depth

        uint8_t nextReg = 0; ///< Next available register index
        uint8_t maxReg = 0; ///< Maximum registers used by the current function

        /** @brief Context for loop control flow (break/continue). */
        struct LoopContext {
            size_t loopStart; ///< Bytecode offset of the loop start
            std::vector<size_t> breakJumps; ///< List of JMP instructions to be patched on 'break'
            int scopeDepthAtLoop; ///< Scope depth where the loop started
        };

        std::vector<LoopContext> loopStack;

        /** @brief Context for switch statements. */
        struct SwitchContext {
            std::vector<size_t> breakJumps; ///< List of JMP instructions for 'break' inside switch
        };

        std::vector<SwitchContext> switchStack;

        // Unified break handling
        enum class BreakableType { Loop, Switch };

        struct Breakable {
            BreakableType type;
            size_t index; // index in loopStack or switchStack
        };

        std::vector<Breakable> breakableStack;

        std::vector<FunctionObject> functions; ///< Compiled function objects
        std::unordered_map<std::string, uint16_t> functionIndex; ///< Maps function name to index
        std::unordered_map<std::string, uint16_t> nativeFunctionIndex; ///< Maps alias to native index
        std::unordered_map<std::string, uint16_t> globalIndex; ///< Maps global name to slot index
        uint16_t globalCount = 0; ///< Number of globals defined

        std::vector<ClassMeta> classes; ///< Compiled class metadata
        std::unordered_map<std::string, uint16_t> classIndex; ///< Maps class name to index
        std::unordered_map<std::string, std::string> varClassMap; ///< Maps variable name to its class name (for typing)
        std::unordered_map<std::string, TypeAnnotation> genericTypeMap; ///< Maps "T" -> concrete TypeAnnotation

        TypeAnnotation resolveType(const TypeAnnotation& annot) {
            if ((annot.kind == TypeKind::Object || annot.kind == TypeKind::GenericParam) && genericTypeMap.contains(annot.name)) {
                return genericTypeMap.at(annot.name);
            }
            TypeAnnotation result = annot;
            for (auto& p : result.params) {
                p = resolveType(p);
            }
            return result;
        }

        bool isCompatible(const TypeAnnotation& src, const TypeAnnotation& target) {
            if (src.kind == TypeKind::None || target.kind == TypeKind::None) return true;
            if (src.kind != target.kind) return false;
            if (src.kind == TypeKind::Object) {
                if (src.name != target.name) return false; // Basic check, ignoring inheritance for now
                if (src.params.size() != target.params.size()) return false;
                for (size_t i = 0; i < src.params.size(); ++i) {
                    if (!isCompatible(src.params[i], target.params[i])) return false;
                }
            }
            return true;
        }
        std::string currentClassName; ///< Name of the class currently being compiled

        struct EnumMeta {
            std::string name;
            std::vector<std::string> values;
            std::vector<int> ordinals;
        };

        std::vector<EnumMeta> enums;
        std::unordered_map<std::string, uint16_t> enumIndex;

    public:
        /**
         * @brief Compiles the entire program AST into a bytecode chunk.
         *
         * @param program The root node of the AST.
         * @return The main chunk containing the compiled program instructions.
         */
        Chunk compile(ProgramNode *program);

        const std::vector<FunctionObject> &getFunctions() const { return functions; }
        std::vector<FunctionObject> &getFunctions() { return functions; }
        const std::vector<ClassMeta> &getClasses() const { return classes; }
        std::vector<ClassMeta> &getClasses() { return classes; }

    private:
        /** @brief Main dispatch for compiling any AST node. */
        void compileNode(ASTNode *node);

        /** @brief Compiles an expression and returns where the result is stored. */
        ExprResult compileExpression(ExpressionNode *expr, uint8_t dst = 255);

        // --- Statement compilation ---
        void compileProgram(ProgramNode *node);

        void compileRepeat(RepeatNode *node);

        void compileWhile(WhileNode *node);

        void compileFor(const ForNode *node);

        void compileIf(IfNode *node);

        void compileLog(PrintNode *node);

        void compileVarDecl(VarDeclNode *node);

        void compileAssignment(AssignmentNode *node);

        void compileWait(WaitNode *node);

        void compileBreak(ASTNode *node);

        void compileContinue(ASTNode *node);

        void compileFunctionDecl(FunctionDeclNode *node);

        void compileReturn(ReturnNode *node);

        void compileClassDecl(ClassDeclNode *node);

        void compileFieldAssign(FieldAssignNode *node);

        void compileExprStmt(ExpressionStmtNode *node);

        void compileIndexAssign(IndexAssignNode *node);

        void compileTryCatch(TryCatchNode *node);

        void compileThrow(ThrowNode *node);

        ExprResult compileSwitch(SwitchNode *node, uint8_t dst = 255);

        void compileEnum(EnumNode *node);

        void compileImportNative(ImportNativeNode *node);

        // --- Expression compilation ---
        ExprResult compileNumber(NumberNode *node, uint8_t dst);

        ExprResult compileDouble(DoubleNode *node, uint8_t dst);

        ExprResult compileBoolean(BooleanNode *node, uint8_t dst);

        ExprResult compileString(StringNode *node, uint8_t dst);

        ExprResult compileStringInterp(StringInterpNode *node, uint8_t dst);

        ExprResult compileVariable(VariableNode *node, uint8_t dst);

        ExprResult compileBinaryOp(BinaryOperationNode *node, uint8_t dst);

        ExprResult compileUnaryOp(UnaryOperationNode *node, uint8_t dst);

        ExprResult compileFunctionCall(FunctionCallNode *node, uint8_t dst);

        ExprResult compileFieldAccess(FieldAccessNode *node, uint8_t dst);

        ExprResult compileMethodCall(MethodCallNode *node, uint8_t dst);

        ExprResult compileIndexAccess(IndexAccessNode *node, uint8_t dst);

        ExprResult compileArrayAlloc(ArrayAllocNode *node, uint8_t dst);

        void compileRecursiveArrayAlloc(ArrayAllocNode *node, uint8_t dst, size_t dimIdx, uint8_t finalElemTypeTag);

        ExprResult compileArrayLiteral(ArrayLiteralNode *node, uint8_t dst);

        // OPTIMIZATION: Peephole Optimizer
        /**
         * @brief Performs simple bytecode optimizations like redundant MOVE removal.
         *
         * @param ch The chunk to optimize in-place.
         */
        void peepholeOptimize(Chunk &ch);

        /** @brief Allocates a new register for temporary use. */
        uint8_t allocReg() {
            const uint8_t r = nextReg++;
            if (nextReg > maxReg) maxReg = nextReg;
            return r;
        }

        /** @brief Frees the last allocated register. */
        void freeReg() { nextReg--; }

        /** @brief Frees all registers above the specified index. */
        void freeRegsTo(const uint8_t to) { nextReg = to; }

        // --- Scope management ---
        void beginScope();

        void endScope();

        void addLocal(const std::string &name, bool isMutable, TypeAnnotation typeAnnot, const iris::node::SourceLocation& loc);

        int resolveLocal(const std::string &name);

        bool isGlobalScope() const { return scopeDepth == 0; }

        bool isGenericParam(const std::string &name) {
            if (currentClassName.empty()) return false;
            auto it = classIndex.find(currentClassName);
            if (it == classIndex.end()) return false;
            const auto &gp = classes[it->second].genericParams;
            return std::find(gp.begin(), gp.end(), name) != gp.end();
        }
    };
}

#endif //COMPILER_H