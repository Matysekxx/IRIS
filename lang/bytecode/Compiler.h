#ifndef COMPILER_H
#define COMPILER_H

#include "Chunk.h"
#include "../node/ASTNode.h"
#include <vector>
#include <string>
#include <unordered_map>

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
        TypeAnnotation typeAnnot = TypeAnnotation::None; ///< Optional type constraint
    };

    /**
     * @brief Represents a compiled function.
     */
    struct FunctionObject {
        std::string name;
        int arity;
        Chunk chunk;
        uint8_t maxRegs;
        TypeAnnotation returnType = TypeAnnotation::None;         ///< Expected return type
        std::vector<TypeAnnotation> paramTypes;                   ///< Expected type per parameter
    };

    /**
     * @brief Metadata for a compiled class.
     */
    struct ClassFieldMeta {
        std::string name;
        bool isMutable;
        AccessModifier access;
    };

    struct ClassMeta {
        std::string name;
        bool isAbstract = false;
        int16_t parentClassId = -1;
        std::vector<ClassFieldMeta> fields;
        std::unordered_map<std::string, uint16_t> fieldIndex;
        std::unordered_map<std::string, uint16_t> methodIndex;  ///< method name → function index
        std::unordered_map<std::string, AccessModifier> methodAccess;
        std::vector<std::string> abstractMethods; ///< methods that need to be implemented
    };

    struct ExprResult {
        uint8_t reg;
        TypeAnnotation type;
    };

    /**
     * @brief Single-pass Compiler (AST -> Bytecode).
     * Handles register allocation, scope management, and control flow.
     */
    class Compiler {
        Chunk chunk;
        int repeatCounter = 0;
        std::vector<Local> locals;
        int scopeDepth = 0;

        uint8_t nextReg = 0;
        uint8_t maxReg = 0;

        struct LoopContext {
            size_t loopStart;
            std::vector<size_t> breakJumps;
            int scopeDepthAtLoop;
        };
        std::vector<LoopContext> loopStack;
        
        struct SwitchContext {
            std::vector<size_t> breakJumps;
        };
        std::vector<SwitchContext> switchStack;

        // Unified break handling
        enum class BreakableType { Loop, Switch };
        struct Breakable {
            BreakableType type;
            size_t index; // index in loopStack or switchStack
        };
        std::vector<Breakable> breakableStack;

        std::vector<FunctionObject> functions;
        std::unordered_map<std::string, uint16_t> functionIndex;
        std::unordered_map<std::string, uint16_t> globalIndex;
        uint16_t globalCount = 0;

        std::vector<ClassMeta> classes;
        std::unordered_map<std::string, uint16_t> classIndex;
        std::unordered_map<std::string, std::string> varClassMap;  ///< variable name → class name
        std::string currentClassName;  ///< set during method compilation

    public:
        /**
         * @brief Compiles the entire program AST into a bytecode chunk.
         * @return The main chunk containing the compiled program.
         */
        Chunk compile(ProgramNode* program);

        const std::vector<FunctionObject>& getFunctions() const { return functions; }
        std::vector<FunctionObject>& getFunctions() { return functions; }
        const std::vector<ClassMeta>& getClasses() const { return classes; }
        std::vector<ClassMeta>& getClasses() { return classes; }

    private:
        void compileNode(ASTNode* node);
        ExprResult compileExpression(ExpressionNode* expr, uint8_t dst = 255);

        void compileProgram(ProgramNode* node);
        void compileRepeat(RepeatNode* node);
        void compileWhile(WhileNode* node);
        void compileFor(const ForNode* node);
        void compileIf(IfNode* node);
        void compileLog(PrintNode* node);
        void compileVarDecl(VarDeclNode* node);
        void compileAssignment(AssignmentNode* node);
        void compileWait(WaitNode* node);
        void compileBreak();
        void compileContinue();
        void compileFunctionDecl(FunctionDeclNode* node);
        void compileReturn(ReturnNode* node);
        void compileClassDecl(ClassDeclNode* node);
        void compileFieldAssign(FieldAssignNode* node);
        void compileExprStmt(ExpressionStmtNode* node);
        void compileIndexAssign(IndexAssignNode* node);
        void compileTryCatch(TryCatchNode* node);
        void compileWait(WaitNode* node);
        ExprResult compileSwitch(SwitchNode* node, uint8_t dst = 255);
        void compileEnum(EnumNode* node);
        void compileImportNative(ImportNativeNode* node);

        ExprResult compileNumber(NumberNode* node, uint8_t dst);
        ExprResult compileDouble(DoubleNode* node, uint8_t dst);
        ExprResult compileBoolean(BooleanNode* node, uint8_t dst);
        ExprResult compileString(StringNode* node, uint8_t dst);
        ExprResult compileStringInterp(StringInterpNode* node, uint8_t dst);
        ExprResult compileVariable(VariableNode* node, uint8_t dst);
        ExprResult compileBinaryOp(BinaryOperationNode* node, uint8_t dst);
        ExprResult compileUnaryOp(UnaryOperationNode* node, uint8_t dst);
        ExprResult compileFunctionCall(FunctionCallNode* node, uint8_t dst);
        ExprResult compileFieldAccess(FieldAccessNode* node, uint8_t dst);
        ExprResult compileMethodCall(MethodCallNode* node, uint8_t dst);
        ExprResult compileIndexAccess(IndexAccessNode* node, uint8_t dst);
        ExprResult compileArrayAlloc(ArrayAllocNode* node, uint8_t dst);
        ExprResult compileArrayLiteral(ArrayLiteralNode* node, uint8_t dst);

        // OPTIMIZATION: Peephole Optimizer
        void peepholeOptimize(Chunk& ch);

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

        void beginScope();
        void endScope();
        void addLocal(const std::string& name, bool isMutable, TypeAnnotation typeAnnot = TypeAnnotation::None);
        int resolveLocal(const std::string& name);
        bool isGlobalScope() const { return scopeDepth == 0; }
    };
}

#endif //COMPILER_H