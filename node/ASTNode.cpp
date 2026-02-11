#include "ASTNode.h"

#include <sstream>

void ProgramNode::execute(RuntimeContext *ctx) {
    for (const auto &stmt: statements) {
        stmt->execute(ctx);
    }
}

void WaitNode::execute(RuntimeContext *ctx) {
    const Value val = duration->evaluate(ctx);
    if (std::holds_alternative<int>(val)) {
        const int ms = std::get<int>(val);
        ctx->logger->info("Waiting " + std::to_string(ms) + "ms");
        ctx->driver->sleep(ms);
    } else {
        throw std::runtime_error("Wait command expects a number (milliseconds)");
    }
}

void MouseBlockNode::execute(RuntimeContext *ctx) {
    for (const auto &action: actions) {
        action->execute(ctx);
    }
}

void ClickNode::execute(RuntimeContext *ctx) {
    if (x.has_value() && y.has_value()) {
        ctx->driver->moveMouse(x.value(), y.value());
    }

    if (button == Left) {
        ctx->driver->clickMouse(true);
        ctx->logger->info("Click Left");
    } else {
        ctx->driver->clickMouse(false);
        ctx->logger->info("Click Right");
    }
}

void MoveNode::execute(RuntimeContext *ctx) {
    const Value valX = x->evaluate(ctx);
    const Value valY = y->evaluate(ctx);

    if (std::holds_alternative<int>(valX) && std::holds_alternative<int>(valY)) {
        const int targetX = std::get<int>(valX);
        const int targetY = std::get<int>(valY);
        ctx->driver->moveMouse(targetX, targetY);
        ctx->logger->info("Move to " + std::to_string(targetX) + ", " + std::to_string(targetY));
    } else {
        throw std::runtime_error("Move command expects two numbers (x, y)");
    }
}

void ShiftNode::execute(RuntimeContext *ctx) {
    const Value dx = deltaX->evaluate(ctx);
    const Value dy = deltaY->evaluate(ctx);

    if (std::holds_alternative<int>(dx) && std::holds_alternative<int>(dy)) {
        const int deltaX = std::get<int>(dx);
        const int deltaY = std::get<int>(dy);
        auto [fst, snd] = ctx->driver->getMousePosition();
        const auto x = fst + deltaX;
        const auto y = snd + deltaY;
        ctx->driver->moveMouse(x, y);
        ctx->logger->info("Move to" + std::to_string(x) + ", " + std::to_string(y));
    } else {
        throw std::runtime_error("Move command expects two numbers (x, y)");
    }
}


void KeyboardBlockNode::execute(RuntimeContext *ctx) {
    for (const auto &action: actions) {
        action->execute(ctx);
    }
}

void WriteNode::execute(RuntimeContext *ctx) {
    Value val = text->evaluate(ctx);
    const std::string str = std::visit([]<typename T0>(T0&& arg) -> std::string {
        using T = std::decay_t<T0>;
        if constexpr (std::is_same_v<T, std::monostate>) return "";
        else if constexpr (std::is_same_v<T, int>) return std::to_string(arg);
        else return arg;
    }, val);

    ctx->logger->info("Writing: " + str);
    ctx->driver->typeText(str);
}

void PressNode::execute(RuntimeContext *ctx) {
    ctx->logger->info("Pressing key: " + key);
    ctx->driver->pressKey(key);
}

Value NumberNode::evaluate(RuntimeContext *ctx) {
    return value;
}

Value VariableNode::evaluate(RuntimeContext *ctx) {
    if (ctx->variables.contains(this->nameOfVariable)) {
        return ctx->variables[this->nameOfVariable];
    }
    throw std::runtime_error("Undefined variable: " + this->nameOfVariable);
}

Value StringNode::evaluate(RuntimeContext *ctx) {
    return value;
}

void VarDeclNode::execute(RuntimeContext *ctx) {
    ctx->variables[this->nameOfVariable] = expression->evaluate(ctx);
}

void AssignmentNode::execute(RuntimeContext *ctx) {
    if (!ctx->variables.contains(nameOfVariable)) {
        throw std::runtime_error("Variable '" + nameOfVariable + "' is not declared. Use 'var " + nameOfVariable + " = ...' first.");
    }
    ctx->variables[nameOfVariable] = expression->evaluate(ctx);
}

Value BinaryOperationNode::evaluate(RuntimeContext *ctx) {
    const Value left = this->leftNode->evaluate(ctx);
    const Value right = this->rightNode->evaluate(ctx);

    if (std::holds_alternative<int>(left) && std::holds_alternative<int>(right)) {
        const int leftInt = std::get<int>(left);
        const int rightInt = std::get<int>(right);
        switch (operation) {
            case '+':
                return leftInt + rightInt;
            case '-':
                return leftInt - rightInt;
            case '*':
                return leftInt * rightInt;
            case '/':
                if (rightInt == 0) throw std::runtime_error("Division by zero");
                return leftInt / rightInt;
            default: throw std::runtime_error(std::string("Unknown operator: ") + operation);
        }
    }

    if (operation != '+') {
        throw std::runtime_error("Type mismatch: Cannot perform operation '" + std::string(1, operation) + "' on non-numbers.");
    }
    
    std::string result = std::visit([](auto &&arg1, auto &&arg2) -> std::string {
        auto to_str = []<typename T0>(T0 &&val) -> std::string {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, int>) {
                return std::to_string(val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return val;
            } else {
                return "";
            }
        };
        return to_str(arg1) + to_str(arg2);
    }, left, right);
    return result;
}
